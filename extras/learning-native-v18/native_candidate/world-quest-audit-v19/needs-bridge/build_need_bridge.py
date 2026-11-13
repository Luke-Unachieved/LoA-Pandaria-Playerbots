"""Pure, composable transformations; never edits source or the owner's hooks file."""

SOURCE_PATHS = (
    'src/Factory/AiFactory.cpp',
    'src/strategy/RPG/RpgStrategy.cpp',
    'src/strategy/actions/ChooseRpgTargetAction.cpp',
    'src/strategy/RPG/RpgAction.cpp',
    'src/strategy/RPG/RpgSubActions.cpp',
    'src/strategy/triggers/RpgTriggers.cpp',
    'src/strategy/actions/CastCustomSpellAction.cpp',
    'src/strategy/actions/ChooseTravelTargetAction.cpp',
    'src/strategy/generic/TravelStrategy.cpp',
    'src/Travel/TravelMgr.cpp',
)
EXTRA_SOURCES = ('AutonomousNeedsBridge.cpp', 'AutonomousServiceTravel.cpp')
EXTRA_HEADERS = ('AutonomousNeedsBridge.h',)


def _once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise ValueError(f'needs bridge expected one anchor, found {count}: {old[:110]!r}')
    return text.replace(old, new, 1)


def _body(text, signature, prefix):
    return _once(text, signature + '\n{\n', signature + '\n{\n' + prefix)


