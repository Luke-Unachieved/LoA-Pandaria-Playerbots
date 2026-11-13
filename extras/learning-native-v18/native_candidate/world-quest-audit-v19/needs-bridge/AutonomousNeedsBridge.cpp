#include "AutonomousNeedsBridge.h"
#include "AutonomousPlayerAudit.h"

#include "Playerbots.h"
#include "BudgetValues.h"
#include "CastCustomSpellAction.h"
#include "Config.h"
#include "Creature.h"
#include "ItemUsageValue.h"
#include "ObjectMgr.h"
#include "RpgSubActions.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TravelMgr.h"
#include "Trigger.h"
#include "WorldSession.h"

#include <cmath>
#include <chrono>
#include <map>
#include <mutex>

namespace AutonomousNeedsV19
{
namespace
{
using Clock = std::chrono::steady_clock;
std::mutex attemptMutex;
std::map<std::pair<uint64, std::string>, Clock::time_point> nextAttempt;
bool AttemptReady(PlayerbotAI* ai, std::string const& name)
{
    std::lock_guard<std::mutex> lock(attemptMutex);
    auto const key = std::make_pair(ai->GetBot()->GetGUID().GetRawValue(), name);
    auto const found = nextAttempt.find(key);
    return found == nextAttempt.end() || Clock::now() >= found->second;
}
}

void NoteServiceAttempt(PlayerbotAI* ai, std::string const& name)
{
    if (!Configured(ai))
        return;
    RefreshServiceValues(ai);
    // An attempted interaction is not evidence of a purchased/trained/repaired
    // result. Back off either way so a native true-with-no-change cannot monopolize
    // NewRpg. Only fixed service names of the exact configured actor reach here.
    std::lock_guard<std::mutex> lock(attemptMutex);
    nextAttempt[std::make_pair(ai->GetBot()->GetGUID().GetRawValue(), name)] =
        Clock::now() + std::chrono::seconds(15);
}

bool Configured(PlayerbotAI* ai)
{
    return ai && !ai->IsRealPlayer() && AutonomousPlayerAuditV19::Reserved(ai->GetBot()) &&
        sConfigMgr->GetBoolDefault("AiPlayerbot.AutonomousPlayerV19.NeedsBridge", true);
}

bool Active(PlayerbotAI* ai)
{
    return Configured(ai) && AutonomousPlayerAuditV19::Owns(ai->GetBot()) &&
        ai->GetBot()->IsAlive() && !ai->GetBot()->IsInCombat() &&
        !ai->GetBot()->InBattleground() && !ai->GetBot()->IsInFlight() &&
        !ai->GetBot()->IsBeingTeleported();
}

bool InitServiceTriggers(PlayerbotAI* ai, std::vector<TriggerNode*>& triggers)
{
    if (!Configured(ai))
        return false;

    // Queue priorities, not multipliers, must exceed NewRpg movement (3).
    // The actor-only scorer cutoff is 5; native RPG selection also accepts <=5.
    auto add = [&triggers](char const* trigger, char const* action, float relevance)
    {
        triggers.push_back(new TriggerNode(trigger,
            NextAction::array(0, new NextAction(action, relevance), nullptr)));
    };
    add("low health", "food", 9.0f);
    add("low mana", "drink", 9.0f);
    add("no rpg target", "choose rpg target", 5.0f);
    add("far from rpg target", "move to rpg target", 5.0f);
    add("rpg sell", "rpg sell", 4.800f);
    add("rpg repair", "rpg repair", 4.780f);
    add("rpg train", "rpg train", 4.720f);
    add("rpg buy", "rpg buy", 4.680f);
    add("rpg discover", "rpg discover", 4.640f);
    add("rpg home bind", "rpg home bind", 4.600f);
    add("rpg craft", "rpg craft", 4.560f);
    add("rpg trade useful", "rpg trade useful", 4.520f);
    if (sPlayerbotAIConfig->randomBotJoinBG)
        add("rpg queue bg", "rpg queue bg", 4.480f);

    // No old-rpg quest, random-idle, taxi-money, or cancellation node. A target
    // with no remaining service gets cleared by the native RpgAction itself.
    return true;
}

bool WantsVendorPurchase(PlayerbotAI* ai)
{
    if (!Active(ai))
        return false;
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    GuidPosition target = context->GetValue<GuidPosition>("rpg target")->Get();
    Creature* vendor = target.GetCreature();
    if (!vendor || !vendor->IsAlive() || !vendor->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        return false;
    VendorItemData const* items = vendor->GetVendorItems();
    if (!items)
        return false;

    // Eligibility is the same native item-usage/budget vocabulary as BuyAction;
    // BuyItemFromVendorSlot still decides stock, price, bags and requirements.
    for (VendorItem const* entry : items->m_items)
    {
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(entry->item);
        if (!item)
            continue;
        ItemUsage const usage = context->GetValue<ItemUsage>("item usage", entry->item)->Get();
        NeedMoneyFor purpose = NeedMoneyFor::none;
        switch (usage)
        {
            case ITEM_USAGE_REPLACE:
            case ITEM_USAGE_EQUIP:
            case ITEM_USAGE_BAD_EQUIP:
            case ITEM_USAGE_BROKEN_EQUIP: purpose = NeedMoneyFor::gear; break;
            case ITEM_USAGE_AMMO: purpose = NeedMoneyFor::ammo; break;
            case ITEM_USAGE_QUEST: purpose = NeedMoneyFor::anything; break;
            case ITEM_USAGE_USE: purpose = NeedMoneyFor::consumables; break;
            case ITEM_USAGE_SKILL: purpose = NeedMoneyFor::tradeskill; break;
            default: continue;
        }
        uint32 const price = static_cast<uint32>(std::floor(
            item->BuyPrice * bot->GetReputationPriceDiscount(vendor)));
        if (context->GetValue<uint32>("free money for", static_cast<uint32>(purpose))->Get() >= price)
            return true;
    }
    return false;
}

bool HasCraftReagents(PlayerbotAI* ai, SpellInfo const* spell)
{
    if (!ai || !spell)
        return false;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        if (spell->Reagent[i] > 0 && spell->ReagentCount[i] > 0 &&
            !ai->GetBot()->HasItemCount(static_cast<uint32>(spell->Reagent[i]),
                static_cast<uint32>(spell->ReagentCount[i]), false))
            return false;
    return true;
}

bool WantsCraft(PlayerbotAI* ai)
{
    if (!Active(ai))
        return false;
    auto* craft = dynamic_cast<CraftRandomItemAction*>(
        ai->GetAiObjectContext()->GetAction("craft random item"));
    if (!craft)
        return false;
    for (auto const& known : craft->GetSpellList())
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(known.first);
        if (spell && ai->GetBot()->HasSpell(known.first) && craft->AcceptSpell(spell) &&
            craft->GetSpellPriority(spell) > 1 && ai->CanCastSpell(known.first, ai->GetBot(), true))
            return true;
    }
    return false;
}

bool ServiceActionPossible(PlayerbotAI* ai, Action* action)
{
    if (!Active(ai) || !action || !ServiceTravelAllows(ai, action->getName()) || !action->isPossible())
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    std::string const name = action->getName();
    if (!AttemptReady(ai, name))
        return false;
    if (name == "rpg sell")
        return context->GetValue<bool>("can sell")->Get();
    if (name == "rpg repair")
        return context->GetValue<bool>("should repair")->Get() &&
            context->GetValue<bool>("can repair")->Get();
    if (name == "rpg buy")
        return WantsVendorPurchase(ai);
    if (name == "rpg craft")
        return WantsCraft(ai);
    if (name == "rpg trade useful")
    {
        GuidPosition target = context->GetValue<GuidPosition>("rpg target")->Get();
        Player* recipient = target.GetPlayer();
        PlayerbotAI* recipientAI = recipient ? GET_PLAYERBOT_AI(recipient) : nullptr;
        auto* trade = dynamic_cast<RpgTradeUsefulAction*>(action);
        return recipient && recipient->GetSession() && recipient->GetSession()->IsBot() &&
            recipientAI && !recipientAI->IsRealPlayer() && !recipientAI->HasRealPlayerMaster() &&
            trade && !trade->CanGiveItems(target).empty();
    }
    if (name == "rpg queue bg")
        return sPlayerbotAIConfig->randomBotJoinBG;
    return name == "rpg train" || name == "rpg discover" || name == "rpg home bind";
}

float RpgMultiplier(PlayerbotAI* ai, Action* action)
{
    if (!action)
        return 1.0f;
    std::string const name = action->getName();
    if (!ServiceTravelAllows(ai, name))
        return 0.0f;
    bool const service = dynamic_cast<RpgEnabled*>(action) != nullptr;
    bool const bridgeAction = service || name == "rpg" || name == "choose rpg target" ||
        name == "move to rpg target";
    if (!bridgeAction)
        return 1.0f;
    if (!Active(ai))
        return 0.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (service)
    {
        std::string const next = context->GetValue<std::string>("next rpg action")->Get();
        if ((!next.empty() && next != name) || !ServiceActionPossible(ai, action))
            return 0.0f;
        return 1.0f;
    }
    if (name == "rpg")
    {
        GuidPosition target = context->GetValue<GuidPosition>("rpg target")->Get();
        // While walking, let native movement own the target. A vanished target
        // is allowed through so native RpgAction can clear it normally.
        return target.GetWorldObject() && target.distance(ai->GetBot()) > INTERACTION_DISTANCE ? 0.0f : 1.0f;
    }
    return 1.0f;
}
}
