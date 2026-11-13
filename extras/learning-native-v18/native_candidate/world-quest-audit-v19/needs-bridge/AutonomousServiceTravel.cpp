#include "AutonomousNeedsBridge.h"
#include "AutonomousPlayerAudit.h"
#include "Playerbots.h"
#include "BudgetValues.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "ItemUsageValue.h"
#include "NewRpgInfo.h"
#include "ObjectMgr.h"
#include "Timer.h"
#include "TravelMgr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <tuple>

namespace AutonomousNeedsV19
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t CatalogPointLimit = 2048;
constexpr size_t CandidateChecksPerNeed = 64;
enum Need : unsigned { Sell = 1, Repair = 2, Supplies = 4, Train = 8 };
struct Center { uint32 map = 0; float x = 0, y = 0, z = 0; };
struct Point
{
    uint32 entry = 0;
    WorldPosition* position = nullptr;
    RpgTravelDestination* destination = nullptr;
};
struct Catalog
{
    Center center;
    size_t available = 0;
    bool partial = false;
    std::vector<std::unique_ptr<RpgTravelDestination>> owned;
    std::vector<Point> points;
};
std::atomic<bool> travelEnabled{false};
std::mutex catalogMutex;
std::shared_ptr<Catalog> publishedCatalog;
bool catalogRequested = false;
Center requestedCenter;
Clock::time_point nextCatalogBuild;

// Only the selected actor's normal map-owned AI touches route state. The world
// catalog worker receives primitives and publishes immutable metadata only.
using BlockKey = std::tuple<uint32, uint32, int32, int32>;
struct Snapshot
{
    uint8 bags = 0, durability = 0;
    uint32 food = 0, drink = 0, spells = 0;
};
struct Route
{
    std::shared_ptr<Catalog> catalog;
    Point point;
    unsigned need = 0;
    uint32 pausedAt = 0, goalStart = 0;
    Clock::time_point deadline, arrived;
    Snapshot before;
};
struct ActorState
{
    Route route;
    Clock::time_point nextSelection;
    std::map<BlockKey, Clock::time_point> blocked;
};
std::map<uint64, ActorState> actors;

