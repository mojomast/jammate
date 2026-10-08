#!/usr/bin/env python3
"""Verify the 19 original corpus WAVs against the manifest sha256 + byte size.

TRACK-004 operates over the untouched original corpus. This script is the
precondition check: it fails (exit 1) on any hash or size mismatch, and writes a
short OK/BAD report so the run log is auditable.

    tools/tracker-diagnostics/verify_corpus_hashes.py [corpus-dir] [report-file]
"""
import hashlib
import json
import os
import sys


def main() -> int:
    corpus = sys.argv[1] if len(sys.argv) > 1 else "testdata/rhythm"
    report = sys.argv[2] if len(sys.argv) > 2 else ""

    with open(os.path.join(corpus, "manifest.json"), "rb") as fh:
        manifest = json.load(fh)

    lines = []
    bad = 0
    for fixture in manifest["fixtures"]:
        path = os.path.join(corpus, fixture["file"])
        with open(path, "rb") as fh:
            digest = hashlib.sha256(fh.read()).hexdigest()
        size = os.path.getsize(path)
        ok = digest == fixture["sha256"] and size == fixture["bytes"]
        if not ok:
            bad += 1
        lines.append(
            f"{'OK ' if ok else 'BAD'} {fixture['name']} "
            f"{digest[:16]} bytes={size} expected={fixture['bytes']}"
        )

    header = (
        f"# corpus {manifest['corpus']['id']} "
        f"fixtures={len(manifest['fixtures'])} bad={bad}\n"
    )
    text = header + "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if report:
        with open(report, "w", encoding="utf-8") as fh:
            fh.write(text)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
