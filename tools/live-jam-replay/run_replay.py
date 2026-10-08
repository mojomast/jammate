#!/usr/bin/env python3
"""EVAL-LIVE-001 replay runner (corrected, fail-closed).

  * refuses to reuse a non-empty --out;
  * builds/preflights; a non-live product yields an awaiting-product receipt
    (exit 3) without invoking any binary;
  * invokes the freshly built harness under a preregistered bounded timeout; a
    timeout yields status=timed-out with invoked_binary=true and partial
    evidence preserved (never an awaiting receipt);
  * a facade-bearing product whose default backend is not experimentalBTrack
    yields awaiting-backend;
  * merges the measured cells with identity/protocol/scope/allocation scope and
    runs the validator (which rejects synthetic evidence by default).
"""
import argparse
import json
import math
import os
import subprocess
import sys
import time

import replay_lib as rl
import build_replay

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_TIMEOUT_S = 300.0
MAX_TIMEOUT_S = 300.0


def valid_timeout(value):
    """Preregistered bound: finite and in (0, 300]. A user override can never
    exceed the frozen cap."""
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and 0 < value <= MAX_TIMEOUT_S)


def report_fresh(out):
    return not (os.path.exists(out) and os.listdir(out))


def load_fixtures(fixtures_dir):
    if not fixtures_dir:
        return {"unmeasured": True, "reason": "no --fixtures-dir supplied to the runner"}, {}
    manifest_path = os.path.join(fixtures_dir, "manifest.json")
    if not os.path.isfile(manifest_path):
        return {"unmeasured": True, "reason": f"no fixture manifest at {manifest_path}"}, {}
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    entries = manifest["entries"]
    block = {"manifest_sha256": rl.sha256_file(manifest_path), "entries": entries}
    by_id = {e["id"]: os.path.join(fixtures_dir, e["path"]) for e in entries}
    return block, by_id


def merge_evidence(predeclared_path, pf, build_manifest, cells, fixtures, scope, scope_reason):
    def sha(p):
        return rl.sha256_file(p) if p and os.path.isfile(p) else None

    findings = cells.get("findings", [])
    status = "measured-findings" if findings else "measured"
    backend_kind = cells.get("backend_kind", "unknown")
    backend_usable = bool(cells.get("backend_usable_at_start")) and backend_kind == "experimentalBTrack"
    identity = {
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "runner_git_head": pf["identity"]["source"].get("head"),
        "source": pf["identity"]["source"],
        "product": pf["identity"]["product"],
        "facade_symbols": pf["identity"]["facade_symbols"],
        "backend": {
            "required": "experimentalBTrack",
            "kind": backend_kind,
            "macro_defined": pf["identity"]["backend"].get("macro_defined", False),
            "signals": pf["identity"]["backend"].get("signals", []),
            "usable": backend_usable,
        },
        "live_seams": pf["identity"].get("live_seams", {}),
        "instrument": {
            "selfcheck_pass": bool(build_manifest.get("instrument_selfcheck_ok")),
            "selfcheck_binary_sha256": build_manifest.get("outputs", {}).get("instrument_selfcheck"),
            "instrumentation_source_sha256": sha(os.path.join(HERE, "src", "RtProbeInstrumentation.cpp")),
        },
    }
    protocol = {
        "predeclared_sha256": sha(predeclared_path),
        "validator_sha256": sha(os.path.join(HERE, "validate_evidence.py")),
        "harness_source_sha256": sha(os.path.join(HERE, "src", "LiveJamReplay.cpp")),
        "predeclared_freeze_commit": pf["identity"]["source"].get("head"),
    }
    with open(predeclared_path, encoding="utf-8") as f:
        predeclared = json.load(f)
    return {
        "schema": "live-jam-replay/evidence/1.1",
        "task": "EVAL-LIVE-001",
        "status": status,
        "synthetic": False,
        "scope": scope,
        "scope_reason": scope_reason or "",
        "allocation_scope": "callback-thread-path-only",
        "worker_allocations": cells.get("worker_allocations",
                                        {"measured": False,
                                         "reason": "thread-local arming counts only the callback thread"}),
        "bootstrap": cells.get("bootstrap", {}),
        "identity": identity,
        "protocol": protocol,
        "matrix": predeclared["matrix"],
        "fixtures": fixtures,
        "cells": cells["cells"],
        "expected_cell_ids": cells.get("expected_cell_ids", [c["id"] for c in cells["cells"]]),
        "findings": findings,
        "scenarios": cells.get("scenarios", []),
        "counts": cells.get("counts", {}),
        "notes": [
            "Non-device replay: not latency/dropout/device or Windows/ASIO evidence.",
            "Allocation counters are the callback-thread path only; worker allocations are unmeasured.",
            "The 54-cell matrix proves callback coverage; join/stop end-to-end is proven by the supplemental scenarios only.",
        ],
    }


