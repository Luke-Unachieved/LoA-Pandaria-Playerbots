# V18 isolated Windows LINK entry

This increment uses the original, hash-locked V15 `Link.rsp`. It changes eight quoted object tokens, prepends seven new objects before every library, and redirects only `/OUT`, `/MAP`, and `/IMPLIB`. The result contains 95 absolute overlay objects plus the six inherited relative worldserver objects. Every original library argument, including repeated `game.lib`, retains its position. F509 Worgen, guild, dungeon, hearthstone, and other inherited objects remain in the response.

There is no Windows access in the implementation workspace. Delivery and the local transform check do **not** claim successful MSVC LINK, valid native provenance, runtime collection, or deployability.

## Native invocation

Run the focused transform check first; it reads the actual V15 response without compiling, linking, or writing:

```powershell
$repo = 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18'
$nativeCandidate = Join-Path $repo 'extras\learning-native-v18\native_candidate'
& "$nativeCandidate\windows-link-v18\Test-LinkResponse.ps1"
```

After all 15 jobs have successful entries in the merged `build-v18\compile-results.json`, validate and write the canonical response plus input snapshot:

```powershell
$repo = 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18'
$nativeCandidate = Join-Path $repo 'extras\learning-native-v18\native_candidate'
$p = @{
  PlanPath = "$nativeCandidate\integrated-v18\v18-compile-plan.json"
  SourceRoot = "$nativeCandidate\integrated-v18"
  CompileRoot = "$repo\build-v18"
  CandidateRoot = "$repo\link-v18"
  WorkingDir = 'E:\WoW548\Build'
}
& "$nativeCandidate\windows-link-v18\Link-Incremental.ps1" @p
```

The default invocation writes `Link.rsp` and `validation-receipt.json`, but does not execute LINK. Missing audit acceptance is reported as a blocked link gate. Missing objects, failed/stale compile receipts, changed hashes, or unresolved libraries stop validation.

When the parent has completed actual native provenance audits, add the evidence paths, rerun validation, then run the same invocation with `-Link`:

```powershell
$p.SpellEffectsAcceptance = "$repo\audit-v18\spelleffects-acceptance.json"
$p.PlayerbotAcceptance = "$repo\audit-v18\playerbot-api-acceptance.json"
& "$nativeCandidate\windows-link-v18\Link-Incremental.ps1" @p
& "$nativeCandidate\windows-link-v18\Link-Incremental.ps1" @p -Link
```

These audit files are not supplied as accepted templates. Their formats are in [ACCEPTANCE.md](ACCEPTANCE.md). Evidence files must live outside the candidate output directory. `-Link` requires the previous accepted validation snapshot and identical input hashes; it refuses stale existing EXE/MAP/LIB/EXP outputs. Use a fresh candidate directory for a retry after a linker invocation.

The native process uses the existing `vcvars64.bat`, `-vcvars_ver=14.44`, and SDK `10.0.22621.0`; it verifies the resulting x64 `link.exe`. It installs nothing. It never copies or starts the candidate, writes source files, accesses a database, or writes to the formal server directory. The protected formal EXE remains `E:\WoW548\Server\worldserver.exe`, SHA256 `F509E005DDF59360F8C3F7EEDB4C1396A2B145F6AD4A17C54FE553FB1DBC0223`.

## Working-directory evidence

The transferred `persistent-constraints-v1\isolated-link-plan.json` (SHA256 `e014d70426985cb5eb6faae936542b45a9fd390ec4cc5b7c0bdecaab322c0210`) names `E:\WoW548\Build`. Its 22 relative input tokens match V15 exactly: six objects, one resource, and 15 library arguments. It is a historical `PLAN_ONLY_NOT_EXECUTED` record, not direct proof of V15 execution. The script hash-checks this file and matches its relative token sequence, then resolves the native inputs.

`E:\WRL-1112v1\D\WOW\RaidAI-Dev\Build\prod0d107-raid-z7-v3` occurs in compile include flags; that does not establish a linker working directory. Automatic selection succeeds only when the historically named directory is the sole complete local resolver among the recorded clues. Missing or ambiguous resolution requires `-WorkingDir`; the documented invocation supplies it explicitly. No relative object or library is rewritten to a guessed path.

## Receipts and limits

Validation rechecks all 88 original object hashes, even the eight replaced objects. It checks the current source/header closure declared by the compile plan, all 15 successful source/object receipts and their required `response_sha256` against the actual compile responses, original compile inputs, the resolved object/resource/library files, evidence artifacts, the formal EXE, and installed linker. The input snapshot locks current libraries, since the transferred V15 manifest did not contain their hashes. It does not retroactively prove V15 library identity or compilation-time header identity.

`link-receipt.json` records native exit status, candidate output hashes/sizes, response and validation hashes, and before/after input/formal integrity. `link.log` retains stdout/stderr. Exit zero and EXE/MAP existence indicate native LINK completion only; native MAP/symbol/PE review and subsequent runtime verification remain separate. `/DEBUG:NONE`, `/INCREMENTAL:NO`, and other original linker flags are preserved: “incremental” here means replacing a limited object set, not enabling the MSVC incremental linker.
