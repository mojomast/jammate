#!/usr/bin/env python3
"""Style-catalog provenance tool (STYLE-DIRECTOR-002).

Resolves every reference shipped in `src/jam/StyleCatalog.cpp` against the
actual compiled drum library (`src/DrumLibrary.cpp`) and enforces a source-hash
regression, because `src/jam` is JUCE-free and cannot link the library directly.

Why this exists
---------------
The library has no string identifiers; entries are `genre` + `name`, and that
pair is not unique (16 collisions, 27 duplicated names). `("FUNK","Linear funk")`
is a groove at index 63 and a *fill* at index 187. Names alone are therefore
insufficient, so the catalog stores positional indices plus full provenance and
this tool proves those indices still mean what the catalog says.

Modes
-----
  --check   Recompute and fail on any mismatch. Reads the committed JSON and the
            generated C++ fixture and verifies they are current.
  --emit    Regenerate `generated/StyleCatalogLibraryFixture.h` and
            `library_provenance.json`, then run the same checks.

Exit code is non-zero on any mismatch, so this is usable as a CI step.
"""

import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
LIBRARY_SRC = os.path.join(REPO, "src", "DrumLibrary.cpp")
CATALOG_SRC = os.path.join(REPO, "src", "jam", "StyleCatalog.cpp")
CATALOG_HDR = os.path.join(REPO, "src", "jam", "StyleCatalog.h")
GENERATED_DIR = os.path.join(HERE, "generated")
FIXTURE_HDR = os.path.join(GENERATED_DIR, "StyleCatalogLibraryFixture.h")
MANIFEST = os.path.join(HERE, "library_provenance.json")

# { "GENRE", "Name", bpm, swing, fill, "spec" [, num, den] }
ENTRY_RE = re.compile(
    r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*,\s*'
    r'"((?:[^"\\]|\\.)*)"\s*(?:,\s*(\d+)\s*,\s*(\d+)\s*)?\}'
)

# The shipped reference form emitted by build_style_catalog.py:
#   { 0, "ROCK", "Basic", false, 0xB771340Eu, 4, 4 },
REF_RE = re.compile(
    r'\{\s*(\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
    r'\s*(true|false)\s*,\s*(0[xX][0-9A-Fa-f]+|\d+)\s*u?\s*,'
    r'\s*(\d+)\s*,\s*(\d+)\s*\}'
)

FINGERPRINT_RE = re.compile(r'libraryFingerprint\s*\(\)\s*noexcept\s*\{[^}]*?"([0-9a-f]{64})"',
                            re.S)


def fnv1a32(text):
    h = 0x811C9DC5
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


class Failure(Exception):
    pass


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def parse_library():
    """Return {index: entry} for the actual compiled library."""
    src = read(LIBRARY_SRC)
    start = src.index("static const std::vector<Groove> lib = {")
    start = src.index("{", start) + 1
    end = src.index("\n    };", start)
    body = src[start:end]
    entries = {}
    for index, match in enumerate(ENTRY_RE.finditer(body)):
        genre, name, bpm, swing, fill, spec, num, den = match.groups()
        entries[index] = dict(
            index=index,
            genre=genre,
            name=name,
            bpm=int(bpm),
            swing=int(swing),
            fill=(fill == "true"),
            spec=spec,
            num=int(num or 4),
            den=int(den or 4),
        )
    if not entries:
        raise Failure("no library entries parsed from %s" % LIBRARY_SRC)
    return src, entries


def parse_catalog_refs():
    src = read(CATALOG_SRC)
    refs = []
    for match in REF_RE.finditer(src):
        index, genre, name, fill, hash_text, num, den = match.groups()
        refs.append(dict(
            index=int(index),
            genre=genre,
            name=name,
            fill=(fill == "true"),
            spec_hash=int(hash_text, 0) & 0xFFFFFFFF,
            num=int(num),
            den=int(den),
        ))
    if not refs:
        raise Failure("no catalog references parsed from %s" % CATALOG_SRC)
    return src, refs


def check_catalog(library, refs):
    """Validate every catalog ref against the actual library. Returns (ok, problems)."""
    problems = []
    for ref in refs:
        entry = library.get(ref["index"])
        label = "#%d" % ref["index"]
        if entry is None:
            problems.append("%s: index not present in library" % label)
            continue
        if entry["fill"] and not ref["fill"]:
            problems.append("%s: catalog says groove, library says fill" % label)
        if not entry["fill"] and ref["fill"]:
            problems.append("%s: catalog says fill, library says groove" % label)
        if entry["genre"] != ref["genre"]:
            problems.append("%s: genre %r != library %r" % (label, ref["genre"], entry["genre"]))
        if entry["name"] != ref["name"]:
            problems.append("%s: name %r != library %r" % (label, ref["name"], entry["name"]))
        if entry["num"] != ref["num"] or entry["den"] != ref["den"]:
            problems.append("%s: meter %d/%d != library %d/%d"
                            % (label, ref["num"], ref["den"], entry["num"], entry["den"]))
        actual_hash = fnv1a32(entry["spec"])
        if actual_hash != ref["spec_hash"]:
            problems.append("%s: spec hash 0x%08X != library 0x%08X"
                            % (label, ref["spec_hash"], actual_hash))
    return (len(problems) == 0), problems