def write_awaiting(out, status, pf, extra_missing=None):
    missing = list(pf["missing"]) + list(extra_missing or [])
    receipt = {
        "schema": "live-jam-replay/awaiting-product/1.0",
        "task": "EVAL-LIVE-001",
        "status": status,
        "clean": False,
        "invoked_binary": False,
        "missing": missing,
        "identity": pf.get("identity", {}),
        "notes": ["No binary was invoked and no measurement was claimed."],
    }
    path = os.path.join(out, "evidence.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(receipt, f, indent=2)
        f.write("\n")
    print(f"awaiting receipt: {path}")
    print(f"missing: {missing}")
    return path


def write_timed_out(out, timeout_s, partial_path, log_path):
    partial = False
    partial_sha = None
    if os.path.isfile(partial_path):
        try:
            with open(partial_path, encoding="utf-8") as f:
                parsed = json.load(f)
            if isinstance(parsed.get("cells"), list) and parsed["cells"]:
                partial = True
                partial_sha = rl.sha256_file(partial_path)
        except Exception:
            partial = False
    ev = {
        "schema": "live-jam-replay/evidence/1.1",
        "task": "EVAL-LIVE-001",
        "status": "timed-out",
        "invoked_binary": True,
        "clean": False,
        "measured_partial": partial,
        "partial_cells_present": partial,
        "partial_cells_sha256": partial_sha,
        "counters_measured": partial,
        "log_sha256": rl.sha256_file(log_path) if os.path.isfile(log_path) else None,
        "timeout_s": timeout_s,
        "notes": ["The replay exceeded the preregistered bounded timeout; the child was terminated."],
    }
    path = os.path.join(out, "evidence.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(ev, f, indent=2)
        f.write("\n")
    print(f"timed-out receipt: {path} partial={partial}")
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True)
    ap.add_argument("--product-build", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--predeclared", default=os.path.join(HERE, "predeclared.json"))
    ap.add_argument("--source-pin-overrides")
    ap.add_argument("--fixtures-dir")
    ap.add_argument("--sysroot-lib",
                    default="/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu")
    ap.add_argument("--scope", default="full", choices=["smoke", "full", "diagnostic"])
    ap.add_argument("--scope-reason")
    ap.add_argument("--timeout-s", type=float, default=DEFAULT_TIMEOUT_S)
    ap.add_argument("--fast", action="store_true")
    ap.add_argument("--warm-blocks", type=int, default=0)
    ap.add_argument("--target-seconds", type=float, default=4.0)
    ap.add_argument("--supplemental-seconds", type=float, default=16.0)
    ap.add_argument("--seed", type=int, default=20261008)
    ap.add_argument("--pipeline", default="all")
    ap.add_argument("--no-supplemental", action="store_true")
    ap.add_argument("--allow-unavailable-backend", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args(argv)

    if not valid_timeout(args.timeout_s):
        print("error: --timeout-s must be finite and in (0, 300]", file=sys.stderr)
        return 64

    if args.scope == "diagnostic" and not args.scope_reason:
        print("error: --scope diagnostic requires --scope-reason", file=sys.stderr)
        return 64

    out = os.path.abspath(args.out)
    if not report_fresh(out):
        print(f"error: --out {out} is not empty; refusing to overwrite old results", file=sys.stderr)
        return 4
    os.makedirs(out, exist_ok=True)

    build_dir = os.path.join(out, "build")
    immutable_pins = build_replay.immutable_pins_from_predeclared(args.predeclared)
    overrides = build_replay.load_overrides(args.source_pin_overrides)
    pf = rl.preflight(os.path.abspath(args.source), os.path.abspath(args.product_build),
                      args.sysroot_lib, immutable_pins, overrides)
    with open(os.path.join(out, "preflight.json"), "w", encoding="utf-8") as f:
        json.dump(pf, f, indent=2)
        f.write("\n")

    if not pf["ok"]:
        write_awaiting(out, "awaiting-product", pf)
        return 3

    if not args.skip_build:
        rc = build_replay.main([
            "--source", os.path.abspath(args.source),
            "--product-build", os.path.abspath(args.product_build),
            "--out", build_dir, "--predeclared", args.predeclared,
            "--sysroot-lib", args.sysroot_lib,
        ] + (["--source-pin-overrides", args.source_pin_overrides] if args.source_pin_overrides else []))
        if rc != 0:
            print("error: harness build failed", file=sys.stderr)
            return 5
    harness = os.path.join(build_dir, "live_jam_replay")
    if not os.path.isfile(harness):
        write_awaiting(out, "awaiting-product", pf, ["harness_binary"])
        return 3

    with open(os.path.join(build_dir, "build-manifest.json"), encoding="utf-8") as f:
        build_manifest = json.load(f)
    facade_ok = (isinstance(build_manifest.get("facade_tests_run"), dict)
                 and build_manifest["facade_tests_run"].get("exit") == 0)
    if not (build_manifest.get("instrument_selfcheck_ok")
            and build_manifest.get("support_selftest_ok") and facade_ok):
        print("error: self-check failure; refusing to invoke any measurement "
              f"(instrument={build_manifest.get('instrument_selfcheck_ok')} "
              f"support={build_manifest.get('support_selftest_ok')} facade={facade_ok})",
              file=sys.stderr)
        return 2
    fixtures, fixture_paths = load_fixtures(args.fixtures_dir)

    harness_args = [harness, "--source", os.path.abspath(args.source),
                    "--product-build", os.path.abspath(args.product_build),
                    "--out", out, "--predeclared", args.predeclared,
                    "--scope", args.scope, "--seed", str(args.seed),
                    "--target-seconds", str(args.target_seconds),
                    "--supplemental-seconds", str(args.supplemental_seconds),
                    "--pipeline", args.pipeline]
    if args.scope_reason:
        harness_args += ["--scope-reason", args.scope_reason]
    if args.fast:
        harness_args.append("--fast")
    if args.warm_blocks > 0:
        harness_args += ["--warm-blocks", str(args.warm_blocks)]
    if args.no_supplemental:
        harness_args.append("--no-supplemental")
    if args.allow_unavailable_backend:
        harness_args.append("--allow-unavailable-backend")
    if fixture_paths.get("strum_120"):
        harness_args += ["--fixture-clean", fixture_paths["strum_120"]]
    if fixture_paths.get("noise"):
        harness_args += ["--fixture-noise", fixture_paths["noise"]]
    if fixture_paths.get("silence"):
        harness_args += ["--fixture-silence", fixture_paths["silence"]]

    log_path = os.path.join(out, "live-jam-replay.log")
    cells_path = os.path.join(out, "cells.json")
    t0 = time.time()
    timed_out = False
    try:
        with open(log_path, "w", encoding="utf-8") as logf:
            p = subprocess.run(harness_args, stdout=logf, stderr=subprocess.STDOUT,
                               text=True, timeout=args.timeout_s)
        harness_rc = p.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        harness_rc = -1
    run_seconds = time.time() - t0
    print(f"harness exit={harness_rc} elapsed={run_seconds:.1f}s timeout={timed_out} log={log_path}")

    if timed_out:
        write_timed_out(out, args.timeout_s, cells_path, log_path)
        vr = subprocess.run([sys.executable, os.path.join(HERE, "validate_evidence.py"),
                             "--evidence", os.path.join(out, "evidence.json")],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(vr.stdout, end="")
        return 1

    if not os.path.isfile(cells_path):
        print("error: harness produced no cells.json", file=sys.stderr)
        return 5
    with open(cells_path, encoding="utf-8") as f:
        cells = json.load(f)

    if harness_rc == 3 or cells.get("backend_kind") != "experimentalBTrack":
        write_awaiting(out, "awaiting-backend", pf, ["backend_usable", "backend_exact"])
        return 3

    evidence = merge_evidence(args.predeclared, pf, build_manifest, cells, fixtures,
                              args.scope, args.scope_reason)
    ev_path = os.path.join(out, "evidence.json")
    with open(ev_path, "w", encoding="utf-8") as f:
        json.dump(evidence, f, indent=2)
        f.write("\n")

    vcmd = [sys.executable, os.path.join(HERE, "validate_evidence.py"),
            "--evidence", ev_path, "--predeclared", args.predeclared,
            "--source", os.path.abspath(args.source),
            "--json", os.path.join(out, "validation-report.json"),
            "--summary-md", os.path.join(out, "summary.md")]
    if args.fixtures_dir:
        vcmd += ["--fixtures-dir", args.fixtures_dir]
    vr = subprocess.run(vcmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(vr.stdout, end="")

    manifest = {
        "task": "EVAL-LIVE-001", "out": out, "harness_exit": harness_rc,
        "run_seconds": run_seconds, "timeout_s": args.timeout_s, "scope": args.scope,
        "evidence": rl.sha256_file(ev_path),
        "harness_binary": rl.sha256_file(harness),
        "build_manifest": rl.sha256_file(os.path.join(build_dir, "build-manifest.json")),
    }
    with open(os.path.join(out, "run-manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    return 0 if vr.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