def transform(relative_name, text):
    """Return candidate source; input is native module-relative path and UTF-8 text."""
    name = str(relative_name).replace('\\', '/')
    if '/src/' in name:
        name = 'src/' + name.split('/src/', 1)[1]
    if name not in SOURCE_PATHS:
        return text
    crlf = '\r\n' in text
    text = text.replace('\r\n', '\n')
    if '#include "AutonomousNeedsBridge.h"' in text:
        raise ValueError('needs bridge transformation already applied: ' + name)
    first_include = text.index('#include ')
    text = text[:first_include] + '#include "AutonomousNeedsBridge.h"\n' + text[first_include:]

    if name.endswith('/AiFactory.cpp'):
        anchor = '    // Objective play, for a bot that is actually inside a match (unchanged).'
        text = _once(text, anchor,
            '    // V19: only the configured actor receives the service bridge.\n'
            '    // Broad goals remain the existing NewRpg weighted status chooser.\n'
            '    if (AutonomousNeedsV19::Configured(facade) && !player->InBattleground() && !TolBaradBots::InWar(player))\n'
            '        nonCombatEngine->addStrategiesNoInit("new rpg", "rpg", "travel", nullptr);\n\n' + anchor)
    elif name.endswith('/RpgStrategy.cpp'):
        text = _once(text, 'new NextAction("rpg", 1.0f)',
            'new NextAction("rpg", AutonomousNeedsV19::Configured(botAI) ? 4.3f : 1.0f)')
        text = _body(text, 'float RpgActionMultiplier::GetValue(Action* action)',
            '    if (AutonomousNeedsV19::Configured(botAI))\n'
            '        return AutonomousNeedsV19::RpgMultiplier(botAI, action);\n\n')
        text = _body(text, 'void RpgStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)',
            '    if (AutonomousNeedsV19::InitServiceTriggers(botAI, triggers))\n'
            '        return;\n\n')
    elif name.endswith('/ChooseRpgTargetAction.cpp'):
        text = _once(text, 'triggerNode->getFirstRelevance() > 2.0f',
            'triggerNode->getFirstRelevance() > (AutonomousNeedsV19::Configured(botAI) ? 5.0f : 2.0f)')
        text = _once(text, '                if (dynamic_cast<RpgEnabled*>(action))',
            '                if (dynamic_cast<RpgEnabled*>(action) &&\n'
            '                    (!AutonomousNeedsV19::Configured(botAI) ||\n'
            '                     AutonomousNeedsV19::ServiceActionPossible(botAI, action)))')
        text = _once(text, '    GuidPosition guidP(guidps.front());',
            '    if (AutonomousNeedsV19::Configured(botAI) && guidps.empty())\n'
            '        return false;\n\n'
            '    GuidPosition guidP(guidps.front());')
    elif name.endswith('/RpgAction.cpp'):
        text = _once(text,
            '                    if (!dynamic_cast<RpgEnabled*>(action) || !action->isUseful() || !action->isPossible())',
            '                    if (!dynamic_cast<RpgEnabled*>(action) || !action->isUseful() || !action->isPossible() ||\n'
            '                        (AutonomousNeedsV19::Configured(botAI) &&\n'
            '                         !AutonomousNeedsV19::ServiceActionPossible(botAI, action)))')
    elif name.endswith('/RpgSubActions.cpp'):
        text = _body(text, 'bool RpgSubAction::Execute(Event event)',
            '    if (AutonomousNeedsV19::Configured(botAI))\n'
            '        rpg->BeforeExecute(); // Set the real selected NPC before the native trainer/vendor action.\n\n')
        text = _once(text,
            '    bool doAction = botAI->DoSpecificAction(ActionName(), ActionEvent(event), true);',
            '    bool doAction = botAI->DoSpecificAction(ActionName(), ActionEvent(event), true);\n'
            '    AutonomousNeedsV19::NoteServiceAttempt(botAI, getName());')
        for signature in ('bool RpgDiscoverAction::Execute(Event /*event*/)',
                          'bool RpgTradeUsefulAction::Execute(Event /*event*/)'):
            text = _body(text, signature,
                '    AutonomousNeedsV19::NoteServiceAttempt(botAI, getName());\n\n')
    elif name.endswith('/ChooseTravelTargetAction.cpp'):
        text = _body(text, 'bool ChooseTravelTargetAction::Execute(Event /*event*/)',
            '    if (AutonomousNeedsV19::Configured(botAI))\n'
            '        return AutonomousNeedsV19::SelectServiceTravel(botAI,\n'
            '            context->GetValue<TravelTarget*>("travel target")->Get());\n\n')
    elif name.endswith('/TravelStrategy.cpp'):
        text = _once(text, 'new NextAction("move to travel target", 1)',
            'new NextAction("move to travel target", AutonomousNeedsV19::Configured(botAI) ? 6.0f : 1.0f)')
    elif name.endswith('/TravelMgr.cpp'):
        text = _body(text, 'bool RpgTravelDestination::isActive(Player* bot)',
            '    if (AutonomousNeedsV19::Configured(GET_PLAYERBOT_AI(bot)))\n'
            '        return AutonomousNeedsV19::ServiceDestinationActive(GET_PLAYERBOT_AI(bot), this);\n\n')
    elif name.endswith('/RpgTriggers.cpp'):
        text = _body(text, 'bool RpgBuyTrigger::IsActive()',
            '    // Native item usage and purpose budget replace the unrelated durability/sell test for this actor.\n'
            '    if (AutonomousNeedsV19::Configured(botAI))\n'
            '        return AutonomousNeedsV19::WantsVendorPurchase(botAI);\n\n')
        text = _body(text, 'bool RpgCraftTrigger::IsActive()',
            '    if (AutonomousNeedsV19::Configured(botAI) && !AutonomousNeedsV19::WantsCraft(botAI))\n'
            '        return false;\n\n')
    elif name.endswith('/CastCustomSpellAction.cpp'):
        text = _once(text,
            '    return spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM && spellInfo->ReagentCount[EFFECT_0] > 0 &&\n'
            '           spellInfo->SchoolMask == 0;',
            '    bool const createsWithReagents = spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM &&\n'
            '        spellInfo->ReagentCount[EFFECT_0] > 0;\n'
            '    if (AutonomousNeedsV19::Configured(botAI))\n'
            '        return createsWithReagents && GetSpellPriority(spellInfo) > 1 &&\n'
            '            AutonomousNeedsV19::HasCraftReagents(botAI, spellInfo);\n'
            '    return createsWithReagents && spellInfo->SchoolMask == 0;')
        text = _once(text,
            '    if (spellInfo->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM)',
            '    // V19 fixes the inverted branch only for its own learned-recipe path.\n'
            '    if (AutonomousNeedsV19::Configured(botAI) ?\n'
            '        spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM :\n'
            '        spellInfo->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM)')
        text = _once(text,
            '    std::sort(spellList.begin(), spellList.end(),\n'
            '              [](std::pair<uint32, std::pair<uint32, WorldObject*>> i,\n'
            '                 std::pair<uint32, std::pair<uint32, WorldObject*>> j) { return i.first > j.first; });',
            '    bool const needsCraft = AutonomousNeedsV19::Configured(botAI) && getName() == "craft random item";\n'
            '    std::sort(spellList.begin(), spellList.end(),\n'
            '              [needsCraft](std::pair<uint32, std::pair<uint32, WorldObject*>> i,\n'
            '                 std::pair<uint32, std::pair<uint32, WorldObject*>> j)\n'
            '              { return needsCraft ? i.second.first > j.second.first : i.first > j.first; });')
        text = _once(text,
            '            if (MultiCast && ((wo && BotHasInArcAC(bot, CAST_ANGLE_IN_FRONT, wo, sPlayerbotAIConfig->sightDistance))))',
            '            // Re-evaluate need and owned reagents after each actor craft; no queued 19-repeat batch.\n'
            '            if (MultiCast && !needsCraft && ((wo && BotHasInArcAC(bot, CAST_ANGLE_IN_FRONT, wo, sPlayerbotAIConfig->sightDistance))))')
    return text.replace('\n', '\r\n') if crlf else text
