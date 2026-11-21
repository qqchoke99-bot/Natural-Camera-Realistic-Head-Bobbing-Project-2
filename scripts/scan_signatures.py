#!/usr/bin/env python3
"""Check CameraOverhaul signature patterns against a libminecraftpe.so build.

The mod resolves two functions and three struct offsets for one specific
Minecraft Bedrock build. When Mojang ships a new build, the questions are:

  1. Do the patterns in GameProfiles.hpp still match?
  2. Do they match exactly once (an ambiguous hit is as bad as a miss)?
  3. At which RVA do they land, so the new build can be compared to the old one?

This script answers those without a disassembler:

    python3 scripts/scan_signatures.py --library /path/to/libminecraftpe.so

It reads patterns straight out of include/cameraoverhaul/memory/GameProfiles.hpp,
so the header stays the single source of truth. Handy extras:

    --pattern "D1 ? ? A9"       scan an ad-hoc pattern instead
    --dump 0x1234567 48         hex dump 48 bytes at an RVA (to craft a new pattern)
    --json                      machine-readable output

Only loadable, executable ELF segments are scanned; patterns live in code.
Exit code is 0 when every signature has exactly one hit, 1 otherwise.
"""

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_HEADER = PROJECT_ROOT / "include" / "cameraoverhaul" / "memory" / "GameProfiles.hpp"

PT_LOAD = 1
PF_X = 1
EM_AARCH64 = 183
EM_X86_AARCH64_MAP = {183: "aarch64", 62: "x86-64", 40: "arm", 3: "x86"}


class ElfError(Exception):
    pass


class Segment:
    def __init__(self, index, offset, vaddr, filesz, memsz, flags):
        self.index = index
        self.offset = offset
        self.vaddr = vaddr
        self.filesz = filesz
        self.memsz = memsz
        self.flags = flags

    @property
    def executable(self):
        return bool(self.flags & PF_X)

    def rva(self, file_offset):
        return self.vaddr + (file_offset - self.offset)


class Elf:
    def __init__(self, data):
        if len(data) < 64 or data[:4] != b"\x7fELF":
            raise ElfError("not an ELF file")
        if data[4] != 2:
            raise ElfError("only 64-bit ELF files are supported")
        if data[5] != 1:
            raise ElfError("only little-endian ELF files are supported")
        self.data = data
        self.machine = int.from_bytes(data[0x12:0x14], "little")
        phoff = int.from_bytes(data[0x20:0x28], "little")
        phentsize = int.from_bytes(data[0x36:0x38], "little")
        phnum = int.from_bytes(data[0x38:0x3A], "little")
        if phentsize < 56:
            raise ElfError(f"unexpected program header size {phentsize}")
        self.segments = []
        for index in range(phnum):
            base = phoff + index * phentsize
            if base + 56 > len(data):
                raise ElfError("truncated program header table")
            p_type = int.from_bytes(data[base:base + 4], "little")
            if p_type != PT_LOAD:
                continue
            flags = int.from_bytes(data[base + 4:base + 8], "little")
            offset = int.from_bytes(data[base + 8:base + 16], "little")
            vaddr = int.from_bytes(data[base + 16:base + 24], "little")
            filesz = int.from_bytes(data[base + 32:base + 40], "little")
            memsz = int.from_bytes(data[base + 40:base + 48], "little")
            self.segments.append(Segment(index, offset, vaddr, filesz, memsz, flags))

    def executable_segments(self):
        return [segment for segment in self.segments
                if segment.executable and segment.filesz > 0 and segment.offset + segment.filesz <= len(self.data)]

    def segment_for_rva(self, rva):
        for segment in self.executable_segments():
            if segment.vaddr <= rva < segment.vaddr + segment.filesz:
                return segment
        return None


def parse_pattern(text):
    """'D1 ? 0F' -> (bytes, mask); '?' bytes are wildcards and ignored for matching."""
    values = []
    masks = []
    for token in text.split():
        token = token.strip()
        if token in ("?", "??"):
            values.append(0)
            masks.append(False)
            continue
        token = token.replace("_", "")
        if token[:2].lower() == "0x":
            token = token[2:]
        if not re.fullmatch(r"[0-9a-fA-F]{1,2}", token):
            raise ValueError(f"bad pattern token {token!r}")
        values.append(int(token, 16))
        masks.append(True)
    if not any(masks):
        raise ValueError("pattern has no fixed bytes")
    return bytes(values), masks


