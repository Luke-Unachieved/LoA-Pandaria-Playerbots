#!/usr/bin/env python3
"""Read-only, stdlib COFF/C13 source-checksum audit. JSON stdout; no file writes.

Format references and deliberately limited acceptance scope: tools/README.md.
Exit 0 = checksum match; 1 = checksum mismatch; 2 = unknown/unsupported/error.
"""

import argparse
import hashlib
import json
import ntpath
import os
from pathlib import Path
import posixpath
import stat
import struct
import sys


METHOD = "embedded-codeview-source-checksum-match"
BIGOBJ_MAGIC = bytes.fromhex("c7a1bad1eebaa94baf20faf66aa4dcb8")
ALGORITHMS = {1: ("MD5", 16), 2: ("SHA1", 20), 3: ("SHA256", 32)}
MAX_OBJECT = 512 * 1024 * 1024
MAX_SOURCE = 64 * 1024 * 1024
MAX_SECTIONS = 200000
MAX_RECORDS = 200000
MAX_PATH_BYTES = 32768


class Unknown(ValueError):
    """The supported format cannot establish a unique source binding."""


def span(data, offset, size, label):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise Unknown("Out-of-bounds " + label)
    return memoryview(data)[offset:offset + size]


def unpack(fmt, data, offset, label):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt), label))


def cstring(data, offset, label):
    span(data, offset, 1, label)
    end = bytes(data).find(b"\0", offset, min(len(data), offset + MAX_PATH_BYTES + 1))
    if end < 0:
        raise Unknown("Unterminated or overlong " + label)
    try:
        return bytes(data[offset:end]).decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise Unknown("Unsupported non-UTF-8 " + label) from exc


def parse_object(data):
    """Return file-checksum entries; never interpret an EXE, archive or /GL object."""
    first, second = unpack("<HH", data, 0, "COFF signature")
    if (first, second) == (0, 0xFFFF):
        span(data, 0, 56, "bigobj header")
        version, machine = unpack("<HH", data, 4, "bigobj version/machine")
        if version != 2 or bytes(data[12:28]) != BIGOBJ_MAGIC:
            raise Unknown("Unsupported anonymous/import/LTCG object or bigobj version")
        count, symptr, symbols = unpack("<III", data, 44, "bigobj table fields")
        table, symbol_size, form = 56, 20, "bigobj-v2"
    else:
        machine, count, _, symptr, symbols, optional, flags = unpack(
            "<HHIIIHH", data, 0, "COFF header")
        if optional or flags & 0x0002:
            raise Unknown("Expected a COFF object, not a PE image")
        table, symbol_size, form = 20, 18, "coff"
    if machine not in (0x014C, 0x8664):
        raise Unknown("Unsupported machine; this helper supports x86/x64 COFF")
    if not 0 < count <= MAX_SECTIONS:
        raise Unknown("Missing or excessive section count")
    span(data, table, count * 40, "section table")
    table_end = table + count * 40
    coff_strings = None
    if symptr:
        if symptr < table_end:
            raise Unknown("Symbol table overlaps headers")
        span(data, symptr, symbols * symbol_size, "symbol table")
        string_start = symptr + symbols * symbol_size
        (string_size,) = unpack("<I", data, string_start, "COFF string-table size")
        if string_size < 4:
            raise Unknown("Invalid COFF string-table size")
        coff_strings = bytes(span(data, string_start, string_size, "COFF string table"))
    elif symbols:
        raise Unknown("Symbol count without symbol-table pointer")

    string_tables, checksum_tables = [], []
    debug_sections = subsection_count = 0
    for index in range(count):
        header = table + index * 40
        name = bytes(span(data, header, 8, "section name")).split(b"\0", 1)[0]
        if name.startswith(b"/"):
            if not name[1:].isdigit() or coff_strings is None:
                raise Unknown("Unsupported or unresolved long section name")
            name_offset = int(name[1:])
            if name_offset < 4:
                raise Unknown("Invalid long section-name offset")
            name = cstring(coff_strings, name_offset, "section name").encode("utf-8")
        raw_size, raw_start = unpack("<II", data, header + 16, "section raw range")
        if raw_start:
            span(data, raw_start, raw_size, "section raw data")
        if name != b".debug$S":
            continue
        debug_sections += 1
        if raw_start < table_end:
            raise Unknown("Debug section overlaps headers or has no raw data")
        section = span(data, raw_start, raw_size, "C13 section")
        if unpack("<I", section, 0, "C13 signature")[0] != 4:
            raise Unknown("Unsupported .debug$S signature; expected C13 (4)")
        cursor = 4
        while cursor < len(section):
            subsection_count += 1
            if subsection_count > MAX_RECORDS:
                raise Unknown("Excessive subsection count")
            kind, length = unpack("<II", section, cursor, "C13 subsection header")
            payload_start = cursor + 8
            payload = span(section, payload_start, length, "C13 subsection payload")
            next_cursor = (payload_start + length + 3) & ~3
            span(section, cursor, next_cursor - cursor, "C13 subsection padding")
            # DEBUG_S_IGNORE must never become positive provenance evidence.
            if not kind & 0x80000000:
                item = (index + 1, raw_start + payload_start, payload)
                if kind == 0xF3:
                    string_tables.append(item)
                elif kind == 0xF4:
                    checksum_tables.append(item)
            cursor = next_cursor
    if not debug_sections or not checksum_tables:
        raise Unknown("No supported C13 file-checksum subsection")
    if len(string_tables) != 1:
        raise Unknown("Missing or ambiguous C13 string table")
    string_section, _, strings = string_tables[0]
    strings = bytes(strings)
    if not strings or strings[0] != 0:
        raise Unknown("Invalid C13 string table")

    entries = []
    for section_index, file_offset, checksums in checksum_tables:
        if section_index != string_section:
            raise Unknown("Unsupported cross-section checksum/string-table pairing")
        cursor = 0
        while cursor < len(checksums):
            if len(entries) >= MAX_RECORDS:
                raise Unknown("Excessive file-checksum count")
            name_offset, size, kind = unpack("<IBB", checksums, cursor, "checksum header")
            value = bytes(span(checksums, cursor + 6, size, "checksum bytes"))
            next_cursor = cursor + ((6 + size + 3) & ~3)
            span(checksums, cursor, next_cursor - cursor, "checksum padding")
            if kind in ALGORITHMS and size != ALGORITHMS[kind][1]:
                raise Unknown("Invalid checksum length for known algorithm")
            if kind == 0 and size != 0:
                raise Unknown("Invalid None checksum length")
            entries.append({
                "embedded_source_path": cstring(strings, name_offset, "source path"),
                "checksum_kind": kind,
                "algorithm": ALGORITHMS.get(kind, ("unsupported", 0))[0],
                "embedded_checksum": value.hex().upper(),
                "checksum_record_file_offset": file_offset + cursor,
                "section_index": section_index,
            })
            cursor = next_cursor
    return {"format": form, "machine": hex(machine), "section_count": count,
            "debug_section_count": debug_sections, "checksum_entry_count": len(entries)}, entries


