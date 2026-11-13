# Small Windows incremental compile entry

This entry compiles one unchanged V17 observer module using the verified local V15 response flags. It requires only the existing Windows PowerShell and VS toolchain. It does not use Library transfer, Python, another compiler installation, a database or a running server. The module alone has no native caller; successful compilation is not integration or runtime proof.

Place these reviewed UTF-8 source files under a dedicated `native_candidate` directory preserving paths:

- `engine-shadow-v17/EngineShadowObserverV17.h`
- `engine-shadow-v17/EngineShadowObserverV17.cpp`
- this `windows-incremental-v18` directory

From existing PowerShell, with `$src` pointing to that `native_candidate` directory:

```powershell
$src = 'E:\WRL-1112v1\RuntimeCheck-v1\RaidParallel-v1\learning-native-v18\native_candidate'
& "$src\windows-incremental-v18\Build-Incremental.ps1" -PlanPath "$src\windows-incremental-v18\observer-compile-plan.json" -SourceRoot $src -OutputRoot 'E:\WRL-1112v1\RuntimeCheck-v1\RaidParallel-v1\learning-native-v18\obj'
# The first call only checks inputs. Compile the checked module:
& "$src\windows-incremental-v18\Build-Incremental.ps1" -PlanPath "$src\windows-incremental-v18\observer-compile-plan.json" -SourceRoot $src -OutputRoot 'E:\WRL-1112v1\RuntimeCheck-v1\RaidParallel-v1\learning-native-v18\obj' -Compile
```

The response-file transform retains the original C++20/MD/Win64 defines and include flags. It changes only the exact checked source and output object/PDB paths and adds reviewed include directories. No original source, object, formal EXE or V15 rsp is overwritten. Inspect `compile-results.json` and the job log. The script has not yet been executed on Windows; report that limitation until the parent supplies its real compiler receipt.

The complete V18 collector, response and scheduler sources are still being composed. Their future compile plan must lock all composed inputs and preserve all 88 existing V15 explicit link objects, especially F509 dungeon/Worgen/guild/hearthstone fixes. This small plan cannot link or deploy a complete V18 service.
