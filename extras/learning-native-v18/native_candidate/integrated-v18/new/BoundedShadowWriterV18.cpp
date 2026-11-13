#include "BoundedShadowWriterV18.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ShadowCollectorV18 {
namespace {
static const std::uint64_t TerminalReserve = 2048;
enum PacketKind : std::uint8_t { ActionRecordPacket = 1, StreamStatsPacket = 2 };
struct StreamIdentity { std::uint64_t run, stream, tick; };
struct Packet {
    PacketKind kind;
    union Payload {
        EngineShadowV17::Record record;
        StreamIdentity streamStats;
    } payload;
    EngineShadowV17::Stats observer;
    std::uint32_t flags;
};
static_assert(std::is_trivially_copyable<Packet>::value, "worker payload must contain only primitive copies");
struct Counters {
    std::atomic<std::uint64_t> submitted, written, queueFull, lockBusy, disabled, wrongRun, invalidStream;
    std::atomic<std::uint64_t> budgetDropped, ioDropped, filesOpened, bytesWritten;
    std::atomic<bool> budgetExhausted, ioFailed;
    Counters() { Reset(); }
    void Reset() {
        submitted = 0; written = 0; queueFull = 0; lockBusy = 0; disabled = 0; wrongRun = 0; invalidStream = 0;
        budgetDropped = 0; ioDropped = 0; filesOpened = 0; bytesWritten = 0;
        budgetExhausted = false; ioFailed = false;
    }
};
struct WriterState {
    std::mutex mutex;
    std::condition_variable changed;
    Packet queue[WriterQueueCapacity];
    std::size_t head, size;
    bool stopRequested;
    std::atomic<bool> enabled;
    std::atomic<std::uint64_t> run;
    std::atomic<std::uint32_t> captureFlags;
    Counters counters;
    WriterConfig config;
    std::thread worker;
    std::FILE* file;
    std::uint64_t fileBytes, totalBytes, clockNanos, processSerial;
    std::uint32_t fileIndex, processId;
    WriterState() : head(0), size(0), stopRequested(false), enabled(false), run(0), captureFlags(0),
        file(NULL), fileBytes(0), totalBytes(0), clockNanos(0), processSerial(0),
        fileIndex(0), processId(0) {}
    ~WriterState() { StopWriter(); }
};
WriterState state;
std::uint64_t lastRun = 0, startSerial = 0; // administrative StartWriter thread only

std::uint64_t Load(std::atomic<std::uint64_t> const& n) { return n.load(std::memory_order_relaxed); }
void Add(std::atomic<std::uint64_t>& n) { n.fetch_add(1, std::memory_order_relaxed); }
std::FILE* OpenExclusive(std::string const& path) {
#if defined(_WIN32)
    int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return NULL;
    std::FILE* f = _fdopen(fd, "wb");
    if (!f) _close(fd);
#else
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    std::FILE* f = fdopen(fd, "wb");
    if (!f) close(fd);
#endif
    // Do not unlink on fdopen failure: preserving the exclusive reservation
    // makes a retry fail closed instead of permitting run identity reuse.
    return f;
}
std::string FileName(std::uint32_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << state.config.directory << "/raw-engine-shadow-v18-" << index << ".jsonl";
    return out.str();
}
void WriteObserver(std::ostream& out, EngineShadowV17::Stats const& s) {
    out << "{\"accepted\":" << s.accepted << ",\"ring_full\":" << s.ringFull
        << ",\"scope_rejected\":" << s.scopeRejected << ",\"scope_changed\":" << s.scopeChanged
        << ",\"provider_errors\":" << s.providerErrors << ",\"unfinished\":" << s.unfinished
        << ",\"identity_exhausted\":" << s.identityExhausted << ",\"multiplier_dropped\":" << s.multiplierDropped
        << ",\"reconfiguration_rejected\":" << s.reconfigurationRejected << '}';
}
void WriteCounters(std::ostream& out, WriterStats const& s) {
    out << "{\"submitted\":" << s.submitted << ",\"written\":" << s.written
        << ",\"queue_full\":" << s.queueFull << ",\"lock_busy\":" << s.lockBusy
        << ",\"disabled\":" << s.disabled << ",\"wrong_run\":" << s.wrongRun
        << ",\"invalid_stream\":" << s.invalidStream
        << ",\"budget_dropped\":" << s.budgetDropped << ",\"io_dropped\":" << s.ioDropped
        << ",\"files_opened\":" << s.filesOpened << ",\"bytes_written_before_line\":" << s.bytesWritten
        << ",\"budget_exhausted\":" << (s.budgetExhausted ? "true" : "false")
        << ",\"io_failed\":" << (s.ioFailed ? "true" : "false") << '}';
}
std::string Header(std::uint32_t index) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema\":\"raw-native-engine-shadow-v18\",\"type\":\"start\",\"diagnostic_only\":true,\"run\":"
        << state.run.load(std::memory_order_relaxed) << ",\"native_system_clock_ns\":" << state.clockNanos
        << ",\"process_id\":" << state.processId << ",\"process_start_serial\":" << state.processSerial
        << ",\"segment\":" << index << ",\"queue_capacity\":" << WriterQueueCapacity
        << ",\"max_files\":" << state.config.maxFiles << ",\"max_file_bytes\":" << state.config.maxFileBytes
        << ",\"max_total_bytes\":" << state.config.maxTotalBytes
        << ",\"source_engine_sha256\":\"" << EngineShadowV17::SourceEngineSha256 << "\"}\n";
    return out.str();
}
void FloatValue(std::ostream& out, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t), "raw diagnostic float bits require binary32 width");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    out << "{\"bits\":" << bits << ",\"number\":";
    if (std::isfinite(value)) out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    else out << "null";
    out << '}';
}
std::string Encode(Packet const& packet) {
    WriterStats w = ReadWriterStats();
    std::uint32_t flags = packet.flags | state.captureFlags.load(std::memory_order_relaxed);
    if (w.queueFull || w.lockBusy || w.disabled || w.wrongRun || w.invalidStream || w.budgetDropped || w.ioDropped)
        flags |= CaptureIncomplete | WriterDropped;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (packet.kind == StreamStatsPacket) {
        StreamIdentity const& id = packet.payload.streamStats;
        out << "{\"schema\":\"raw-native-engine-shadow-v18\",\"type\":\"stream_stats\",\"capture_flags\":" << flags
            << ",\"run\":" << id.run << ",\"stream\":" << id.stream << ",\"tick\":" << id.tick
            << ",\"observer_cumulative_thread_stats\":";
        WriteObserver(out, packet.observer);
        out << ",\"writer_cumulative_run_stats\":"; WriteCounters(out, w);
        out << "}\n";
        return out.str();
    }
    EngineShadowV17::Record const& r = packet.payload.record;
    EngineShadowV17::Scope const& s = r.scope;
    out << "{\"schema\":\"raw-native-engine-shadow-v18\",\"type\":\"record\",\"capture_flags\":" << flags
        << ",\"scope_kind\":" << unsigned(s.kind)
        << ",\"encounter_epoch_known\":" << (s.kind == EngineShadowV17::SupportedRaidEncounter && s.epoch != 0 ? "true" : "false")
        << ",\"observed_roster_count\":" << s.observedRoster << ",\"roster_observation_only\":true"
        << ",\"run\":" << s.run << ",\"session\":" << s.session << ",\"actor\":" << s.actor
        << ",\"actor_incarnation\":" << s.actorIncarnation << ",\"ai_incarnation\":" << s.aiIncarnation
        << ",\"group\":" << s.group << ",\"epoch\":" << s.epoch << ",\"map_incarnation\":" << s.mapIncarnation
        << ",\"stream\":" << s.stream << ",\"engine\":" << s.engine << ",\"engine_incarnation\":" << s.engineIncarnation
        << ",\"tick\":" << s.tick << ",\"map\":" << s.map << ",\"instance\":" << s.instance
        << ",\"decision\":" << r.decision << ",\"action_fingerprint\":" << r.actionFingerprint
        << ",\"evaluated\":" << r.evaluated << ",\"values\":" << r.values << ",\"depth\":" << r.depth
        << ",\"iteration\":" << r.iteration << ",\"multiplier_count\":" << r.multiplierCount
        << ",\"multiplier_dropped\":" << r.multiplierDropped << ",\"entry\":" << unsigned(r.entry)
        << ",\"outcome\":" << unsigned(r.outcome) << ",\"minimal\":" << (r.minimal ? "true" : "false")
        << ",\"initial_relevance\":";
    FloatValue(out, r.initialRelevance);
    out << ",\"final_relevance\":";
    FloatValue(out, r.finalRelevance);
    out << ",\"multipliers\":[";
    std::size_t count = r.multiplierCount < EngineShadowV17::MultiplierCapacity ? r.multiplierCount : EngineShadowV17::MultiplierCapacity;
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        out << "{\"factor\":"; FloatValue(out, r.multipliers[i].factor);
        out << ",\"cumulative_relevance\":"; FloatValue(out, r.multipliers[i].cumulativeRelevance);
        out << ",\"cumulative_evaluated\":" << (r.multipliers[i].cumulativeEvaluated ? "true" : "false") << '}';
    }
    out << "],\"observer_cumulative_thread_stats\":"; WriteObserver(out, packet.observer);
    out << ",\"writer_cumulative_run_stats\":"; WriteCounters(out, w);
    out << "}\n";
    return out.str();
}
bool WriteBytes(std::string const& line) {
    if (!state.file || line.size() > state.config.maxFileBytes - state.fileBytes ||
        line.size() > state.config.maxTotalBytes - state.totalBytes) return false;
    std::size_t actual = std::fwrite(line.data(), 1, line.size(), state.file);
    state.fileBytes += actual;
    state.totalBytes += actual;
    state.counters.bytesWritten.store(state.totalBytes, std::memory_order_relaxed);
    if (actual != line.size()) return false;
    // Buffer memory is small and fixed; flush permits IO errors to disable capture
    // promptly. This is worker/administrative code, never the map thread.
    return std::fflush(state.file) == 0;
}
void FailIo() {
    state.counters.ioFailed.store(true, std::memory_order_relaxed);
    state.enabled.store(false, std::memory_order_release);
}
bool RoomFor(std::size_t bytes) {
    return state.fileBytes + bytes + TerminalReserve <= state.config.maxFileBytes &&
           state.totalBytes + bytes + TerminalReserve <= state.config.maxTotalBytes;
}
bool Rotate(std::size_t bytes) {
    if (state.fileIndex + 1 >= state.config.maxFiles) return false;
    std::string header = Header(state.fileIndex + 1);
    if (header.size() + bytes + TerminalReserve > state.config.maxFileBytes ||
        state.totalBytes + header.size() + bytes + TerminalReserve > state.config.maxTotalBytes) return false;
    std::FILE* next = OpenExclusive(FileName(state.fileIndex + 1));
    if (!next) { FailIo(); return false; }
    Add(state.counters.filesOpened);
    if (std::fclose(state.file) != 0) {
        std::fclose(next); state.file = NULL; FailIo(); return false;
    }
    state.file = next;
    state.fileBytes = 0;
    ++state.fileIndex;
    if (!WriteBytes(header)) { FailIo(); return false; }
    return true;
}
void Handle(Packet const& packet) {
    if (state.counters.ioFailed.load(std::memory_order_relaxed)) { Add(state.counters.ioDropped); return; }
    if (state.counters.budgetExhausted.load(std::memory_order_relaxed)) { Add(state.counters.budgetDropped); return; }
    std::string line = Encode(packet);
    if (!RoomFor(line.size()) && !Rotate(line.size())) {
        if (state.counters.ioFailed.load(std::memory_order_relaxed)) Add(state.counters.ioDropped);
        else {
            state.counters.budgetExhausted.store(true, std::memory_order_relaxed);
            state.enabled.store(false, std::memory_order_release);
            Add(state.counters.budgetDropped);
        }
        return;
    }
    if (WriteBytes(line)) Add(state.counters.written);
    else { FailIo(); Add(state.counters.ioDropped); }
}
void Terminal() {
    if (!state.file || state.counters.ioFailed.load(std::memory_order_relaxed)) return;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema\":\"raw-native-engine-shadow-v18\",\"type\":\"stop\",\"run\":"
        << state.run.load(std::memory_order_relaxed) << ",\"queue_drained\":true,\"capture_flags\":"
        << (state.captureFlags.load(std::memory_order_relaxed) | CaptureIncomplete)
        << ",\"writer_cumulative_run_stats\":";
    WriteCounters(out, ReadWriterStats());
    out << "}\n";
    std::string line = out.str();
    if (line.size() > TerminalReserve || !WriteBytes(line)) FailIo();
}
void Worker() noexcept {
    bool handlingPacket = false;
    try {
        for (;;) {
            Packet packet;
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                state.changed.wait(lock, [] { return state.stopRequested || state.size != 0; });
                if (!state.size && state.stopRequested) break;
                packet = state.queue[state.head];
                state.head = (state.head + 1) % WriterQueueCapacity;
                --state.size;
            }
            handlingPacket = true;
            Handle(packet);
            handlingPacket = false;
        }
        Terminal();
    } catch (...) {
        FailIo();
        // No game pointers or cleanup callbacks can escape into this thread.
        std::lock_guard<std::mutex> lock(state.mutex);
        state.counters.ioDropped.fetch_add(state.size + (handlingPacket ? 1 : 0), std::memory_order_relaxed);
        state.size = 0;
    }
    if (state.file) {
        if (std::fclose(state.file) != 0) FailIo();
        state.file = NULL;
    }
}
} // namespace

