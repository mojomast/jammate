#!/usr/bin/env python3
"""Build and run the INT-DRUM-001 clock-bridge tests without touching the product build.

Fresh-compiles ONLY the sources this task owns or changes:

   tests/TestMain.cpp                  (JUCE test harness)
   tests/DrumClockBridgeTests.cpp      (new: actual DrumEngine integration)
   src/DrumEngine.cpp                  (changed: injected-clock audio owner)
   src/DrumLibrary.cpp                 (unchanged, needed by the engine)
   src/DrumGenerator.cpp               (unchanged, needed by the engine)
   src/jam/DrumClockBridge.cpp         (new: worker-side bridge)

and reuses the read-only JUCE module objects + GuitarCompanionAssets archive
from an existing product build. It then:

   * runs the new actual-engine integration suite;
   * compiles and runs the portable, JUCE-free bridge suite with the jam-core
     test harness and the bare compiler;
   * compiles and runs the EXISTING drum suites (foundation/midi/codec/...) from
     this worktree against the changed engine, as a standalone-behavior
     regression.

The product Ninja build is read for its compile/link flags only; nothing in it is
written. Usage:

    export PATH=/tmp/opencode/venv/bin:$PATH
    export TMPDIR=/home/mojo/projects/build-INT-DRUM-001-worker/tmp
    python3 tools/drum-clock-bridge/run.py
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def extract_recipe(product):
    try:
        commands = subprocess.check_output(
            ["ninja", "-t", "commands", "GuitarCompanionTests"],
            cwd=product, text=True).splitlines()
    except FileNotFoundError:
        sys.exit("ninja not found; export PATH=/tmp/opencode/venv/bin:$PATH first")

    compile_template = next(shlex.split(c)
                            for c in commands
                            if " -c " in c and c.endswith("/tests/TestMain.cpp"))

    flags = []
    i = 1
    while i < len(compile_template):
        flag = compile_template[i]
        if flag in ("-o", "-c", "-MT", "-MF"):
            i += 2
        elif flag == "-MD":
            i += 1
        else:
            flags.append(flag)
            i += 1

    link_line = next(c for c in reversed(commands)
                     if " -o tests/GuitarCompanionTests_artefacts/" in c)
    link = shlex.split(link_line)
    link = link[link.index("/usr/bin/c++"):]
    link = link[:link.index("&&")] if "&&" in link else link

    retained, reused = [], []
    i = 1
    while i < len(link):
        arg = link[i]
        if arg == "-o":
            i += 2
            continue
        if arg.startswith("-Wl,--dependency-file="):
            i += 1
            continue
        if arg.endswith(".o"):
            if "/third_party/JUCE/" not in arg:
                i += 1
                continue
            arg = str(product / arg)
            reused.append(arg)
        elif arg.endswith(".a") and not Path(arg).is_absolute():
            arg = str(product / arg)
            reused.append(arg)
        retained.append(arg)
        i += 1

    for path in reused:
        if not Path(path).is_file():
            raise FileNotFoundError(f"Required prebuilt input missing: {path}")

    return compile_template[0], flags, retained, reused


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--product-build", type=Path,
                        default=Path("/home/mojo/projects/build-RT-003-integration/product"))
    parser.add_argument("--output", type=Path,
                        default=Path("/home/mojo/projects/build-INT-DRUM-001-worker"))
    parser.add_argument("--no-portable", action="store_true")
    parser.add_argument("--no-legacy", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    product = args.product_build.resolve()
    output = args.output.resolve()
    (output / "obj").mkdir(parents=True, exist_ok=True)

    cxx, product_flags, retained, reused = extract_recipe(product)
    # The worktree's own headers must win over the product's source include.
    flags = [f"-I{source / 'src'}"] + product_flags

    fresh = [
        source / "tests" / "TestMain.cpp",
        source / "tests" / "DrumClockBridgeTests.cpp",
        source / "src" / "DrumEngine.cpp",
        source / "src" / "DrumLibrary.cpp",
        source / "src" / "DrumGenerator.cpp",
        source / "src" / "jam" / "DrumClockBridge.cpp",
    ]
    for path in fresh:
        if not path.is_file():
            raise FileNotFoundError(f"Missing fresh source: {path}")

    executed = []
    build_log = (output / "build.log").open("w")

    def compile_one(src, obj):
        cmd = [cxx, *flags, "-c", str(src), "-o", str(obj)]
        executed.append(cmd)
        subprocess.run(cmd, cwd=product, stdout=build_log, stderr=build_log, check=True)

    def link_binary(objs, name):
        binary = output / name
        cmd = [cxx, *objs, *retained, "-o", str(binary)]
        executed.append(cmd)
        subprocess.run(cmd, cwd=product, stdout=build_log, stderr=build_log, check=True)
        return binary

    try:
        fresh_objs = []
        for src in fresh:
            obj = output / "obj" / (src.stem + ".o")
            compile_one(src, obj)
            fresh_objs.append(str(obj))

        intdrum_binary = link_binary(fresh_objs, "DrumClockBridgeTests")

        legacy_test_sources = [source / "tests" / n for n in (
            "DrumSpecTests.cpp", "DrumLibraryTests.cpp", "DrumGeneratorTests.cpp",
            "DrumCodecTests.cpp", "DrumMidiTests.cpp", "DrumFoundationTests.cpp")]
        legacy_objs = []
        for src in legacy_test_sources:
            obj = output / "obj" / ("legacy_" + src.stem + ".o")
            compile_one(src, obj)
            legacy_objs.append(str(obj))

        # Reuse the freshly compiled TestMain + engine objects for the legacy
        # binary (same harness, same changed engine); only the test sources differ.
        shared = [str(output / "obj" / (s.stem + ".o")) for s in fresh
                  if s.stem in ("TestMain", "DrumEngine", "DrumLibrary", "DrumGenerator")]
        legacy_binary = link_binary([*shared, *legacy_objs], "LegacyDrumTests")
    finally:
        build_log.close()

    exit_code = 0

    def run_binary(binary, title, filt=None):
        nonlocal exit_code
        argv = [str(binary)] + ([filt] if filt else [])
        result = subprocess.run(argv, cwd=output, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (binary.name + "-results.log")).write_text(result.stdout)
        print(f"=== {title} ===")
        print(result.stdout, end="")
        exit_code = max(exit_code, result.returncode)

    run_binary(intdrum_binary, "INT-DRUM-001 actual DrumEngine integration")

    portable_binary = None
    if not args.no_portable:
        portable_binary = output / "DrumClockBridgePortableTests"
        portable_cmd = [
            cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic",
            f"-I{source / 'src'}", f"-I{source / 'tests' / 'jam'}",
            f"-I{source / 'tests'}",
            str(source / "tests" / "jam" / "JamTestMain.cpp"),
            str(source / "src" / "jam" / "DrumClockBridge.cpp"),
            str(source / "tests" / "jam" / "DrumClockCommandTests.cpp"),
            "-o", str(portable_binary),
        ]
        executed.append(portable_cmd)
        subprocess.run(portable_cmd, check=True)
        run_binary(portable_binary, "portable JUCE-free DrumClockBridge")

    if not args.no_legacy:
        run_binary(legacy_binary, "existing drum regression suites (changed engine)")

    manifest = {
        "source": str(source),
        "sourceHead": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
        "freshSources": {str(p): digest(p) for p in fresh},
        "reusedInputs": {p: digest(p) for p in reused},
        "commands": executed,
        "binaries": {
            b.name: digest(b) for b in
            [intdrum_binary, legacy_binary] + ([portable_binary] if portable_binary else [])
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"\nManifest and logs: {output}")
    return exit_code


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", "/home/mojo/projects/build-INT-DRUM-001-worker/tmp")
    raise SystemExit(main())