uint32 SupplyCount(PlayerbotAI* ai, char const* kind)
{
    uint32 count = 0;
    bool const hasMana = ai->GetBot()->GetMaxPower(POWER_MANA) > 0;
    for (Item* item : ai->GetAiObjectContext()->GetValue<std::vector<Item*>>("inventory items", kind)->Get())
        if (item && ItemUsageValue::GetConsumableType(item->GetTemplate(), hasMana) == kind)
            count += item->GetCount();
    return count;
}
Snapshot ObserveNeeds(PlayerbotAI* ai)
{
    AiObjectContext* c = ai->GetAiObjectContext();
    Snapshot value;
    value.bags = c->GetValue<uint8>("bag space")->Get();
    value.durability = c->GetValue<uint8>("durability")->Get();
    value.food = SupplyCount(ai, "food");
    value.drink = SupplyCount(ai, "drink");
    value.spells = static_cast<uint32>(ai->GetBot()->GetSpellMap().size());
    return value;
}
bool Changed(Snapshot const& a, Snapshot const& b)
{
    return a.bags != b.bags || a.durability != b.durability || a.food != b.food ||
        a.drink != b.drink || a.spells != b.spells;
}
unsigned CurrentNeeds(PlayerbotAI* ai)
{
    AiObjectContext* c = ai->GetAiObjectContext();
    unsigned needs = 0;
    bool const repair = c->GetValue<bool>("should repair")->Get();
    bool const canRepair = c->GetValue<bool>("can repair")->Get();
    if (c->GetValue<bool>("can sell")->Get() &&
        (c->GetValue<bool>("should sell")->Get() || (repair && !canRepair)))
        needs |= Sell;
    if (repair && canRepair)
        needs |= Repair;
    if (SupplyCount(ai, "food") == 0 ||
        (ai->GetBot()->GetMaxPower(POWER_MANA) > 0 && SupplyCount(ai, "drink") == 0))
        needs |= Supplies;
    if (c->GetValue<bool>("can train")->Get() && c->GetValue<uint32>("train cost")->Get() > 0)
        needs |= Train;
    return needs;
}
bool FriendlyTemplate(Player* bot, CreatureTemplate const* info)
{
    if (!info)
        return false;
    FactionTemplateEntry const* npc = sFactionTemplateStore.LookupEntry(info->faction);
    FactionTemplateEntry const* own = bot->GetFactionTemplateEntry();
    if (!npc || !own || own->IsHostileTo(*npc))
        return false;
    return !npc->faction || bot->GetReputationRank(npc->faction) >= REP_NEUTRAL;
}
bool SuppliesAt(PlayerbotAI* ai, uint32 entry)
{
    AiObjectContext* c = ai->GetAiObjectContext();
    bool const needFood = SupplyCount(ai, "food") == 0;
    bool const hasMana = ai->GetBot()->GetMaxPower(POWER_MANA) > 0;
    bool const needDrink = hasMana && SupplyCount(ai, "drink") == 0;
    VendorItemData const* vendor = sObjectMgr->GetNpcVendorItemList(entry);
    if (!vendor)
        return false;
    uint32 const budget = c->GetValue<uint32>("free money for", uint32(NeedMoneyFor::consumables))->Get();
    for (VendorItem const* item : vendor->m_items)
    {
        // Distant routing requires an ordinary gold-priced stock item. The live
        // vendor interaction remains authoritative about stock/bags/requirements.
        if (!item || item->ExtendedCost)
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item->item);
        if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->BuyPrice > budget)
            continue;
        std::string const type = ItemUsageValue::GetConsumableType(proto, hasMana);
        if (((needFood && type == "food") || (needDrink && type == "drink")) &&
            c->GetValue<ItemUsage>("item usage", item->item)->Get() == ITEM_USAGE_USE)
            return true;
    }
    return false;
}
bool TrainingAt(PlayerbotAI* ai, CreatureTemplate const* info)
{
    TrainerSpellData const* trainer = sObjectMgr->GetNpcTrainerSpells(info->Entry);
    if (!trainer)
        return false;
    Player* bot = ai->GetBot();
    bool const classTrainer = info->trainer_type == TRAINER_TYPE_CLASS && info->trainer_class == bot->GetClass();
    uint32 const budget = ai->GetAiObjectContext()->GetValue<uint32>("free money for", uint32(NeedMoneyFor::spells))->Get();
    for (auto const& row : trainer->spellList)
    {
        TrainerSpell const& spell = row.second;
        // No arbitrary first-profession selection: route to own class or an
        // already held prerequisite skill. Native trainer executes the purchase.
        if (!classTrainer && (!spell.reqSkill || !bot->HasSkill(spell.reqSkill)))
            continue;
        if (spell.spellCost <= budget && bot->GetTrainerSpellState(&spell) == TRAINER_SPELL_GREEN)
            return true;
    }
    return false;
}
unsigned NeedsAt(PlayerbotAI* ai, CreatureTemplate const* info, unsigned wanted)
{
    if (!FriendlyTemplate(ai->GetBot(), info))
        return 0;
    unsigned found = 0;
    if ((wanted & Sell) && (info->npcflag & UNIT_NPC_FLAG_VENDOR)) found |= Sell;
    if ((wanted & Repair) && (info->npcflag & UNIT_NPC_FLAG_REPAIR)) found |= Repair;
    if ((wanted & Supplies) && (info->npcflag & UNIT_NPC_FLAG_VENDOR) && SuppliesAt(ai, info->Entry)) found |= Supplies;
    if ((wanted & Train) && (info->npcflag & UNIT_NPC_FLAG_TRAINER) && TrainingAt(ai, info)) found |= Train;
    return found;
}
BlockKey Key(Point const& point)
{
    return {point.entry, point.position->GetMapId(), int32(point.position->GetPositionX()),
        int32(point.position->GetPositionY())};
}
void Finish(PlayerbotAI* ai, ActorState& state, char const* reason, bool failed)
{
    Route& route = state.route;
    if (!route.need)
        return;
    TravelTarget* target = ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    bool const sameTarget = target && target->getDestination() == route.point.destination;
    if (sameTarget && (!sTravelMgr.nullTravelDestination || !sTravelMgr.nullWorldPosition))
    {
        // Do not release privately owned points while a native target still
        // holds them. This can only arise outside the proven initialized-world
        // lifecycle; keep one bounded route alive until native teardown/reset.
        target->setStatus(TRAVEL_STATUS_EXPIRED);
        return;
    }
    if (sameTarget)
    {
        NewRpgInfo& info = ai->GetRpgInfo();
        // Keep native goal data. Compensate only the unchanged paused goal's
        // status clock and restart its native POI approach rather than assigning a quest.
        if (info.startT == route.goalStart)
        {
            uint32 const pause = getMSTime() - route.pausedAt;
            info.startT += pause;
            // Native quest execution only travels to a POI when lastReachPOI
            // is zero. Preserve its quest/objective/position, reset arrival so
            // it actually walks back from the service NPC.
            if (auto* q = std::get_if<NewRpgInfo::DoQuest>(&info.data)) q->lastReachPOI = 0;
            if (auto* npc = std::get_if<NewRpgInfo::WanderNpc>(&info.data)) npc->lastReach = 0;
            info.SetMoveFarTo(info.moveFarPos); // native fresh approach/stuck baseline
        }
        if (sTravelMgr.nullTravelDestination && sTravelMgr.nullWorldPosition)
            target->setTarget(sTravelMgr.nullTravelDestination, sTravelMgr.nullWorldPosition, true);
        target->setStatus(TRAVEL_STATUS_EXPIRED);
        ai->GetAiObjectContext()->GetValue<GuidPosition>("rpg target")->Reset();
        ai->GetAiObjectContext()->GetValue<std::string>("next rpg action")->Set("");
    }
    if (failed)
    {
        if (state.blocked.size() >= 64) state.blocked.erase(state.blocked.begin());
        state.blocked[Key(route.point)] = Clock::now() + std::chrono::minutes(5);
    }
    TC_LOG_INFO("playerbots", "[AutonomousNeedsV19] route_end guid=%llu entry=%u reason=%s observed_change=%u",
        static_cast<unsigned long long>(ai->GetBot()->GetGUID().GetRawValue()), route.point.entry, reason,
        Changed(route.before, ObserveNeeds(ai)) ? 1u : 0u);
    route = Route{};
    state.nextSelection = Clock::now() + std::chrono::seconds(failed ? 60 : 15);
}
void RequestCatalog(Player* bot)
{
    if (!std::isfinite(bot->GetPositionX()) || !std::isfinite(bot->GetPositionY()) ||
        !std::isfinite(bot->GetPositionZ())) return;
    std::lock_guard<std::mutex> lock(catalogMutex);
    requestedCenter = {bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
    catalogRequested = true;
}
std::shared_ptr<Catalog> ReadCatalog(Player* bot)
{
    std::lock_guard<std::mutex> lock(catalogMutex);
    if (!publishedCatalog || publishedCatalog->center.map != bot->GetMapId())
        return {};
    if (publishedCatalog->partial &&
        std::hypot(bot->GetPositionX() - publishedCatalog->center.x, bot->GetPositionY() - publishedCatalog->center.y) > 2000.0f)
        return {};
    return publishedCatalog;
}
}

