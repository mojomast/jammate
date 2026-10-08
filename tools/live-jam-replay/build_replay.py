#!/usr/bin/env python3
"""EVAL-LIVE-001 replay harness build (fail-closed, read-only w.r.t. the product).

Always builds and runs the instrumentation self-check and smoke-compiles the
harness and facade tests against the frozen headers. Links and runs the full
actual-processor replay binary ONLY when the product passes the live-readiness
preflight; otherwise it writes a preflight receipt and exits 3
("harness ready, awaiting actual product") without invoking any stale binary.

It reuses the product's shared-code and NAM/jam archives plus the existing JUCE
objects and system libraries: no full rebuild of shared dependencies.
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True)
    ap.add_argument("--product-build", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--predeclared", default=os.path.join(HERE, "predeclared.json"))
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

    pf = rl.preflight(source, product, args.sysroot_lib)
    with open(os.path.join(out, "preflight.json"), "w", encoding="utf-8") as f:
        json.dump(pf, f, indent=2)
        f.write("\n")

    flags = pf["flags"]
    defines = shlex.split(flags.get("DEFINES", ""))
    includes = shlex.split(flags.get("INCLUDES", ""))
    cxxflags = shlex.split(flags.get("FLAGS", ""))
    # Keep default visibility so the replaced global new/delete win; -fvisibility
    # is last so it overrides the product's -fvisibility=hidden.
    probe_flags = cxxflags + ["-fvisibility=default", "-Wno-frame-address", "-Wno-float-equal"]
    inc_all = [f"-I{source}/src", f"-I{HERE}/src"] + includes
    common = probe_flags + inc_all + defines

    manifest = {
        "task": "EVAL-LIVE-001",
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": source, "product": product, "out": out,
        "preflight_ok": pf["ok"], "preflight_missing": pf["missing"],
        "commands": [], "outputs": {}, "compiler": args.cxx,
        "flags": {"FLAGS": flags.get("FLAGS", ""), "DEFINES": flags.get("DEFINES", "")},
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

    # 1. Instrumentation self-check (always buildable; no processor needed).
    obj_instr, _ = compile_one("RtProbeInstrumentation", os.path.join(HERE, "src", "RtProbeInstrumentation.cpp"))
    obj_self, _ = compile_one("InstrumentSelfCheck", os.path.join(HERE, "src", "InstrumentSelfCheck.cpp"))
    self_check_ok = False
    if obj_instr and obj_self:
        binp = link("instrument_selfcheck", [obj_instr, obj_self],
                    wraps + ["-lrt", "-ldl", "-lpthread"])
        if binp:
            rc, log = sh([binp])
            manifest["instrument_selfcheck"] = {"exit": rc, "log": log}
            self_check_ok = (rc == 0)
            print(log, end="")

    # 2. Replay support logic self-test (JSON serializer + synthetic WAV reader).
    obj_support, _ = compile_one("SupportSelfTest", os.path.join(HERE, "src", "SupportSelfTest.cpp"))
    support_ok = False
    if obj_support:
        binp = link("replay_support_selftest", [obj_support], ["-lpthread"])
        if binp:
            rc, log = sh([binp])
            manifest["support_selftest"] = {"exit": rc, "log": log}
            support_ok = (rc == 0)
            print(log, end="")

    # 3. Smoke compile the harness against the frozen headers (always; no link).
    obj_harness, smoke_log = compile_one("LiveJamReplay", os.path.join(HERE, "src", "LiveJamReplay.cpp"))
    manifest["harness_smoke_compile_ok"] = obj_harness is not None
    manifest["support_selftest_ok"] = support_ok

    # 4. Facade contract tests: compile, and link+run against the product
    #    jam-core when available (the runtime engine checks). This needs only the
    #    frozen headers and jam-core, not the live processor archive.
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

    # 5. Full actual-processor harness: only when live-ready.
    harness_bin = None
    if pf["ok"] and obj_harness and obj_instr:
        extra = (["-Wl,--push-state,--whole-archive", pf["paths"]["nam_archive"],
                  "-Wl,--pop-state", pf["paths"]["assets_archive"]]
                 + ([jamcore] if jamcore and os.path.isfile(jamcore) else []))
        sysroot = args.sysroot_lib
        sys_libs = [os.path.join(sysroot, "libasound.so"),
                    os.path.join(sysroot, "libfontconfig.so"),
                    os.path.join(sysroot, "libfreetype.so")]
        sys_libs = [p for p in sys_libs if os.path.isfile(p)]
        harness_bin = link("live_jam_replay", [obj_instr, obj_harness],
                           wraps + [pf["paths"]["shared_archive"]] + extra + sys_libs
                           + ["-lrt", "-ldl", "-lpthread"])
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
