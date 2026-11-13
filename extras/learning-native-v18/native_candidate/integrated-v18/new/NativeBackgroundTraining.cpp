// GPL-2.0-or-later. See INTEGRATION.md for ownership, verified call sites and gaps.
#include "NativeBackgroundTraining.h"
#include "NativeTrainingRoster.h"
#include "deps/BackgroundTrainingQueue.h"
#include "Config.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceScript.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperation.h"
#include "PlayerbotSpec.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "WorldSession.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace NativeBackgroundTraining {
#ifdef NATIVE_BACKGROUND_TRAINING_FIXTURE
double FixtureNow();
#endif
namespace {
using namespace Detail;
namespace Queue = PlayerbotTrainingCandidate;
using Clock = std::chrono::steady_clock;
static_assert(Queue::DefaultTrainingPlan::Teams==3 && Queue::DefaultTrainingPlan::Actors==40 &&
    Queue::DefaultTrainingPlan::DungeonMembers==5 && Queue::DefaultTrainingPlan::Raid10Members==10 &&
    Queue::DefaultTrainingPlan::Raid25Members==25,"retain the verified 5/10/25 plan and 3/40 budget");
double Now() {
#ifdef NATIVE_BACKGROUND_TRAINING_FIXTURE
    return FixtureNow();
#else
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
#endif
}
struct Settings {
    bool enabled = false, admission = false;
    unsigned dungeon = 0, maxDiff = 100, cooldown = 300;
};
struct Run {
    std::vector<Observation> roster;
    Guid group = 0;
    std::string id, binding;
    double renewed = 0, transitionSince = 0;
    unsigned expectedMap = 0, expectedDifficulty = 0;
    bool nativeLfgEntry = false;
};
struct Service {
    Settings cfg;
    std::atomic<bool> observing{false};
    std::mutex inboxMutex, diagnosticMutex;
    std::map<Guid,Observation> observations;
    std::map<Guid,Guid> generations;
    Guid generation = 0;
    double humanObservedAt = -1;
    // The following fields belong ONLY to the serialized world thread, including Execute().
    std::unique_ptr<Queue::BackgroundTrainingQueue> queue;
    Run run;
    bool pending = false, observationPending = false;
    double busyUntil = 0, nextAdmissionAt = 0, lastLog = 0;
    std::uint64_t tickMs = 0, serial = 0, processEpoch = 0;
    Diagnostic diagnostic;
};
// Intentionally process-lifetime: late PlayerbotAI destruction may occur after WorldScript shutdown.
// Shutdown clears observations and disables the queue; no static-destruction callback touches dead mutexes.
Service& S() { static Service* instance=new Service(); return *instance; }
std::string Sequence(char const* kind) {
    return std::string(kind) + ":" + std::to_string(S().processEpoch) + ":" + std::to_string(++S().serial);
}
void Report(std::string const& reason, bool ready = false, bool combat = false) {
    auto& s=S();
    std::lock_guard<std::mutex> lock(s.diagnosticMutex);
    s.diagnostic.enabled=s.cfg.enabled; s.diagnostic.admissionEnabled=s.cfg.admission;
    s.diagnostic.dungeon=reason; s.diagnostic.ready=ready; s.diagnostic.inCombat=combat;
    s.diagnostic.queued=s.queue ? unsigned(s.queue->queued()) : 0;
    s.diagnostic.reservedActors=s.queue ? unsigned(s.queue->reservedActors()) : 0;
}
std::map<Guid,Observation> Observations() {
    std::lock_guard<std::mutex> lock(S().inboxMutex); return S().observations;
}
bool PlayersRecentlyObserved(double now) {
    std::lock_guard<std::mutex> lock(S().inboxMutex);
    return S().humanObservedAt>=0 && now-S().humanObservedAt<=15;
}
bool SameSession(Observation const& observation) {
    std::lock_guard<std::mutex> lock(S().inboxMutex);
    auto const it=S().generations.find(observation.guid);
    return it!=S().generations.end() && it->second==observation.session;
}
unsigned Role(Player* p) {
    // Same specialization classification used by LfgActions.cpp GetLfgRoles for random bots.
    if (PlayerBotSpec::IsTank(p,true)) return lfg::PLAYER_ROLE_TANK;
    if (PlayerBotSpec::IsHeal(p,true)) return lfg::PLAYER_ROLE_HEALER;
    return lfg::PLAYER_ROLE_DAMAGE;
}
bool NativeBot(Player* p) {
    if (!p || !p->GetSession() || !p->GetSession()->IsBot()) return false;
    PlayerbotAI* ai=GET_PLAYERBOT_AI(p);
    return ai && !ai->IsRealPlayer();
}
bool Quiet(Player* p) {
    return p->IsInWorld() && p->IsAlive() && !p->IsInCombat() && !p->IsBeingTeleported() &&
        !p->IsDuringRemoveFromWorld() && !p->duel && !p->InBattleground() && !p->InBattlegroundQueue() &&
        !p->IsInFlight() && !p->IsFlying() && !p->GetTransport() && !p->GetVehicle() &&
        !p->GetTrader() && !p->IsNonMeleeSpellCasted(false,false,true);
}
void FinishQueue(double now) {
    auto& s=S();
    if (!s.run.id.empty()) s.queue->finish(s.run.id,Sequence("finish"),now);
    s.run.id.clear(); s.run.binding.clear();
}
void Retire(double now, char const* reason) {
    FinishQueue(now); S().run=Run(); S().nextAdmissionAt=now+S().cfg.cooldown;
    Report(reason);
    // No LeaveLfg, Disband, teleport, AI reset, strategy edit, logout or combat stop here.
}
void WaitForTransition(double now, char const* reason, bool combat=false) {
    auto& s=S();
    FinishQueue(now);
    if (!s.run.transitionSince) s.run.transitionSince=now;
    if (now-s.run.transitionSince>120) { Retire(now,"run_native_transition_timeout_native_ai_unchanged"); return; }
    Report(reason,false,combat);
}
Queue::Snapshot MakeSnapshot(std::vector<Observation> const& roster, Guid group,
                             std::string const& runId, bool ready) {
    Queue::Snapshot q;
    q.runId=runId; q.buildId="native-scheduler-v18";
    q.sessionId="process:"+std::to_string(S().processEpoch);
    q.groupId=std::to_string(group);
    q.instanceId=std::to_string(roster.front().map)+":"+std::to_string(roster.front().instance)+":"+
                 std::to_string(roster.front().difficulty);
    q.epochId="sessions";
    q.runtimeKnown=true; q.verifiedDummyOnly=true; q.humanMembers=0; q.ready=ready;
    // 'Dummy' is the unchanged queue's field name; actual evidence is a native bot session,
    // registered AI lifecycle, exact native group and current RandomPlayerbotMgr membership.
    for (auto const& x:roster) {
        Queue::Actor a; a.id=std::to_string(x.guid); a.verifiedDummy=x.nativeBot;
        a.humanControlled=!x.nativeBot; a.connected=true; q.actors.push_back(a);
        q.epochId += ":"+std::to_string(x.session);
    }
    return q;
}
bool BindQueue(std::vector<Observation> const& roster, Guid group, bool ready, double now) {
    auto& s=S();
    std::string const binding=std::to_string(roster.front().map)+":"+std::to_string(roster.front().instance)+":"+
                              std::to_string(roster.front().difficulty);
    if (!s.run.id.empty() && s.run.binding!=binding) FinishQueue(now);
    if (s.run.id.empty()) {
        Queue::Request request; request.commandId=Sequence("enqueue"); request.authorizationNonce=Sequence("native");
        request.snapshot=MakeSnapshot(roster,group,Sequence("run"),ready);
        request.issuedAt=now; request.expiresAt=now+120;
        if (!s.queue->enqueue(request,now).accepted) return false;
        s.run.id=request.snapshot.runId; s.run.binding=binding; s.run.renewed=now;
    } else if (now-s.run.renewed>=60) {
        if (!s.queue->renew(s.run.id,Sequence("renew"),Sequence("native"),now+120,
                           MakeSnapshot(roster,group,s.run.id,ready),now).accepted) return false;
        s.run.renewed=now;
    }
    return true;
}

// Same native world-thread ownership route as LfgActions.cpp LfgWorldThreadOperation.
// Carries only GUIDs/session receipts and primitive observations; no retained Player*/Group*/Map*.
class AdmitFiveOperation : public PlayerbotOperation {
public:
    explicit AdmitFiveOperation(std::vector<Observation> roster):roster_(std::move(roster)) {}
    ObjectGuid GetBotGuid() const override { return roster_.empty()?ObjectGuid::Empty:ObjectGuid(roster_[0].guid); }
    uint32 GetPriority() const override { return 0; }
    std::string GetName() const override { return "BackgroundTrainingAdmitFive"; }
    // Execute must always run so that a stale operation releases its pending flag.
    bool Execute() override {
        auto& s=S(); s.pending=false; double const now=Now();
        if (!s.observing.load() || !MayAdmit(s.cfg.enabled,s.cfg.admission,now<s.busyUntil,false,
                                           !s.run.roster.empty(),now,s.nextAdmissionAt)) return false;
        s.nextAdmissionAt=now+s.cfg.cooldown;
        auto const fresh=Observations();
        std::vector<Player*> players; // stack-only, native post-map dispatcher, never in controller/mailbox
        for (auto& bound:roster_) {
            auto const it=fresh.find(bound.guid);
            if (it==fresh.end() || it->second.session!=bound.session || !SameSession(bound) ||
                !Selectable(it->second,now)) { Report("waiting_stale_or_occupied_actor"); return false; }
            bound=it->second;
            Player* p=ObjectAccessor::FindPlayer(ObjectGuid(bound.guid));
            // The processor runs after map workers join. Recheck self state and world-owned registry/LFG.
            // No Map/InstanceScript traversal here; proximity and encounter evidence is map-owned above.
            if (!NativeBot(p) || !sRandomPlayerbotMgr->IsRandomBot(p) || !Quiet(p) ||
                GET_PLAYERBOT_AI(p)->GetMaster() || p->GetGroup() || p->GetOriginalGroup() || p->GetGroupInvite() ||
                PlayerbotHolder::IsRaidOwnedBot(p->GetGUID()) || PlayerbotHolder::IsManagedLogin(p->GetGUID()) ||
                sLFGMgr->GetActiveState(p->GetGUID())!=lfg::LFG_STATE_NONE || Role(p)!=bound.role ||
                p->GetMapId()!=bound.map || p->GetInstanceId()!=bound.instance || p->GetPhaseMask()!=bound.phase ||
                p->GetTeamId()!=bound.team || p->GetLevel()!=bound.level) {
                Report("waiting_native_revalidation_failed"); return false;
            }
            players.push_back(p);
        }
        LFGDungeonEntry const* dbc=sLFGDungeonStore.LookupEntry(s.cfg.dungeon);
        lfg::LFGDungeonData const* dungeon=sLFGMgr->GetLFGDungeon(s.cfg.dungeon);
        if (players.size()!=5 || !dbc || !dungeon || dbc->MapID<=0 || dbc->tankNeeded!=1 ||
            dbc->healerNeeded!=1 || dbc->dpsNeeded!=3 ||
            (dungeon->type!=lfg::LFG_TYPE_DUNGEON && dungeon->type!=lfg::LFG_TYPE_HEROIC) ||
            SelectFive(roster_,now,dungeon->minlevel,dungeon->maxlevel).size()!=5) {
            Report("waiting_specific_five_man_dungeon"); return false;
        }
        // GroupInviteOperation additionally summons under summonWhenGroup. Reuse its core Create/AddMember
        // sequence directly, excluding that unrelated movement behavior and any conversion to a raid.
        Group* group=new Group();
        if (!group->Create(players.front())) { delete group; Report("waiting_group_create_failed"); return false; }
        sGroupMgr->AddGroup(group);
        for (std::size_t i=1;i<players.size();++i) {
            if (!group->AddMember(players[i])) {
                group->Disband(); Report("waiting_group_add_failed_rolled_back"); return false;
            }
        }
        Guid const groupId=group->GetGUID().GetRawValue();
        for (auto& x:roster_) { x.group=groupId; x.members.clear(); for(auto const& y:roster_) x.members.push_back(y.guid); }
        s.run.roster=roster_; s.run.group=groupId;
        s.run.expectedMap=unsigned(dbc->MapID); s.run.expectedDifficulty=dbc->DifficultyID;
        if (!BindQueue(roster_,groupId,false,now)) {
            group->Disband(); s.run=Run(); Report("waiting_queue_refused_rolled_back"); return false;
        }
        lfg::LfgDungeonSet dungeons; dungeons.insert(dungeon->id);
        // Native eligibility/gear/lock checks remain in JoinLfg. Existing role-check/proposal actions
        // answer native packets. No synthetic acceptance, forced instance entry, or GM command.
        sLFGMgr->JoinLfg(players.front(),lfg::LfgRoles(roster_.front().role),dungeons,"native background training");
        if (sLFGMgr->GetActiveState(players.front()->GetGUID())==lfg::LFG_STATE_NONE) {
            FinishQueue(now); group->Disband(); s.run=Run(); Report("waiting_lfg_rejected_rolled_back"); return false;
        }
        { std::lock_guard<std::mutex> lock(s.diagnosticMutex); ++s.diagnostic.admitted; }
        Report("lfg_queued_waiting_native_proposal_and_entry"); return true;
    }
private:
    std::vector<Observation> roster_;
};

void ObserveRun(std::map<Guid,Observation> const& observations, std::set<Guid> const& online, double now) {
    auto& s=S(); std::vector<Observation> roster;
    bool combat=false, allSame=true, nativeIdentity=true, humans=false, allAlive=true;
    for (auto const& bound:s.run.roster) {
        auto const it=observations.find(bound.guid);
        if (!online.count(bound.guid) || !SameSession(bound) ||
            (it!=observations.end() && it->second.session!=bound.session)) {
            Retire(now,"run_session_lost_native_ai_unchanged"); return;
        }
        if (it==observations.end() || !Fresh(it->second,now)) {
            // LFG loading/map transfer can legitimately stop callbacks for seconds. Release stale
            // queue readiness, retain the bound roster, and resume only after actual fresh observations.
            WaitForTransition(now,"waiting_native_transition_map_observation"); return;
        }
        auto const& x=it->second;
        if (x.group!=s.run.group || !ExactMembers(x.members,s.run.roster)) {
            // Native dispatcher has already validated the actual exact roster. Wait for map snapshots
            // to catch up with a native LFG group replacement; never label old snapshots ready.
            WaitForTransition(now,"waiting_native_transition_group_observation"); return;
        }
        nativeIdentity=nativeIdentity && x.nativeBot;
        humans=humans || x.nearbyHuman;
        combat=combat || x.combat || x.encounter; allAlive=allAlive && x.alive;
        if (!roster.empty()) allSame=allSame && x.map==roster.front().map && x.instance==roster.front().instance &&
                                                   x.difficulty==roster.front().difficulty;
        roster.push_back(x);
    }
    if (!nativeIdentity || humans) { Retire(now,"run_human_or_identity_changed_native_ai_unchanged"); return; }
    if (!allSame) {
        // Normal LFG transition can span several map ticks. Old physical binding must not renew falsely.
        WaitForTransition(now,"waiting_native_transition_all_members_same_instance",combat); return;
    }
    s.run.transitionSince=0;
    bool const ready=s.run.nativeLfgEntry && allAlive && roster.front().instance!=0 && roster.front().instanceable &&
        roster.front().map==s.run.expectedMap && roster.front().difficulty==s.run.expectedDifficulty;
    if (!BindQueue(roster,s.run.group,ready,now)) {
        FinishQueue(now); Report("waiting_queue_lease_or_ledger_refused_native_ai_unchanged",false,combat); return;
    }
    bool const busy=now<s.busyUntil;
    // Holding a queue budget is informational. Nothing dispatches Hold/Stop to native AI.
    s.queue->evaluate({MakeSnapshot(roster,s.run.group,s.run.id,ready)},now,PlayersRecentlyObserved(now),busy && !combat);
    Report(combat ? "native_encounter_running" : (busy ? "busy_at_encounter_boundary_no_new_admission" :
           (ready ? "native_instance_observed" : "lfg_queued_waiting_native_proposal_and_entry")),ready,combat);
}
// Revalidate control/group ownership in the same proven dispatcher used for admission. Map callbacks
// deliberately never chase a master pointer onto another map. Here all map workers have joined.
class ObserveRunOperation : public PlayerbotOperation {
public:
    ObserveRunOperation(std::map<Guid,Observation> observations, std::set<Guid> online)
        : observations_(std::move(observations)), online_(std::move(online)) {}
    std::string GetName() const override { return "BackgroundTrainingObserveRun"; }
    uint32 GetPriority() const override { return 0; }
    bool Execute() override {
        auto& s=S(); s.observationPending=false;
        if (!s.observing.load() || s.run.roster.empty()) return false;
        std::vector<Player*> players;
        Guid observedGroup=0; bool allLfgEntered=true;
        for(auto const& bound:s.run.roster) {
            Player* p=ObjectAccessor::FindPlayer(ObjectGuid(bound.guid));
            if (!SameSession(bound) || !NativeBot(p) || !sRandomPlayerbotMgr->IsRandomBot(p) ||
                !p->GetGroup() || p->GetOriginalGroup() || p->GetGroupInvite()) {
                Retire(Now(),"run_native_ownership_changed_native_ai_unchanged"); return false;
            }
            Guid const actualGroup=p->GetGroup()->GetGUID().GetRawValue();
            if ((observedGroup && observedGroup!=actualGroup) ||
                (actualGroup!=s.run.group && (!p->GetGroup()->isLFGGroup() ||
                 sLFGMgr->GetActiveState(p->GetGUID())<lfg::LFG_STATE_DUNGEON))) {
                Retire(Now(),"run_native_group_changed_native_ai_unchanged"); return false;
            }
            observedGroup=actualGroup;
            allLfgEntered=allLfgEntered && sLFGMgr->GetActiveState(p->GetGUID())>=lfg::LFG_STATE_DUNGEON;
            std::vector<Guid> members;
            for (auto const& slot:p->GetGroup()->GetMemberSlots()) members.push_back(slot.guid.GetRawValue());
            if (!ExactMembers(members,s.run.roster)) {
                Retire(Now(),"run_native_members_changed_native_ai_unchanged"); return false;
            }
            players.push_back(p);
        }
        for (Player* p:players) {
            Player* master=GET_PLAYERBOT_AI(p)->GetMaster();
            // Null or another verified native member of this exact roster only; excludes humans,
            // self-bots and bot-master chains leading outside this group without following the chain.
            if (master && std::find(players.begin(),players.end(),master)==players.end()) {
                Retire(Now(),"run_external_master_native_ai_unchanged"); return false;
            }
        }
        if (observedGroup!=s.run.group) { FinishQueue(Now()); s.run.group=observedGroup; }
        s.run.nativeLfgEntry=allLfgEntered;
        ObserveRun(observations_,online_,Now()); return true;
    }
private:
    std::map<Guid,Observation> observations_;
    std::set<Guid> online_;
};
} // namespace

