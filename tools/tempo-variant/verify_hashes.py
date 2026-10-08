#!/usr/bin/env python3
"""Verify the corpus hashes used by the TRACK-005 tempo-variant diagnostic.

Checks that:
  * every one of the 19 original `testdata/rhythm` WAVs matches its manifest
    sha256 and byte size (the untouched EVAL-001 corpus);
  * the `repaired-sustain` manifest references the 18 original WAVs with
    identical sha256 and carries the one repaired sustained_chords WAV whose
    own sha256/size match its manifest entry.

Writes a short OK/BAD report. Exit 1 on any mismatch.

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
    by_name = {}
    for fx in manifest["fixtures"]:
        path = os.path.join(corpus, fx["file"])
        if not os.path.exists(path):
            path = os.path.join(ROOT, fx["file"])
        digest = sha256(path)
        size = os.path.getsize(path)
        ok = digest == fx["sha256"] and size == fx["bytes"]
        if not ok:
            bad.append(f"{corpus}:{fx['name']}")
        by_name[fx["name"]] = (digest, size)
        report.append(
            f"{'OK ' if ok else 'BAD'} {manifest['corpus']['id']}/{fx['name']} "
            f"{digest[:16]} bytes={size} expected={fx['bytes']}"
        )
    return by_name


def main() -> int:
    report: list = []
    bad: list = []

    original = check_manifest(ROOT, report, bad)
    repaired = check_manifest(os.path.join(ROOT, "repaired-sustain"), report, bad)

    # The repaired manifest must inherit the 18 original hashes exactly.
    refs_ok = 0
    for name, (digest, size) in repaired.items():
        if name == "sustained_chords":
            continue
        if original.get(name) != (digest, size):
            bad.append(f"repaired-ref-mismatch:{name}")
        else:
            refs_ok += 1

    header = (
        f"# original fixtures={len(original)} repaired fixtures={len(repaired)} "
        f"repaired original refs ok={refs_ok}/18 bad={len(bad)}\n"
    )
    text = header + "\n".join(report) + "\n"
    sys.stdout.write(text)
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as fh:
            fh.write(text)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