void InitializeTravel()
{
    // Startup only initializes configuration. The first actual need requests a
    // bounded actor-map catalog; no full LoadQuestTravelTable/Clear/graph build.
    std::string const raw = sConfigMgr->GetStringDefault("AiPlayerbot.AutonomousPlayerAudit.ActorGuid", "0");
    char* end = nullptr;
    uint64 const guid = std::strtoull(raw.c_str(), &end, 10);
    travelEnabled.store(end && !*end && AutonomousPlayerAuditV19::ReservedGuid(guid) &&
        sConfigMgr->GetBoolDefault("AiPlayerbot.AutonomousPlayerV19.NeedsBridge", true));
}

void UpdateTravelCatalog()
{
    if (!travelEnabled.load())
        return;
    Center center;
    {
        std::lock_guard<std::mutex> lock(catalogMutex);
        if (!catalogRequested || Clock::now() < nextCatalogBuild)
            return;
        center = requestedCenter;
        catalogRequested = false;
        nextCatalogBuild = Clock::now() + std::chrono::seconds(60);
    }
    struct Candidate
    {
        uint32 entry; float x, y, z, o, distance;
        RpgTravelDestination* nativeDestination; WorldPosition* nativePoint;
        bool operator<(Candidate const& rhs) const { return distance < rhs.distance; }
    };
    std::priority_queue<Candidate> nearest;
    size_t available = 0;
    auto consider = [&](uint32 entry, uint32 map, float x, float y, float z, float o,
                        RpgTravelDestination* destination, WorldPosition* point)
    {
        if (map != center.map || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(o)) return;
        CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry);
        if (!info || !(info->npcflag & (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR | UNIT_NPC_FLAG_TRAINER))) return;
        ++available;
        float const distance = (x-center.x)*(x-center.x) + (y-center.y)*(y-center.y);
        if (!std::isfinite(distance)) return;
        if (nearest.size() == CatalogPointLimit && distance >= nearest.top().distance) return;
        nearest.push({entry,x,y,z,o,distance,destination,point});
        if (nearest.size() > CatalogPointLimit) nearest.pop();
    };
    bool const reused = !sTravelMgr.rpgNpcs.empty();
    if (reused)
    {
        for (RpgTravelDestination* destination : sTravelMgr.rpgNpcs)
        {
            CreatureTemplate const* info = destination ? destination->GetCreatureTemplate() : nullptr;
            if (!info) continue;
            for (WorldPosition* point : destination->getPoints(true))
                if (point) consider(info->Entry, point->GetMapId(), point->GetPositionX(), point->GetPositionY(),
                    point->GetPositionZ(), point->GetOrientation(), destination, point);
        }
    }
    else
    {
        // ObjectMgr's existing immutable spawn/template metadata, on the world
        // owner thread. No database query, loaded-grid scan or object mutation.
        for (auto const& row : sObjectMgr->GetAllCreatureData())
        {
            CreatureData const& spawn = row.second;
            consider(spawn.id, spawn.mapId, spawn.posX, spawn.posY, spawn.posZ, spawn.orientation, nullptr, nullptr);
        }
    }
    auto catalog = std::make_shared<Catalog>();
    catalog->center = center;
    catalog->available = available;
    catalog->partial = available > CatalogPointLimit;
    while (!nearest.empty())
    {
        Candidate value = nearest.top(); nearest.pop();
        if (!value.nativeDestination)
        {
            auto destination = std::make_unique<RpgTravelDestination>(value.entry,
                sPlayerbotAIConfig->tooCloseDistance, sPlayerbotAIConfig->sightDistance);
            destination->setExpireDelay(45000);
            destination->setCooldownDelay(60000);
            destination->setMaxVisitors(15, 0);
            value.nativePoint = new WorldPosition(center.map, value.x, value.y, value.z, value.o);
            destination->addPoint(value.nativePoint); // native destination owns point
            value.nativeDestination = destination.get();
            catalog->owned.push_back(std::move(destination));
        }
        catalog->points.push_back({value.entry, value.nativePoint, value.nativeDestination});
    }
    TC_LOG_INFO("playerbots", "[AutonomousNeedsV19] service_catalog map=%u retained=%u available=%u coverage=%s source=%s",
        center.map, uint32(catalog->points.size()), uint32(available), available == 0 ? "unknown-empty" : (catalog->partial ? "partial" : "complete-source"),
        reused ? "existing-native-cache" : "native-spawn-metadata");
    std::lock_guard<std::mutex> lock(catalogMutex);
    publishedCatalog = std::move(catalog);
}

