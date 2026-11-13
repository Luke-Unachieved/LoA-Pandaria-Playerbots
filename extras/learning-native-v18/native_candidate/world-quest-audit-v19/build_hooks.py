#!/usr/bin/env python3
"""Compose exact sparse V19 candidates; original source and frozen V18 stay read-only."""
from pathlib import Path
import argparse, difflib, hashlib, importlib.util, json, re, sys
sys.dont_write_bytecode=True

HERE=Path(__file__).resolve().parent
LAB=HERE.parent
DEFAULT=Path('/workspace/scratch/91fc7ee3b2c6/native-v15-input/extracted/D/WOW/RaidAI-Dev/BuildSources/prod0d107-raid-utf8-5c4d3725e00b/modules/mod_playerbots/src')
NS='AutonomousPlayerAuditV19'
def sha(data): return hashlib.sha256(data).hexdigest()
def once(text, old, new):
    if text.count(old)!=1: raise ValueError('Expected exactly one native seam: '+old[:140])
    return text.replace(old,new,1)
def entry(text, signature, statement):
    seam=signature+'\n{' if signature+'\n{' in text else signature+' {'
    return once(text, seam,seam+'\n    '+statement)
def guard(text, signature, actor='bot', result=''):
    return entry(text,signature,f'if ({NS}::Reserved({actor})) return{(" "+result) if result else ""};')
def include(text):
    bom='\ufeff' if text.startswith('\ufeff') else ''
    return bom+'#include "AutonomousPlayerAudit.h"\n'+text[len(bom):]