WriterStats ReadWriterStats() noexcept {
    WriterStats s = {};
    s.submitted = Load(state.counters.submitted); s.written = Load(state.counters.written);
    s.queueFull = Load(state.counters.queueFull); s.lockBusy = Load(state.counters.lockBusy);
    s.disabled = Load(state.counters.disabled); s.wrongRun = Load(state.counters.wrongRun);
    s.invalidStream = Load(state.counters.invalidStream);
    s.budgetDropped = Load(state.counters.budgetDropped); s.ioDropped = Load(state.counters.ioDropped);
    s.filesOpened = Load(state.counters.filesOpened); s.bytesWritten = Load(state.counters.bytesWritten);
    s.budgetExhausted = state.counters.budgetExhausted.load(std::memory_order_relaxed);
    s.ioFailed = state.counters.ioFailed.load(std::memory_order_relaxed);
    return s;
}
void AddCaptureFlags(std::uint32_t flags) noexcept { state.captureFlags.fetch_or(flags, std::memory_order_relaxed); }
bool WriterEnabled() noexcept { return state.enabled.load(std::memory_order_acquire); }
std::uint64_t WriterRunId() noexcept { return state.run.load(std::memory_order_acquire); }
bool StartWriter(WriterConfig const& config) noexcept {
    if (state.worker.joinable() || state.file || config.directory.empty() || !config.maxFiles ||
        config.maxFiles > WriterHardMaxFiles || config.maxFileBytes < 4096 ||
        config.maxFileBytes > WriterHardMaxFileBytes || config.maxTotalBytes < 4096 ||
        config.maxTotalBytes > WriterHardMaxTotalBytes || startSerial == UINT64_MAX ||
        !state.run.is_lock_free() || !state.enabled.is_lock_free() || !state.captureFlags.is_lock_free()) return false;
    try {
        state.config = config;
        state.counters.Reset();
        state.captureFlags = 0;
        state.head = state.size = 0;
        state.stopRequested = false;
        state.fileBytes = state.totalBytes = 0;
        state.fileIndex = 0;
        std::int64_t ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (ticks <= 0 || lastRun == UINT64_MAX) return false;
        state.clockNanos = static_cast<std::uint64_t>(ticks);
        std::uint64_t run = state.clockNanos > lastRun ? state.clockNanos : lastRun + 1;
        lastRun = run;
        state.processSerial = ++startSerial;
#if defined(_WIN32)
        state.processId = static_cast<std::uint32_t>(_getpid());
#else
        state.processId = static_cast<std::uint32_t>(getpid());
#endif
        state.run.store(run, std::memory_order_release);
        state.file = OpenExclusive(FileName(0));
        if (!state.file) { state.run.store(0, std::memory_order_release); return false; }
        Add(state.counters.filesOpened);
        std::string header = Header(0);
        if (!RoomFor(header.size()) || !WriteBytes(header)) {
            std::fclose(state.file); state.file = NULL; FailIo(); state.run = 0; return false;
        }
        state.enabled.store(true, std::memory_order_release);
        state.worker = std::thread(Worker);
        return true;
    } catch (...) {
        state.enabled.store(false, std::memory_order_release);
        if (state.file) { std::fclose(state.file); state.file = NULL; }
        state.run.store(0, std::memory_order_release);
        return false;
    }
}
void StopWriter() noexcept {
    state.enabled.store(false, std::memory_order_release);
    if (state.worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.stopRequested = true;
        }
        state.changed.notify_one();
        state.worker.join();
    }
    state.run.store(0, std::memory_order_release);
}
static bool EnqueuePacket(Packet const& input, std::uint64_t run) noexcept {
    if (!WriterEnabled()) { Add(state.counters.disabled); return false; }
    if (!run || run != WriterRunId()) { Add(state.counters.wrongRun); return false; }
    try {
        std::unique_lock<std::mutex> lock(state.mutex, std::try_to_lock);
        if (!lock.owns_lock()) { Add(state.counters.lockBusy); return false; }
        if (!WriterEnabled() || state.stopRequested) { Add(state.counters.disabled); return false; }
        if (run != WriterRunId()) { Add(state.counters.wrongRun); return false; }
        if (state.size == WriterQueueCapacity) { Add(state.counters.queueFull); return false; }
        std::uint32_t captureFlags = input.flags;
        EngineShadowV17::Stats const& stats = input.observer;
        if (stats.ringFull || stats.multiplierDropped ||
            (input.kind == ActionRecordPacket && input.payload.record.multiplierDropped))
            captureFlags |= CaptureIncomplete | ObserverDropped;
        if (stats.scopeRejected || stats.scopeChanged || stats.providerErrors || stats.unfinished ||
            stats.identityExhausted || stats.reconfigurationRejected)
            captureFlags |= CaptureIncomplete | ObserverInvalidated;
        Packet& packet = state.queue[(state.head + state.size) % WriterQueueCapacity];
        packet = input;
        packet.flags = captureFlags;
        ++state.size;
        Add(state.counters.submitted);
        lock.unlock();
        state.changed.notify_one();
        return true;
    } catch (...) {
        // A mutex/system failure must not escape into native gameplay callers.
        Add(state.counters.lockBusy);
        AddCaptureFlags(CaptureIncomplete | WriterDropped);
        return false;
    }
}
bool TrySubmit(EngineShadowV17::Record const& record, EngineShadowV17::Stats const& stats,
               std::uint32_t captureFlags) noexcept {
    Packet packet = {};
    packet.kind = ActionRecordPacket;
    packet.payload.record = record;
    packet.observer = stats;
    packet.flags = captureFlags;
    return EnqueuePacket(packet, record.scope.run);
}
bool TrySubmitStreamStats(std::uint64_t run, std::uint64_t stream, std::uint64_t tick,
                         EngineShadowV17::Stats const& stats,
                         std::uint32_t captureFlags) noexcept {
    if (!stream || !tick) {
        Add(state.counters.invalidStream);
        AddCaptureFlags(CaptureIncomplete | WriterDropped);
        return false;
    }
    Packet packet = {};
    packet.kind = StreamStatsPacket;
    packet.payload.streamStats = StreamIdentity{run, stream, tick};
    packet.observer = stats;
    packet.flags = captureFlags;
    return EnqueuePacket(packet, run);
}
} // namespace ShadowCollectorV18
