# V19 checkpoint

V18 is frozen. Work here is a separate source candidate, not deployed and not native compiled.

Resumed the existing AutonomousPlayerAudit implementation rather than replacing it. The exact composable hook generator produces sparse native candidates and a standard patch, with the authoritative current file list in `hook-inputs.json`. Focused patch-application/final-hash and standalone evidence/recovery checks passed, with originals and V18 unchanged. Evidence rejects invalid/nonfinite/backward input and accounts for needs-cleared/money progress. The need bridge uses actual queue priorities, native services, real owned craft reagents and per-action attempt backoff; multipliers only gate eligibility.

Neither Player.cpp nor NewRpgAction.cpp is replaced, preserving the known separate F509/current Player source boundary. Other baseline hashes identify the upload only and are not proof of current Windows source. Actual current-source mapping/rebase and native compilation are still required before deployment.

The requested one autonomous world player is not yet claimed complete: NewRpg alone does not dispatch all maintenance/profession/trade/dungeon needs. No database, human character, server process or original native input has been modified.

Bounded native login/relogin is implemented using exact cache→random-account ownership, the existing QueuePlayerBot/world callback and native cross-holder lease. It begins after completed world startup plus 30 seconds, refuses human/account collisions and reset/service rows, and backs off to 15 minutes without clearing a pending lease. Native random AI identity and logout cleanup are actor-specific. The focused patch/hash and login-budget checks pass; native compile/runtime remain false.

The full-activity seam is pinned to exact current composed V18 PlayerbotAI raw hash `2c0b398061b6a61893a380458ef4fbf79d55e1d34450decfadd1ddef712abc94`. One verified V19 actor remains active without nearby humans, after native validity checks, while normal population throttling stays unchanged. Existing LFG/instance/combat activity rules already cover V18 training actors; no roster boost was added. Current-map maintenance routing is frozen and composed: startup/world hooks connect a bounded native service catalog and ordinary native travel/interaction, with no global travel rebuild or new database query. All final composition hashes are regenerated below by the normal generator/validator; native compile/runtime/current-Windows-source identity remain unverified.

Parent V18 compilation must continue independently using its already published dependency-closure fix commit `5bcff1a16872b13c68b1bba2b8a5c6456b5ea182`. Do not mix this V19 source candidate into the ongoing V18 MSVC run. Publish V19 helpers, patch, manifests and docs; sparse `native/` review copies and temporary test binaries are not delivery requirements.
