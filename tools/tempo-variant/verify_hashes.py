#!/usr/bin/env python3
"""Verify the corpus bytes used by the TRACK-005 tempo-variant diagnostic.

Reproduces ALL byte hashes, strictly from the paths the CLI actually reads:

  * all 19 original `testdata/rhythm` WAVs match their manifest sha256 + size;
  * all 19 `repaired-sustain` WAVs match their manifest sha256 + size (18
    original references resolved by relative path plus the one repaired
    sustained_chords WAV);
  * the 18 referenced files are byte-identical between the two manifests.

There is NO fallback to a different directory: if a manifest path does not
resolve, the check fails. Exit 1 on any mismatch, duplicate name, or missing
file. Writes a short OK/BAD report.

    tools/tempo-variant/verify_hashes.py [report-file]
"""
import hashlib
import json
import os
import sys

ROOT = "testdata/rhythm"


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_manifest(corpus: str, report: list, bad: list) -> dict:
    with open(os.path.join(corpus, "manifest.json"), "rb") as fh:
        manifest = json.load(fh)
    names = [fx["name"] for fx in manifest["fixtures"]]
    if len(names) != len(set(names)):
        bad.append(f"{corpus}:duplicate-fixture-names")
    if len(names) != 19:
        bad.append(f"{corpus}:fixture-count-{len(names)}")
    by_name = {}
    for fx in manifest["fixtures"]:
        # Strict: the exact path the CLI composes, no fallback.
        path = os.path.join(corpus, fx["file"])
        try:
            digest = sha256(path)
        except OSError:
            bad.append(f"{corpus}:{fx['name']}:missing:{path}")
            report.append(f"BAD {manifest['corpus']['id']}/{fx['name']} MISSING {path}")
            continue
        size = os.path.getsize(path)
        ok = digest == fx["sha256"] and size == fx["bytes"]
        if not ok:
            bad.append(f"{corpus}:{fx['name']}")
        by_name[fx["name"]] = (digest, size)
        report.append(
            f"{'OK ' if ok else 'BAD'} {manifest['corpus']['id']}/{fx['name']} "
            f"{digest[:16]} bytes={size} expected={fx['bytes']} path={path}"
        )
    return by_name


def main() -> int:
    report: list = []
    bad: list = []

    original = check_manifest(ROOT, report, bad)
    repaired = check_manifest(os.path.join(ROOT, "repaired-sustain"), report, bad)

    # The 18 referenced originals must be byte-identical between the manifests.
    refs_ok = 0
    for name, (digest, size) in repaired.items():
        if name == "sustained_chords":
            continue
        if original.get(name) != (digest, size):
            bad.append(f"repaired-ref-mismatch:{name}")
        else:
            refs_ok += 1

    header = (
        f"# original=19 repaired=19 byte-identical refs ok={refs_ok}/18 "
        f"bad={len(bad)}\n"
    )
    text = header + "\n".join(report) + "\n"
    sys.stdout.write(text)
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as fh:
            fh.write(text)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
