#!/usr/bin/env python3
"""Offline environment probe for the TRACK-003 BeatNet feasibility review.

Never installs anything and never runs BeatNet. It measures whether the pinned
official BeatNet could *import* in this interpreter and reports the exact
blockers. Exit status: 0 if BeatNet imports (a real benchmark might be
runnable), 2 otherwise.

Usage:
    python3 tools/beatnet-eval/probe.py [--json] [--beatnet-dir DIR] [--timeout 30]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "beatnet_eval"))
import provenance as prov  # noqa: E402  (path set above for script use)

MODULES = ["BeatNet", "madmom", "madmom.features", "madmom.ml.hmm", "torch"]


def _try_import(module: str, timeout: float) -> dict:
    code = (
        "import importlib,sys\n"
        "m=sys.argv[1]\n"
        "try:\n"
        "    importlib.import_module(m)\n"
        "    print('OK')\n"
        "except BaseException as e:\n"
        "    print(type(e).__name__ + ': ' + str(e).splitlines()[0])\n"
        "    sys.exit(1)\n"
    )
    try:
        proc = subprocess.run(
            [sys.executable, "-c", code, module],
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return {"module": module, "importable": False, "detail": f"timeout after {timeout}s"}
    detail = (proc.stdout + proc.stderr).strip().splitlines()
    detail = detail[-1] if detail else ""
    return {"module": module, "importable": proc.returncode == 0, "detail": detail}


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_weights(beatnet_dir: str) -> dict:
    """Hash the three inference weights if a BeatNet source dir was supplied."""
    results = {}
    for name, expected in prov.MODEL_WEIGHT_SHA256.items():
        path = os.path.join(beatnet_dir, "src", "BeatNet", "models", name)
        if not os.path.exists(path):
            path = os.path.join(beatnet_dir, "models", name)
        if not os.path.exists(path):
            results[name] = {"present": False, "sha256": None, "matches_pinned": None}
            continue
        actual = _sha256(path)
        results[name] = {
            "present": True,
            "sha256": actual,
            "matches_pinned": actual == expected,
        }
    return results


def probe(beatnet_dir: str | None, timeout: float) -> dict:
    imports = [_try_import(m, timeout) for m in MODULES]
    importable = all(r["importable"] for r in imports)
    report = {
        "schema": "beatnet-eval/probe/v1",
        "python": {
            "version": platform.python_version(),
            "executable": sys.executable,
            "implementation": platform.python_implementation(),
        },
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "pinned": {
            "spec_cited_repo": prov.SPEC_CITED_REPO,
            "spec_cited_commit": prov.SPEC_CITED_COMMIT,
            "upstream_repo": prov.UPSTREAM_REPO,
            "upstream_commit": prov.UPSTREAM_COMMIT,
            "license_spdx": prov.LICENSE_SPDX,
        },
        "imports": imports,
        "beatnet_importable": importable,
        "weights": check_weights(beatnet_dir) if beatnet_dir else None,
        "blockers": [] if importable else [
            "The pinned causal BeatNet path imports madmom, which does not import on Python "
            "3.13 unmodified (collections.MutableSequence, numpy.float); see provenance.json "
            "dependency_blockers.",
            "Only a supported interpreter (or a patched dependency stack) can run the real "
            "benchmark; this probe does not fabricate observations.",
        ],
        "ran_benchmark": False,
    }
    return report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="TRACK-003 offline BeatNet probe (never installs)")
    ap.add_argument("--json", action="store_true", help="print the report as JSON")
    ap.add_argument("--out", default=None, help="write the JSON report to this path")
    ap.add_argument("--beatnet-dir", default=None,
                    help="path to a BeatNet source checkout to hash pinned weights")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-import timeout seconds")
    args = ap.parse_args(argv)

    report = probe(args.beatnet_dir, args.timeout)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
            fh.write("\n")
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"python            : {report['python']['version']} ({report['python']['executable']})")
        for entry in report["imports"]:
            mark = "OK  " if entry["importable"] else "FAIL"
            print(f"import {entry['module']:<22}: {mark} {entry['detail']}")
        print(f"beatnet_importable: {report['beatnet_importable']}")
        if report["weights"]:
            for name, info in report["weights"].items():
                print(f"weight {name:<22}: present={info['present']} matches_pinned={info['matches_pinned']}")
        for b in report["blockers"]:
            print(f"BLOCKER: {b}")
    return 0 if report["beatnet_importable"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
