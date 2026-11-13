# V19 native actor candidate

This directory extends the frozen V18 source increment; it does not replace it. V18 source, collector, response/raid hooks and deployment files were not edited. V19 is still a candidate: native compilation, linking, runtime and one-player completion are **not verified**.

## Native hook composition

`build_hooks.py` reads the uploaded module source plus V18's prepared `mod_playerbots.cpp` and `NativeBackgroundTraining.cpp`. It writes only sparse `native/` candidates, `native-hooks.patch`, and `hook-inputs.json` in this directory. Every replacement requires one exact source anchor. `--check` regenerates in memory and requires exact source/output hashes and patch bytes. The baseline sources must match this upload or be explicitly rebased; never apply a different whole source over newer local fixes.

`validate_hooks.py` applies the standard patch in a disposable sparse directory, checks all final hashes, rechecks original inputs and compiles/runs only `tests/test_evidence.cpp`. The resulting `validation-report.json` distinguishes these checks from native compilation. This does not justify deploying or claiming gameplay success.

The composed candidate changes the source files enumerated in `hook-inputs.json`, including the need bridge and native login holder header/TU. Neither `Player.cpp` nor `NewRpgAction.cpp` is replaced: the formal V15 link's F509/current Player fixes must remain intact. Hashes in this package identify the uploaded baseline, not current Windows source identity. Any differing current TU requires a narrow rebase and fresh hashes before compilation; do not overwrite it with these sparse candidates.

The hooks perform these operations:

| Native seam | Candidate behavior |
|---|---|
| V18 `mod_playerbots.cpp` startup/shutdown/world drain | After normal travel startup, start immutable config, bounded native session admission and primitive evidence drain; no map traversal. |
| Existing native post-player update | Observe the same actor after native AI actions; never run a global actor-scanning loop. |
| Current composed V18 `PlayerbotAI::AllowActive/AllowActivity` | Full background and detailed movement activity only for the verified V19 actor, after native invalid-session/world/teleport guards; no human-presence or rotation requirement. Cache rechecks preserve transition legality. |
| Existing login/logout | Reset observation generation/recovery state. A bounded scheduler reuses native asynchronous login for the exact existing managed actor; no actor creation. |
| `PlayerbotHolder::QueuePlayerBot` / native callback | Recheck authoritative cache/account ownership, no conflicting session, no shutdown, no pending reset/service flags before core login. Existing native cross-holder GUID lease remains authoritative. |
| V18 five-man observation/pool/admission | Exclude the exact designated GUID, including final native admission recheck. |
| Random population/factory/level-up/level-reset entry points | Exclude this actor from manufactured levels, skills, gear, consumables, money and starter quest completions. |
| Native NewRpg/legacy quest acceptance and reward | Reject known-broken templates and record real pre/post quest state; never treat an attempted action as acceptance/reward proof. |
| Native alternative path calculation | Record actual cone path results. Recovery can request the existing owned hearthstone item action after failed alternatives and time budget. |
| Recovery shortcuts elsewhere | Exclude raw stuck/graph portal/corpse/BG placement/Tol Barad/arena/Lost City relocation and forced combat clear. Native taxi map transitions and normal core invitation transfers remain. |
| Existing service actions | `needs-bridge/` composes the existing native needs/triggers/actions with NewRpg for this actor only; see its README for limits. |

## Compile composition

Treat `native/mod_playerbots.cpp` as the V19 replacement for V18's prepared callback TU, and `native/NativeBackgroundTraining.cpp` as the replacement for V18's helper TU. The remaining `native/` paths correspond to module `src/` paths. Do not compile both old and replacement versions. Other V18 helpers and already integrated mechanic sources remain the same.

`native/AI/PlayerbotAI.cpp` uses **only** the exact current composed V18 raw SHA-256 `2c0b398061b6a61893a380458ef4fbf79d55e1d34450decfadd1ddef712abc94`. The generator rejects any other baseline. Its collector/response/F509-related integrated hooks are retained; generic uploaded PlayerbotAI is never substituted. The activity change is performance policy for one actor, not action authority or pull permission. Existing native code already forces activity for combat, BG queues, active LFG queues and non-overworld dungeon/raid/BG maps, so no V18 training-roster boost or new controller state was added. Ordinary population rotation/scaling remains unchanged.

