#!/usr/bin/env python3
"""EVAL-LIVE-001 replay harness build (corrected, fail-closed).

Always builds and runs the instrumentation and support self-tests and
smoke-compiles the harness/facade tests against the frozen headers. Links and
runs the full actual-processor harness only when the product passes the
live-readiness preflight (exact backend macro, facade symbols, source/product
identity, immutable frozen headers, optional preregistered source-pin
overrides). Reuses the product's real link closure; no full rebuild.
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import time

import replay_lib as rl

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def sh(cmd, cwd=None, timeout=1200, env=None):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=timeout, env=env)
    return p.returncode, p.stdout


def immutable_pins_from_predeclared(path):
    try:
        data = json.load(open(path, encoding="utf-8"))
    except Exception:
        return {}
    pins = {}
    for entry in (data.get("source_pins") or {}).values():
        if isinstance(entry, dict) and entry.get("path") in rl.IMMUTABLE_HEADERS:
            pins[entry["path"]] = entry.get("sha256")
    return {k: v for k, v in pins.items() if v}


def load_overrides(path):
    if not path:
        return None
    data = json.load(open(path, encoding="utf-8"))
    if data.get("schema") != "live-jam-replay/source-pin-overrides/1.0":
        raise SystemExit(f"error: {path} is not a source-pin-overrides/1.0 file")
    return {e["path"]: e["sha256"] for e in data.get("overrides", [])}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True)
    ap.add_argument("--product-build", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--predeclared", default=os.path.join(HERE, "predeclared.json"))
    ap.add_argument("--source-pin-overrides")
    ap.add_argument("--sysroot-lib",
                    default="/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu")
    ap.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args(argv)

    source = os.path.abspath(args.source)
    product = os.path.abspath(args.product_build)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    objdir = os.path.join(out, "obj")
    os.makedirs(objdir, exist_ok=True)

    immutable_pins = immutable_pins_from_predeclared(args.predeclared)
    overrides = load_overrides(args.source_pin_overrides)
    pf = rl.preflight(source, product, args.sysroot_lib, immutable_pins, overrides)
    with open(os.path.join(out, "preflight.json"), "w", encoding="utf-8") as f:
        json.dump(pf, f, indent=2)
        f.write("\n")

    flags = pf["flags"]
    defines = shlex.split(flags.get("DEFINES", ""))
    includes = shlex.split(flags.get("INCLUDES", ""))
    cxxflags = shlex.split(flags.get("FLAGS", ""))
    probe_flags = cxxflags + ["-fvisibility=default", "-Wno-frame-address", "-Wno-float-equal"]
    inc_all = [f"-I{source}/src", f"-I{HERE}/src"] + includes
    common = probe_flags + inc_all + defines

    seams = pf["identity"].get("live_seams", {})
    injection_define = []
    if seams.get("setJamTrackerForTesting") and seams.get("tracker_injection_archive_symbol"):
        injection_define = ["-DLIVE_JAM_HAVE_TRACKER_INJECTION"]

    manifest = {
        "task": "EVAL-LIVE-001",
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": source, "product": product, "out": out,
        "preflight_ok": pf["ok"], "preflight_missing": pf["missing"],
        "source_pin_overrides_applied": bool(overrides),
        "live_seams": seams,
        "injection_define": injection_define,
        "commands": [], "outputs": {}, "compiler": args.cxx,
        "flags": {"FLAGS": flags.get("FLAGS", ""), "DEFINES": flags.get("DEFINES", "")},
        "link_archives": pf["identity"]["product"].get("link_archives", []),
        "link_closure_source": pf["identity"]["product"].get("link_closure_source"),
    }

    def compile_one(name, src, extra=None):
        obj = os.path.join(objdir, name + ".o")
        cmd = [args.cxx] + common + (extra or []) + ["-c", src, "-o", obj]
        manifest["commands"].append(cmd)
        rc, log = sh(cmd)
        if rc != 0:
            print(f"FAILED compile {name}:\n{log}", file=sys.stderr)
            return None, log
        manifest["outputs"][name] = rl.sha256_file(obj)
        return obj, log

    def link(name, objs, extra):
        binp = os.path.join(out, name)
        cmd = [args.cxx] + objs + extra + ["-o", binp]
        manifest["commands"].append(cmd)
        rc, log = sh(cmd)
        if rc != 0:
            print(f"FAILED link {name}:\n{log}", file=sys.stderr)
            return None
        manifest["outputs"][name] = rl.sha256_file(binp)
        return binp

    wraps = ["-Wl,--wrap=malloc", "-Wl,--wrap=calloc", "-Wl,--wrap=realloc",
             "-Wl,--wrap=free", "-Wl,--wrap=pthread_mutex_lock",
             "-Wl,--wrap=pthread_mutex_trylock", "-Wl,--wrap=pthread_mutex_unlock",
             "-Wl,--wrap=pthread_cond_clockwait"]

    obj_instr, _ = compile_one("RtProbeInstrumentation", os.path.join(HERE, "src", "RtProbeInstrumentation.cpp"))
    obj_self, _ = compile_one("InstrumentSelfCheck", os.path.join(HERE, "src", "InstrumentSelfCheck.cpp"))
    self_check_ok = False
    if obj_instr and obj_self:
        binp = link("instrument_selfcheck", [obj_instr, obj_self], wraps + ["-lrt", "-ldl", "-lpthread"])
        if binp:
            rc, log = sh([binp])
            manifest["instrument_selfcheck"] = {"exit": rc, "log": log}
            self_check_ok = (rc == 0)
            print(log, end="")

    obj_support, _ = compile_one("SupportSelfTest", os.path.join(HERE, "src", "SupportSelfTest.cpp"))
    support_ok = False
    if obj_support:
        binp = link("replay_support_selftest", [obj_support], ["-lpthread"])
        if binp:
            rc, log = sh([binp])
            manifest["support_selftest"] = {"exit": rc, "log": log}
            support_ok = (rc == 0)
            print(log, end="")

    obj_harness, _ = compile_one("LiveJamReplay", os.path.join(HERE, "src", "LiveJamReplay.cpp"),
                                 extra=injection_define)
    manifest["harness_smoke_compile_ok"] = obj_harness is not None
    manifest["support_selftest_ok"] = support_ok

    facade_src = os.path.join(REPO_ROOT, "tests", "LiveJamProcessorTests.cpp")
    jamcore = pf["paths"].get("jam_archive")
    have_jamcore = bool(jamcore) and os.path.isfile(jamcore)
    obj_facade = None
    facade_run = None
    if os.path.isfile(facade_src):
        extra = (["-DLIVE_JAM_HAVE_JAM_CORE", "-Wno-float-equal"] if have_jamcore else [])
        obj_facade, _ = compile_one("LiveJamProcessorTests", facade_src, extra=extra)
    if obj_facade and have_jamcore:
        binp = link("live_jam_facade_tests", [obj_facade, jamcore], ["-lpthread"])
        if binp:
            rc, log = sh([binp])
            facade_run = {"exit": rc, "log": log}
            print(log, end="")
    manifest["facade_tests_run"] = facade_run

    harness_bin = None
    if pf["ok"] and obj_harness and obj_instr:
        closure_inputs = list(pf["paths"]["link_closure"]["inputs"])
        # Wrap the archive/library closure in a group so circular references
        # resolve; the real whole-archive wrapping of libnam_core is preserved.
        grouped = ["-Wl,--start-group"] + closure_inputs + ["-Wl,--end-group"]
        harness_bin = link("live_jam_replay", [obj_instr, obj_harness], wraps + grouped)
    manifest["harness_binary"] = harness_bin
    manifest["instrument_selfcheck_ok"] = self_check_ok

    with open(os.path.join(out, "build-manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"\nbuild manifest: {os.path.join(out, 'build-manifest.json')}")
    if pf["ok"]:
        print("preflight: LIVE-READY")
        if harness_bin:
            print(f"harness binary: {harness_bin}")
            return 0
        print("preflight passed but harness link failed", file=sys.stderr)
        return 5
    print(f"preflight: NOT LIVE-READY missing={pf['missing']}")
    print("status: harness ready, awaiting actual product (exit 3)")
    return 3


if __name__ == "__main__":
    sys.exit(main())
