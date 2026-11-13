#pragma once
#include <cstdint>
class Player;
class PlayerbotAI;
class WorldPosition;
namespace AutonomousPlayerAuditV19 {
// All defaults false/zero. Exactly one explicitly configured EXISTING managed bot GUID; no creation API.
void Startup();
void Shutdown();
void WorldUpdate(std::uint32_t diff); // native world-thread session admission + primitive reports; no map traversal
void Observe(Player*);              // owning map, after native AI
void OnLogin(Player*);
void OnLogout(Player*);
bool ReservedGuid(std::uint64_t rawGuid); // immutable configuration; V18 roster exclusion seam
bool Reserved(Player const*);            // only the caller's own actor
bool Owns(Player*);                      // runtime guard: verified native bot, no master
// Both enqueue and native asynchronous callback recheck authoritative cache/account ownership.
bool CanRequestLogin(std::uint64_t rawGuid, std::uint32_t expectedAccount=0);
bool AllowQuest(Player*, std::uint32_t questId); // records known-broken/missing template, no bypass
void AlternativePath(Player*, WorldPosition const& originalGoal, bool reachable, std::uint32_t pathType);
// Called ONLY from an existing native stuck fallback after actual ordinary alternative paths.
// Uses the existing 'use' item action; never TeleportTo or cooldown/combat clearing.
bool TryHearthstone(PlayerbotAI*, WorldPosition const& originalGoal);
enum class AttemptKind : unsigned { Accept=1, Reward=2 };
// Stack-only in a native quest action; records real before/after status, never action-bool-as-success.
class QuestAttempt {
public:
    QuestAttempt(Player*, AttemptKind, std::uint32_t questId);
    ~QuestAttempt();
private:
    Player* player_; AttemptKind kind_; std::uint32_t quest_, before_; bool rewarded_;
};
}
