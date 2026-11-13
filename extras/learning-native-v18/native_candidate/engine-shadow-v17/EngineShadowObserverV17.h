#ifndef ENGINE_SHADOW_OBSERVER_V17_H
#define ENGINE_SHADOW_OBSERVER_V17_H
#include <cstddef>
#include <cstdint>
class PlayerbotAI;

// Raw diagnostics only. No authority, action execution, model, file IO or game-object ownership.
namespace EngineShadowV17 {
static const std::size_t RingCapacity = 128;
static const std::size_t MultiplierCapacity = 16;
static const char SourceEngineSha256[] = "3a4576ff06835e2e877e1ca421b6aadc9ae96bd84929a39f21dcde4a5c1e5d5e";
enum Entry : std::uint8_t { QueueDecision = 1, DirectDecision = 2 };
enum RawOutcome : std::uint8_t { Unfinished = 0, Unknown, Useless, Impossible, PrerequisiteDeferred, EngineOk, EngineFailed };
enum Field : std::uint32_t {
    ActionKnown = 1u << 0, Useful = 1u << 1, Possible = 1u << 2,
    RelevancePositive = 1u << 3, SkipPrerequisites = 1u << 4,
    PrerequisitesPushed = 1u << 5, BeforeAllowed = 1u << 6,
    ExecutionAllowed = 1u << 7, ExecuteCalled = 1u << 8,
    ExecuteReturned = 1u << 9, ListenerFinalReturned = 1u << 10,
    AfterCompleted = 1u << 11, InitialRelevance = 1u << 12,
    FinalRelevance = 1u << 13, ActionFingerprint = 1u << 14,
    QualifierPresent = 1u << 15
};
// Supplied only by a trusted native, map-thread test-session owner. All identifiers
// are opaque scalar IDs; no account/chat text. tick belongs to this exact native
// decision scope, not wall clock. map/instance/group zero are valid values.
struct Scope {
    std::uint64_t run, session, actor, actorIncarnation, aiIncarnation;
    std::uint64_t group, epoch, mapIncarnation, stream, engine, engineIncarnation, tick;
    std::uint32_t map, instance;
};
// Must recheck current actor, group, test session, generations and owner thread on
// EVERY invocation. A provider is evidence, never self-granted action authority.
// The passed game pointers are ephemeral and MUST NOT be retained. No async calls.
// The model has no installation/configuration transport in this candidate.
typedef bool (*ScopeProvider)(PlayerbotAI*, void const* engine, Scope& current);
struct MultiplierValue { float factor, cumulativeRelevance; bool cumulativeEvaluated; };
struct Record {
    Scope scope;
    std::uint64_t decision, actionFingerprint;
    std::uint32_t evaluated, values, depth, iteration, multiplierCount, multiplierDropped;
    float initialRelevance, finalRelevance;
    Entry entry;
    RawOutcome outcome;
    bool minimal;
    MultiplierValue multipliers[MultiplierCapacity];
};
struct Stats {
    std::uint64_t accepted, ringFull, scopeRejected, scopeChanged, providerErrors;
    std::uint64_t unfinished, identityExhausted, multiplierDropped, reconfigurationRejected;
};
// Owner-thread only. Default state: disabled, provider unset. Configuration must
// be installed manually by trusted native test harness code, never model output.
// Reconfiguration invalidates in-flight observations; it does not reset IDs/stats.
void ConfigureThread(ScopeProvider provider, bool enabled) noexcept;
// Drain only this diagnostic ring, never the Engine queue. Same owner thread only.
bool PopRecord(Record& out) noexcept;
Stats ReadStats() noexcept;
std::size_t BufferedRecords() noexcept;

class Decision {
public:
    Decision(PlayerbotAI* ai, void const* engine, Entry entry, bool minimal = false,
             std::uint32_t depth = 0, std::uint32_t iteration = 0) noexcept;
    ~Decision() noexcept;
    bool Active() const noexcept { return active_; }
    bool Observe(Field field, bool value) noexcept;
    void SetActionFingerprint(char const* name) noexcept;
    void SetInitialRelevance(float value) noexcept;
    float ObserveMultiplier(float factor) noexcept;
    void MultiplierApplied(float relevance) noexcept;
    void SetFinalRelevance(float value) noexcept;
    void SetOutcome(RawOutcome outcome) noexcept;
    void Finish(PlayerbotAI* ai, void const* engine) noexcept;
private:
    Decision(Decision const&) = delete;
    Decision& operator=(Decision const&) = delete;
    Record record_;
    Decision* previous_;
    std::uint64_t configuration_;
    bool active_, finished_;
};
Decision* CurrentDecision() noexcept;
bool Observe(Decision* decision, Field field, bool value) noexcept;
void Mark(Decision* decision, Field field, bool value) noexcept;
}
#endif