Compile `AutonomousPlayerAudit.cpp`, `needs-bridge/AutonomousNeedsBridge.cpp` and `needs-bridge/AutonomousServiceTravel.cpp` once each. Add this directory and `needs-bridge/` to the module include path. This input has partial core headers, so no fake native stub build is supplied and no native compile success is claimed. Actual Windows response flags/object/link provenance must be validated on the real build separately, preserving the parent's current V18 incremental compilation.

## Configuration and evidence

The master switch and actor GUID default off/zero. Supply one **existing managed bot**'s exact raw GUID and enable only after native validation. Restart is required because ownership configuration is immutable. Thirty seconds after completed native world startup, the future runtime can log in/relog this actor through the existing `QueuePlayerBot` asynchronous world callback. The exact character must map in `CharacterCache` to the loaded native random-account allowlist and be outside raid/provisioning ownership. No other character/account is searched, no account or character is created, and no scenario/test registration or population-event write is used.

The scheduler checks the existing cross-holder GUID lease; it never abandons a pending query or queues a second one on a timeout. Failed/refused attempts use 30/60/120/240/480/900-second bounded backoff. A present actor is checked every 30 seconds. Enqueue and callback revalidate the same authoritative ownership and refuse conflicting connected/account sessions; the native queue also retains its existing human-login pending tracker. Before `HandlePlayerLogin`, existing loaded native rows must match GUID/account with no at-login reset flags or pending character services. Shutdown stops new admissions and makes pending callbacks refuse before core login. Normal native logout persistence is retained; designated-actor central AI cleanup prevents stale registration on relog. No live server or database operation was executed while developing this package; the normal login query is future native behavior.

The ambient random manager cannot seize this actor's session/level. Native random-player AI identity is recognized only after the existing cache/account/raid exclusions, without the test-only `AddCurrentBotForTest` helper. Runtime needs/recovery require a native bot session, no master and no loading/logout/removal. Group/master transitions remain an autonomy limitation.

Keep the existing XP policy (AI 1x, humans 5x). V19 records the actual per-player kill/quest/explore rates and flags mismatch without changing any rate. This upload lacks the complete core rate mapping; gathering XP's global-rate call is a separate policy gap. Human rates are not measured by this actor observer.

Hearthstone 0 cooldown is treated as intended loaded item/spell policy. The code records the loaded spell/item cooldown fields and observed remaining cooldown, waits on actual legality, and never clears a cooldown, combat, attackers or movement to make a cast legal. A dispatched `use` result is only an attempt. Native cast observation plus bind arrival is the success postcondition; a timeout is unknown with no repeated cast. The original goal is replanned only if its exact task/status/location still matches.

Evidence covers XP, quest objectives, live maintenance/service needs, owned money and position (capability mask 31). It does not yet observe skill/gear/durability/trade receipts. Clearing needs or money gain counts as activity, and pending maintenance suppresses navigation suspicion. A navigation finding still means suspected path/AI coverage after three failed paths and five quiet minutes, never proof of a server bug. Invalid/nonfinite/backward observations are rejected. The queue is bounded to 256 primitive records with reported drop counts; individual frames/objective deltas are sampled/bounded.

## Honest completion boundary

See `CAPABILITY-MATRIX.md`. Current-map repair/sell/supplies/training routing now uses native TravelTarget/MoveToTravelTargetAction and a bounded service catalog, with native nearby interaction on arrival. Catalog work is world-thread metadata only; no global travel cache clear or new query runs. Cross-map service planning, independent dungeon choice, full profession/material routing, native paid spirit-healer interaction and general scripted quest coverage remain gaps. The implementation reuses normal native play and prevents known shortcuts; it does **not** establish that every requested gameplay capability is complete. No server, database or human character was accessed or changed.