void Startup() {
    auto& s=S();
    s.cfg.enabled=sConfigMgr->GetBoolDefault("AiPlayerbot.BackgroundTraining.Enabled",false);
    s.cfg.admission=sConfigMgr->GetBoolDefault("AiPlayerbot.BackgroundTraining.AdmissionEnabled",false);
    s.cfg.dungeon=unsigned(std::max(0,sConfigMgr->GetIntDefault("AiPlayerbot.BackgroundTraining.DungeonId",0)));
    s.cfg.maxDiff=unsigned(std::max(20,sConfigMgr->GetIntDefault("AiPlayerbot.BackgroundTraining.MaxWorldDiffMs",100)));
    s.cfg.cooldown=unsigned(std::max(30,sConfigMgr->GetIntDefault("AiPlayerbot.BackgroundTraining.NewRunCooldownSeconds",300)));
    s.processEpoch=std::uint64_t(std::chrono::system_clock::now().time_since_epoch().count());
    Queue::Config cfg; cfg.enabled=s.cfg.enabled; // shadowOnly remains true; never fabricate executable queue advice.
    s.queue=std::make_unique<Queue::BackgroundTrainingQueue>(cfg);
    s.observing.store(s.cfg.enabled);
    Report(s.cfg.enabled ? "waiting_first_native_observations" : "disabled");
    TC_LOG_INFO("playerbots","[TRAINING] enabled=%u admission=%u dungeon=%u plan=5+10+25 cap=3/40; raids awaiting native entry; restart to change config",
        unsigned(s.cfg.enabled),unsigned(s.cfg.admission),s.cfg.dungeon);
}
void Shutdown() {
    auto& s=S(); s.observing.store(false);
    if (s.queue) s.queue->disable(Now());
    s.pending=false; s.observationPending=false; s.run=Run();
    { std::lock_guard<std::mutex> lock(s.inboxMutex); s.observations.clear(); s.generations.clear(); }
    Report("shutdown_native_ai_unchanged");
}
void OnAiCreated(Player* player) {
    if (!player) return;
    auto& s=S(); std::lock_guard<std::mutex> lock(s.inboxMutex);
    Guid const id=player->GetGUID().GetRawValue();
    s.generations[id]=++s.generation; s.observations.erase(id);
}
void OnAiDestroyed(Player* player) {
    if (!player) return;
    auto& s=S(); std::lock_guard<std::mutex> lock(s.inboxMutex);
    Guid const id=player->GetGUID().GetRawValue();
    s.generations.erase(id); s.observations.erase(id);
}
void ObservePlayer(Player* player) {
    auto& s=S(); if (!s.observing.load() || !player || !player->IsInWorld()) return;
    Observation x; x.guid=player->GetGUID().GetRawValue(); x.at=Now();
    if (!NativeBot(player)) {
        // Immutable human-presence heartbeat only; the controller never scans human Player pointers.
        std::lock_guard<std::mutex> lock(s.inboxMutex);
        s.humanObservedAt=std::max(s.humanObservedAt,x.at); return;
    }
    {
        std::lock_guard<std::mutex> lock(s.inboxMutex);
        auto const generation=s.generations.find(x.guid);
        if (generation==s.generations.end()) return;
        auto const previous=s.observations.find(x.guid);
        if (previous!=s.observations.end() && x.at-previous->second.at<0.5) return;
        x.session=generation->second;
    }
    // This function runs only in this player's map callback, after its native AI update.
    Map* map=player->GetMap(); PlayerbotAI* ai=GET_PLAYERBOT_AI(player);
    if (!map || !ai) return;
    x.nativeBot=NativeBot(player); x.hasMaster=ai->GetMaster()!=nullptr;
    x.alive=player->IsAlive(); x.combat=player->IsInCombat();
    x.map=player->GetMapId(); x.instance=player->GetInstanceId(); x.difficulty=map->GetSpawnMode();
    x.instanceable=map->Instanceable(); x.team=player->GetTeamId(); x.phase=player->GetPhaseMask();
    x.level=player->GetLevel(); x.role=Role(player);
    x.occupied=!Quiet(player) || player->GetOriginalGroup() || player->GetGroupInvite();
    if (Group* group=player->GetGroup()) {
        x.group=group->GetGUID().GetRawValue();
        // Copies member GUIDs, never resolves or follows members onto another map.
        for (auto const& slot:group->GetMemberSlots()) x.members.push_back(slot.guid.GetRawValue());
    }
    if (InstanceScript* instance=player->GetInstanceScript()) x.encounter=instance->IsEncounterInProgress();
    x.nearbyHuman=false;
    for (auto const& ref:map->GetPlayers()) {
        Player* other=ref.GetSource();
        if (!other || other==player || !other->IsInWorld()) continue;
        // Same map's player list only. Any human inside an instance disqualifies that instance.
        if (!NativeBot(other) && (x.instanceable || player->GetDistance2d(other)<=150.0f)) { x.nearbyHuman=true; break; }
    }
    std::lock_guard<std::mutex> lock(s.inboxMutex);
    auto const generation=s.generations.find(x.guid);
    if (generation!=s.generations.end() && generation->second==x.session) s.observations[x.guid]=std::move(x);
}
void WorldUpdate(std::uint32_t diff) {
    auto& s=S(); if (!s.observing.load() || !s.queue) return;
    double const now=Now();
    if (diff>s.cfg.maxDiff) s.busyUntil=now+30;
    s.tickMs+=diff; if (s.tickMs<1000) return; s.tickMs=0;
    auto const observations=Observations();
    // World-owned registry enumeration: copy KEYS only. Do not dereference stored Player* values.
    std::set<Guid> online;
    for (auto it=sRandomPlayerbotMgr->GetPlayerBotsBegin();it!=sRandomPlayerbotMgr->GetPlayerBotsEnd();++it)
        online.insert(it->first);
    std::vector<Observation> pool;
    for (auto const& item:observations) if (online.count(item.first) && Selectable(item.second,now)) pool.push_back(item.second);
    {
        std::lock_guard<std::mutex> lock(s.diagnosticMutex);
        s.diagnostic.available=unsigned(pool.size()); s.diagnostic.tanks=s.diagnostic.healers=s.diagnostic.damage=0;
        for(auto const& x:pool) { if(x.role==2) ++s.diagnostic.tanks; else if(x.role==4) ++s.diagnostic.healers; else if(x.role==8) ++s.diagnostic.damage; }
    }
    if (!s.run.roster.empty()) {
        if (!s.observationPending) {
            std::map<Guid,Observation> runObservations;
            for (auto const& bound:s.run.roster) { auto const it=observations.find(bound.guid); if(it!=observations.end()) runObservations.insert(*it); }
            s.observationPending=PlayerbotWorldThreadProcessor::instance().QueueOperation(
                std::make_unique<ObserveRunOperation>(std::move(runObservations),online));
            if (!s.observationPending) Report("waiting_observation_dispatcher_capacity");
        }
    }
    else if (s.pending) Report("waiting_native_dispatcher");
    else if (now<s.busyUntil) Report("waiting_host_busy_no_new_run");
    else if (now<s.nextAdmissionAt) Report("waiting_new_run_cooldown");
    else {
        lfg::LFGDungeonData const* dungeon=sLFGMgr->GetLFGDungeon(s.cfg.dungeon);
        LFGDungeonEntry const* dbc=sLFGDungeonStore.LookupEntry(s.cfg.dungeon);
        if (!s.cfg.dungeon || !dungeon || !dbc || dbc->MapID<=0 || dbc->tankNeeded!=1 || dbc->healerNeeded!=1 || dbc->dpsNeeded!=3 ||
            (dungeon->type!=lfg::LFG_TYPE_DUNGEON && dungeon->type!=lfg::LFG_TYPE_HEROIC)) Report("waiting_specific_five_man_dungeon");
        else {
            auto const roster=SelectFive(pool,now,dungeon->minlevel,dungeon->maxlevel);
            if (roster.empty()) Report("waiting_five_available_same_faction_map_roles_1_1_3");
            else if (!s.cfg.admission) Report("observation_only_roster_available");
            else if (MayAdmit(s.cfg.enabled,s.cfg.admission,false,s.pending,false,now,s.nextAdmissionAt)) {
                s.pending=PlayerbotWorldThreadProcessor::instance().QueueOperation(std::make_unique<AdmitFiveOperation>(roster));
                Report(s.pending ? "waiting_native_dispatcher" : "waiting_dispatcher_capacity");
            }
        }
    }
    if (now-s.lastLog>=30) {
        Diagnostic const d=GetDiagnostic(); s.lastLog=now;
        TC_LOG_INFO("playerbots","[TRAINING] five=%s ready=%u combat=%u pool=%u roles=%u/%u/%u queue=%u actors=%u admitted=%u raid10=%s raid25=%s",
            d.dungeon.c_str(),unsigned(d.ready),unsigned(d.inCombat),d.available,d.tanks,d.healers,d.damage,d.queued,d.reservedActors,d.admitted,
            d.raid10.c_str(),d.raid25.c_str());
    }
}
Diagnostic GetDiagnostic() { std::lock_guard<std::mutex> lock(S().diagnosticMutex); return S().diagnostic; }
} // namespace NativeBackgroundTraining