void RefreshServiceValues(PlayerbotAI* ai)
{
    if (!Configured(ai)) return;
    AiObjectContext* c = ai->GetAiObjectContext();
    for (char const* name : {"can sell", "should sell", "can repair", "should repair", "can train"})
        c->GetValue<bool>(name)->Reset();
    c->GetValue<uint8>("bag space")->Reset();
    c->GetValue<uint8>("durability")->Reset();
    c->GetValue<uint32>("repair cost")->Reset();
    c->GetValue<uint32>("train cost")->Reset();
    for (char const* name : {"food", "drink"}) c->GetValue<std::vector<Item*>>("inventory items", name)->Reset();
    for (unsigned i = 1; i <= 9; ++i) c->GetValue<uint32>("free money for", i)->Reset();
}

bool ServiceDestinationActive(PlayerbotAI* ai, RpgTravelDestination* destination)
{
    if (!Configured(ai) || !AutonomousPlayerAuditV19::Owns(ai->GetBot()) || !destination)
        return false;
    // This read does not drive routing or mutate a goal; TravelTarget calls it
    // while deciding whether a real native travel/work target remains useful.
    return NeedsAt(ai, destination->GetCreatureTemplate(), CurrentNeeds(ai)) != 0;
}

bool ServiceTravelBusy(PlayerbotAI* ai)
{
    if (!Configured(ai)) return false;
    auto it = actors.find(ai->GetBot()->GetGUID().GetRawValue());
    if (it == actors.end() || !it->second.route.need) return false;
    ActorState& state = it->second;
    Route& route = state.route;
    TravelTarget* target = ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    // New AI contexts after login do not inherit or restore an old session goal.
    if (!target || target->getDestination() != route.point.destination)
    { Finish(ai, state, "native_target_changed", false); return false; }
    if (!AutonomousPlayerAuditV19::Owns(ai->GetBot()) || ai->GetBot()->InBattleground() || ai->GetBot()->IsInFlight())
    { Finish(ai, state, "native_owner_or_activity_changed", false); return false; }
    if (Clock::now() >= route.deadline || ai->GetBot()->GetMapId() != route.point.position->GetMapId())
    { Finish(ai, state, "route_budget_or_map_changed", true); return false; }
    if (!(NeedsAt(ai, route.point.destination->GetCreatureTemplate(), CurrentNeeds(ai)) & route.need))
    { Finish(ai, state, "need_no_longer_actionable", false); return false; }
    target->isActive(); // native expiry, arrival and retry state; never forced
    if (target->getStatus() == TRAVEL_STATUS_COOLDOWN || target->getStatus() == TRAVEL_STATUS_EXPIRED)
    { Finish(ai, state, "native_path_or_target_expired", true); return false; }
    if (target->getStatus() == TRAVEL_STATUS_WORK)
    {
        if (route.arrived == Clock::time_point{}) route.arrived = Clock::now();
        if (Clock::now() - route.arrived >= std::chrono::seconds(45))
        { Finish(ai, state, "service_no_resolution_in_budget", true); return false; }
    }
    return true;
}