def declared_fingerprint():
    for path in (CATALOG_HDR, CATALOG_SRC):
        match = FINGERPRINT_RE.search(read(path))
        if match:
            return match.group(1)
    return None


def build_manifest(library_src, library, refs):
    sha = hashlib.sha256(library_src.encode("utf-8")).hexdigest()
    referenced = sorted({ref["index"] for ref in refs})
    return {
        "schemaVersion": 1,
        "source": "src/DrumLibrary.cpp",
        "sourceSha256": sha,
        "libraryEntryCount": len(library),
        "referencedIndexCount": len(referenced),
        "references": [
            {
                "index": ref["index"],
                "genre": ref["genre"],
                "name": ref["name"],
                "fill": ref["fill"],
                "specHash": "0x%08X" % ref["spec_hash"],
                "meter": "%d/%d" % (ref["num"], ref["den"]),
            }
            for ref in refs
        ],
        "entries": [
            {
                "index": library[index]["index"],
                "genre": library[index]["genre"],
                "name": library[index]["name"],
                "fill": library[index]["fill"],
                "spec": library[index]["spec"],
                "specHash": "0x%08X" % fnv1a32(library[index]["spec"]),
                "meter": "%d/%d" % (library[index]["num"], library[index]["den"]),
            }
            for index in referenced
        ],
    }


def cxx_escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def render_fixture(manifest):
    lines = [
        "// GENERATED by tools/style-catalog/style_catalog_provenance.py --emit.",
        "// Snapshot of the ACTUAL compiled drum library for the indices referenced by",
        "// src/jam/StyleCatalog.cpp. DO NOT EDIT by hand; re-run --emit after a",
        "// catalog change, then commit the regenerated file.",
        "#pragma once",
        "",
        '#include "jam/StyleCatalog.h"',
        "",
        "namespace jamtest_style_fixture",
        "{",
        'inline constexpr const char* kLibrarySourceSha256 = "%s";' % manifest["sourceSha256"],
        "inline constexpr int kLibraryEntryCount = %d;" % manifest["libraryEntryCount"],
        "inline constexpr int kReferencedIndexCount = %d;" % manifest["referencedIndexCount"],
        "",
        "struct ActualEntry",
        "{",
        "    int index;",
        "    const char* genre;",
        "    const char* name;",
        "    bool fill;",
        "    const char* spec;",
        "    int num;",
        "    int den;",
        "};",
        "",
        "inline const ActualEntry kActualEntries[] = {",
    ]
    for entry in manifest["entries"]:
        lines.append('    { %d, "%s", "%s", %s, "%s", %d, %d },'
                     % (entry["index"], cxx_escape(entry["genre"]), cxx_escape(entry["name"]),
                        "true" if entry["fill"] else "false", cxx_escape(entry["spec"]),
                        int(entry["meter"].split("/")[0]), int(entry["meter"].split("/")[1])))
    lines += [
        "};",
        "",
        "inline constexpr int kActualEntryCount =",
        "    (int) (sizeof (kActualEntries) / sizeof (kActualEntries[0]));",
        "",
        "} // namespace jamtest_style_fixture",
        "",
    ]
    return "\n".join(lines)


def run(emit):
    library_src, library = parse_library()
    _, refs = parse_catalog_refs()
    sha = hashlib.sha256(library_src.encode("utf-8")).hexdigest()

    problems = []
    declared = declared_fingerprint()
    if declared != sha:
        problems.append("StyleCatalog fingerprint %s != actual sha256 %s" % (declared, sha))

    ref_ok, ref_problems = check_catalog(library, refs)
    problems += ref_problems

    manifest = build_manifest(library_src, library, refs)

    if emit:
        os.makedirs(GENERATED_DIR, exist_ok=True)
        with open(FIXTURE_HDR, "w", encoding="utf-8") as handle:
            handle.write(render_fixture(manifest))
        with open(MANIFEST, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle, indent=2, sort_keys=True)
            handle.write("\n")
    else:
        if not os.path.exists(MANIFEST):
            problems.append("missing %s; run --emit" % MANIFEST)
        else:
            committed = json.loads(read(MANIFEST))
            if committed.get("sourceSha256") != sha:
                problems.append("committed manifest sha256 is stale; run --emit")
            committed_refs = committed.get("references", [])
            current = [dict(r) for r in manifest["references"]]
            if committed_refs != current:
                problems.append("committed manifest references differ; run --emit")
        if not os.path.exists(FIXTURE_HDR):
            problems.append("missing %s; run --emit" % FIXTURE_HDR)
        elif render_fixture(manifest) != read(FIXTURE_HDR):
            problems.append("generated fixture is stale; run --emit")

    print("style-catalog: %d refs checked, %d distinct indices, library sha256 %s"
          % (len(refs), manifest["referencedIndexCount"], sha))
    if problems:
        print("style-catalog: FAILED")
        for problem in problems:
            print("  - " + problem)
        return 1
    print("style-catalog: OK")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="verify without writing")
    group.add_argument("--emit", action="store_true", help="regenerate generated/ then verify")
    args = parser.parse_args(argv)
    try:
        return run(emit=args.emit)
    except Failure as failure:
        print("style-catalog: FAILED: %s" % failure, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
