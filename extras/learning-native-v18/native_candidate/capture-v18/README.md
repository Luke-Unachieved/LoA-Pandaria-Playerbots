# Native V18 capture startup plan

These scripts prepare local configuration and optionally run the accepted candidate quietly. They do not replace the formal F509 EXE, install tools, clone a database, require a human client, or enable a learned policy. No service was started in the cloud. **Do not start until the parent confirms localization/database coordination is clear.**

## Readiness before any process

1. All15native objects compiled and linked with actual matching PlayerbotMgr API and SpellEffects provenance. Native link receipt must have candidate_accepted=true, current output hash and unchanged formal/input locks. The runner reopens the hash-bound sibling validation-receipt.json, requires all15native source/object jobs and rehashes every input lock, the canonical Link.rsp and the exact protected F509 formal EXE before validation or startup.
2. Parent confirms no simultaneous localization DB work and no worldserver process. MySQL should already be the existing configured service; these scripts do not start another DB/auth service. Authserver is unnecessary for this bot-only backend check.
3. Read the actual native Main.cpp/CLI help if this build's `-c` or `server shutdown 0` differs. Those are the existing conventional server config/console interfaces; the wrapper records forced shutdown as incomplete if graceful shutdown fails.

## Prepare profile and configurations

Run existing Python3.11.16; no new Python installation. Set `$tools` to the Git source `extras\learning-native-v18\native_candidate\capture-v18` directory. Example same-E-drive run:

```powershell
& $python "$tools\Prepare-Capture.py" --base-runtime 'E:\WoW548\Server' --data-dir 'E:\WoW548\Data' --output 'E:\WoW548\Staging\Capture-v18-first-five' --profile first-five
# Read-only validation above; actually prepare the small local config copy:
& $python "$tools\Prepare-Capture.py" --base-runtime 'E:\WoW548\Server' --data-dir 'E:\WoW548\Data' --output 'E:\WoW548\Staging\Capture-v18-first-five' --profile first-five --prepare
```

The generator copies at most64one-level `.conf` files/4MiB, sets explicit existing DataDir and candidate LogsDir, and adds the same new keys to worldserver.conf and playerbots.conf so the module's later LoadAdditionalFile cannot silently override them. The actual module fallback filename is **playerbots.conf**, despite old log strings saying aiplayerbot.conf. Original configs remain unchanged. Actual copied configs contain local connection secrets: **keep them local; never upload them to GitHub or Library**. Only these source tools/templates belong in the repository.

Profiles:

| Profile | Collector | Native five-man admission | Purpose |
| --- | --- | --- | --- |
| boot | off | off | Startup compatibility of linked candidate |
| observe | on | off | Raw observations of already existing eligible bot-only instance groups |
| first-five | on | on | First concrete full1tank/1healer/3DPS Group/LFG attempt |

All profiles leave learned control absent, new interrupt shadow features off and existing native raid/character settings unchanged. Profile first-five uses concrete LFG464/青龙寺, map960, normaldifficulty1, level85–90,1/1/3, from the original18414client table SHA4bbb40c43efd3b7f29f9013ba5eec587ceab34b48a9528fc9c8e5becf728725c. Runtime server DBC/LFG eligibility remains authoritative and is rechecked by native code. It does not force admission, fabricate roles/levels/gear or skip mechanics. Native AiPlayerbot.RandomBotJoinLfg defaults true; confirm the copied actual config keeps native proposal/role handlers available. If no suitable five free native actors exist, record waiting/role counts; do not call it a successful run or force an unmatched roster.

## Explicit bounded background run

After coordination is actually confirmed:

```powershell
& $python "$tools\Run-Capture.py" --link-receipt 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\link-v18\link-receipt.json' --run-directory 'E:\WoW548\Staging\Capture-v18-first-five'
# Above only validates. The parent uses these switches only after coordination:
& $python "$tools\Run-Capture.py" --link-receipt 'E:\WRL-1112v1\RuntimeCheck-v1\LearningShadow-v18\link-v18\link-receipt.json' --run-directory 'E:\WoW548\Staging\Capture-v18-first-five' --start --localization-clear --seconds 120
```

The selected run directory must match the original prepared manifest. Its validated worldserver.conf is the exact config passed to the child; config/raw/log paths cannot escape through symlinks or reparse points. The existing external DataDir remains required. A startup is refused if a run receipt, console logs or raw collector segments already exist; prepare a fresh run directory for every launch. Console outputs are created exclusively.

Use `--seconds 120` (separate argument and value). The default bounded window is120seconds from process launch, not a guaranteed120seconds of ready-world gameplay. Startup can consume the window; inspect server-ready logs before interpreting an empty run. No desktop window opens. Only the owned candidate receives the normal console shutdown command; after60seconds without shutdown, only that child is terminated and its capture remains explicitly incomplete.

## Result interpretation

- `[TRAINING]` backend logs distinguish pool/roles, queued native proposal, actual instance readiness, combat and transition waits. A queue or native Boolean is not dungeon success.
- Raw directory is four exclusive files, at most4MiB each/16MiB total. Existing segment0 prevents accidental overwrite. Use one explicit new run directory; remove obsolete run directories only after retaining the useful receipt/evidence.
- `start` alone proves writer initialization. `record` proves an observed native Engine path; `stream_stats` preserves rejected-only diagnostics. `stop`/queue_drained refers only to the writer queue, not the game action queue or a complete episode.
- All rows remain diagnostic/incomplete; generic five-man scope keeps unknown encounter epoch0. No learning-performance claim, automatic model promotion, finished-instance repeat run or autonomous10/25raid is established by this first check.
