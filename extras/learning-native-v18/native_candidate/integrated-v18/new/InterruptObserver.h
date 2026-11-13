#pragma once
#include "deps/interrupt-attempt-v16/AttemptBook.h"
#include "CastReceipts.h"
class Spell;
class Unit;
namespace InterruptObserverV17 {
// Evidence only. No scheduler, authority grant or ReservationBook release.
// V18 native caller owns a real exact shadow submission and validates the
// synchronous actor/AI/session binding. This observer never grants an action.
bool BindShadowResponse(Spell*, Unit* target, InterruptAttemptV16::Binding const&);
void PrepareEntered(Spell*); // actual prepare only; never CheckCast probes
void Ended(Spell*);          // cleanup only, no inferred outcome
void ForgetMap(std::uintptr_t);
void ForgetEvent(InterruptAttemptV16::TaskKey const&);
// Defer only the exact source event ended inside its receipted native cancel.
bool DeferEventRetirement(InterruptAttemptV16::TaskKey const&);

class NativeInterruptFrame {
public:
    NativeInterruptFrame();
    ~NativeInterruptFrame();
    NativeInterruptFrame(NativeInterruptFrame const&) = delete;
    NativeInterruptFrame& operator=(NativeInterruptFrame const&) = delete;
private:
    std::uint64_t previousDepth;
    bool previousOverflow;
};
struct NativeCancelReceipt {
    std::uintptr_t scope = 0;
    std::uint64_t generation = 0, depth = 0;
};
// BeforeNativeCancel is placed AFTER CastInterrupted callback, immediately before
// the actual cancel call, and only in its not-FINISHED branch.
NativeCancelReceipt BeforeNativeCancel(Unit*, Spell*, unsigned slot);
void AfterNativeCancel(NativeCancelReceipt const&, Unit*, unsigned slot);

class EffectScope {
public:
    EffectScope(Spell* response, Unit* target, Spell* oldCast, unsigned slot);
    ~EffectScope(); // restores TLS only; never confirms an effect
    bool Complete(); // explicit, after InterruptSpell returned
    EffectScope(EffectScope const&) = delete;
    EffectScope& operator=(EffectScope const&) = delete;
private:
    friend NativeCancelReceipt BeforeNativeCancel(Unit*, Spell*, unsigned);
    friend void AfterNativeCancel(NativeCancelReceipt const&, Unit*, unsigned);
    friend bool DeferEventRetirement(InterruptAttemptV16::TaskKey const&);
    EffectScope* previous = nullptr;
    InterruptAttemptV16::InterruptEvidence evidence{};
    CastReceiptsV15::Identity source{};
    Spell* response = nullptr;
    Unit* target = nullptr;
    Spell* oldCast = nullptr; // opaque comparison after cancellation, never deref
    unsigned slot = 0;
    std::uint64_t generation = 0, expectedDepth = 0;
    bool nativeCancelled = false, completed = false;
    bool cancelEntered = false, retirePending = false;
};
}
