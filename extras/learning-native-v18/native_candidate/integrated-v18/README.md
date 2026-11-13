# Composed V18 native source increment

This is a source-only increment: nine native translation units are changed and six helper/controller units are added. Native Windows compilation, worldserver linking, deployment and gameplay have not been verified. No learned policy controls actions.

## Existing code retained

The patch composes the current V14 Engine/PlayerbotAI, V15 authority/cast receipts and V11 **coordinated** DualInterrupt source. It preserves the existing sharedWindows reservations, final checks and return values. Do not compile the obsolete original Nazgrim TU over `dual_pending.obj`. The V15 88 explicit link input hashes were verified by the parent; `link-overlay.json` records eight replacements and seven additions, retaining all other local fixes.

The V18 observer header/TU in `new/` has explicit dungeon scope fields and replaces V17 consistently for every compilation. Do not link both observer versions. Quote includes must resolve `new/EngineShadowObserverV17.h`.

## Prepare sparse native source text

Use existing Python and git (including GitHub Desktop embedded git if necessary). The script does not install anything or use Library transfer. `inputs.json` checks the exact original raw bytes, makes only a sparse LF-normalized source staging copy, applies the standard textual patch, checks every final hash and rechecks originals. No full source/database/server copies or overwrites are made.

With `$here` pointing to this directory and `$python` to the already-present Python executable:

```powershell
& $python "$here\Prepare-Sources.py" --output "$here\prepared"
& $python "$here\Prepare-Sources.py" --output "$here\prepared" --apply
```

Both commands first validate all source/dependency hashes. The first is read-only. If git is not on PATH, add `--git 'PATH-TO-EXISTING-GIT.EXE'`; inspect installed tools before installing. Optional `--source-map` supplies explicit local paths for known sources but does not relax any hash check.

`SpellEffects.cpp` in the uploaded input is only a base comparison file. Its link-member/current-source identity remains unproven: do not link/deploy this increment until the parent verifies its current local provenance or rebases the minimal interrupt hunk onto the actual current file. A matching old uploaded file alone does not establish currentness.

## Incremental compilation

Use the sibling `windows-incremental-v18/Build-Incremental.ps1` with `v18-compile-plan.json`, `-SourceRoot $here`, a dedicated E-drive `-OutputRoot`, and then `-Compile`. It preserves exact existing compiler flags (including both linewise module and single-line core response formats). Fifteen TU entries are explicit; original rsp/source/object files are not modified. This script only compiles; link mapping is separate and remains blocked until actual compiler and source provenance receipts are available.

## What is connected in source

- Actual native AI callback installs a concrete scope provider, lifecycle identity registry, bounded diagnostic drain and async writer. Dungeon raw scope explicitly keeps unknown encounter epoch at zero. Output is always marked incomplete, not a reward episode.
- Real coordinated interrupt call binds the actual response immediately before prepare; native cancel/effect proof has a consumer and lifecycle retirement. It supports the existing narrow normal10 whitelist, not every class/boss/difficulty.
- Existing post-map Playerbots dispatcher performs optional native Group/LFG admission of a full1/1/3 five-man roster; default off. Transient loading retains roster tracking rather than orphaning it. Native proposal/role/entry logic is reused.

Still missing: repeat finished-dungeon exit/requeue, autonomous10/25 raid entry, full episode features/reward/learner/shared-experience/model promotion and resource measurements. The three-team40-actor budget is planned, not a demonstrated runtime. The additional normally-progressing autonomous world player requested later is being developed separately as V19, without blocking this source increment.

## Reuse

LoA native Engine, existing class/action execution, Group/LFG, V14 raid constraints, V15 cast identities/reservation book and V16 attempt book are retained. External research repositories are pinned in the lab reuse map; framework research is not falsely described as copied/installed production code. No native test-double header or test executable is part of this source increment.
