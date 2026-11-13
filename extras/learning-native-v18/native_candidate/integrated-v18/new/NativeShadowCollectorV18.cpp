#include "NativeShadowCollectorV18.h"
#include "BoundedShadowWriterV18.h"
#include "Playerbots.h"
#include "Config.h"
#include "ScriptMgr.h"
#include "Log.h"
#include <atomic>
#include <mutex>
#include <limits>

namespace ShadowCollectorV18 {
namespace {
ScopeRegistry registry;
std::mutex registryMutex;
std::atomic<bool> running(false), registryFault(false);
thread_local NativeTick* nativeTick=nullptr;
thread_local std::uint64_t streamId=0, tickSequence=0, lastStatsRun=0;
thread_local EngineShadowV17::Stats lastExportedStats={};
bool SameStats(EngineShadowV17::Stats const& a,EngineShadowV17::Stats const& b) {
    return a.accepted==b.accepted && a.ringFull==b.ringFull && a.scopeRejected==b.scopeRejected &&
        a.scopeChanged==b.scopeChanged && a.providerErrors==b.providerErrors && a.unfinished==b.unfinished &&
        a.identityExhausted==b.identityExhausted && a.multiplierDropped==b.multiplierDropped &&
        a.reconfigurationRejected==b.reconfigurationRejected;
}
static const std::uint32_t RegistryFaultFlag=1u<<16, StopTailUnknownFlag=1u<<17;
void Fault() noexcept { registryFault.store(true); AddCaptureFlags(RegistryFaultFlag|CaptureIncomplete); }
template<class F> void Mutate(F operation) noexcept {
    try {
        // Rare lifecycle updates serialize only bounded sidecar operations. No
        // native pointer dereference, writer call, or file IO occurs under this
        // lock; hot-path Resolve below still uses try-lock/drop. Contention is
        // ordinary concurrency, never a missing lifecycle notification.
        std::lock_guard<std::mutex> lock(registryMutex);
        operation();
    } catch(...) { Fault(); }
}
bool ResolveScope(PlayerbotAI* ai,void const* engine,EngineShadowV17::Scope& out) noexcept {
    if(!running.load() || !WriterEnabled() || registryFault.load() || !nativeTick ||
       !nativeTick->Admitted() || nativeTick->Actor()!=ai || !engine) return false;
    NativeFacts facts={};
    // This native adapter rereads the actual actor, membership, roots and epoch
    // on EVERY Begin/Finish. No legality probe, RNG, queue or cached AI value.
    if(!MopRaidActions::ReadShadowFactsV18(ai,facts)) return false;
    try {
        std::unique_lock<std::mutex> lock(registryMutex,std::try_to_lock);
        if(!lock.owns_lock()) return false;
        return !registryFault.load() && registry.Resolve(facts,reinterpret_cast<std::uintptr_t>(engine),
            WriterRunId(),streamId,nativeTick->Tick(),out);
    } catch(...) { Fault(); return false; }
}
class CollectorWorld final:public WorldScript {
public:
    CollectorWorld():WorldScript("raid_engine_shadow_collector_v18") {}
    void OnStartup() override { StartCollector(); }
    void OnShutdown() override { StopCollector(); }
};
}
void RegisterCollectorHook() { new CollectorWorld(); }
void StartCollector() noexcept {
    if(running.load() || !sConfigMgr->GetBoolDefault("RaidAI.ShadowCollectorV18.Enabled",false)) return;
    if(registryFault.load()) {
        TC_LOG_ERROR("playerbots","Shadow collector v18 refused startup after a registry lifecycle gap"); return;
    }
    try {
        WriterConfig config;
        config.directory=sConfigMgr->GetStringDefault("RaidAI.ShadowCollectorV18.Directory","");
        if(config.directory.empty()) {
            TC_LOG_ERROR("playerbots","Shadow collector v18 requires an explicit existing output directory"); return;
        }
        if(!StartWriter(config)) {
            TC_LOG_ERROR("playerbots","Shadow collector v18 writer startup failed (directory, existing files, or limits)"); return;
        }
        running.store(true);
    } catch(...) { Fault(); StopWriter(); }
}
void StopCollector() noexcept {
    running.store(false);
    // Shutdown may run after map threads stop. Never access another thread's ring:
    // a final partial owner-thread ring is explicitly an unknown tail, not success.
    AddCaptureFlags(CaptureIncomplete|StopTailUnknownFlag);
    StopWriter();
}
void RegisterActor(PlayerbotAI* ai,Player* actor) noexcept {
    if(!ai || !actor) return;
    std::uint64_t guid=actor->GetGUID().GetRawValue();
    Mutate([&](){ if(!registry.CreateActor(reinterpret_cast<std::uintptr_t>(ai),
        reinterpret_cast<std::uintptr_t>(actor),guid)) Fault(); });
}
void ForgetActor(PlayerbotAI* ai) noexcept {
    Mutate([&](){registry.ForgetActor(reinterpret_cast<std::uintptr_t>(ai));});
}
void ResetActor(PlayerbotAI* ai) noexcept {
    Mutate([&](){registry.ResetActor(reinterpret_cast<std::uintptr_t>(ai));});
}
void RegisterEngine(void const* engine,PlayerbotAI* ai) noexcept {
    Mutate([&](){if(!registry.CreateEngine(reinterpret_cast<std::uintptr_t>(engine),
        reinterpret_cast<std::uintptr_t>(ai))) Fault();});
}
void ForgetEngine(void const* engine) noexcept {
    Mutate([&](){registry.ForgetEngine(reinterpret_cast<std::uintptr_t>(engine));});
}
void ResetEngine(void const* engine) noexcept {
    Mutate([&](){registry.ResetEngine(reinterpret_cast<std::uintptr_t>(engine));});
}
void EncounterChanged(std::uintptr_t map) noexcept {
    Mutate([&](){if(!registry.TouchMap(map)) Fault();});
}
void ForgetMap(std::uintptr_t map) noexcept { Mutate([&](){registry.ForgetMap(map);}); }
void MapStateChanged(std::uintptr_t map) noexcept { Mutate([&](){registry.InvalidateMap(map);}); }
void RootRemoved(std::uintptr_t map) noexcept { MapStateChanged(map); }
NativeTick::NativeTick(PlayerbotAI* ai) noexcept
    :ai_(ai),previous_(nativeTick),tick_(0),admitted_(false) {
    nativeTick=this;
    if(running.load() && WriterEnabled() && !registryFault.load() && ai) {
        if(!streamId) Mutate([&](){streamId=registry.Next(); if(!streamId) Fault();});
        if(streamId && tickSequence!=std::numeric_limits<std::uint64_t>::max()) {
            tick_=++tickSequence; admitted_=true;
        } else Fault();
    }
    EngineShadowV17::ConfigureThread(admitted_?ResolveScope:nullptr,admitted_);
}
NativeTick::~NativeTick() noexcept {
    EngineShadowV17::ConfigureThread(nullptr,false);
    // Only the diagnostic ring, never Engine's native action queue. Fixed budget
    // bounds map-thread work even if producers overrun the async writer.
    EngineShadowV17::Stats stats=EngineShadowV17::ReadStats();
    EngineShadowV17::Record record;
    bool exported=false;
    for(unsigned n=0;n<32 && EngineShadowV17::PopRecord(record);++n)
        exported=TrySubmit(record,stats,CaptureIncomplete)||exported;
    std::uint64_t run=WriterRunId();
    if(!exported && admitted_ && run && (run!=lastStatsRun || !SameStats(stats,lastExportedStats)))
        exported=TrySubmitStreamStats(run,streamId,tick_,stats,CaptureIncomplete);
    if(exported) {lastExportedStats=stats;lastStatsRun=run;}
    nativeTick=previous_;
    bool restore=previous_ && previous_->Admitted() && running.load() && !registryFault.load();
    EngineShadowV17::ConfigureThread(restore?ResolveScope:nullptr,restore);
}
}
