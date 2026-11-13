#include "ResponseBindingV18.h"
#include "NativeCastSourceResolver.h"
#include "MechanicActionAuthority.h"
#include "Playerbots.h"
#include "Creature.h"
#include "Map.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Config.h"
#include "Log.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>

namespace ResponseBindingV18 {
namespace {
using namespace InterruptAttemptV16;
constexpr std::uint64_t InterruptTask = 1;
constexpr std::size_t MaxEvents = 256, MaxActors = 4096;
struct ActorRow { std::uint64_t incarnation = 0; };
struct EventRow {
    Binding binding{};
    CastReceiptsV15::Identity source{};
    std::uintptr_t sourceSpell = 0, ai = 0;
    bool submitted = false, confirmed = false, ended = false;
};
TeamDecisionV15::ReservationBook reservations;
std::map<std::uintptr_t, ActorRow> actors;
std::map<TaskKey, EventRow> events;
std::atomic<bool> anyEvents{false};
std::mutex mutex;
std::uint64_t serial = 0; // never reset: both AI incarnations and call/session bindings
thread_local NativeClaimScope* activeClaim = nullptr;
thread_local NativeCallFrame* activeFrame = nullptr;
bool Enabled() {
    return sConfigMgr->GetBoolDefault("RaidAI.ResponseBindingV18.Shadow", false) &&
        sConfigMgr->GetBoolDefault("RaidAI.InterruptObserverV17.Shadow", false);
}
std::uint64_t Now() {
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
bool Live(Unit* unit) {
    return unit && unit->IsInWorld() && !unit->IsDuringRemoveFromWorld() && unit->IsAlive();
}
bool Actor(PlayerbotAI* ai, Unit* expected = nullptr, std::uintptr_t session = 0) {
    auto p = ai ? ai->GetBot() : nullptr;
    if (!ai || ai->IsRealPlayer() || !Live(p) || GET_PLAYERBOT_AI(p) != ai ||
        (expected && p != expected) || p->IsCharmed() || p->IsBeingTeleported()) return false;
    auto s = p->GetSession();
    return s && s->IsBot() && !s->isLogingOut() &&
        (!session || reinterpret_cast<std::uintptr_t>(s) == session);
}
bool ExactActor(Binding const& binding, PlayerbotAI* ai) {
    auto it = actors.find(reinterpret_cast<std::uintptr_t>(ai));
    return it != actors.end() && it->second.incarnation == binding.owner[1];
}
bool CurrentActor(Binding const& b, PlayerbotAI* ai, Unit* expected,
    std::uintptr_t session, Spell* response) {
    if (!response || response->GetCaster() != expected) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!ExactActor(b, ai)) return false; // never dereference a retired AI
    }
    // The live native response supplies the current caster; stored actor/AI
    // pointers are comparisons only until they match its current association.
    auto player = response->GetCaster() ? response->GetCaster()->ToPlayer() : nullptr;
    return Live(player) && GET_PLAYERBOT_AI(player) == ai && Actor(ai, player, session);
}
void Log(Binding const& b, CastReceiptsV15::Identity const& source,
    char const* status, std::uint64_t responseToken = 0) {
    TC_LOG_INFO("raidai", "response-binding-v18 mode=shadow status=%s map=%llu instance=%llu epoch=%llu root=%llu source=%llu target_cast_token=%llu task=%llu actor=%llu ai_incarnation=%llu session_binding=%llu reservation=%llu response=%llu skill=%u",
        status, static_cast<unsigned long long>(b.task[0]), static_cast<unsigned long long>(b.task[1]),
        static_cast<unsigned long long>(b.task[2]), static_cast<unsigned long long>(source.root),
        static_cast<unsigned long long>(b.task[3]), static_cast<unsigned long long>(b.task[4]),
        static_cast<unsigned long long>(b.task[5]), static_cast<unsigned long long>(b.owner[0]),
        static_cast<unsigned long long>(b.owner[1]), static_cast<unsigned long long>(b.owner[2]),
        static_cast<unsigned long long>(b.reservationToken), static_cast<unsigned long long>(responseToken), b.skillId);
}
void Retire(std::vector<TaskKey> const& keys) { for (auto const& key : keys) RetireEvent(key); }
}

