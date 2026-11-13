# Read-only CodeView source binding

`Read-CodeViewSource.py` uses Python 3.11's standard library. It reads the extracted object and the exact source bytes, emits JSON on stdout, and does not write files, invoke processes, install packages, access a database, or run the server. The caller may save stdout in its audit directory.

```powershell
$nativeCandidate = Join-Path $repo 'extras\learning-native-v18\native_candidate'
$helper = "$nativeCandidate\windows-link-v18\tools\Read-CodeViewSource.py"
$json = & $python $helper --object "$audit\SpellEffects.archive.obj" `
  --source 'E:\WoW548\Source\src\server\game\Spells\SpellEffects.cpp'
$result = $LASTEXITCODE
# This saves only the audit report, not either inspected input.
[IO.File]::WriteAllText("$audit\source-binding.json", ($json -join "`n"),
  [Text.UTF8Encoding]::new($false))
if ($result -ne 0) { throw "Source binding was not established; inspect the JSON report." }
```

Use the existing Python executable for `$python`; there is no dependency installation. `$audit` is the existing isolated audit directory containing the member extracted by `lib.exe /EXTRACT`. Exit status 0 means `accepted: true` and `status: matched`; status 1 means an exact-path source record was found but its checksum differs; status 2 means unknown, malformed, unsupported, ambiguous, missing or unreadable evidence. Never turn a nonzero result into an accepted receipt.

The report has the link acceptance contract's `method: embedded-codeview-source-checksum-match`, `source: {path, sha256}`, `object_sha256`, and `accepted` fields. It also records the object path/hash/size, COFF format and counts, exact embedded path, algorithm, embedded checksum, actual source checksum, record offset, and repeated-record count. Bind the saved report's actual SHA256 as `source_binding.report` in the separate `current-source-to-library` evidence. This helper does not produce the overall SpellEffects acceptance and does not establish archive membership: the separate native extraction and standalone/member equality checks remain necessary.

Path selection is exact after Windows slash/case and lexical normalization (POSIX paths are case-sensitive). There is no basename/suffix match and no guessed compiler working directory. Missing exact paths or conflicting records return unknown. If an inspected object was intentionally relocated, an auditor may explicitly specify `--embedded-source-path 'E:\original\full\SpellEffects.cpp'`; the report records that mapping, and the checksum must still match the actual supplied source bytes. Relative embedded paths are unsupported. Repeated entries for the same full path are accepted only when algorithm and checksum agree. BOMs and CRLF are not stripped: the compiler checksum is compared against the bytes on disk.

Supported formats are standard x86/x64 COFF and bigobj version 2 with the documented UUID; `.debug$S` must have C13 signature 4. The parser reads little-endian subsection headers, `StringTable` (0xF3) and `FileChecksums` (0xF4), and four-byte record/subsection alignment. MD5, SHA1 and SHA256 have their specified 16/20/32-byte lengths. Ignored subsections cannot supply evidence. Unsupported `/GL`, archives, PE images, older C13 alternatives, non-UTF-8 paths, absent/ambiguous string tables, or cross-section table pairing return unknown rather than guessed results. The supported normal object layout has its file-checksum and string table in the same `.debug$S`; other debug sections may hold unrelated subsections. Input sizes and all consumed ranges/counts are bounded (512 MiB object, 64 MiB source, 200,000 sections/subsections/checksum records, 32,768-byte paths).

A positive report establishes equality with a compiler-recorded source checksum inside those exact object bytes. It is not proof of transitive-header identity, compiler authenticity, code equivalence, the chosen link provider, or runtime correctness. Historical MD5/SHA1 records are compiler provenance evidence in this non-adversarial audit; they are not a tamper-resistance claim. Current full-file SHA256 values bind the evidence to the native link receipts. The limited acceptance flow still checks archive/member equality, exact three-line source reconstruction, immutable input hashes, original no-`/FORCE` options, and the selected providers in the post-link MAP. An exhaustive 149-method scan remains optional diagnostic evidence.

Format definitions were checked against these primary implementations:

- [Microsoft C13 signature, subsection header/types and ignore flag](https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h).
- [LLVM standard and bigobj headers, and section layout](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/Object/COFF.h), [bigobj UUID and byte sizes](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/BinaryFormat/COFF.h).
- [LLVM checksum entry fields and four-byte alignment](https://github.com/llvm/llvm-project/blob/main/llvm/lib/DebugInfo/CodeView/DebugChecksumsSubsection.cpp), [checksum enum](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/DebugInfo/CodeView/CodeView.h).
- [LLVM subsection bounds/alignment](https://github.com/llvm/llvm-project/blob/main/llvm/lib/DebugInfo/CodeView/DebugSubsectionRecord.cpp) and [NUL-terminated string lookup](https://github.com/llvm/llvm-project/blob/main/llvm/lib/DebugInfo/CodeView/DebugStringTableSubsection.cpp).

Fixture checks exercise these layouts and failure paths only. They do not constitute an actual native SpellEffects provenance result; that requires running this helper against the real extracted Windows archive member and current source.
