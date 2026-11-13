#ifndef BOUNDED_SHADOW_WRITER_V18_H
#define BOUNDED_SHADOW_WRITER_V18_H
#include "EngineShadowObserverV17.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace ShadowCollectorV18 {
// Administrative Start/Stop only: never call these from an AI/map callback.
// directory must already exist. Invalid/out-of-bound limits fail closed.
struct WriterConfig {
    std::string directory;
    std::uint32_t maxFiles;
    std::uint32_t maxFileBytes;
    std::uint64_t maxTotalBytes;
    WriterConfig() : directory("."), maxFiles(4), maxFileBytes(4u * 1024u * 1024u),
                     maxTotalBytes(16u * 1024u * 1024u) {}
};
static const std::size_t WriterQueueCapacity = 512;
static const std::uint32_t WriterHardMaxFiles = 4;
static const std::uint32_t WriterHardMaxFileBytes = 4u * 1024u * 1024u;
static const std::uint64_t WriterHardMaxTotalBytes = 16u * 1024u * 1024u;
enum CaptureFlag : std::uint32_t {
    CaptureIncomplete = 1u << 0,
    ObserverDropped = 1u << 1,
    ObserverInvalidated = 1u << 2,
    WriterDropped = 1u << 3
};
struct WriterStats {
    // submitted/written count all tagged packets, including stream_stats.
    std::uint64_t submitted, written, queueFull, lockBusy, disabled, wrongRun, invalidStream;
    std::uint64_t budgetDropped, ioDropped, filesOpened, bytesWritten;
    bool budgetExhausted, ioFailed;
};
// The returned run is a native writer-start lifecycle ID seeded by system-clock
// nanoseconds, monotonically allocated in this process, not globally unique.
// Fixed exclusive segment names bound files/disk across restarts in one directory.
// Existing segment 0 makes Start fail closed. No gameplay RNG is used.
bool StartWriter(WriterConfig const& config) noexcept;
// Disables submission, handles at most the fixed pending queue, writes a final
// cumulative summary if IO remains possible, and joins the worker. Work/bytes
// are bounded; OS disk latency is not a wall-time guarantee.
void StopWriter() noexcept;
bool WriterEnabled() noexcept;
std::uint64_t WriterRunId() noexcept;
WriterStats ReadWriterStats() noexcept;
// Sticky diagnostic flags, retained through final summary. Does not grant authority.
void AddCaptureFlags(std::uint32_t flags) noexcept;
// Owner thread copies only scalar Record+Stats. No allocations, file IO, waits,
// native object pointers, or gameplay state. Failed try_lock/full queue drops the
// new record. The caller must bound its drain loop independently.
bool TrySubmit(EngineShadowV17::Record const& record, EngineShadowV17::Stats const& stats,
               std::uint32_t captureFlags = 0) noexcept;
// Distinct primitive diagnostics packet for a native tick with no action record.
// run/stream/tick must identify a real admitted native callback and be nonzero.
bool TrySubmitStreamStats(std::uint64_t run, std::uint64_t stream, std::uint64_t tick,
                         EngineShadowV17::Stats const& stats,
                         std::uint32_t captureFlags = 0) noexcept;
}
#endif
