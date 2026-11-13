# Native V18: next local action

## Confirmed actual evidence

Parent reports original15/15 MSVC units passed; native response transform test passed95 explicit/101 total objects with37 original library tokens. The current game.lib SpellEffects member was actually extracted and its CodeView source SHA256 matched the current source. Keep that evidence; do not repeat it merely to accumulate checks.

Actual modules.lib does not export IsRaidOwnedBot or IsManagedLogin. The active135-line PlayerbotMgr.h is the older API; the bound166-line header has the new managed layer. No final LINK, deployment, runtime or learning success has happened. F509 remains unchanged.

## New minimal increment

This source delivery adds the complete genuine provided PlayerbotMgr.cpp as one16th TU. It uses the same bound header, with only a Creature forward declaration/comment inserted before the original first include. Source SHA2566a8379b0d58f0c25dd3683fb1a8dc0bd357b61c50cb71b1b4a9dd8a6fa853494; original SHA263feddd092098d84dfdfae429cbde4bf94f1c5731f51fb510363a806c1d3460. Registry, lease, real callback, session and logout implementation remains intact. No safety guard was removed.

```powershell
$repo = 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18'
$n = Join-Path $repo 'extras\learning-native-v18\native_candidate'
& "$n\windows-incremental-v18\Build-Incremental.ps1" `
  -PlanPath "$n\integrated-v18\v18-compile-plan.json" `
  -SourceRoot "$n\integrated-v18" -OutputRoot "$repo\build-v18" `
  -Compile -OnlyJob native_playerbot_mgr
```

All15 existing job records are unchanged, so the compiler entry checks and reuses their actual source/object/response triples. It compiles only the new source using the existing PlayerbotAI flag template. Do not run the whole branch as a clean replacement or rerun the nine-source preparation over already prepared files.

## Actual source closure that must be checked

1. Hash/read current active PlayerbotMgr.cpp and compare with the reviewed baseline. The known Git baseline fromefe7f291 has CRLF SHA f1645eb69e2f2f8746be2c1385c1967ac6ade4981a04b8d17d928f336c406b6a. Header CRLF SHA0b10575107d124367fcf51f523ad8f903392968d8b8b2e1f9de2b9fc147f8ac0. An actual CPP mismatch requires a review/rebase, not blind replacement. The provided candidate is a real pending managed implementation, not proof that it was ever built into F509.
2. Locate the **actual CharacterLoginPending.h and its native core hooks**, referenced by the supplied CPP but absent from the cloud handoff. Existing Windows core sources may already contain them. Use their real include path/implementation; do not synthesize an isolated substitute. If absent, return the genuine source closure for integration. Missing headers or native tracker symbols remain real blockers.
3. Before linking, bind the actual current modules.lib member to its active baseline source and review the minimal delta. Source layout comparison keeps PlayerbotHolder base/type aliases/fields/virtuals unchanged. Actual local matching still needs evidence. Updated windows-link-v18/ACCEPTANCE.md accepts the explicit Holder object as provider, since confirmed absent exports cannot pass the old modules-only contract.

Inspect-PlayerbotApi.ps1 now uses explicit global:LASTEXITCODE initialization/capture. PowerShell native commands update that global value; the old script-local initializer masked it. A successful dumpbin with no selected symbols now remains exit0 and empty symbols. No new process wrapper or full archive-index dump was added. Remove only the old erroneous inspection report before rerunning the same fixed output path; it refuses to overwrite an existing report.

## Link and bounded background capture

Read windows-link-v18/README.md and ACCEPTANCE.md. Composition is now16 jobs,8 original replacements+8 additions,96 explicit/102 total objects; all37 original libraries remain. The new actual MAP must select native_playerbot_mgr.obj for the required queries and selected existing Holder methods, while SpellEffects keeps its already reviewed source and provider check. No /FORCE bypass is permitted.

The parent runs the limited native parser/transform check for changed link entry, then validates and links into a dedicated candidate directory. The child has no Windows executor; native compilation/link status must come from actual local receipts.

Read capture-v18/README.md for optional no-window bounded capture using the existing Python. Reuse the existing DB, no full server/data/DB copies. Keep copied configurations local because they can contain secrets. Coordinate localization/DB use before the already authorized --start --localization-clear operation. First-five tries actual native LFG464/map960,1tank/1healer/3DPS; waiting or raw rows do not mean a finished dungeon.

## Remaining gameplay scope

Dedicated recruitment registration/bootstrap is not enabled just by supplying the Holder TU. Autonomous10/25 entry, repeat/reset, complete learning episodes, shared model training/promotion and resource evidence remain unfinished. User plan remains5/10/25 teams40AI, no human prerequisite, quiet ordinary dungeons, optional raid/challenge guidance. V19 normal autonomous world player remains separate and requires later rebase; F509 Worgen/guild/hearthstone/dungeon/Player.cpp and localization patches remain protected.
