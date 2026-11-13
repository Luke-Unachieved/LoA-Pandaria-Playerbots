#pragma once
#include "InterruptObserver.h"
class PlayerbotAI;
class Creature;
class Item;
namespace ResponseBindingV18 {
// Telemetry only. The existing native final legality + ClaimAction must precede
// this scope. Failure never rejects, retries or changes the native action.
class NativeCallFrame;
class NativeClaimScope {
public:
    NativeClaimScope(PlayerbotAI*, Creature* root, Creature* target, unsigned skill);
    ~NativeClaimScope();
    NativeClaimScope(NativeClaimScope const&) = delete;
    NativeClaimScope& operator=(NativeClaimScope const&) = delete;
private:
    friend class NativeCallFrame;
    friend bool ValidateSubmitted(InterruptAttemptV16::Binding const&, Spell*, Unit*);
    friend bool ObserveConfirmed(InterruptAttemptV16::Binding const&, Spell*, std::uint64_t, CastReceiptsV15::Identity const&);
    NativeClaimScope* previous = nullptr;
    NativeCallFrame* frame = nullptr;
    PlayerbotAI* ai = nullptr;
    Unit* actor = nullptr;
    Unit* target = nullptr;
    std::uintptr_t session = 0;
    CastReceiptsV15::Identity source{};
    InterruptAttemptV16::Binding binding{};
    bool entered = false;
};
// At entry to the unit-target CastSpell overload, so nested calls mask the outer
// issuer even when they have identical actor/spell/target arguments.
class NativeCallFrame {
public:
    NativeCallFrame(PlayerbotAI*, unsigned skill, Unit*, Item*);
    ~NativeCallFrame();
    void SubmitBeforePrepare(Spell*, Unit* finalTarget);
    NativeCallFrame(NativeCallFrame const&) = delete;
    NativeCallFrame& operator=(NativeCallFrame const&) = delete;
private:
    friend bool ValidateSubmitted(InterruptAttemptV16::Binding const&, Spell*, Unit*);
    friend bool ObserveConfirmed(InterruptAttemptV16::Binding const&, Spell*, std::uint64_t, CastReceiptsV15::Identity const&);
    NativeCallFrame* previous = nullptr;
    NativeClaimScope* claim = nullptr;
    Spell* actualResponse = nullptr;
    bool eligible = false, used = false;
};
void RegisterActor(PlayerbotAI*); // exact native ctor; no action authority
void ForgetActor(PlayerbotAI*);   // exact native dtor; no inferred outcome
void ForgetMap(std::uintptr_t);
void ForgetRoot(std::uintptr_t, std::uint64_t root);
// Exact V15 Ended hook, before its receipt is erased. Never dereferences Spell.
void SourceEnded(Spell*);
// Called by V17 after the deferred native cancel proof has been consumed.
void RetireEvent(InterruptAttemptV16::TaskKey const&);
// V17 must validate before binding/capturing and consume only ConfirmInterrupt.
bool ValidateSubmitted(InterruptAttemptV16::Binding const&, Spell*, Unit*);
bool ObserveConfirmed(InterruptAttemptV16::Binding const&, Spell*, std::uint64_t responseToken,
    CastReceiptsV15::Identity const&);
}