bool SelectServiceTravel(PlayerbotAI* ai, TravelTarget* target)
{
    if (!travelEnabled.load() || !Active(ai) || !target ||
        !sTravelMgr.nullTravelDestination || !sTravelMgr.nullWorldPosition || ServiceTravelBusy(ai)) return false;
    if (!std::isfinite(ai->GetBot()->GetPositionX()) || !std::isfinite(ai->GetBot()->GetPositionY()) ||
        !std::isfinite(ai->GetBot()->GetPositionZ())) return false;
    ActorState& state = actors[ai->GetBot()->GetGUID().GetRawValue()];
    if (Clock::now() < state.nextSelection) return false;
    state.nextSelection = Clock::now() + std::chrono::seconds(60);
    RefreshServiceValues(ai);
    unsigned const wanted = CurrentNeeds(ai);
    if (!wanted) return false;
    std::shared_ptr<Catalog> catalog = ReadCatalog(ai->GetBot());
    if (!catalog)
    {
        RequestCatalog(ai->GetBot());
        state.nextSelection = Clock::now() + std::chrono::seconds(2);
        return false;
    }
    std::vector<std::pair<float, Point>> candidates;
    for (Point const& point : catalog->points)
    {
        auto blocked = state.blocked.find(Key(point));
        if (blocked != state.blocked.end() && Clock::now() < blocked->second) continue;
        float const distance = point.position->distance(ai->GetBot());
        if (distance <= 10000.0f) candidates.push_back({distance,point});
    }
    std::sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b){return a.first < b.first;});
    for (unsigned priority : {unsigned(Repair), unsigned(Sell), unsigned(Supplies), unsigned(Train)})
    {
        if (!(wanted & priority)) continue;
        size_t checked = 0;
        for (auto const& candidate : candidates)
        {
            Point const& point = candidate.second;
            CreatureTemplate const* info = point.destination->GetCreatureTemplate();
            if (!info) continue;
            uint32 const flags = priority == Repair ? UNIT_NPC_FLAG_REPAIR :
                priority == Train ? UNIT_NPC_FLAG_TRAINER : UNIT_NPC_FLAG_VENDOR;
            if (!(info->npcflag & flags)) continue;
            if (++checked > CandidateChecksPerNeed) break;
            if (!NeedsAt(ai, info, priority) || point.destination->isFull(false)) continue;
            target->setTarget(point.destination, point.position);
            target->setForced(false); // retain native expiry and failed-move retries
            target->setRetry(true);
            target->setRetry(false);
            state.route.catalog = catalog;
            state.route.point = point;
            state.route.need = priority;
            state.route.pausedAt = getMSTime();
            state.route.goalStart = ai->GetRpgInfo().startT;
            state.route.deadline = Clock::now() + std::chrono::minutes(10);
            state.route.before = ObserveNeeds(ai);
            ai->GetAiObjectContext()->GetValue<GuidPosition>("rpg target")->Reset();
            ai->GetAiObjectContext()->GetValue<std::string>("next rpg action")->Set("");
            TC_LOG_INFO("playerbots", "[AutonomousNeedsV19] route_begin guid=%llu entry=%u map=%u need=%u distance=%.1f coverage=%s",
                static_cast<unsigned long long>(ai->GetBot()->GetGUID().GetRawValue()), point.entry,
                point.position->GetMapId(), priority, candidate.first, catalog->partial ? "partial" : "complete-source");
            return true;
        }
    }
    // No candidate is coverage/eligibility evidence, not proof of a server bug.
    return false;
}

bool ServiceTravelAllows(PlayerbotAI* ai, std::string const& action)
{
    bool const busy = ServiceTravelBusy(ai);
    if (action == "travel") return false; // native action is dormant; use RPG arrival handoff
    if (action == "choose travel target") return !busy && Active(ai);
    if (action == "move to travel target")
        return busy && ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get()->getStatus() == TRAVEL_STATUS_TRAVEL;
    if (!busy) return true;
    if (action.compare(0, 8, "new rpg ") == 0 || action == "attack anything" || action == "move random" ||
        action == "add gathering loot" || action == "clean quest log") return false;
    if (action == "rpg" || action == "choose rpg target" || action == "move to rpg target" || action.compare(0, 4, "rpg ") == 0)
    {
        if (ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get()->getStatus() != TRAVEL_STATUS_WORK) return false;
        if (action == "choose rpg target" || action == "rpg") return true;
        GuidPosition gp = ai->GetAiObjectContext()->GetValue<GuidPosition>("rpg target")->Get();
        return gp && gp.GetEntry() == actors[ai->GetBot()->GetGUID().GetRawValue()].route.point.entry;
    }
    return true;
}
}
