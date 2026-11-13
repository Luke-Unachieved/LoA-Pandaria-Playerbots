# Native evidence contracts

Only create an `accepted` receipt after the corresponding native audit succeeds. Reviewer identity may be the parent agent recording tool-supported evidence; no human approval ceremony is required. These receipts state evidence-based conclusions, and are not substitutes for collecting the evidence. All paths are absolute native Windows paths; every SHA256 is a 64-character hexadecimal digest of the actual file.

## SpellEffects acceptance

`-SpellEffectsAcceptance` reads a JSON object with these fields:

| Field | Required meaning |
| --- | --- |
| `schema` | `native-v18-spelleffects-acceptance-v1` |
| `decision` | `accepted` after the audit passes |
| `reviewer`, `rationale` | Nonempty audit identity and evidence-based explanation |
| `formal_sha256` | Protected F509 formal EXE hash |
| `game_library` | `{ "path": "...", "sha256": "..." }`, the actual `src\server\game\game.lib` resolved under the selected working directory |
| `current_source` | `{ "path": "...", "sha256": "..." }`, the exact current baseline source proven to belong to that library member |
| `prepared_source_sha256` | Hash of the compiled `prepared/native/SpellEffects.cpp` |
| `evidence` | Array of `{ "kind": "...", "path": "...", "sha256": "..." }`; exactly one each of `current-source-to-library` and `minimal-delta-review` is required |

The `current-source-to-library` evidence file is JSON:

- `game_library`: same path/hash as the acceptance.
- `archive_member`: `{ "name": "<exact archive member name>", "extracted_path": "...", "sha256": "..." }`.
- `standalone_object`: `{ "path": "...", "sha256": "..." }`. Its bytes must match the extracted member.
- `source_binding`: `{ "kind": "...", "report": { "path": "...", "sha256": "..." } }`.

The source-binding report is JSON with `source: {path, sha256}`, `object_sha256` matching the member, `method` matching the kind, and `accepted: true`, plus the actual audit details. Accepted methods are:

1. `recompiled-object-byte-match`: baseline recompilation produces the exact member bytes, with the actual response, headers and compiler documented.
2. `recompiled-linkable-content-match`: a documented comparison verifies runtime code/data, unwind data, relocations, external definitions and linker directives. Only COFF timestamps/debug metadata may be excluded; report the exclusions and differences.
3. `embedded-codeview-source-checksum-match`: the extracted object's actual CodeView `DEBUG_S_FILECHKSMS` entry for this exact source matches the current source checksum. Record checksum algorithm, embedded checksum, actual source checksum, source hash, member hash, and extraction evidence. A path string, timestamp, `/Z7` flag, or matching uploaded source hash alone is insufficient. This proves source identity; it does not claim transitive-header reproducibility.

The `minimal-delta-review` evidence should record the two source hashes, the three additions, zero deletions, and equality after normalization/removal. The link entry independently normalizes CRLF to LF, removes exactly once each of the reviewed include/EffectScope/Complete lines, and requires the remainder to equal `current_source` byte-for-byte. This preserves all baseline behavior, including Legacy changes, outside the additive observer hook.

Optional additional evidence, such as a bounded baseline provider check or a full symbol scan, may appear in `evidence`; its hash is also locked. An exhaustive scan of 149 methods across all archives is a diagnostic for actual conflicts, not a prerequisite for an ordinary isolated candidate. The fixed original response must have no `/FORCE` duplicate-symbol bypass. Native LINK uses `/VERBOSE:LIB` and writes the original requested MAP. After exit zero, the script requires the MAP to attribute both `Spell::EffectInterruptCast` and global `SpellEffects` to `native_spell_effects.obj`; otherwise the candidate is rejected. Any known older explicit override must also be resolved in the parent audit.

## Explicit PlayerbotMgr implementation acceptance

`-PlayerbotAcceptance` reads JSON:

