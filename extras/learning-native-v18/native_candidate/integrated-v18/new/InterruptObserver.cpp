#include "InterruptObserver.h"
#include "ResponseBindingV18.h"
#include "NativeCastSourceResolver.h"
#include "MechanicActionAuthority.h"
#include "Unit.h"
#include "Creature.h"
#include "TemporarySummon.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Map.h"
#include "Config.h"
#include "Log.h"
#include <atomic>
#include <limits>
#include <map>
#include <mutex>

namespace InterruptObserverV17 {
namespace {
using namespace InterruptAttemptV16;
struct Context { Handle handle{}; CastReceiptsV15::Identity source{}; };
AttemptBook attempts;
std::map<std::uintptr_t, Context> responses;
std::mutex mutex;
std::atomic<bool> anyResponses{false};
thread_local EffectScope* activeScope = nullptr;
thread_local std::uint64_t nativeDepth = 0, scopeSerial = 0;
thread_local bool depthOverflow = false;
bool Enabled() {
    return sConfigMgr->GetBoolDefault("RaidAI.InterruptObserverV17.Shadow", false);
}
std::uintptr_t Address(Spell* spell) { return reinterpret_cast<std::uintptr_t>(spell); }
bool Live(Unit* unit) {
    return unit && unit->IsInWorld() && !unit->IsDuringRemoveFromWorld() && unit->IsAlive();
}
bool ExecutorMatches(Spell* response, Unit* target, Binding const& binding) {
    if (!response || !Live(target) ||
        !ResponseBindingV18::ValidateSubmitted(binding, response, target)) return false;
    // Actual executor only: never OriginalCaster or a pet's controller GUID.
    Unit* actor = response->GetCaster();
    return Live(actor) && actor->GetMap() == target->GetMap() &&
        actor->GetGUID().GetRawValue() == binding.owner[0] &&
        actor->InSamePhase(target) && response->GetSpellInfo() &&
        response->GetSpellInfo()->Id == binding.skillId && !response->IsTriggered();
}
bool CurrentSource(Unit* target, Spell* oldCast, unsigned slot,
    Binding const& binding, CastReceiptsV15::Identity& source) {
    if (!Live(target) || !oldCast || slot != CURRENT_GENERIC_SPELL ||
        target->GetCurrentSpell(CurrentSpellTypes(slot)) != oldCast ||
        CastReceiptsV15::Current(target, slot) != binding.targetCastToken ||
        !CastReceiptsV15::ResolveSupportedCast(target, oldCast, source)) return false;
    return source.Valid() && source.map == binding.task[0] &&
        target->GetMap()->GetInstanceId() == binding.task[1] &&
        source.epoch == binding.task[2] && source.source == binding.task[3] &&
        binding.task[4] == binding.targetCastToken && source.slot == slot;
}
bool SameContextAfterCancel(Unit* target, CastReceiptsV15::Identity const& id) {
    // No old Spell dereference or lookup of its already-retired V15 receipt.
    if (!Enabled() || !Live(target) || !id.Valid() ||
        reinterpret_cast<std::uintptr_t>(target) != id.sourceObject ||
        reinterpret_cast<std::uintptr_t>(target->GetMap()) != id.map ||
        target->GetGUID().GetRawValue() != id.source) return false;
    auto source = target->ToCreature();
    auto map = target->GetMap();
    if (!source || !map || map->GetCreature(source->GetGUID()) != source ||
        !source->IsInCombat() || source->IsInEvadeMode()) return false;
    auto summon = source->ToTempSummon();
    auto root = summon && summon->GetSummoner() ? summon->GetSummoner()->ToCreature() : nullptr;
    return root && Live(root) && !root->IsInEvadeMode() && root->IsInCombat() &&
        root->GetMap() == map && root->GetGUID().GetRawValue() == id.root &&
        summon->GetSummonerGUID().GetRawValue() == id.root &&
        map->GetCreature(root->GetGUID()) == root && source->InSamePhase(root) &&
        MopRaidActions::EncounterEpoch(root) == id.epoch;
}
bool Lookup(Spell* response, Context& out) {
    if (!anyResponses.load()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = responses.find(Address(response));
    if (it == responses.end()) return false;
    out = it->second;
    return true;
}
}

bool BindShadowResponse(Spell* response, Unit* target, Binding const& binding) {
    if (!Enabled() || !binding.Valid() || !response ||
        response->getState() != SPELL_STATE_NULL || !ExecutorMatches(response, target, binding)) return false;
    Spell* oldCast = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    CastReceiptsV15::Identity source;
    if (!CurrentSource(target, oldCast, CURRENT_GENERIC_SPELL, binding, source)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if (responses.count(Address(response)) || responses.size() >= 256) return false;
    Handle h = attempts.Bind(binding, Address(response));
    if (!h.Valid()) return false;
    responses.emplace(Address(response), Context{h, source});
    anyResponses.store(true);
    return true;
}
void PrepareEntered(Spell* response) {
    Context row;
    if (!Enabled() || !Lookup(response, row)) return;
    // From here even a failing CheckCast can execute scripts: no negative proof.
    attempts.EnterExecution(row.handle);
}
void Ended(Spell* response) {
    if (!anyResponses.load()) return; // no global lock on the default-off path
    std::lock_guard<std::mutex> lock(mutex);
    auto it = responses.find(Address(response));
    if (it == responses.end()) return;
    attempts.End(it->second.handle);
    responses.erase(it); // no success, no no-effect and no release
    anyResponses.store(!responses.empty());
}
void ForgetMap(std::uintptr_t map) {
    std::lock_guard<std::mutex> lock(mutex);
    attempts.ForgetMap(map);
    for (auto it = responses.begin(); it != responses.end(); )
        if (it->second.handle.binding.task[0] == map) it = responses.erase(it); else ++it;
    anyResponses.store(!responses.empty());
}
void ForgetEvent(TaskKey const& key) {
    std::lock_guard<std::mutex> lock(mutex);
    attempts.ForgetEvent(key);
    for (auto it = responses.begin(); it != responses.end(); )
        if (it->second.handle.binding.task == key) it = responses.erase(it); else ++it;
    anyResponses.store(!responses.empty());
}
NativeInterruptFrame::NativeInterruptFrame()
    : previousDepth(nativeDepth), previousOverflow(depthOverflow) {
    if (nativeDepth == std::numeric_limits<std::uint64_t>::max()) depthOverflow = true;
    else ++nativeDepth;
}
NativeInterruptFrame::~NativeInterruptFrame() {
    nativeDepth = previousDepth;
    depthOverflow = previousOverflow;
}
EffectScope::EffectScope(Spell* response, Unit* target_, Spell* oldCast_, unsigned slot_)
    : previous(activeScope), response(response), target(target_), oldCast(oldCast_), slot(slot_) {
    // Invalid inner scopes mask outer ones too, so unrelated nested effects
    // cannot borrow a parent's evidence. Serial saturation disables new scopes.
    activeScope = this;
    if (!Enabled() || depthOverflow || scopeSerial == std::numeric_limits<std::uint64_t>::max() ||
        nativeDepth == std::numeric_limits<std::uint64_t>::max()) return;
    generation = ++scopeSerial;
    expectedDepth = nativeDepth + 1;
    Context row;
    CastReceiptsV15::Identity current;
    if (!Enabled() || !Lookup(response, row) ||
        !ExecutorMatches(response, target, row.handle.binding) ||
        !CurrentSource(target, oldCast, slot, row.handle.binding, current) ||
        !(current == row.source)) return;
    evidence = attempts.CaptureInterrupt(row.handle, row.handle.binding.targetCastToken);
    if (evidence.response.Valid()) source = current;
}
EffectScope::~EffectScope() {
    // An abandoned proof still retires a genuinely ended native event; it never
    // confirms an outcome and never releases on the response's lifetime alone.
    if (retirePending) ResponseBindingV18::RetireEvent(evidence.response.binding.task);
    activeScope = previous;
}
bool DeferEventRetirement(TaskKey const& key) {
    EffectScope* scope = activeScope;
    if (!scope || !scope->cancelEntered || scope->completed || depthOverflow ||
        nativeDepth != scope->expectedDepth || !scope->evidence.response.Valid() ||
        scope->evidence.response.binding.task != key) return false;
    scope->retirePending = true;
    return true;
}
NativeCancelReceipt BeforeNativeCancel(Unit* target, Spell* spell, unsigned slot) {
    EffectScope* scope = activeScope;
    if (!scope || !scope->generation || !scope->evidence.response.Valid() ||
        scope->completed || scope->nativeCancelled || depthOverflow || nativeDepth != scope->expectedDepth ||
        target != scope->target || spell != scope->oldCast || slot != scope->slot ||
        !Enabled()) return {};
    CastReceiptsV15::Identity current;
    // Recheck AFTER arbitrary CastInterrupted callbacks and immediately BEFORE
    // the actual cancel call. FINISHED cleanup never enters this function.
    if (!CurrentSource(target, spell, slot, scope->evidence.response.binding, current) ||
        !(current == scope->source) || spell->getState() != SPELL_STATE_PREPARING ||
        !ExecutorMatches(scope->response, target, scope->evidence.response.binding)) return {};
    scope->cancelEntered = true;
    return {reinterpret_cast<std::uintptr_t>(scope), scope->generation, nativeDepth};
}
void AfterNativeCancel(NativeCancelReceipt const& receipt, Unit* target, unsigned slot) {
    EffectScope* scope = activeScope;
    if (!scope || depthOverflow || !receipt.scope || reinterpret_cast<std::uintptr_t>(scope) != receipt.scope ||
        receipt.generation != scope->generation || receipt.depth != nativeDepth ||
        nativeDepth != scope->expectedDepth || target != scope->target || slot != scope->slot ||
        !scope->evidence.response.Valid() || scope->completed) return;
    scope->nativeCancelled = target->GetCurrentSpell(CurrentSpellTypes(slot)) != scope->oldCast &&
        SameContextAfterCancel(target, scope->source);
}
bool EffectScope::Complete() {
    if (completed || activeScope != this) return false;
    completed = true;
    bool observed = nativeCancelled && !depthOverflow && nativeDepth + 1 == expectedDepth &&
        evidence.response.Valid() && SameContextAfterCancel(target, source) &&
        target->GetCurrentSpell(CurrentSpellTypes(slot)) != oldCast;
    bool confirmed = observed && ExecutorMatches(response, target, evidence.response.binding) &&
        attempts.ConfirmInterrupt(evidence, true);
    bool consumed = confirmed && ResponseBindingV18::ObserveConfirmed(evidence.response.binding, response,
        evidence.response.responseToken, source);
    // V15 retires its nonce inside cancel(). Preserve proof just until this
    // consumer runs, then retire the same task in both shadow books.
    if (retirePending) {
        retirePending = false;
        ResponseBindingV18::RetireEvent(evidence.response.binding.task);
    }
    return consumed;
}
}
