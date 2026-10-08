#!/usr/bin/env python3
"""Build fresh drum/test objects, reuse existing JUCE objects and assets; run all cases.

Reads Ninja metadata only; never invokes a product build or modifies its outputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("/home/mojo/projects/guitars"))
    parser.add_argument("--product-build", type=Path,
                        default=Path("/home/mojo/projects/build-RT-003-integration/product"))
    parser.add_argument("--output", type=Path,
                        default=Path("/home/mojo/projects/guitars-build-resume/foundation-tests"))
    args = parser.parse_args()
    source, product, output = (p.resolve() for p in
                               (args.source, args.product_build, args.output))
    owned = Path(__file__).resolve().parents[2]
    output.mkdir(parents=True, exist_ok=True)
    commands = subprocess.check_output(
        ["ninja", "-t", "commands", "GuitarCompanionTests"], cwd=product, text=True).splitlines()
    compile_template = next(shlex.split(c) for c in commands
                            if " -c " in c and c.endswith("/tests/TestMain.cpp"))
    # Keep actual configured definitions/includes, remove dependency output flags.
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
    sources = [source / "tests" / name for name in
               ("TestMain.cpp", "DrumSpecTests.cpp", "DrumLibraryTests.cpp",
                "DrumGeneratorTests.cpp", "DrumCodecTests.cpp", "DrumMidiTests.cpp")]
    sources += [owned / "tests/DrumFoundationTests.cpp"]
    sources += [source / "src" / name for name in
                ("DrumEngine.cpp", "DrumLibrary.cpp", "DrumGenerator.cpp")]
    objects = []
    executed = []
    with (output / "build.log").open("w") as log:
        for src in sources:
            obj = output / (src.stem + ".o")
            cmd = [compile_template[0], *flags, "-c", str(src), "-o", str(obj)]
            executed.append(cmd)
            subprocess.run(cmd, cwd=product, stdout=log, stderr=log, check=True)
            objects.append(str(obj))
        link_line = next(c for c in reversed(commands)
                         if " -o tests/GuitarCompanionTests_artefacts/" in c)
        link = shlex.split(link_line)
        link = link[link.index("/usr/bin/c++"):]
        link = link[:link.index("&&")] if "&&" in link else link
        retained = []
        reused = []
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
        binary = output / "DrumFoundationTests"
        cmd = [link[0], *objects, *retained, "-o", str(binary)]
        executed.append(cmd)
        subprocess.run(cmd, cwd=product, stdout=log, stderr=log, check=True)
    def digest(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps({
        "source": str(source), "sourceHead": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
        "freshSources": {str(p): digest(p) for p in sources},
        "reusedInputs": {p: digest(p) for p in reused},
        "commands": executed, "binarySha256": digest(binary),
    }, indent=2) + "\n")
    result = subprocess.run([str(binary)], cwd=output, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "results.log").write_text(result.stdout)
    print(result.stdout, end="")
    print(f"Build log and input hashes: {output}")
    return result.returncode


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", "/home/mojo/projects/guitars-build-resume/tmp")
    raise SystemExit(main())
