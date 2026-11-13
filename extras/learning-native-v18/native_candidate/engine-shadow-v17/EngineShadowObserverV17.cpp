#include "EngineShadowObserverV17.h"
#include <limits>
#include <type_traits>

namespace EngineShadowV17 {
namespace {
struct ThreadState {
    Record ring[RingCapacity];
    Stats stats;
    ScopeProvider provider;
    Decision* current;
    std::uint64_t sequence, configuration;
    std::size_t head, size;
    bool enabled;
};
thread_local ThreadState state = {};
void Increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
}
bool CompleteScope(Scope const& s) noexcept {
    return s.run && s.session && s.actor && s.actorIncarnation && s.aiIncarnation &&
           s.epoch && s.mapIncarnation && s.stream && s.engine && s.engineIncarnation;
}
bool SameScope(Scope const& a, Scope const& b) noexcept {
    return a.run == b.run && a.session == b.session && a.actor == b.actor &&
           a.actorIncarnation == b.actorIncarnation && a.aiIncarnation == b.aiIncarnation &&
           a.group == b.group && a.epoch == b.epoch && a.mapIncarnation == b.mapIncarnation &&
           a.stream == b.stream && a.engine == b.engine && a.engineIncarnation == b.engineIncarnation &&
           a.tick == b.tick && a.map == b.map && a.instance == b.instance;
}
bool Resolve(PlayerbotAI* ai, void const* engine, Scope& result) noexcept {
    if (!state.enabled || !state.provider) return false;
    try { return state.provider(ai, engine, result) && CompleteScope(result); }
    catch (...) { Increment(state.stats.providerErrors); return false; }
}
}
static_assert(std::is_trivially_copyable<Record>::value, "diagnostics must be scalar copies");
static_assert(std::is_standard_layout<Record>::value, "diagnostics must not retain game objects");
void ConfigureThread(ScopeProvider provider, bool enabled) noexcept {
    if (state.configuration == std::numeric_limits<std::uint64_t>::max()) {
        state.enabled = false; state.provider = 0;
        Increment(state.stats.reconfigurationRejected); return;
    }
    ++state.configuration;
    state.provider = provider;
    state.enabled = enabled && provider;
}
bool PopRecord(Record& out) noexcept {
    if (!state.size) return false;
    out = state.ring[state.head];
    state.head = (state.head + 1) % RingCapacity;
    --state.size;
    return true;
}
Stats ReadStats() noexcept { return state.stats; }
std::size_t BufferedRecords() noexcept { return state.size; }
Decision* CurrentDecision() noexcept { return state.current; }
Decision::Decision(PlayerbotAI* ai, void const* engine, Entry entry, bool minimal,
                   std::uint32_t depth, std::uint32_t iteration) noexcept
    : record_(), previous_(state.current), configuration_(state.configuration), active_(false), finished_(false) {
    // Even inactive nested decisions mask the outer decision's listener recorder.
    state.current = this;
    if (!state.enabled || !state.provider) return;
    if (!Resolve(ai, engine, record_.scope)) { Increment(state.stats.scopeRejected); return; }
    if (state.sequence == std::numeric_limits<std::uint64_t>::max()) {
        Increment(state.stats.identityExhausted); return;
    }
    record_.decision = ++state.sequence; // Never reset/reused, no game RNG.
    record_.entry = entry; record_.minimal = minimal;
    record_.depth = depth; record_.iteration = iteration;
    active_ = true;
}
Decision::~Decision() noexcept {
    if (active_ && !finished_) Increment(state.stats.unfinished);
    state.current = previous_;
}
bool Decision::Observe(Field field, bool value) noexcept {
    if (active_ && !finished_) {
        record_.evaluated |= field;
        if (value) record_.values |= field; else record_.values &= ~std::uint32_t(field);
    }
    return value;
}
void Decision::SetActionFingerprint(char const* name) noexcept {
    if (!active_ || finished_ || !name) return;
    // Bounded noncryptographic diagnostic fingerprint, NOT a unique decision ID,
    // full candidate identity, provenance proof or suitable public anonymization.
    // No raw action name, qualifier, Event payload or chat is copied into records.
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < 256 && name[i]; ++i) { hash ^= static_cast<unsigned char>(name[i]); hash *= 1099511628211ULL; }
    record_.actionFingerprint = hash;
    record_.evaluated |= ActionFingerprint;
}
void Decision::SetInitialRelevance(float value) noexcept {
    if (active_ && !finished_) { record_.initialRelevance = value; record_.evaluated |= InitialRelevance; }
}
float Decision::ObserveMultiplier(float factor) noexcept {
    if (active_ && !finished_) {
        if (record_.multiplierCount < MultiplierCapacity) {
            record_.multipliers[record_.multiplierCount].factor = factor;
        } else {
            if (record_.multiplierDropped != std::numeric_limits<std::uint32_t>::max()) ++record_.multiplierDropped;
            Increment(state.stats.multiplierDropped);
        }
        if (record_.multiplierCount != std::numeric_limits<std::uint32_t>::max()) ++record_.multiplierCount;
    }
    return factor;
}
void Decision::MultiplierApplied(float relevance) noexcept {
    if (active_ && !finished_ && record_.multiplierCount && record_.multiplierCount <= MultiplierCapacity) {
        MultiplierValue& value = record_.multipliers[record_.multiplierCount - 1];
        value.cumulativeRelevance = relevance; value.cumulativeEvaluated = true;
    }
}
void Decision::SetFinalRelevance(float value) noexcept {
    if (active_ && !finished_) { record_.finalRelevance = value; record_.evaluated |= FinalRelevance; }
}
void Decision::SetOutcome(RawOutcome outcome) noexcept { if (active_ && !finished_) record_.outcome = outcome; }
void Decision::Finish(PlayerbotAI* ai, void const* engine) noexcept {
    if (!active_ || finished_) return;
    finished_ = true;
    Scope current = {};
    if (configuration_ != state.configuration || !Resolve(ai, engine, current) ||
        configuration_ != state.configuration || !state.enabled || !state.provider ||
        !SameScope(record_.scope, current)) {
        Increment(state.stats.scopeChanged); return;
    }
    if (record_.outcome == Unfinished) { Increment(state.stats.unfinished); return; }
    if (state.size == RingCapacity) { Increment(state.stats.ringFull); return; }
    state.ring[(state.head + state.size) % RingCapacity] = record_;
    ++state.size;
    Increment(state.stats.accepted);
}
bool Observe(Decision* decision, Field field, bool value) noexcept { return decision ? decision->Observe(field, value) : value; }
void Mark(Decision* decision, Field field, bool value) noexcept { if (decision) decision->Observe(field, value); }
}