- `schema`: `native-v18-playerbot-api-acceptance-v2`.
- `decision`: `accepted`; nonempty `reviewer` and `rationale`.
- `modules_library`: `{path, sha256}` for the actual `modules\modules.lib` resolved under the same working directory.
- `binding_header`: `{path, sha256}` for the verified header used by both compiled scheduler and complete implementation TUs.
- `active_header`: `{path, sha256}` for the actual active native header reviewed against that binding.
- `provided_source`: `{path, sha256}` for the genuine provided complete implementation, before the include-order prelude. This is a candidate origin, not a claim that the active native CPP matches it.
- `baseline_source`: `{path, sha256}` for the actual active baseline CPP proven to correspond to the reviewed old modules member. Locate and compare the actual native file; if it differs from the reviewed baseline, resolve/rebase that difference before acceptance.
- `baseline_object`: `{path, sha256, archive_member}` for the actual old extracted modules member and its exact archive member name.
- `implementation`: `{job: "native_playerbot_mgr", source: {path, sha256}, object: {path, sha256}, compile_response: {path, sha256}}`, matching the actual successful 16th compile receipt. The source is the complete native implementation; query stubs or reconstructed policy are not accepted substitutes.
- `evidence`: exactly one `{kind, path, sha256}` each for `native-api-symbols`, `class-layout-review`, `baseline-source-binding`, and `module-member-preservation`. Additional evidence is also hash-locked.

The parent reviews actual native tools and files before recording acceptance. Evidence may be raw native output or a readable parent audit report; complex ABI and preservation reviews need no additional JSON schema. The `native-api-symbols` report must contain actual `dumpbin /symbols` records from the accepted explicit object: exactly one `SECT...` / `External | <decorated symbol>` definition each for `PlayerbotHolder::IsManagedLogin`, `IsRaidOwnedBot`, its constructor, ordinary `AddPlayerBot`, and `GetPlayerBotsSnapshot`. The entry checks these real definition records. UNDEF references and archive-index names do not qualify. The two new APIs need not exist in the old modules library.

The layout review compares the active and exact binding headers, including bases, virtuals, field types and ordering. The baseline source-binding report preserves the actual module/member extraction and source CodeView checksum proof (or another supported source-binding method described above), with source and object identities. The module-member-preservation review accounts for the baseline-to-provided implementation delta and shared ownership/registry lifecycle, preserving existing behavior outside reviewed changes. It must establish that replacing the complete old member is appropriate. These reports are parent evidence-based judgments; locking a report alone does not establish its conclusion.

The entry binds the scheduler to this exact header using either its sole actual `/FI` response token or its first literal source include, `#include "../native-api/PlayerbotMgr.h"`. The new implementation must use its first literal `#include "PlayerbotMgr.h"`, resolving to the same header beside the staged CPP. Source-include mode requires `class Creature;` before that include and no forced includes. Both source hashes and actual responses must match successful compile receipts. Validation records both bindings and the explicit implementation identities.

The entry independently normalizes CRLF to LF and removes exactly one ordered pair of prelude lines immediately before the native include: `// Complete Creature before Player.h -> ConditionMgr.h in this native include order.` followed by `class Creature;`. The remainder must equal `provided_source`. UTF-8 BOMs are handled by text decoding. This verifies preparation preserves the complete provided implementation; it does not prove that the active baseline matches that candidate origin.

After native LINK exits zero, exact MAP public-symbol rows must attribute both new APIs, the Holder constructor, ordinary `AddPlayerBot`, and `GetPlayerBotsSnapshot` to `native_playerbot_mgr.obj`. Unwind/pdata rows do not qualify. Holder public functions attributed to the reviewed old archive member reject the candidate, preventing mixed Holder state providers. The receipt records these MAP rows and requires `playerbot_providers_verified` before `candidate_accepted` can be true. No `/FORCE` bypass is permitted.

Both acceptance files and all directly referenced evidence/source/member/header/library files are rehashed during validation and immediately before LINK through the repeated validation pass. The complete input snapshot is checked again after LINK. This prevents reusing accepted evidence with changed objects or libraries, while retaining the audit's precise limits.