def find_matches(data, values, masks, limit=16):
    """Return every file offset where the pattern matches."""
    anchor = next(index for index, fixed in enumerate(masks) if fixed)
    anchor_byte = values[anchor]
    span = len(values)
    matches = []
    position = data.find(bytes([anchor_byte]))
    while position != -1 and len(matches) < limit:
        start = position - anchor
        if start >= 0 and start + span <= len(data):
            if all(not masks[i] or data[start + i] == values[i] for i in range(span)):
                matches.append(start)
        position = data.find(bytes([anchor_byte]), position + 1)
    return matches


def scan(elf, values, masks):
    hits = []
    for segment in elf.executable_segments():
        chunk = elf.data[segment.offset:segment.offset + segment.filesz]
        for offset in find_matches(chunk, values, masks):
            hits.append(segment.rva(segment.offset + offset))
    return hits


SIGNATURE_MARKER = re.compile(r"SignaturePatterns\{\{\s*//\s*(\w+)")
STRING_LITERAL = re.compile(r'"((?:\\.|[^"\\])*)"')


def parse_header(path):
    """Pull profiles and their candidate patterns out of GameProfiles.hpp.

    The header is deliberately shaped so this stays a simple regex job:
    each SignaturePatterns block starts with a `// SignatureName` comment and
    the patterns are the quoted strings that follow, until the block closes.
    """
    text = path.read_text(encoding="utf-8")
    profiles = []
    profile_re = re.compile(
        r"GameProfile\{\s*\"([^\"]*)\",\s*(0x[0-9a-fA-F]+|\d+),\s*(0x[0-9a-fA-F]+|\d+),\s*(0x[0-9a-fA-F]+|\d+),\s*\{\{",
        re.S)
    matches = list(profile_re.finditer(text))
    for index, match in enumerate(matches):
        version = match.group(1)
        offsets = tuple(int(match.group(i), 0) for i in (2, 3, 4))
        body_start = match.end()
        body_end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[body_start:body_end]
        signatures = {}
        markers = list(SIGNATURE_MARKER.finditer(body))
        for marker_index, marker in enumerate(markers):
            stop = markers[marker_index + 1].start() if marker_index + 1 < len(markers) else len(body)
            chunk = body[marker.end():stop]
            chunk = chunk.split("}}", 1)[0]
            signatures[marker.group(1)] = [literal for literal in STRING_LITERAL.findall(chunk) if literal.strip()]
        profiles.append({"version": version, "offsets": offsets, "signatures": signatures})
    if not profiles:
        raise ValueError(f"no GameProfile entries parsed from {path}")
    return profiles


def describe_offsets(offsets):
    return "actorStateVector=0x{:X} actorRotation=0x{:X} cameraRotation=0x{:X}".format(*offsets)


def dump_at(elf, rva, length):
    segment = elf.segment_for_rva(rva)
    if segment is None:
        print(f"0x{rva:X} is not inside an executable segment", file=sys.stderr)
        return None
    file_offset = segment.offset + (rva - segment.vaddr)
    data = elf.data[file_offset:file_offset + length]
    lines = []
    for start in range(0, len(data), 16):
        chunk = data[start:start + 16]
        hex_part = " ".join(f"{byte:02X}" for byte in chunk)
        ascii_part = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        lines.append(f"0x{rva + start:08X}  {hex_part:<47}  {ascii_part}")
    return lines