def path_key(value):
    """Exact full paths only; no basename/suffix fallback or guessed build directory."""
    drive, tail = ntpath.splitdrive(value)
    if drive and tail.startswith(("\\", "/")):
        return "windows", ntpath.normcase(ntpath.normpath(value))
    if value.startswith("/") and not value.startswith("//"):
        return "posix", posixpath.normpath(value)
    return None


def verify_source(data, source_bytes, expected_path):
    metadata, entries = parse_object(data)
    expected = path_key(expected_path)
    if expected is None:
        raise Unknown("Expected embedded source path must be fully qualified")
    selected = [entry for entry in entries if path_key(entry["embedded_source_path"]) == expected]
    if not selected:
        names = sorted({entry["embedded_source_path"] for entry in entries
                        if ntpath.basename(entry["embedded_source_path"]).lower()
                        == ntpath.basename(expected_path).lower()})
        raise Unknown("No exact full-path checksum entry; same-basename candidates: "
                      + json.dumps(names[:16], ensure_ascii=True))
    identities = {(entry["checksum_kind"], entry["embedded_checksum"]) for entry in selected}
    if len(identities) != 1:
        raise Unknown("Ambiguous checksum records for the selected source path")
    entry = selected[0]
    if entry["checksum_kind"] not in ALGORITHMS:
        raise Unknown("Selected source has no supported checksum")
    actual = hashlib.new(entry["algorithm"].lower(), source_bytes,
                         usedforsecurity=False).hexdigest().upper()
    match = actual == entry["embedded_checksum"]
    return {"coff": metadata, "checksum": {**entry, "actual_source_checksum": actual,
            "matching_record_count": len(selected), "match": match}}, match


def read_bounded(path, limit):
    # Keep the caller's absolute spelling: resolving Windows junctions would
    # silently change the compile-time path we are trying to match.
    resolved = Path(os.path.abspath(path))
    if not stat.S_ISREG(resolved.stat().st_mode):
        raise Unknown("Expected a regular input file: " + str(resolved))
    with resolved.open("rb") as handle:
        before = os.fstat(handle.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise Unknown("Expected a regular input file: " + str(resolved))
        if before.st_size > limit:
            raise Unknown("Input exceeds helper size limit: " + str(resolved))
        data = handle.read(limit + 1)
        after = os.fstat(handle.fileno())
    if len(data) > limit or len(data) != before.st_size or (
            before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise Unknown("Input changed during bounded read: " + str(resolved))
    return data, {"path": str(resolved), "sha256": hashlib.sha256(data).hexdigest().upper(),
                  "bytes": len(data)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", required=True, help="Actual extracted COFF .obj")
    parser.add_argument("--source", required=True, help="Current source file; read exact bytes")
    parser.add_argument("--embedded-source-path", help="Explicit audited original full path if relocated")
    args = parser.parse_args(argv)
    report = {"schema": "native-v18-codeview-source-binding-v1", "method": METHOD,
              "accepted": False, "status": "unknown", "scope": "source-checksum-only",
              "transitive_headers_verified": False, "archive_membership_verified": False}
    code = 2
    try:
        data, report["object"] = read_bounded(args.object, MAX_OBJECT)
        report["object_sha256"] = report["object"]["sha256"]
        source, report["source"] = read_bounded(args.source, MAX_SOURCE)
        expected = args.embedded_source_path or report["source"]["path"]
        report["path_selection"] = {"expected_embedded_path": expected,
            "mode": "explicit-audited-path" if args.embedded_source_path else "current-source-full-path"}
        result, match = verify_source(data, source, expected)
        report.update(result)
        report.update(accepted=match, status="matched" if match else "mismatch")
        code = 0 if match else 1
    except (Unknown, OSError, ValueError, struct.error) as exc:
        report["reason"] = str(exc)
    print(json.dumps(report, indent=2, ensure_ascii=True))
    return code


if __name__ == "__main__":
    sys.exit(main())
