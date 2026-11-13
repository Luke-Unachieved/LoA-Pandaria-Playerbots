#pragma once
// GPL-2.0-or-later. Native Playerbots adapter, default disabled. No model API.
#include <cstdint>
#include <string>
class Player;
namespace NativeBackgroundTraining {
// WorldScript::OnStartup/OnShutdown; exactly one process-wide service.
void Startup();
void Shutdown();
// WorldScript::OnUpdate, immediately before existing post-map operation processor.
// This function never resolves a Player* or dereferences a Map*/InstanceScript*.
void WorldUpdate(std::uint32_t diff);
// PlayerScript::OnAfterUpdate, after ordinary native AI completes, on owning map.
void ObservePlayer(Player* player);
// PlayerbotAI constructors/destructor, composed by collector-v18 lifecycle patch.
// nullptr is valid for default PlayerbotAI constructor and ignored.
void OnAiCreated(Player* player);
void OnAiDestroyed(Player* player);
struct Diagnostic {
    bool enabled = false, admissionEnabled = false, inCombat = false, ready = false;
    unsigned available = 0, tanks = 0, healers = 0, damage = 0;
    unsigned queued = 0, reservedActors = 0, admitted = 0;
    std::string dungeon = "disabled";
    std::string raid10 = "waiting_raid10_entry_unimplemented";
    std::string raid25 = "waiting_raid25_entry_unimplemented";
};
// Immutable diagnostic copy, no action/control interface.
Diagnostic GetDiagnostic();
}
