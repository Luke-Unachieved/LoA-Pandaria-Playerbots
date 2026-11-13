#ifndef AUTONOMOUS_NEEDS_BRIDGE_V19_H
#define AUTONOMOUS_NEEDS_BRIDGE_V19_H

#include <string>
#include <vector>

class Action;
class PlayerbotAI;
class TriggerNode;
class SpellInfo;
class TravelTarget;
class RpgTravelDestination;

// This bridge supplies eligibility and priority only. Native actions own movement,
// vendors, trainers, spell casts, trades, taxis and battleground queue requests.
namespace AutonomousNeedsV19
{
// Initialize/configure on world startup, then service queued primitive catalog
// requests on the world owner thread. Never load global quest/travel caches.
void InitializeTravel();
void UpdateTravelCatalog();
bool SelectServiceTravel(PlayerbotAI* ai, TravelTarget* target);
bool ServiceTravelBusy(PlayerbotAI* ai);
bool ServiceTravelAllows(PlayerbotAI* ai, std::string const& action);
bool ServiceDestinationActive(PlayerbotAI* ai, RpgTravelDestination* destination);
void RefreshServiceValues(PlayerbotAI* ai);
bool Configured(PlayerbotAI* ai);
bool Active(PlayerbotAI* ai);
bool InitServiceTriggers(PlayerbotAI* ai, std::vector<TriggerNode*>& triggers);
bool ServiceActionPossible(PlayerbotAI* ai, Action* action);
void NoteServiceAttempt(PlayerbotAI* ai, std::string const& actionName);
bool WantsVendorPurchase(PlayerbotAI* ai);
bool WantsCraft(PlayerbotAI* ai);
bool HasCraftReagents(PlayerbotAI* ai, SpellInfo const* spell);
float RpgMultiplier(PlayerbotAI* ai, Action* action);
}

#endif
