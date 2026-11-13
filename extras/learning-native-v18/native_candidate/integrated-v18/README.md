# V18 native increment: current state

The parent completed the original **15/15 actual MSVC units** and the pure native link-response test. Actual extraction of the current game.lib SpellEffects member plus its CodeView SHA256 matched the current source. No native LINK, deployment, background collection or learning episode is claimed yet.

The remaining integration gap is genuine: the actual modules.lib does not define PlayerbotHolder::IsRaidOwnedBot or IsManagedLogin, while the scheduler has compiled against their new declarations. Header-only compatibility cannot solve this. The native plan now adds one complete real implementation as a **16th unit**, preserving every previous job/source/include order and successful object.

## Compile just the genuine Holder implementation

```powershell
$repo = 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18'
$nativeCandidate = Join-Path $repo 'extras\learning-native-v18\native_candidate'
& "$nativeCandidate\windows-incremental-v18\Build-Incremental.ps1" `
  -PlanPath "$nativeCandidate\integrated-v18\v18-compile-plan.json" `
  -SourceRoot "$nativeCandidate\integrated-v18" `
  -OutputRoot "$repo\build-v18" -Compile -OnlyJob native_playerbot_mgr
```

The supplied PlayerbotMgr.cpp SHA256 is263feddd092098d84dfdfae429cbde4bf94f1c5731f51fb510363a806c1d3460. The staged native-api/PlayerbotMgr.cpp SHA256 is6a8379b0d58f0c25dd3683fb1a8dc0bd357b61c50cb71b1b4a9dd8a6fa853494. The only staging additions are a Creature forward declaration and explanatory comment before the first include; its source-local PlayerbotMgr.h resolves the already bound166-line header49050bb78713278eee9b0f2513b3b2ba7cf7c37c4852b1ed3835faf35aa79a5a. All actual registry, lease, callback, session receipt and logout code is retained. These are not detached query stubs.

The new job reuses the existing PlayerbotAI ABI/flags response as an explicit flag template; that template is not claimed as PlayerbotMgr baseline provenance. The real CharacterLoginPending.h/core tracker must already exist and resolve from the actual native tree. It was not in the2864-file source handoff. Find and bind the real file if the existing flags do not locate it. Never create a look-alike tracker or bypass the native login-pending check.

The compiled full Holder replaces the current library's entire Holder translation unit, so linking requires current-source/member preservation review. See ../holder-native-v18 for baseline delta and layout evidence and ../windows-link-v18/ACCEPTANCE.md for the native provider contract. An active PlayerbotMgr.cpp differing from the reviewed baseline must be rebased; do not overwrite unknown local fixes.

## Source and link composition

The existing nine prepared native files are unchanged. Do not append a tenth source to the sparse patch or rerun its mixed-state apply over already prepared files. The new implementation is already reviewable source under native-api; no new source/database clone is needed.

The unchanged nine-file patch composes current V14 Engine/PlayerbotAI, V15 authority/cast receipts and V11 coordinated DualInterrupt. It retains sharedWindows reservations and final returns. The obsolete Nazgrim TU must not replace dual_pending. The updated link overlay is eight replacements plus eight additions:96 explicit/102 total objects, retaining the original37 library arguments and all inherited F509 fixes. It must select the new Holder object for old and new methods; duplicate-symbol suppression is forbidden.

## Connected and missing scope

The source connects the AI scope provider, lifecycle registry, bounded async diagnostics, response binding and optional native five-person Group/LFG attempt. Generic dungeon epoch remains explicitly unknown0. No learned policy controls actions.

Real managed Holder APIs are supplied here; dedicated recruitment/bootstrap callers are a separate source layer and are not claimed newly registered by this16th unit. With no managed callers enabled their registries are genuinely empty. Do not treat query existence as proof that dedicated raid actors were created. Existing ambient five-person admission remains distinct.

Still missing: actual16th compile and full LINK, controlled raw capture, repeated completed-instance reset/requeue, autonomous10/25 raid entry, complete episode/reward/shared learner/promotion, and native resource measurements. The separately saved V19 normal world player is not mixed into this compile.

## Existing source preparation

For a new workspace only, Prepare-Sources.py validates the original nine raw sources, applies their exact sparse git patch and verifies prepared hashes. Existing prepared source and input objects remain untouched. The script installs nothing. No test-double header or fixture executable belongs in the native increment. Current Windows continuation should use the selective command above instead of regenerating or recompiling the15 successful units.
