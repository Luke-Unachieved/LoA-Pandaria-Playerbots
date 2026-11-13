// GPL-2.0-or-later. One native actor; existing Playerbots remains the gameplay executor.
#include "AutonomousPlayerAudit.h"
#include "AutonomousNeedsBridge.h"
#include "AuditEvidence.h"
#include "AiObjectContext.h"
#include "Config.h"
#include "CharacterCache.h"
#include "Event.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "Map.h"
#include "NewRpgInfo.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "QuestDef.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TravelMgr.h"
#include "World.h"
#include "WorldSession.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <sstream>
#include <set>
#include <string>
#include <tuple>
#include <limits>

namespace AutonomousPlayerAuditV19 {
namespace {
using Clock=std::chrono::steady_clock;
double Now(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
struct Settings {
    bool enabled=false,recovery=false; std::uint64_t actor=0;
    Settings(){
        enabled=sConfigMgr->GetBoolDefault("AiPlayerbot.AutonomousPlayerAudit.Enabled",false);
        recovery=sConfigMgr->GetBoolDefault("AiPlayerbot.AutonomousPlayerAudit.HearthstoneRecovery",false);
        std::string text=sConfigMgr->GetStringDefault("AiPlayerbot.AutonomousPlayerAudit.ActorGuid","0");
        if(text.empty()||text.size()>20)return;
        for(char c:text){if(c<'0'||c>'9'){actor=0;return;} unsigned digit=unsigned(c-'0');
            if(actor>((std::numeric_limits<std::uint64_t>::max)()-digit)/10){actor=0;return;}actor=actor*10+digit;}
    }
};
Settings const& Cfg(){static Settings const c;return c;}
struct Goal {
    unsigned map=0,task=0,status=0;float x=0,y=0,z=0;
    bool Same(Goal const& b)const{return map==b.map&&task==b.task&&status==b.status&&
        std::abs(x-b.x)<1&&std::abs(y-b.y)<1&&std::abs(z-b.z)<1;}
};
struct State {
    std::mutex lock; std::atomic<bool> running{false};
    std::uint64_t session=1,sequence=0,dropped=0;double lastSample=0;unsigned elapsed=0;
    Evidence::ProgressBook progress;
    std::map<std::pair<unsigned,unsigned>,std::pair<unsigned,bool>> objectiveState;
    std::map<unsigned,std::pair<unsigned,bool>> questState;
    std::deque<std::string> records;
    Goal recoveryGoal; Evidence::RecoveryBudget recovery;
    Evidence::LoginBudget login;
    bool haveRecoveryGoal=false,arrivalObserved=false;
};
State& S(){static State* s=new State();return *s;}
void PushLocked(std::string const& json){if(S().records.size()>=256){++S().dropped;return;}S().records.push_back(json);}
void Note(char const* type,unsigned task=0,unsigned detail=0){
    auto& s=S();std::lock_guard<std::mutex> guard(s.lock);std::ostringstream out;double timestamp=Now();
    if(!std::isfinite(timestamp))timestamp=0;
    out<<"{\"kind\":\""<<type<<"\",\"actor\":"<<Cfg().actor<<",\"session\":"<<s.session
       <<",\"seq\":"<<++s.sequence<<",\"t\":"<<timestamp<<",\"task\":"<<task<<",\"detail\":"<<detail<<"}";
    PushLocked(out.str());
}
bool Owned(Player* p){
    if(!Reserved(p)||!p->GetSession()||!p->GetSession()->IsBot()||
       p->GetSession()->PlayerLoading()||p->GetSession()->isLogingOut()||p->IsDuringRemoveFromWorld())return false;
    PlayerbotAI* ai=GET_PLAYERBOT_AI(p);
    // Never chase a master pointer onto another map. Human-controlled or commanded actors are not driven.
    return ai&&!ai->IsRealPlayer()&&!ai->GetMaster();
}
unsigned Task(PlayerbotAI* ai){
    if(auto const* q=std::get_if<NewRpgInfo::DoQuest>(&ai->GetRpgInfo().data))return q->questId;
    TravelTarget* target=ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if(target&&target->getDestination())if(Quest const* q=target->getDestination()->GetQuestTemplate())return q->GetQuestId();
    return 0;
}
Goal NativeGoal(Player* p,WorldPosition const& dest){
    Goal g;g.map=dest.GetMapId();g.x=dest.GetPositionX();g.y=dest.GetPositionY();g.z=dest.GetPositionZ();
    if(PlayerbotAI* ai=GET_PLAYERBOT_AI(p)){g.task=Task(ai);g.status=unsigned(ai->GetRpgInfo().GetStatus());}return g;
}
void PollRecovery(Player* p,PlayerbotAI* ai,double now){
    bool replan=false;Goal goal;
    {
        auto& s=S();std::lock_guard<std::mutex> guard(s.lock);
        if(!s.recovery.pending)return;
        if(Spell* cast=p->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if(cast->GetSpellInfo()->Id==8690)s.recovery.castObserved=true;
        double dx=p->GetPositionX()-p->m_homebindX,dy=p->GetPositionY()-p->m_homebindY,dz=p->GetPositionZ()-p->m_homebindZ;
        // Arrival is an observed native postcondition, not UseAction's return value. A cast must also have been observed.
        if(s.recovery.castObserved && !p->IsBeingTeleported() && p->GetMapId()==p->m_homebindMapId && dx*dx+dy*dy+dz*dz<100){
            s.recovery.pending=false;s.arrivalObserved=true;goal=s.recoveryGoal;replan=true;
        }else if(s.recovery.TimedOut(now)){
            s.recovery.pending=false;PushLocked("{\"kind\":\"hearth_outcome_unknown_wait_no_retry\"}");
        }
    }
    if(replan){
        Note("hearth_native_cast_and_bind_arrival_observed",goal.task);
        // Do not discard the native goal or manufacture a quest state. Reset only movement progress for the SAME goal.
        if(NativeGoal(p,ai->GetRpgInfo().moveFarPos).Same(goal)){
            ai->GetRpgInfo().SetMoveFarTo(WorldPosition(goal.map,goal.x,goal.y,goal.z,0));
            Note("original_goal_native_replan_requested",goal.task);
        }else Note("native_goal_changed_no_restore",goal.task);
    }
}
void MaintainActorLogin(){
    auto& s=S();double now=Now();
    if(!s.login.Due(now))return;
    if(!sPlayerbotAIConfig->enabled||World::IsStopped()){s.login.Pending(now);return;}
    ObjectGuid guid(Cfg().actor);
    if(!guid.IsPlayer()||!guid.GetCounter()){
        s.login.Backoff(now);Note("actor_login_wait_invalid_player_guid");return;
    }
    // Only primitive presence checks: no Player/session pointer retained or map traversal.
    // Ownership is checked before any connected-account lookup or native DB login request.
    std::uint32_t account=sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if(!account||!sPlayerbotAIConfig->IsInRandomAccountList(account)||
       PlayerbotHolder::IsRaidOwnedBot(guid)||PlayerbotHolder::IsRaidProvisioningAccount(account)){
        s.login.Backoff(now);Note("actor_login_wait_unverified_managed_account");return;
    }
    if(ObjectAccessor::FindConnectedPlayer(guid)||sRandomPlayerbotMgr->GetPlayerBot(guid)){
        s.login.Present(now);return;
    }
    if(PlayerbotHolder::IsBotLoginPending(guid)){
        s.login.Pending(now);Note("actor_login_pending_native_lease");return;
    }
    if(!CanRequestLogin(Cfg().actor,account)){
        s.login.Backoff(now);Note("actor_login_wait_native_admission");return;
    }
    bool queued=sRandomPlayerbotMgr->AddAutonomousPlayerBot(guid);
    s.login.Backoff(now);
    Note(queued?"actor_login_native_query_queued_not_success":"actor_login_native_enqueue_refused");
}
} // namespace
bool ReservedGuid(std::uint64_t guid){return Cfg().enabled&&Cfg().actor&&guid==Cfg().actor;}
bool Reserved(Player const* p){return p&&ReservedGuid(p->GetGUID().GetRawValue());}
bool Owns(Player* p){return Owned(p);}
bool CanRequestLogin(std::uint64_t raw,std::uint32_t expectedAccount){
    if(!S().running.load()||!ReservedGuid(raw)||!sPlayerbotAIConfig->enabled||World::IsStopped())return false;
    ObjectGuid guid(raw);if(!guid.IsPlayer()||!guid.GetCounter())return false;
    std::uint32_t account=sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if(!account||(expectedAccount&&expectedAccount!=account)||!sPlayerbotAIConfig->IsInRandomAccountList(account)||
       PlayerbotHolder::IsRaidOwnedBot(guid)||PlayerbotHolder::IsRaidProvisioningAccount(account))return false;
    return !sWorld->FindSession(account)&&!ObjectAccessor::FindConnectedPlayer(guid);
}
void Startup(){
    S().login.Start(Now());S().running.store(Cfg().enabled&&Cfg().actor!=0);
    if(S().running.load())AutonomousNeedsV19::InitializeTravel();
    if(Cfg().enabled)Note(Cfg().actor?"configured_existing_managed_actor_delayed_login":"waiting_existing_actor_guid");
}
void Shutdown(){S().running.store(false);}
void OnLogin(Player* p){if(!Reserved(p))return;auto& s=S();std::lock_guard<std::mutex> g(s.lock);
    ++s.session;s.lastSample=0;s.progress=Evidence::ProgressBook();s.questState.clear();s.objectiveState.clear();
    s.recovery=Evidence::RecoveryBudget();s.haveRecoveryGoal=false;s.arrivalObserved=false;}
void OnLogout(Player* p){OnLogin(p);if(Reserved(p))Note(S().running.load()?"actor_offline_native_relogin_scheduled":"actor_offline_shutdown_no_relogin");}
void WorldUpdate(std::uint32_t diff){
    auto& s=S();if(!s.running.load())return;s.elapsed+=diff;if(s.elapsed<1000)return;s.elapsed=0;
    MaintainActorLogin();
    AutonomousNeedsV19::UpdateTravelCatalog();
    std::deque<std::string> batch;std::uint64_t drops=0;
    {std::lock_guard<std::mutex> guard(s.lock);unsigned budget=diff>100?2:16;
     while(!s.records.empty()&&batch.size()<budget){batch.push_back(std::move(s.records.front()));s.records.pop_front();}
     drops=s.dropped;s.dropped=0;}
    for(auto const& json:batch)TC_LOG_INFO("playerbots","[AUTONOMOUS_AUDIT] %s",json.c_str());
    if(drops)TC_LOG_INFO("playerbots","[AUTONOMOUS_AUDIT] {\"kind\":\"dropped_evidence\",\"count\":%llu}",static_cast<unsigned long long>(drops));
}
void Observe(Player* p){
    if(!S().running.load()||!Owned(p)||!p->IsInWorld()||!p->GetMap())return;
    PlayerbotAI* ai=GET_PLAYERBOT_AI(p);double now=Now();
    if(!std::isfinite(now)||!std::isfinite(p->GetPositionX())||!std::isfinite(p->GetPositionY())||!std::isfinite(p->GetPositionZ())){
        Note("invalid_native_position_or_time_no_finding");return;
    }
    PollRecovery(p,ai,now);
    bool backwards=false;
    {std::lock_guard<std::mutex> g(S().lock);if(now<S().lastSample){S().progress=Evidence::ProgressBook();backwards=true;}
     else if(now-S().lastSample<2)return;S().lastSample=now;}
    if(backwards){Note("backward_native_time_no_finding");return;}
    Evidence::Frame f;f.at=now;f.map=p->GetMapId();f.instance=p->GetInstanceId();f.phase=p->GetPhaseMask();
    f.x=p->GetPositionX();f.y=p->GetPositionY();f.z=p->GetPositionZ();f.level=p->GetLevel();
    f.xp=p->GetUInt32Value(PLAYER_FIELD_XP);f.nextXp=p->GetUInt32Value(PLAYER_FIELD_NEXT_LEVEL_XP);
    f.money=p->GetMoney();
    f.goal=unsigned(ai->GetRpgInfo().GetStatus());f.task=Task(ai);f.alive=p->IsAlive();f.combat=p->IsInCombat();f.vehicle=p->IsOnVehicle();
    auto* context=ai->GetAiObjectContext();
    if(context->GetValue<bool>("should repair")->Get())f.needs|=1;
    if(context->GetValue<bool>("should sell")->Get())f.needs|=2;
    if(context->GetValue<bool>("should get money")->Get())f.needs|=4;
    if(context->GetValue<bool>("can train")->Get())f.needs|=8;
    if(context->GetValue<GuidPosition>("rpg target")->Get() &&
       !context->GetValue<std::string>("next rpg action")->Get().empty())f.needs|=16;
    float killRate=sWorld->getRate(RATE_XP_KILL,p),questRate=sWorld->getRate(RATE_XP_QUEST,p),exploreRate=sWorld->getRate(RATE_XP_EXPLORE,p);
    std::set<unsigned> quests;for(uint16 slot=0;slot<MAX_QUEST_LOG_SIZE;++slot)if(unsigned q=p->GetQuestSlotQuestId(slot))quests.insert(q);
    if(f.task)quests.insert(f.task);
    {std::lock_guard<std::mutex> g(S().lock);for(auto const& q:S().questState)quests.insert(q.first);}
    std::vector<std::string> detail;
    for(unsigned id:quests){
        Quest const* q=sObjectMgr->GetQuestTemplate(id);
        if(!q){f.known=false;Note("missing_quest_template",id);continue;}
        unsigned status=p->GetQuestStatus(id);bool rewarded=p->GetQuestRewardStatus(id);
        std::pair<unsigned,bool> state{status,rewarded};bool changed=false;
        {std::lock_guard<std::mutex> g(S().lock);auto it=S().questState.find(id);changed=it==S().questState.end()||it->second!=state;
         if(S().questState.size()<64||it!=S().questState.end())S().questState[id]=state;}
        if(changed){std::ostringstream out;out<<"{\"kind\":\"quest_state\",\"task\":"<<id<<",\"map\":"<<f.map<<",\"status\":"<<status
            <<",\"rewarded\":"<<rewarded<<",\"min_level\":"<<q->GetMinLevel()<<",\"current_level\":"<<f.level
            <<",\"previous_quests_satisfied\":"<<p->SatisfyQuestPreviousQuest(q,false)<<",\"known_broken\":"<<sObjectMgr->IsBrokenQuest(id)<<"}";detail.push_back(out.str());}
        unsigned objectives=0;
        for(auto const& native:q->GetObjectives()){
            if(++objectives>32){f.known=false;break;}
            Evidence::Objective o;o.quest=id;o.id=native.ID;o.type=native.Type;o.object=native.ObjectID;o.required=native.Amount;
            o.count=p->GetQuestObjectiveCounter(native.ID);o.complete=p->IsQuestObjectiveComplete(q,native);f.objectives.push_back(o);
        }
    }
    {
        auto& s=S();std::lock_guard<std::mutex> g(s.lock);f.session=s.session;f.sequence=++s.sequence;
        Evidence::Verdict v=s.progress.Observe(f);
        std::ostringstream out;out<<"{\"kind\":\"native_frame\",\"actor\":"<<Cfg().actor<<",\"session\":"<<f.session<<",\"seq\":"<<f.sequence
            <<",\"t\":"<<now<<",\"map\":"<<f.map<<",\"instance\":"<<f.instance<<",\"phase\":"<<f.phase<<",\"goal\":"<<f.goal
            <<",\"task\":"<<f.task<<",\"needs\":"<<f.needs<<",\"level\":"<<f.level<<",\"xp\":"<<f.xp<<",\"next_xp\":"<<f.nextXp
            <<",\"money\":"<<f.money<<",\"money_delta\":"<<v.moneyDelta<<",\"needs_cleared\":"<<v.needsCleared
            <<",\"observed_capabilities\":"<<f.observedCapabilities<<",\"x\":"<<f.x<<",\"y\":"<<f.y<<",\"z\":"<<f.z<<",\"combat\":"<<f.combat
            <<",\"vehicle\":"<<f.vehicle<<",\"finding\":"<<unsigned(v.finding)<<",\"quiet_seconds\":"<<v.quietSeconds
            <<",\"failed_paths\":"<<v.failedPaths<<",\"kill_rate\":"<<killRate<<",\"quest_rate\":"<<questRate<<",\"explore_rate\":"<<exploreRate<<"}";
        PushLocked(out.str());for(auto const& row:detail)PushLocked(row);
        if(killRate!=1||questRate!=1||exploreRate!=1)PushLocked("{\"kind\":\"xp_base_rate_policy_mismatch_no_rate_changed\"}");
        for(auto const& o:f.objectives){auto key=std::make_pair(o.quest,o.id),value=std::make_pair(o.count,o.complete);
            auto it=s.objectiveState.find(key);if(it!=s.objectiveState.end()&&it->second==value)continue;
            if(s.objectiveState.size()>=512&&it==s.objectiveState.end()){++s.dropped;continue;}
            s.objectiveState[key]=value;std::ostringstream row;row<<"{\"kind\":\"objective_state\",\"task\":"<<o.quest<<",\"objective\":"<<o.id
               <<",\"type\":"<<o.type<<",\"object\":"<<o.object<<",\"count\":"<<o.count<<",\"required\":"<<o.required<<",\"complete\":"<<o.complete<<"}";PushLocked(row.str());}
    }
}
bool AllowQuest(Player* p,std::uint32_t id){if(!Reserved(p))return true;
    if(!sObjectMgr->GetQuestTemplate(id)){Note("missing_quest_template",id);return false;}
    if(sObjectMgr->IsBrokenQuest(id)){Note("known_broken_quest_no_auto_complete",id);return false;}return true;}
QuestAttempt::QuestAttempt(Player* p,AttemptKind kind,std::uint32_t id):player_(Reserved(p)?p:nullptr),kind_(kind),quest_(id),before_(0),rewarded_(false){
    if(player_){before_=player_->GetQuestStatus(id);rewarded_=player_->GetQuestRewardStatus(id);Note("native_quest_attempt",id,unsigned(kind));}}
QuestAttempt::~QuestAttempt(){if(!player_)return;unsigned after=player_->GetQuestStatus(quest_);bool reward=player_->GetQuestRewardStatus(quest_);
    bool observed=kind_==AttemptKind::Reward?(!rewarded_&&reward):
        (before_==QUEST_STATUS_NONE&&(after==QUEST_STATUS_INCOMPLETE||after==QUEST_STATUS_COMPLETE));
    Note(observed?"native_quest_postcondition_changed":"native_quest_postcondition_not_observed_unclassified",quest_,unsigned(kind_));}
void AlternativePath(Player* p,WorldPosition const& originalGoal,bool reachable,std::uint32_t pathType){
    if(!Owned(p))return;Goal goal=NativeGoal(p,originalGoal);
    {auto& s=S();std::lock_guard<std::mutex> g(s.lock);s.progress.Path(reachable);
     if(!s.haveRecoveryGoal||!s.recoveryGoal.Same(goal)){s.recoveryGoal=goal;s.recovery=Evidence::RecoveryBudget();s.haveRecoveryGoal=true;}
     s.recovery.Alternative(reachable,Now());}
    Note(reachable?"native_alternative_path_reachable":"native_alternative_path_rejected",goal.task,pathType);
}
bool TryHearthstone(PlayerbotAI* ai,WorldPosition const& originalGoal){
    Player* p=ai?ai->GetBot():nullptr;if(!Owned(p)||!Cfg().recovery)return false;
    Goal goal=NativeGoal(p,originalGoal);double now=Now();
    bool legal=p->IsAlive()&&!p->IsInCombat()&&p->getAttackers().empty()&&!p->isMoving()&&!p->IsOnVehicle()&&!p->GetVehicle()&&
        !p->GetTransport()&&!p->IsInFlight()&&!p->IsBeingTeleported()&&!p->IsNonMeleeSpellCasted(false);
    if(!legal){Note("hearth_wait_native_restrictions",goal.task,p->IsInCombat()?1:0);return false;}
    Item* item=p->GetItemByEntry(6948);SpellInfo const* spell=sSpellMgr->GetSpellInfo(8690);
    if(!item||!spell){Note("hearth_missing_item_or_spell",goal.task);return false;}
    bool ready=p->GetSpellHistory()->IsReady(8690,6948,false);
    unsigned delay=p->GetSpellHistory()->GetCooldownDelay(8690,6948);
    if(!ready){Note("hearth_native_cooldown_wait_no_clear",goal.task,delay);return false;}
    {
        auto& s=S();std::lock_guard<std::mutex> g(s.lock);
        if(!s.haveRecoveryGoal||!s.recoveryGoal.Same(goal)||!s.recovery.MayAttempt(now,legal,ready))return false;
        s.recovery.Attempt(now);
    }
    // Fixed native action and fixed owned item; no chat packet, command parser, GM handler or direct teleport.
    bool requested=ai->DoSpecificAction("use",Event("autonomous native hearthstone recovery","Hitem:6948:"),true);
    Note(requested?"hearth_item_request_dispatched_not_success":"hearth_item_request_rejected",goal.task,delay);
    if(!requested){std::lock_guard<std::mutex> g(S().lock);S().recovery.Rejected(now);}
    // Zero current delay only means ready. Loaded spell/item recovery values are separately recorded below.
    std::ostringstream row;row<<"{\"kind\":\"hearth_loaded_cooldown\",\"spell_recovery_ms\":"<<spell->RecoveryTime
        <<",\"spell_category_ms\":"<<spell->CategoryRecoveryTime<<",\"observed_remaining_ms\":"<<p->GetSpellHistory()->GetCooldownDelay(8690,6948);
    for(unsigned i=0;i<MAX_ITEM_PROTO_SPELLS;++i)if(item->GetTemplate()->Spells[i].SpellId==8690)
        row<<",\"item_recovery_ms\":"<<item->GetTemplate()->Spells[i].SpellCooldown<<",\"item_category_ms\":"<<item->GetTemplate()->Spells[i].SpellCategoryCooldown;
    row<<"}";{std::lock_guard<std::mutex> g(S().lock);PushLocked(row.str());}
    return requested;
}
}