void RegisterActor(PlayerbotAI* ai) {
    if (!ai) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto address = reinterpret_cast<std::uintptr_t>(ai);
    // Registration is tied to construction even while observation is disabled.
    // Duplicate or saturated registration cannot refresh a stale incarnation.
    if (actors.count(address) || actors.size() >= MaxActors || serial == UINT64_MAX) return;
    actors.emplace(address, ActorRow{++serial});
}
void ForgetActor(PlayerbotAI* ai) {
    if (!ai) return;
    std::vector<TaskKey> keys;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto address = reinterpret_cast<std::uintptr_t>(ai);
        actors.erase(address);
        // Actor loss invalidates the proof; it is NOT event-end or no-effect.
        // Submitted reservations remain unknown until exact source/map/root end.
        for (auto const& row : events) if (row.second.ai == address) keys.push_back(row.first);
    }
    for (auto const& key : keys) InterruptObserverV17::ForgetEvent(key);
}
NativeClaimScope::NativeClaimScope(PlayerbotAI* ai_, Creature* root, Creature* target_, unsigned skill)
    : previous(activeClaim), ai(ai_), target(target_) {
    activeClaim = this; // invalid nested scopes mask the outer scope too
    if (previous || activeFrame || !Enabled() || !skill || !Actor(ai) || !Live(root) ||
        !Live(target) || !MopRaidActions::Authorized(ai, root)) return;
    actor = ai->GetBot();
    if (actor->GetMap() != target->GetMap() || root->GetMap() != target->GetMap()) return;
    auto old = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    auto nonce = CastReceiptsV15::Current(target, CURRENT_GENERIC_SPELL);
    if (!old || !nonce || !CastReceiptsV15::ResolveSupportedCast(target, old, source) ||
        source.root != root->GetGUID().GetRawValue() ||
        MopRaidActions::Epoch(ai, root) != source.epoch) return;
    session = reinterpret_cast<std::uintptr_t>(ai->GetBot()->GetSession());
    Binding b;
    b.task = {{source.map, target->GetMap()->GetInstanceId(), source.epoch, source.source, nonce, InterruptTask}};
    b.targetCastToken = nonce; b.skillId = skill;
    std::lock_guard<std::mutex> lock(mutex);
    auto registration = actors.find(reinterpret_cast<std::uintptr_t>(ai));
    if (registration == actors.end() || serial == UINT64_MAX || events.size() >= MaxEvents || events.count(b.task)) return;
    // owner[2] identifies this exact live session binding in the owning native
    // call, not a claimed WorldSession lifetime nonce. No delayed attribution.
    b.owner = {{actor->GetGUID().GetRawValue(), registration->second.incarnation, ++serial}};
    b.reservationToken = reservations.Claim(b.task, b.owner, Now(), 5000);
    if (!b.Valid()) return;
    binding = b;
    events.emplace(b.task, EventRow{b, source, reinterpret_cast<std::uintptr_t>(old),
        reinterpret_cast<std::uintptr_t>(ai), false, false, false});
    anyEvents.store(true);
}
NativeClaimScope::~NativeClaimScope() {
    if (binding.Valid()) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = events.find(binding.task);
        if (it != events.end() && it->second.binding == binding) {
            if (!it->second.submitted) {
                reservations.CancelBeforeSubmit(binding.task, binding.owner, binding.reservationToken);
                events.erase(it);
                anyEvents.store(!events.empty());
            } else if (!it->second.confirmed) {
                Log(binding, source, "submitted_unknown");
            }
        }
    }
    activeClaim = previous;
}
NativeCallFrame::NativeCallFrame(PlayerbotAI* ai, unsigned skill, Unit* target, Item* item)
    : previous(activeFrame), claim(activeClaim) {
    activeFrame = this;
    if (!claim || previous || claim->entered) return;
    claim->entered = true; // first attempted call consumes this narrow context
    if (!Enabled() || !claim->binding.Valid() || claim->ai != ai || claim->target != target ||
        claim->binding.skillId != skill || item || !Actor(ai, claim->actor, claim->session)) return;
    claim->frame = this;
    eligible = true;
}
NativeCallFrame::~NativeCallFrame() { activeFrame = previous; }
void NativeCallFrame::SubmitBeforePrepare(Spell* response, Unit* target) {
    if (!eligible || used || activeFrame != this || activeClaim != claim || !Enabled()) return;
    used = true;
    auto const& b = claim->binding;
    if (!response || target != claim->target || !Live(target) ||
        !CurrentActor(b, claim->ai, claim->actor, claim->session, response) ||
        response->IsTriggered() || response->getState() != SPELL_STATE_NULL ||
        !response->GetSpellInfo() || response->GetSpellInfo()->Id != b.skillId ||
        claim->actor->GetMap() != target->GetMap() || !claim->actor->InSamePhase(target)) return;
    CastReceiptsV15::Identity current;
    auto old = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!old || CastReceiptsV15::Current(target, CURRENT_GENERIC_SPELL) != b.targetCastToken ||
        !CastReceiptsV15::ResolveSupportedCast(target, old, current) || !(current == claim->source)) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = events.find(b.task);
        if (!ExactActor(b, claim->ai) || it == events.end() || !(it->second.binding == b) ||
            it->second.ended || it->second.submitted || it->second.sourceSpell != reinterpret_cast<std::uintptr_t>(old) ||
            !reservations.Submit(b.task, b.owner, b.reservationToken, Now())) return;
        it->second.submitted = true;
    }
    actualResponse = response;
    // From this point original prepare still runs even when the observer rejects
    // binding. A failed sidecar bind therefore stays SubmittedUnknown.
    bool bound = InterruptObserverV17::BindShadowResponse(response, target, b);
    Log(b, claim->source, bound ? "bound" : "binding_rejected_unknown");
}
bool ValidateSubmitted(Binding const& b, Spell* response, Unit* target) {
    auto claim = activeClaim;
    auto frame = activeFrame;
    if (!Enabled() || !claim || !frame || claim->frame != frame || frame->claim != claim ||
        !frame->eligible || !frame->used || !(claim->binding == b) ||
        target != claim->target || !response || frame->actualResponse != response ||
        !CurrentActor(b, claim->ai, claim->actor, claim->session, response)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = events.find(b.task);
    return ExactActor(b, claim->ai) && it != events.end() && it->second.binding == b && it->second.submitted;
}
bool ObserveConfirmed(Binding const& b, Spell* response, std::uint64_t responseToken, CastReceiptsV15::Identity const& source) {
    auto claim = activeClaim;
    auto frame = activeFrame;
    if (!Enabled() || !responseToken || !claim || !frame || claim->frame != frame ||
        !(claim->binding == b) || frame->actualResponse != response ||
        !CurrentActor(b, claim->ai, claim->actor, claim->session, response)) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = events.find(b.task);
        if (!ExactActor(b, claim->ai) || it == events.end() || !(it->second.binding == b) ||
            !it->second.submitted || it->second.confirmed || !(it->second.source == source)) return false;
        it->second.confirmed = true;
    }
    // No reward or command is issued. Native effect proof is the sole success.
    Log(b, source, "confirmed", responseToken);
    return true;
}
void RetireEvent(TaskKey const& key) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        reservations.ObserveEventEnd(key);
        events.erase(key);
        anyEvents.store(!events.empty());
    }
    InterruptObserverV17::ForgetEvent(key);
}
void SourceEnded(Spell* spell) {
    if (!anyEvents.load()) return; // default-off native Ended takes no book lock
    std::vector<TaskKey> keys;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& row : events) if (row.second.sourceSpell == reinterpret_cast<std::uintptr_t>(spell)) {
            row.second.ended = true;
            keys.push_back(row.first);
        }
    }
    for (auto const& key : keys)
        if (!InterruptObserverV17::DeferEventRetirement(key)) RetireEvent(key);
}
void ForgetMap(std::uintptr_t map) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        reservations.ForgetMap(map);
        for (auto it = events.begin(); it != events.end(); )
            if (it->first[0] == map) it = events.erase(it); else ++it;
        anyEvents.store(!events.empty());
    }
    InterruptObserverV17::ForgetMap(map);
}
void ForgetRoot(std::uintptr_t map, std::uint64_t root) {
    std::vector<TaskKey> keys;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto const& row : events)
            if (row.second.source.map == map && row.second.source.root == root) keys.push_back(row.first);
    }
    Retire(keys);
}
}
