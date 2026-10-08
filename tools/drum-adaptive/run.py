#!/usr/bin/env python3
"""Build and run the DRUM-ADAPT-002 adaptive-drum tests without touching the product build.

Fresh-compiles the new actual-engine suite, the modified engine, the modified
worker bridge, all existing drum suites and the shared heap probe into ONE
combined JUCE binary, TWICE:

  * with the product's `DRUM_MIDI_HEAP_PROBE` + `-Wl,--wrap` recipe (allocation
    checks active), and
  * WITHOUT the macro and without the wrap flags (allocation checks explicitly
    skipped), proving every test still compiles and runs on a default/Windows
    style configuration.

Sources in each combined binary:

   tests/TestMain.cpp                  (JUCE test harness)
   tests/DrumAdaptiveTests.cpp         (new: adaptive pattern/fill on DrumEngine)
   tests/DrumClockBridgeTests.cpp      (existing: INT-DRUM-001 integration)
   tests/DrumHeapProbe.cpp             (single shared malloc/new probe TU)
   tests/DrumSpecTests.cpp             (existing)
   tests/DrumLibraryTests.cpp          (existing)
   tests/DrumGeneratorTests.cpp        (existing)
   tests/DrumCodecTests.cpp            (existing)
   tests/DrumMidiTests.cpp             (existing)
   tests/DrumFoundationTests.cpp       (existing)
   src/DrumEngine.cpp                  (changed: adaptive bank + BarChange owner)
   src/DrumLibrary.cpp                 (unchanged, needed by the engine)
   src/DrumGenerator.cpp               (unchanged, needed by the engine)
   src/jam/DrumClockBridge.cpp         (changed: requestBarChange / fill staging)

The read-only JUCE module objects + GuitarCompanionAssets archive are reused from
an existing product build; nothing in it is written. The portable, JUCE-free
bridge suite (existing DrumClockCommandTests + new DrumAdaptiveBridgeTests) is
compiled separately with the bare compiler and the jam-core harness. Artifacts are
kept in per-mode subdirectories. Usage:

    export PATH=/tmp/opencode/venv/bin:$PATH
    export TMPDIR=/home/mojo/projects/build-DRUM-ADAPT-002-worker/tmp
    python3 tools/drum-adaptive/run.py
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
                        default=Path("/home/mojo/projects/build-DRUM-ADAPT-002-worker"))
    parser.add_argument("--no-portable", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    product = args.product_build.resolve()
    output = args.output.resolve()

    cxx, product_flags, retained, reused = extract_recipe(product)
    # The worktree's own headers must win over the product's source include.
    base_flags = [f"-I{source / 'src'}"] + product_flags

    all_sources = [
        source / "tests" / "TestMain.cpp",
        source / "tests" / "DrumAdaptiveTests.cpp",
        source / "tests" / "DrumClockBridgeTests.cpp",
        source / "tests" / "DrumHeapProbe.cpp",
        source / "tests" / "DrumSpecTests.cpp",
        source / "tests" / "DrumLibraryTests.cpp",
        source / "tests" / "DrumGeneratorTests.cpp",
        source / "tests" / "DrumCodecTests.cpp",
        source / "tests" / "DrumMidiTests.cpp",
        source / "tests" / "DrumFoundationTests.cpp",
        source / "src" / "DrumEngine.cpp",
        source / "src" / "DrumLibrary.cpp",
        source / "src" / "DrumGenerator.cpp",
        source / "src" / "jam" / "DrumClockBridge.cpp",
    ]
    for path in all_sources:
        if not path.is_file():
            raise FileNotFoundError(f"Missing fresh source: {path}")

    # Sources of the portable, JUCE-free suites (compiled with the bare compiler).
    portable_sources = [
        source / "tests" / "jam" / "JamTestMain.cpp",
        source / "src" / "jam" / "DrumClockBridge.cpp",
        source / "tests" / "jam" / "DrumClockCommandTests.cpp",
        source / "tests" / "jam" / "DrumAdaptiveBridgeTests.cpp",
    ]

    # Provenance is not just the compiled .cpp files: a header change is enough to
    # change the binary, so the receipt hashes EVERY consumed project header and
    # the driver itself as well. Without this a pre-commit run could report the
    # old HEAD next to new .cpp bytes (the integration-review observation).
    project_headers = [
        source / "src" / "DrumEngine.h",
        source / "src" / "DrumMidiCapacity.h",
        source / "src" / "jam" / "DrumClockBridge.h",
        source / "src" / "jam" / "IDrumTransport.h",
        source / "src" / "jam" / "RhythmTypes.h",
        source / "src" / "rt" / "RtSignal.h",
        source / "tests" / "TestHarness.h",
        source / "tests" / "DrumHeapProbe.h",
        source / "tests" / "jam" / "JamTest.h",
        Path(__file__).resolve(),
    ]
    project_inputs = []
    for path in [*all_sources, *portable_sources, *project_headers]:
        if path not in project_inputs:
            project_inputs.append(path)
    for path in project_inputs:
        if not path.is_file():
            raise FileNotFoundError(f"Missing consumed project input: {path}")

    # Self-test provenance: record the exact HEAD and the dirty status. A dirty
    # worktree is reported explicitly (not a hard failure) so an intermediate run
    # still produces a diagnostic receipt, but a final evidence receipt MUST be
    # from a clean commit.
    source_head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    git_status = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=source, text=True)
    git_clean = git_status.strip() == ""
    if not git_clean:
        print("WARNING: worktree is DIRTY; this is NOT a clean-commit receipt:\n"
              + git_status, file=sys.stderr)

    executed = []
    exit_code = 0

    def build_combined(name, flags, retained_flags):
        """Compile + link one combined binary under output/<name>."""
        mode_dir = output / name
        (mode_dir / "obj").mkdir(parents=True, exist_ok=True)
        log = (mode_dir / "build.log").open("w")
        try:
            objects = []
            for src in all_sources:
                obj = mode_dir / "obj" / (src.stem + ".o")
                cmd = [cxx, *flags, "-c", str(src), "-o", str(obj)]
                executed.append(cmd)
                subprocess.run(cmd, cwd=product, stdout=log, stderr=log, check=True)
                objects.append(str(obj))
            binary = mode_dir / "DrumAdaptiveAllTests"
            cmd = [cxx, *objects, *retained_flags, "-o", str(binary)]
            executed.append(cmd)
            subprocess.run(cmd, cwd=product, stdout=log, stderr=log, check=True)
        finally:
            log.close()
        return binary

    def run_binary(binary, title, mode_dir, filt=None):
        nonlocal exit_code
        argv = [str(binary)] + ([filt] if filt else [])
        result = subprocess.run(argv, cwd=mode_dir, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (mode_dir / (binary.name + "-results.log")).write_text(result.stdout)
        print(f"=== {title} ===")
        print(result.stdout, end="")
        exit_code = max(exit_code, result.returncode)

    # --- with the allocation probe (product recipe) -------------------------
    probe_binary = build_combined("probe", base_flags, retained)
    run_binary(probe_binary,
               "combined WITH DRUM_MIDI_HEAP_PROBE + --wrap (all suites)",
               output / "probe")

    # --- WITHOUT the macro / wrap (default and Windows-style config) --------
    nomacro_flags = [f for f in base_flags if not f.startswith("-DDRUM_MIDI_HEAP_PROBE")]
    nomacro_retained = [a for a in retained if not a.startswith("-Wl,--wrap=")]
    nomacro_binary = build_combined("no-probe", nomacro_flags, nomacro_retained)
    run_binary(nomacro_binary,
               "combined WITHOUT DRUM_MIDI_HEAP_PROBE (allocation checks skipped)",
               output / "no-probe")

    # --- portable JUCE-free suites ------------------------------------------
    portable_binary = None
    if not args.no_portable:
        mode_dir = output / "portable"
        mode_dir.mkdir(parents=True, exist_ok=True)
        portable_binary = mode_dir / "DrumAdaptivePortableTests"
        portable_cmd = [
            cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic",
            f"-I{source / 'src'}", f"-I{source / 'tests' / 'jam'}",
            f"-I{source / 'tests'}",
            *[str(p) for p in portable_sources],
            "-o", str(portable_binary),
        ]
        executed.append(portable_cmd)
        subprocess.run(portable_cmd, check=True)
        run_binary(portable_binary, "portable JUCE-free bridge suites", mode_dir)

    binaries = [probe_binary, nomacro_binary] + ([portable_binary] if portable_binary else [])
    manifest = {
        "source": str(source),
        "sourceHead": source_head,
        "gitClean": git_clean,
        "gitStatus": git_status,
        "freshSources": {str(p): digest(p) for p in all_sources},
        "projectInputs": {str(p): digest(p) for p in project_inputs},
        "reusedInputs": {p: digest(p) for p in reused},
        "commands": executed,
        "binaries": {str(b.relative_to(output)): digest(b) for b in binaries},
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"\nManifest and logs: {output}")
    print(f"sourceHead: {source_head}  gitClean: {git_clean}")
    return exit_code


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", "/home/mojo/projects/build-DRUM-ADAPT-002-worker/tmp")
    raise SystemExit(main())
