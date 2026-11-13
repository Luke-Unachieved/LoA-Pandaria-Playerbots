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

## PlayerbotMgr API acceptance

`-PlayerbotAcceptance` reads JSON:

- `schema`: `native-v18-playerbot-api-acceptance-v1`.
- `decision`: `accepted`; nonempty `reviewer` and `rationale`.
- `modules_library`: `{path, sha256}` for the actual `modules\modules.lib` resolved under the same working directory.
- `binding_header`: `{path, sha256}` for the verified header binding used by the compiled scheduler TU.
- `evidence`: exactly one `{kind, path, sha256}` each for `native-api-symbols` and `class-layout-review`.

The native-symbol report must show real definitions of `PlayerbotHolder::IsRaidOwnedBot` and `PlayerbotHolder::IsManagedLogin` in the resolved implementation and compatibility with the compiled declarations. UNDEF references are not definitions. The layout review must account for the active header and exact binding header, preserving class layout and ABI. If implementation symbols are absent or layout is incompatible, the parent must resolve the matched implementation; the link entry offers no stub or policy-removal bypass.

The link entry binds this exact header to the compiled scheduler using either its actual `/FI` response token or its first literal source include, `#include "../native-api/PlayerbotMgr.h"`. Source-include mode requires the `class Creature;` declaration before that include, no compiler-forced includes, and the header include before every other include. The relative path must resolve to `binding_header.path`. The source hash must already match both the compile plan and successful compile receipt. The validation receipt records `playerbot_api_binding.mode`, header/source hashes, and actual response path. The current fix uses source-include mode so the forward declaration precedes the legacy condition-header chain; no additional prelude header is required.

Both acceptance files and all directly referenced evidence/source/member/header/library files are rehashed during validation and immediately before LINK through the repeated validation pass. The complete input snapshot is checked again after LINK. This prevents reusing accepted evidence with changed objects or libraries, while retaining the audit's precise limits.
