# V19 native needs bridge and current-map service travel

Source candidate only. This directory does not claim native compilation, linking, server execution or successful gameplay. Original source, V18, Windows, databases and human players were not changed.

## Integration

`build_need_bridge.py` exports ten native `SOURCE_PATHS`, a pure `transform(relative_name, text)`, two `EXTRA_SOURCES`, and one `EXTRA_HEADERS` entry. The owner's generator composes these transformations with its shortcut exclusions and produces the final combined native patch. Unique anchors are checked, CRLF is preserved, and double application is rejected. Do not install a standalone bridge patch over the owner's composed candidate.

Compile `AutonomousNeedsBridge.cpp` and `AutonomousServiceTravel.cpp` as native TUs, with this directory and the owner's directory containing `AutonomousPlayerAudit.h` on the include path. Ownership APIs are `Reserved(Player const*)`, `ReservedGuid(uint64)` and `Owns(Player*)`.

The owner calls `InitializeTravel()` at the end of safe world startup after TravelMgr initialization, and `UpdateTravelCatalog()` from its world update after map workers join. Initialization only reads configuration. Actual catalog work is requested by a normal map-owned AI using primitive map/position data; the world worker never dereferences a Player or PlayerbotAI. No logout callback is needed: retained state has no actor pointers, and a fresh AI context's different native target invalidates an old route without restoring its goal.

`AiPlayerbot.AutonomousPlayerV19.NeedsBridge = 1` is optional, default true but inert unless the parent's master enable/GUID and actual bot ownership checks pass. No server rates are changed. Existing `StrategyContext` registrations are reused.

## Native behavior

Only the selected actor receives existing `new rpg`, `rpg` and `travel` strategies. Its old RPG trigger list contains services instead of a competing quest loop. The actor's legacy travel selector is replaced by a needs-filtered selector, so it cannot randomly choose old travel quests. Other actors keep the original bodies.

Nearby service selection and execution reuse native `ChooseRpgTargetAction`, RPG movement and subactions. Services cover real inventory selling, paid repair, eligible training, useful affordable vendor purchases, flight-master discovery, binding, learned-recipe crafting with owned reagents, useful trades with verified unmastered bots, and BG queues only when existing BG policy permits. The native temporary-money `rpg taxi` action is absent.

Distant service demand now selects a real service anywhere within 10,000 yards on the actor's current map, instead of waiting for NewRpg to happen past a vendor. Priority is affordable repair, selling when bags require it or repair needs money, missing food/drink supply, then class-cost-driven training. Targets use existing native `RpgTravelDestination`, owned `WorldPosition`, `TravelTarget` and `MoveToTravelTargetAction`; the target stays unforced, preserving native expiry/retries. Native movement owns pathfinding and movement. There is no raw teleport, temporary money or generated gear/materials.

Native WORK means arrival only. Existing nearby RPG selection/movement/actions perform the actual interaction. The route ends when its need is no longer actionable or a bounded failure/timeout occurs; action booleans never imply completion. Actual bag/durability/food/drink/spellbook changes are logged separately as `observed_change`. Service attempts refresh relevant native values and receive a per-actor/action 15-second cooldown.

While the route owns movement, NewRpg goal actions/status updates and opportunistic grind/gather/random movement are gated. Its quest ID, objective index, position and other goal data are preserved. On return, the unchanged goal's status clock is compensated; native quest/NPC arrival markers are reset so it actually walks back, and `NewRpgInfo::SetMoveFarTo` resets stale stuck tracking. Combat and native food/drink remain available. Service raw queue priorities are 4.48–4.8, RPG selection default 4.3, service travel movement 6, low-health food/low-mana drink 9. Multipliers gate eligibility only because the engine applies them after queue selection.

Crafting keeps the native learned-recipe executor. For this actor it fixes the inverted CREATE_ITEM priority branch, sorts by the computed priority, requires a native useful output or skill-up, checks all owned bag reagents, and suppresses the deferred 19-repeat batch. Every actual cast still passes through native casting. The bridge does not create inventory.

## Resource and ownership bounds

No `LoadQuestTravelTable()` call is added. That full loader calls `Clear()`, resets online travel targets, populates unrelated caches and truncates twelve logs. Existing populated native RPG service cache is reused without clearing or changing it. Otherwise only the existing ObjectMgr spawn/template metadata is read and private native service destination objects are created.

Catalogs retain the nearest 2,048 service points on the requested map, with complete-source/partial/unknown-empty coverage labels. At most one pending primitive request is held; builds occur at most once per 60 seconds. Only an actual need triggers requests, and a partial catalog requests recentering after 2,000 yards of movement. Candidate eligibility checks are bounded at 64 per needed service category. A route retains shared ownership of its catalog; publication of another catalog cannot invalidate native destination pointers. Current catalog plus one exact actor's retained route bounds ordinary catalog memory. Selecting a route requires initialized native null-target objects, and cleanup retains the old catalog if those objects unexpectedly disappear before detachment.

Route budget: 10 minutes; interaction budget after arrival: 45 seconds. Failed points are excluded for five minutes, with at most 64 exclusions and at least 60 seconds before another failed-route selection. Native failed-move retries remain active. Missing metadata, a partial catalog or absent candidate is coverage/eligibility evidence, never automatically a server bug.

## Remaining limits

This increment routes on the current map only. Cross-map transports are not supplied by this bridge; NewRpg retains its normal broader transport behavior subject to the owner's exclusions. Distant supplies currently mean food/drink, and distant training is gated by the native class-training cost value. Existing-profession-only training, first profession selection, recipe/material itineraries, auction/bank/mail planning, and full-world service coverage are not claimed. Native vendor eligibility may still fail because of stock, bags or restrictions; bounded retries handle that honestly.

Source-specific limitations remain: `RpgTravelDestination::getEntry()` literally returns zero, so the bridge obtains real entry IDs from its proven public `GetCreatureTemplate()` instead of changing global behavior. Native generic `TravelAction::isUseful()` is false, so arrival uses the existing nearby RPG path. Native recipe format is limited to EFFECT_0 CREATE_ITEM with a first reagent. Native drink usage may be unavailable at exactly zero mana until regeneration because its existing classification uses current mana.

Checks: exact ten source transformations, native API reads, unchanged baseline hashes and rejection of second application. Focused source reviews corrected queue ordering, attempt retry coverage, quest return-to-POI behavior, stale stuck counters and private target lifetime. No broad mock rerun or native/runtime test was substituted for actual evidence. `source-baseline.json`, `verification.json` and `PATCH-MANIFEST.txt` record the checkpoint.
