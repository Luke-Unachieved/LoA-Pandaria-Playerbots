# Native V18: current local continuation

## Actual checkpoint

- Repository: Luke-Unachieved/LoA-Pandaria-Playerbots; branch: `dev/learning-shadow-v18`.
- Latest compiler correction: `2c6762312c0eebf0de0c87a51f804f1ee70d58fa` (descends from `512fd1c`).
- Parent Windows evidence: **14/15 native translation units passed**. Only `NativeBackgroundTraining` failed. All-15 success, native LINK, runtime collection, training episodes and model control are still unverified.
- Formal server remains the parent-reported F509 EXE. No source in this child starts a server automatically. Coordinate the existing localization/DB owner before the later authorized background run.

## 1. Resume only the failed native unit

Fetch the branch's current reviewed text sources. Preserve raw bytes using the supplied `.gitattributes`. Do not rebuild the branch baseline or overwrite local F509 patches.

```powershell
$repo = 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18'
$nativeCandidate = Join-Path $repo 'extras\learning-native-v18\native_candidate'
& "$nativeCandidate\windows-incremental-v18\Build-Incremental.ps1" `
  -PlanPath "$nativeCandidate\integrated-v18\v18-compile-plan.json" `
  -SourceRoot "$nativeCandidate\integrated-v18" `
  -OutputRoot "$repo\build-v18" -Compile -OnlyJob NativeBackgroundTraining
```

The new script verifies and reuses each of the other 14 actual successful objects, source hashes and exact response contents. It writes a merged 15-job receipt with response hashes. The last source now declares `class Creature;` before its first include and explicitly binds `../native-api/PlayerbotMgr.h`. Both `IsRaidOwnedBot` and `IsManagedLogin` checks remain. No ownership check was removed.

Binding header SHA256: `49050bb78713278eee9b0f2513b3b2ba7cf7c37c4852b1ed3835faf35aa79a5a`. Final scheduler source SHA256: `39e90fe72ccf032374c70e1db681b6ada46b0d09d23566e1297d8abd0f164dba`.

## 2. Verify the actual native implementation, then LINK

The last correction fixes header selection; it does not prove the actual `modules.lib` exports the required matching implementation. Use `windows-link-v18/tools/Inspect-PlayerbotApi.ps1` with the actual active header path (override its default if needed). Read the symbol report and compare class layout/type aliases/bases/virtuals/fields. If APIs are absent, resolve the genuine implementation rather than adding stubs or deleting checks.

For SpellEffects, extract the exact current `game.lib` member and compare it with the standalone object. `windows-link-v18/tools/Read-CodeViewSource.py` can bind that member's actual compiler checksum to the current baseline source. The candidate is exactly three additive lines; removing them must reproduce the baseline. Paths, timestamps and ninja_log alone are insufficient. A full 149-method scan is not required unless a real symbol conflict needs diagnosis.

Read `windows-link-v18/README.md` and `ACCEPTANCE.md`. The new native LINK entry preserves the original V15 libraries and inherited objects, replaces eight objects and adds seven. Run the focused PowerShell transform/parser checks on Windows, collect the actual two acceptance reports, validate once, then pass `-Link`. Candidate outputs stay in `$repo\link-v18`; the formal directory is never an output target. No `/FORCE` bypass is added. The receipt distinguishes actual LINK success from accepted inputs/MAP providers. There is no Windows PowerShell runtime in the cloud; these scripts need their first native execution by the parent.

## 3. Bounded background capture after coordination

Read `capture-v18/README.md`. It contains small configuration preparation and quiet candidate launch entries using the existing Python 3.11 executable. No new DB, game copy or desktop client is required. Actual copied configuration files may contain secrets: keep them local, never upload them.

Profiles are `boot`, `observe` and `first-five`. The latter attempts native Group/LFG admission of five existing eligible actors (1 tank, 1 healer, 3 DPS) to normal 青龙寺, client LFG 464/map960. Runtime native eligibility remains authoritative. A default 120-second bounded process window is a collection check, not a promise of a complete dungeon. MySQL must already be available; the wrapper does not start DB/auth services. Parent must confirm localization/DB coordination before `--start --localization-clear`.

## Remaining scope

- This V18 establishes observer/provider/response binding and a first five-person native admission path. Native 10/25 autonomous raid entry, completed-instance repeat/reset, complete episode extraction, shared learning/promotion and actual resource/quality measurements are not finished.
- The user target remains 5/10/25 teams (40 AI), shared experience, no human prerequisite, quiet normal dungeons and optional raid/challenge guidance.
- V19 autonomous world player is a separate source checkpoint from `512fd1c`; do not mix it into the current 15-unit V18 compile. It requires rebasing its older scheduler baseline after this V18 fix. No V19 Windows mapping/compile/runtime success is claimed.
- Preserve parent F509 Player.cpp and all Worgen, guild, hearthstone, dungeon and localization fixes. Existing V11 coordinated DualInterrupt is the source being patched, not the obsolete Nazgrim version.
- Model remains shadow-only. Raw diagnostic rows, successful admission or a successful Boolean do not prove an action succeeded or a native training episode completed.