def transform(name,t):
    if name=='mod_playerbots.cpp':
        t=once(t,'            sTravelNodeMap.LoadOrGenerate();\n    }',
            '            sTravelNodeMap.LoadOrGenerate();\n        '+NS+'::Startup();\n    }')
        t=once(t,'NativeBackgroundTraining::Shutdown();','NativeBackgroundTraining::Shutdown(); '+NS+'::Shutdown();')
        t=once(t,'NativeBackgroundTraining::WorldUpdate(diff);','NativeBackgroundTraining::WorldUpdate(diff);\n        '+NS+'::WorldUpdate(diff);')
        t=once(t,'NativeBackgroundTraining::ObservePlayer(player);','NativeBackgroundTraining::ObservePlayer(player);\n        '+NS+'::Observe(player);')
        t=once(t,'NativeBackgroundTraining::OnAiCreated(player);','NativeBackgroundTraining::OnAiCreated(player); '+NS+'::OnLogin(player);')
        t=once(t,'void OnLogout(Player* p) override { if(p) {','void OnLogout(Player* p) override { if(p) { '+NS+'::OnLogout(p);')
        t=once(t,'void OnPlayerbotLogout(Player* player) override\n    {','void OnPlayerbotLogout(Player* player) override\n    {\n        '+NS+'::OnLogout(player);')
    elif name=='NativeBackgroundTraining.cpp':
        t=once(t,'if (!NativeBot(p) || !sRandomPlayerbotMgr->IsRandomBot(p) || !Quiet(p) ||',
            'if ('+NS+'::Reserved(p) || !NativeBot(p) || !sRandomPlayerbotMgr->IsRandomBot(p) || !Quiet(p) ||')
        t=entry(t,'void ObservePlayer(Player* player)',f'if ({NS}::Reserved(player)) return;')
        t=once(t,'if (online.count(item.first) && Selectable(item.second,now))',
            'if (!'+NS+'::ReservedGuid(item.first) && online.count(item.first) && Selectable(item.second,now))')
    elif name=='AI/PlayerbotAI.cpp':
        anchor='''    // always allow packet handling (e.g. group invites, trade, loot, friend requests etc)'''
        t=once(t,anchor,
            '    // V19: one verified existing native actor keeps full/background path activity.\n'
            '    // The native invalid-session/world/teleport guard above remains authoritative.\n'
            '    if ('+NS+'::Owns(bot)) return true;\n\n'+anchor)
        t=entry(t,'bool PlayerbotAI::AllowActivity(ActivityType activityType, bool checkNow)',
            'if ('+NS+'::Reserved(bot)) checkNow = true; // Re-evaluate native legality; never reuse an actor activity cache across transitions.')
    elif name=='Manager/RandomPlayerbotMgr.cpp':
        for signature,actor,result in [
            ('bool RandomPlayerbotMgr::ProcessBot(Player* player)','player','false'),
            ('void RandomPlayerbotMgr::TagForRandomize(Player* bot, uint32 level)','bot',''),
            ('void RandomPlayerbotMgr::Revive(Player* player)','player',''),
            ('void RandomPlayerbotMgr::RandomizeFirst(Player* bot)','bot',''),
            ('void RandomPlayerbotMgr::Randomize(Player* bot)','bot',''),
            ('void RandomPlayerbotMgr::Refresh(Player* bot)','bot',''),
            ('void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot)','bot',''),
            ('bool RandomTeleportToLocs(Player* bot, std::vector<WorldLocation> const& locs, bool hearth = false, bool revive = false)','bot','false'),
            ('bool IsBotSafeForLevelReset(Player* bot)','bot','false'),
            ('void RandomBotLevelMgr::AdjustBotToRange(Player* bot, int targetRangeIndex, TeamId team)','bot',''),
            ('void RandomBotLevelMgr::ResetBot(Player* player, uint8 currentLevel)','player',''),
            ('void RandomBotLevelMgr::SkipBotLevel(Player* player, uint8 currentLevel)','player',''),
            ('void RandomBotLevelMgr::OnBotLogin(Player* player)','player',''),
            ('void RandomBotLevelMgr::OnBotLevelChanged(Player* player, uint8 oldLevel)','player','')]:
            t=guard(t,signature,actor,result)
        # Low GUID is a player GUID on this core. Never let the population manager log in/out the designated actor.
        t=entry(t,'bool RandomPlayerbotMgr::ProcessBot(uint32 bot)',f'if ({NS}::ReservedGuid(ObjectGuid(HighGuid::Player, bot).GetRawValue())) return false;')
        # This callback consists of dynamic population/factory/forced starter shortcuts, not core login setup.
        t=entry(t,'void RandomPlayerbotMgr::OnBotLoginInternal(Player* const bot)',
            'if ('+NS+'::Reserved(bot)) { '+NS+'::OnLogin(bot); return; }')
        t=entry(t,'void RandomPlayerbotMgr::OnPlayerLogin(Player* player)',
            'if ('+NS+'::Reserved(player)) {\n        if (player->GetSession() && player->GetSession()->IsBot()) player->Unit::SetPvP(sWorld->IsPvPRealm());\n        return; // No ambient master assignment or level reset for this actor.\n    }')
        t=once(t,'    if (std::find(_currentBots.begin(), _currentBots.end(), bot) != _currentBots.end())\n        return true;',
            '    if ('+NS+'::ReservedGuid(guid.GetRawValue())) return true; // Account and raid exclusions above remain authoritative.\n    if (std::find(_currentBots.begin(), _currentBots.end(), bot) != _currentBots.end())\n        return true;')
    elif name=='Manager/PlayerbotMgr.h':
        t=once(t,'    void AddPlayerBot(ObjectGuid guid, uint32 masterAccountId);',
            '    void AddPlayerBot(ObjectGuid guid, uint32 masterAccountId);\n    bool AddAutonomousPlayerBot(ObjectGuid guid); // Same native queue, exact configured managed actor only.\n    static bool IsBotLoginPending(ObjectGuid guid); // Existing cross-holder lease, no timeout clearing.')
        return t
    elif name=='Manager/PlayerbotMgr.cpp':
        anchor='void PlayerbotHolder::AddPlayerBot(ObjectGuid playerGuid, uint32 masterAccountId)'
        addition='''bool PlayerbotHolder::IsBotLoginPending(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(botLoginLeaseMutex);
    auto found = botLoginLeases.find(guid);
    return found != botLoginLeases.end() && !found->second.expired();
}

bool PlayerbotHolder::AddAutonomousPlayerBot(ObjectGuid guid)
{
    return AutonomousPlayerAuditV19::CanRequestLogin(guid.GetRawValue()) && QueuePlayerBot(guid, 0);
}

'''
        t=once(t,anchor,addition+anchor)
        t=entry(t,'bool PlayerbotHolder::QueuePlayerBot(ObjectGuid playerGuid, uint32 masterAccountId,\n    std::shared_ptr<ManagedBotLogin> const& token, uint32 addclassLevel)',
            'if ('+NS+'::ReservedGuid(playerGuid.GetRawValue()) &&\n        (masterAccountId || token || addclassLevel || !'+NS+'::CanRequestLogin(playerGuid.GetRawValue()))) return false;')
        t=once(t,'    if ((managed && !ManagedValid(managed)) || ObjectAccessor::FindConnectedPlayer(botGUID) ||',
            '    if ('+NS+'::ReservedGuid(botGUID.GetRawValue()) &&\n        (managed || holder.GetMasterAccountId() || !'+NS+'::CanRequestLogin(botGUID.GetRawValue(), botAccountId))) return;\n    if ((managed && !ManagedValid(managed)) || ObjectAccessor::FindConnectedPlayer(botGUID) ||')
        anchor='    // At login DBC locale should be what the server is set to use by default (as spells etc are hardcoded to ENUS this'
        t=once(t,anchor,'''    if (AutonomousPlayerAuditV19::ReservedGuid(botGUID.GetRawValue()))
    {
        // Reuse the already-loaded native login row. Never consume reset/service flags for the actor.
        PreparedQueryResult row = holder.GetPreparedResult(PLAYER_LOGIN_QUERY_LOAD_FROM);
        if (holder.GetPreparedResult(PLAYER_LOGIN_QUERY_LOAD_SERVICES) || !row ||
            (*row)[0].GetUInt32() != botGUID.GetCounter() || (*row)[1].GetUInt32() != botAccountId ||
            (*row)[34].GetUInt16() != 0)
        {
            TC_LOG_INFO("playerbots", "[AUTONOMOUS_AUDIT] native_login_row_refused actor=%u", botGUID.GetCounter());
            return;
        }
    }

'''+anchor)
        # Prevent stale central AI registration when the actor later logs out and logs back in.
        anchor='            botWorldSessionPtr->LogoutPlayer(true);  // this will delete the bot Player object and PlayerbotAI object'
        t=once(t,anchor,'            if ('+NS+'::Reserved(bot) && botWorldSessionPtr->IsBot() && !botAI->IsRealPlayer()) { sPlayerbotsMgr->RemovePlayerBotData(guid, true); delete botAI; }\n'+anchor)
        anchor='        delete botAI;\n    }\n}\n\nPlayer* PlayerbotHolder::GetPlayerBot(ObjectGuid::LowType lowGuid) const'
        t=once(t,anchor,'        if ('+NS+'::Reserved(bot) && bot->GetSession() && bot->GetSession()->IsBot() && !botAI->IsRealPlayer()) sPlayerbotsMgr->RemovePlayerBotData(guid, true);\n'+anchor)
    elif name=='Factory/BotFactory.cpp':
        # Block mutation entry points even when invoked directly by maintenance or login code.
        for sig in re.findall(r'^void BotFactory::(?:CancelAuras|InitFood|InitReagents|InitPotions|InitConsumables|CleanupConsumables|Prepare|Randomize|Refresh|InitPet|InitTalentsTree|InitTalentTiers|InitGlyphs|InitClassSpells|InitMountSpells|InitTradeSkills|InitSkills|ClearEverything|InitBags|InitEquipment)\([^\n{}]*\)',t,re.M):
            if sig=='void BotFactory::CancelAuras()':
                t=once(t,sig+' { bot->RemoveAllAuras(); }',sig+' { if ('+NS+'::Reserved(bot)) return; bot->RemoveAllAuras(); }')
            else: t=guard(t,sig)
    elif name=='Manager/DynamicPopulationMgr.cpp':
        t=guard(t,'bool BotEligible(Player* bot, Player* target, bool city, bool includeReservation=true)',result='false')
        t=entry(t,'char const* LocalOwnerFailure(Player* bot)',f'if ({NS}::Reserved(bot)) return "autonomous-player";')
        t=entry(t,'bool DeferRandomRelocation(Player* bot)',f'if ({NS}::Reserved(bot)) return true;')
    elif name=='strategy/actions/AutoMaintenanceOnLevelupAction.cpp':
        t=guard(t,'bool AutoMaintenanceOnLevelupAction::Execute(Event /*event*/)',result='false')
    elif name=='strategy/actions/QuestAction.cpp':
        t=guard(t,'bool QuestAction::CompleteQuest(Player* player, uint32 entry)','player','false')
        t=entry(t,'bool QuestAction::AcceptQuest(Quest const* quest, ObjectGuid questGiver)',
            f'if (!quest || !{NS}::AllowQuest(bot, quest->GetQuestId())) return false;\n    {NS}::QuestAttempt audit(bot, {NS}::AttemptKind::Accept, quest->GetQuestId());')
        t=once(t,'if (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE && sPlayerbotAIConfig->syncQuestWithPlayer)',
            'if (!'+NS+'::Reserved(bot) && bot->GetQuestStatus(questId) == QUEST_STATUS_NONE && sPlayerbotAIConfig->syncQuestWithPlayer)')
    elif name=='strategy/actions/AcceptQuestAction.cpp':
        # Shares require an external requester; autonomous actor never consumes forced direct-add fallback.
        for sig in ['bool AcceptQuestShareAction::Execute(Event event)','bool ConfirmQuestAction::Execute(Event event)']:
            t=guard(t,sig,result='false')
    elif name=='strategy/RPG/NewRpgRoamTransport.cpp':
        t=once(t,'auto stall = [&](char const* why, WorldPosition const& target) -> int32\n    {',
            'auto stall = [&](char const* why, WorldPosition const& target) -> int32\n    {\n        if ('+NS+'::Reserved(bot)) return abandon("autonomous_no_stuck_teleport");')
        t=once(t,'if (from.type == NODE_PORTAL || from.type == NODE_TELEPORT)\n        {',
            'if (from.type == NODE_PORTAL || from.type == NODE_TELEPORT)\n        {\n            if ('+NS+'::Reserved(bot)) return abandon("autonomous_requires_native_portal_or_item");')
    elif name=='strategy/Dead/ReviveFromCorpseAction.cpp':
        t=once(t,'if (!botAI->AllowActivity(ALL_ACTIVITY))','if (!'+NS+'::Reserved(bot) && !botAI->AllowActivity(ALL_ACTIVITY))')
        # Existing spirit-healer action fabricates resurrection; retain normal reclaim/death actions.
        t=guard(t,'bool SpiritHealerAction::Execute(Event event)',result='false')
    elif name=='strategy/Dead/ReleaseSpiritAction.cpp':
        t=guard(t,'bool RepopAction::Execute(Event event)',result='false')
        t=once(t,'if (bot->GetDistance(spiritHealer) >= INTERACTION_DISTANCE)\n    {',
            'if (bot->GetDistance(spiritHealer) >= INTERACTION_DISTANCE)\n    {\n        if ('+NS+'::Reserved(bot)) return false; // Await ordinary movement; no healer-range relocation.')
    elif name=='strategy/bg/BattleGroundJoinAction.cpp':
        t=once(t,'// AC BattleGroundJoinAction.cpp:1027-1028 (#210 S10 G8): dismount before the port, the way',
            'if ('+NS+'::Reserved(bot) && (!bot->IsAlive() || bot->IsInCombat() || !bot->getAttackers().empty() || bot->IsInFlight())) continue;\n\n        // AC BattleGroundJoinAction.cpp:1027-1028 (#210 S10 G8): dismount before the port, the way')
        t=once(t,'bot->CombatStopWithPets(true);','if (!'+NS+'::Reserved(bot)) bot->CombatStopWithPets(true);')
    elif name=='strategy/bg/BattleGroundTactics.cpp':
        for condition in ['distToStart > startRadius','distToAnchor > startRadius || distToStart > startRadius',
            'bot->GetDistance(1333.07f, 817.18f, 13.35f) < 4','bot->GetDistance(1250.13f, 764.79f, 13.34f) < 4']:
            old='if ('+condition+')';t=once(t,old,'if (!'+NS+'::Reserved(bot) && ('+condition+'))')
    elif name=='strategy/bg/BgTacticsTB.cpp':
        t=once(t,'if (stuck >= 2 && !bot->GetVehicle() && OnIsland(bot))',
            'if (!'+NS+'::Reserved(bot) && stuck >= 2 && !bot->GetVehicle() && OnIsland(bot))')
    elif name=='strategy/dungeons/CataLct/CataLctActions.cpp':
        t=guard(t,'bool LctReturnToPlatformAction::Execute(Event /*event*/)',result='false')
    elif name=='strategy/actions/GuildCreateActions.cpp':
        t=once(t,'if (have < REQUIRED)','if (!'+NS+'::Reserved(bot) && have < REQUIRED)')
    elif name=='strategy/RPG/RpgSubActions.cpp':
        t=guard(t,'bool RpgTaxiAction::Execute(Event /*event*/)',result='false')
    elif name=='strategy/RPG/NewRpgDuelSpot.cpp':
        t=guard(t,'bool TeleportToSpot(Player* bot, DuelSpotInfo const& spot)',result='false')
        t=guard(t,'bool NewRpgDuelSpotAction::Execute(Event /*event*/)',result='false')
        t=once(t,'if (p && p->IsInWorld() && sRandomPlayerbotMgr->IsRandomBot(p))',
            'if (p && !'+NS+'::Reserved(p) && p->IsInWorld() && sRandomPlayerbotMgr->IsRandomBot(p))')
    elif name=='strategy/RPG/NewRpgBaseAction.cpp':
        t=entry(t,'bool NewRpgBaseAction::AcceptQuest(Quest const* quest, ObjectGuid guid)',
            f'if (!quest || !{NS}::AllowQuest(bot, quest->GetQuestId())) return false;\n    {NS}::QuestAttempt audit(bot, {NS}::AttemptKind::Accept, quest->GetQuestId());')
        t=entry(t,'bool NewRpgBaseAction::TurnInQuest(Quest const* quest, ObjectGuid guid)',
            f'if (!quest || !{NS}::AllowQuest(bot, quest->GetQuestId())) return false;\n    {NS}::QuestAttempt audit(bot, {NS}::AttemptKind::Reward, quest->GetQuestId());')
        t=once(t,'bot->AddQuestAndCheckCompletion(quest, questGiver);\n    return true;',
            'bot->AddQuestAndCheckCompletion(quest, questGiver);\n    return !'+NS+'::Reserved(bot) || bot->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_INCOMPLETE || bot->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE;')
        t=once(t,'bot->RewardQuest(quest, rewardItemId, questGiver);\n    return true;',
            'bot->RewardQuest(quest, rewardItemId, questGiver);\n    return !'+NS+'::Reserved(bot) || bot->GetQuestRewardStatus(questId);')
        t=once(t,'accepted |= AcceptQuest(quest, guid);',
            'bool const acceptedNow = AcceptQuest(quest, guid);\n            accepted |= acceptedNow;\n            if ('+NS+'::Reserved(bot) && !acceptedNow) continue;')
        t=once(t,'            TurnInQuest(quest, guid);',
            '            bool const rewardedNow = TurnInQuest(quest, guid);\n            if ('+NS+'::Reserved(bot) && !rewardedNow) continue;')
        t=once(t,'else if (++info.stuckAttempts >= 5 && GetMSTimeDiffToNow(info.stuckTs) >= stuckTime)',
            'else if (++info.stuckAttempts >= 5 && GetMSTimeDiffToNow(info.stuckTs) >= stuckTime &&\n             !'+NS+'::Reserved(bot))')
        cone='''        bool canReach = !(type & (~typeOk));

        if (canReach && fabs(delta) <= minDelta)'''
        t=once(t,cone,'''        bool canReach = !(type & (~typeOk));
        // Evidence from each real native alternative calculation, before any recovery request.
        AutonomousPlayerAuditV19::AlternativePath(bot, dest, canReach, uint32(type));

        if (canReach && fabs(delta) <= minDelta)''')
        t=once(t,'return flyLeg("nopath");\n    return false;',
            'return flyLeg("nopath");\n    if ('+NS+'::Reserved(bot)) return '+NS+'::TryHearthstone(botAI, dest);\n    return false;')
    else: raise ValueError('Unknown source '+name)
    return include(t)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=DEFAULT);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    names=['mod_playerbots.cpp','NativeBackgroundTraining.cpp','AI/PlayerbotAI.cpp','Manager/RandomPlayerbotMgr.cpp','Manager/PlayerbotMgr.h','Manager/PlayerbotMgr.cpp','Factory/BotFactory.cpp',
        'Manager/DynamicPopulationMgr.cpp','strategy/actions/AutoMaintenanceOnLevelupAction.cpp','strategy/actions/QuestAction.cpp',
        'strategy/actions/AcceptQuestAction.cpp','strategy/RPG/NewRpgBaseAction.cpp','strategy/RPG/NewRpgRoamTransport.cpp',
        'strategy/Dead/ReviveFromCorpseAction.cpp','strategy/Dead/ReleaseSpiritAction.cpp','strategy/bg/BattleGroundJoinAction.cpp',
        'strategy/bg/BattleGroundTactics.cpp','strategy/bg/BgTacticsTB.cpp','strategy/dungeons/CataLct/CataLctActions.cpp',
        'strategy/actions/GuildCreateActions.cpp','strategy/RPG/RpgSubActions.cpp','strategy/RPG/NewRpgDuelSpot.cpp']
    base_names=set(names)
    bridge_path=HERE/'needs-bridge/build_need_bridge.py'
    spec=importlib.util.spec_from_file_location('v19_needs_bridge',bridge_path)
    bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
    for path in bridge.SOURCE_PATHS:
        name=path.removeprefix('src/')
        if name not in names:names.append(name)
    overrides={'mod_playerbots.cpp':LAB/'scheduler-v18/mod_playerbots.cpp.candidate',
        'NativeBackgroundTraining.cpp':LAB/'integrated-v18/new/NativeBackgroundTraining.cpp',
        'AI/PlayerbotAI.cpp':LAB/'composed-v18/PlayerbotAI.cpp'}
    rows=[];patch=[];outputs=[]
    for name in names:
        source=overrides.get(name,args.source_root/name);raw=source.read_bytes();old=raw.decode('utf-8').replace('\r\n','\n')
        if name=='AI/PlayerbotAI.cpp' and sha(raw)!='2c0b398061b6a61893a380458ef4fbf79d55e1d34450decfadd1ddef712abc94':
            raise ValueError('Current composed V18 PlayerbotAI baseline changed: review/rebase, never fall back to generic source')
        new=transform(name,old) if name in base_names else old
        new=bridge.transform('src/'+name,new)
        target='native/'+name
        rows.append(dict(target=target,source=str(source),source_sha256=sha(raw),normalized_source_sha256=sha(old.encode()),prepared_sha256=sha(new.encode())))
        patch.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+target,tofile='b/'+target))
        outputs.append((target,new))
    receipt=dict(schema='autonomous-v19-native-hooks-v1',native_sources=rows,native_compile=False,native_link=False,runtime=False,
        frozen_v18_untouched=True,actor_creation=False,server_or_database_modified=False)
    helpers=['AutonomousPlayerAudit.cpp','AutonomousPlayerAudit.h','AuditEvidence.h','needs-bridge/build_need_bridge.py']
    helpers+=['needs-bridge/'+name for name in bridge.EXTRA_SOURCES+bridge.EXTRA_HEADERS]
    receipt['helper_sources']=[dict(path=name,sha256=sha((HERE/name).read_bytes())) for name in helpers]
    patch=''.join(patch);receipt['patch_sha256']=sha(patch.encode())
    if args.check:
        actual=json.loads((HERE/'hook-inputs.json').read_text());assert actual==receipt,'Input or output drift'
        assert (HERE/'native-hooks.patch').read_text()==patch
        for target,new in outputs: assert (HERE/target).read_text()==new,target
    else:
        for target,new in outputs:
            dest=HERE/target;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(new)
        (HERE/'native-hooks.patch').write_text(patch)
        (HERE/'hook-inputs.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(source_candidates=len(rows),exact_seams=True,check_only=args.check,native_compile=False,runtime=False)))
if __name__=='__main__': main()