def load_library(path):
    """Read a .so, or pull libminecraftpe.so out of an APK / zip that holds it."""
    if path.suffix.lower() in (".zip", ".apk", ".levipack"):
        with zipfile.ZipFile(path) as archive:
            entries = [name for name in archive.namelist() if name.endswith("libminecraftpe.so")]
            if not entries:
                raise ElfError(f"{path.name} has no libminecraftpe.so entry")
            entries.sort(key=lambda name: (0 if ("arm64" in name or "aarch64" in name) else 1, name))
            return archive.read(entries[0]), entries[0]
    return path.read_bytes(), path.name


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library", required=True, type=Path,
                        help="libminecraftpe.so, or a zip/apk containing it")
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER, help="GameProfiles.hpp (default: %(default)s)")
    parser.add_argument("--pattern", action="append", default=[], help="extra ad-hoc pattern to scan")
    parser.add_argument("--dump", nargs=2, metavar=("RVA", "LENGTH"), type=lambda value: int(value, 0),
                        help="hex dump code at an RVA")
    parser.add_argument("--json", action="store_true", help="print JSON instead of a report")
    args = parser.parse_args(argv)

    try:
        blob, entry_name = load_library(args.library)
        elf = Elf(blob)
    except (OSError, zipfile.BadZipFile, ElfError) as error:
        print(f"cannot read {args.library}: {error}", file=sys.stderr)
        return 2

    machine = EM_X86_AARCH64_MAP.get(elf.machine, f"machine {elf.machine}")
    if elf.machine != EM_AARCH64:
        print(f"warning: {args.library.name} is {machine}, not aarch64 "
              "(fine for dry runs, but real builds must be arm64)", file=sys.stderr)

    if args.dump:
        lines = dump_at(elf, args.dump[0], args.dump[1])
        if lines is None:
            return 2
        print("\n".join(lines))
        return 0

    try:
        profiles = parse_header(args.header)
    except (OSError, ValueError) as error:
        print(f"cannot parse {args.header}: {error}", file=sys.stderr)
        return 2

    results = {"library": str(args.library), "entry": entry_name, "machine": machine,
               "profiles": [], "patterns": []}
    ok = True
    for profile in profiles:
        entry = {"version": profile["version"], "offsets": describe_offsets(profile["offsets"]),
                 "signatures": {}, "complete": True}
        for name, candidates in profile["signatures"].items():
            signature_entry = {"candidates": [], "resolved": None}
            for candidate in candidates:
                try:
                    values, masks = parse_pattern(candidate)
                except ValueError as error:
                    signature_entry["candidates"].append({"pattern": candidate, "error": str(error)})
                    continue
                hits = scan(elf, values, masks)
                signature_entry["candidates"].append({
                    "pattern": candidate,
                    "hits": [f"0x{rva:X}" for rva in hits],
                    "unique": len(hits) == 1,
                })
                if len(hits) == 1 and signature_entry["resolved"] is None:
                    signature_entry["resolved"] = f"0x{hits[0]:X}"
            if signature_entry["resolved"] is None:
                entry["complete"] = False
                ok = False
            entry["signatures"][name] = signature_entry
        results["profiles"].append(entry)

    for pattern in args.pattern:
        values, masks = parse_pattern(pattern)
        hits = scan(elf, values, masks)
        results["patterns"].append({"pattern": pattern, "hits": [f"0x{rva:X}" for rva in hits]})
        if len(hits) != 1:
            ok = False

    if args.json:
        print(json.dumps(results, indent=2))
        return 0 if ok else 1

    print(f"library : {args.library}")
    if entry_name != args.library.name:
        print(f"entry   : {entry_name}")
    print(f"machine : {machine}")
    print(f"size    : {len(elf.data):,} bytes, {len(elf.executable_segments())} executable segment(s)")
    for entry in results["profiles"]:
        status = "MATCH" if entry["complete"] else "INCOMPLETE"
        print(f"\nprofile {entry['version']} [{status}]")
        print(f"  offsets: {entry['offsets']}")
        for name, signature in entry["signatures"].items():
            resolved = signature["resolved"] or "not found"
            print(f"  {name:<22} {resolved}")
            for candidate in signature["candidates"]:
                if "error" in candidate:
                    print(f"      ! {candidate['error']}")
                    continue
                hits = candidate["hits"]
                if len(hits) == 1:
                    verdict = "unique"
                elif not hits:
                    verdict = "no match"
                else:
                    verdict = f"{len(hits)} matches (ambiguous)"
                print(f"      {verdict:<22} {' '.join(hits[:4])}")
    for pattern in results["patterns"]:
        state = "unique" if len(pattern["hits"]) == 1 else ("no match" if not pattern["hits"] else "ambiguous")
        print(f"\npattern [{state}]: {pattern['pattern']}")
        for rva in pattern["hits"][:8]:
            print(f"  {rva}")
    print("\nresolved" if ok else "\nNOT resolved: update the patterns in GameProfiles.hpp")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
