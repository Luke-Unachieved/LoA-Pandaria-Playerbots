#ifndef NATIVE_SHADOW_COLLECTOR_V18_H
#define NATIVE_SHADOW_COLLECTOR_V18_H
#include "ScopeRegistryV18.h"
class Player; class PlayerbotAI;
namespace MopRaidActions {
// Implemented beside the existing native authority checks, read only; not an
// action claim, witness, training permission, eligibility label or reward.
bool ReadShadowFactsV18(PlayerbotAI*, ShadowCollectorV18::NativeFacts&) noexcept;
}
namespace ShadowCollectorV18 {
void RegisterCollectorHook();
void StartCollector() noexcept; // world startup, reads default-false config
void StopCollector() noexcept;  // world shutdown, stops admissions then writer
void RegisterActor(PlayerbotAI*,Player*) noexcept;
void ForgetActor(PlayerbotAI*) noexcept;
void ResetActor(PlayerbotAI*) noexcept;
void RegisterEngine(void const*,PlayerbotAI*) noexcept;
void ForgetEngine(void const*) noexcept;
void ResetEngine(void const*) noexcept;
void EncounterChanged(std::uintptr_t map) noexcept;
void ForgetMap(std::uintptr_t map) noexcept;
void MapStateChanged(std::uintptr_t map) noexcept;
void RootRemoved(std::uintptr_t map) noexcept;
// Constructed only by the real UpdateAIInternal map-thread callback. Engine
// observations outside that callback have no provider scope, including commands
// on other threads. The pointer is ephemeral, limited to this stack lifetime.
class NativeTick {
public:
    explicit NativeTick(PlayerbotAI*) noexcept;
    ~NativeTick() noexcept;
    PlayerbotAI* Actor() const noexcept { return ai_; }
    std::uint64_t Tick() const noexcept { return tick_; }
    bool Admitted() const noexcept { return admitted_; }
private:
    NativeTick(NativeTick const&)=delete;
    NativeTick& operator=(NativeTick const&)=delete;
    PlayerbotAI* ai_;
    NativeTick* previous_;
    std::uint64_t tick_;
    bool admitted_;
};
}
#endif
