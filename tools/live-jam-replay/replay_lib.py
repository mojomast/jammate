#!/usr/bin/env python3
"""Shared fail-closed helpers for the EVAL-LIVE-001 replay build/run scripts.

Corrected per docs/research/live-jam-replay/CORRECTION-CONTRACT.md:
  * exact backend macro detection (JAM_LIVE_BTRACK_AVAILABLE), not substring;
  * real link-closure extraction from the target's link metadata;
  * source-pin overrides for the pipeline's changed headers with immutable
    frozen headers still enforced;
  * live seam detection (setJamTrackerForTesting, requestStopNow).

Nothing here mutates the product build or any shared source.
"""
import glob
import hashlib
import json
import os
import re
import shlex
import subprocess


class PreflightError(Exception):
    pass


IMMUTABLE_HEADERS = (
    "src/jam/JamLiveInterface.h",
    "src/jam/RhythmTypes.h",
    "src/jam/MusicalClock.h",
    "src/jam/JamConfig.h",
)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_tree(root):
    h = hashlib.sha256()
    if not os.path.isdir(root):
        return None
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            rel = os.path.relpath(p, root)
            h.update(rel.encode("utf-8") + b"\0")
            with open(p, "rb") as f:
                h.update(f.read())
            h.update(b"\0")
    return h.hexdigest()


def run(cmd, cwd=None, timeout=600, env=None):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=timeout, env=env)
    return p.returncode, p.stdout


def git_info(source):
    info = {"path": os.path.abspath(source), "head": None, "branch": None,
            "dirty": None, "src_tree_hash_committed": None}
    try:
        rc, out = run(["git", "-C", source, "rev-parse", "HEAD"])
        if rc == 0:
            info["head"] = out.strip()
        rc, out = run(["git", "-C", source, "rev-parse", "--abbrev-ref", "HEAD"])
        if rc == 0:
            info["branch"] = out.strip()
        rc, out = run(["git", "-C", source, "status", "--porcelain"])
        if rc == 0:
            info["dirty"] = bool(out.strip())
        if info["head"]:
            rc, out = run(["git", "-C", source, "rev-parse", "HEAD:src"])
            if rc == 0:
                info["src_tree_hash_committed"] = out.strip()
    except FileNotFoundError:
        pass
    return info


def find_shared_archive(product):
    pattern = os.path.join(product, "GuitarCompanion_artefacts", "Release", "*SharedCode.a")
    hits = sorted(glob.glob(pattern))
    return hits[0] if hits else None


def parse_ninja_rule_vals(build_ninja, object_basename):
    lines = open(build_ninja, "r", encoding="utf-8", errors="replace").read().split("\n")
    start = None
    pat = re.compile(r"^build\s+\S*" + re.escape(object_basename) + r":")
    for i, line in enumerate(lines):
        if pat.match(line):
            start = i
            break
    if start is None:
        return None
    vals = {"DEFINES": "", "FLAGS": "", "INCLUDES": ""}
    j = start + 1
    while j < len(lines):
        ln = lines[j]
        if ln.startswith("build ") and not ln.startswith("  "):
            break
        m = re.match(r"\s*(DEFINES|FLAGS|INCLUDES) = (.*)", ln)
        if m:
            vals[m.group(1)] = m.group(2)
        j += 1
    return vals


def production_flags(product):
    ninja = os.path.join(product, "build.ninja")
    if not os.path.isfile(ninja):
        raise PreflightError(f"missing product build.ninja: {ninja}")
    vals = parse_ninja_rule_vals(ninja, "PluginProcessor.cpp.o")
    if vals is None:
        raise PreflightError("could not find the PluginProcessor.cpp compile rule in build.ninja")
    return vals


def product_source_dir(product, flags):
    for token in shlex.split(flags.get("INCLUDES", "")):
        if token.startswith("-I") and token.endswith("/src"):
            root = token[2:-len("/src")]
            if os.path.isfile(os.path.join(root, "src", "PluginProcessor.h")):
                return os.path.realpath(root)
    return None


def archive_symbols(archive):
    try:
        rc, out = run(["nm", "-C", "--defined-only", archive])
    except FileNotFoundError:
        raise PreflightError("nm is required for fail-closed symbol checks")
    return out if rc == 0 else ""


def backend_macro_defined(flags):
    for token in shlex.split(flags.get("DEFINES", "")):
        t = token[2:] if token.startswith("-D") else token
        if t == "JAM_LIVE_BTRACK_AVAILABLE" or t.startswith("JAM_LIVE_BTRACK_AVAILABLE="):
            return True
    return False


def detect_backend_signals(product, source, archive, flags):
    signals = []
    if backend_macro_defined(flags):
        signals.append("define:JAM_LIVE_BTRACK_AVAILABLE")
    ninja = os.path.join(product, "build.ninja")
    if os.path.isfile(ninja):
        text = open(ninja, "r", encoding="utf-8", errors="replace").read()
        if "JAM_ENABLE_BTRACK" in text:
            signals.append("ninja:JAM_ENABLE_BTRACK")
    if os.path.isfile(archive):
        syms = archive_symbols(archive)
        for needle in ("BTrack", "jam::BTrack"):
            if needle in syms:
                signals.append(f"symbol:{needle}")
        if "GuitarCompanionProcessor::submitJamCommand" in syms:
            signals.append("symbol:live_facade")
    for pat in ("src/btrack/*.cpp", "src/jam/*Backend*.cpp", "src/jam/*btrack*"):
        if glob.glob(os.path.join(source, pat)):
            signals.append(f"source:{pat}")
    return sorted(set(signals))


def detect_live_seams(source, archive):
    seams = {"setJamTrackerForTesting": False, "requestStopNow": False,
             "tracker_injection_archive_symbol": False}
    proc_h = os.path.join(source, "src", "PluginProcessor.h")
    bridge_h = os.path.join(source, "src", "jam", "DrumClockBridge.h")
    if os.path.isfile(proc_h):
        seams["setJamTrackerForTesting"] = "setJamTrackerForTesting" in open(
            proc_h, "r", encoding="utf-8", errors="replace").read()
    if os.path.isfile(bridge_h):
        seams["requestStopNow"] = "requestStopNow" in open(
            bridge_h, "r", encoding="utf-8", errors="replace").read()
    if os.path.isfile(archive):
        syms = archive_symbols(archive)
        seams["tracker_injection_archive_symbol"] = (
            "GuitarCompanionProcessor::setJamTrackerForTesting" in syms)
    return seams


def extract_link_closure(product):
    """Extract the real link inputs (archives, shared objects, -l, -Wl) from the
    Standalone target's link command, preserving order. Falls back to the known
    set."""
    archives, libs, wl_flags, inputs, source = [], [], [], [], "fallback"
    target = "GuitarCompanion_Standalone"
    try:
        rc, out = run(["ninja", "-t", "commands", target], cwd=product)
    except FileNotFoundError:
        rc, out = 1, ""
    if rc == 0 and out.strip():
        line = out.strip().splitlines()[-1]
        try:
            args = shlex.split(line)
        except ValueError:
            args = line.split()
        for a in args:
            if a.endswith(".a"):
                p = a if os.path.isabs(a) else os.path.join(product, a)
                archives.append(p)
                inputs.append(p)
            elif a.endswith(".so"):
                p = a if os.path.isabs(a) else os.path.join(product, a)
                libs.append(p)
                inputs.append(p)
            elif re.match(r"^-l", a):
                libs.append(a)
                inputs.append(a)
            elif a.startswith("-Wl,"):
                wl_flags.append(a)
                if not a.startswith("-Wl,--dependency-file"):
                    inputs.append(a)
        if archives:
            source = f"ninja:{target}"
    if not archives:
        shared = find_shared_archive(product)
        for p in (shared, os.path.join(product, "libnam_core.a"),
                  os.path.join(product, "libGuitarCompanionAssets.a"),
                  os.path.join(product, "jam-core", "libjam-core.a")):
            if p and os.path.isfile(p):
                archives.append(p)
        sysroot = "/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu"
        for lib in ("libasound.so", "libfontconfig.so", "libfreetype.so"):
            p = os.path.join(sysroot, lib)
            if os.path.isfile(p):
                libs.append(p)
        libs += ["-lrt", "-ldl", "-lpthread"]
        inputs = list(archives) + list(libs)
        source = "fallback"
    return {"source": source, "archives": archives, "libs": libs,
            "wl_flags": wl_flags, "inputs": inputs}


def apply_source_pin_overrides(source, immutable_pins, overrides):
    """Return (ok, missing, detail). Immutable headers must match their original
    pins; each override must match the file's actual hash exactly."""
    missing = []
    detail = {}
    for rel, expected in (immutable_pins or {}).items():
        p = os.path.join(source, rel)
        if not os.path.isfile(p):
            missing.append(f"immutable_pin_missing:{rel}")
            continue
        actual = sha256_file(p)
        detail[rel] = actual
        if actual != expected:
            missing.append(f"immutable_pin_changed:{rel}")
    for rel, expected in (overrides or {}).items():
        if rel in (immutable_pins or {}):
            continue
        p = os.path.join(source, rel)
        if not os.path.isfile(p):
            missing.append(f"override_missing:{rel}")
            continue
        actual = sha256_file(p)
        detail[rel] = actual
        if actual != expected:
            missing.append(f"override_mismatch:{rel}")
    return (len(missing) == 0), missing, detail


def preflight(source, product, sysroot_lib=None, immutable_pins=None, source_pin_overrides=None):
    result = {"ok": False, "missing": [], "identity": {}, "flags": {}, "paths": {}}
    source = os.path.realpath(source)
    product = os.path.realpath(product)

    live_header = os.path.join(source, "src", "jam", "JamLiveInterface.h")
    proc_h = os.path.join(source, "src", "PluginProcessor.h")
    proc_cpp = os.path.join(source, "src", "PluginProcessor.cpp")

    if not os.path.isfile(live_header):
        result["missing"].append("live_header")
    if not os.path.isfile(proc_h):
        result["missing"].append("plugin_processor_header")
    if not os.path.isfile(proc_cpp):
        result["missing"].append("plugin_processor_source")

    facade_defs = False
    if os.path.isfile(proc_cpp):
        text = open(proc_cpp, "r", encoding="utf-8", errors="replace").read()
        facade_defs = ("GuitarCompanionProcessor::submitJamCommand" in text
                       and "GuitarCompanionProcessor::readJamLiveState" in text)
    if not facade_defs:
        result["missing"].append("facade_definition")

    facade_decl = False
    if os.path.isfile(proc_h):
        text = open(proc_h, "r", encoding="utf-8", errors="replace").read()
        facade_decl = ("submitJamCommand" in text and "readJamLiveState" in text)
    if not facade_decl:
        result["missing"].append("facade_declaration")

    shared = find_shared_archive(product)
    if shared is None:
        result["missing"].append("shared_archive")
    nam = os.path.join(product, "libnam_core.a")
    assets = os.path.join(product, "libGuitarCompanionAssets.a")
    jamcore = os.path.join(product, "jam-core", "libjam-core.a")
    if not os.path.isfile(nam):
        result["missing"].append("nam_archive")
    if not os.path.isfile(assets):
        result["missing"].append("assets_archive")

    syms = archive_symbols(shared) if shared else ""
    sym_submit = "GuitarCompanionProcessor::submitJamCommand" in syms
    sym_read = "GuitarCompanionProcessor::readJamLiveState" in syms
    if not sym_submit:
        result["missing"].append("facade_symbol_submit")
    if not sym_read:
        result["missing"].append("facade_symbol_read")

    try:
        flags = production_flags(product)
    except PreflightError as e:
        flags = {"DEFINES": "", "FLAGS": "", "INCLUDES": ""}
        result["missing"].append("production_flags")
        result["flags_error"] = str(e)

    pdir = product_source_dir(product, flags) if flags else None
    source_matches = bool(pdir) and os.path.realpath(pdir) == source
    if not source_matches:
        result["missing"].append("source_matches_product")

    macro_defined = backend_macro_defined(flags) if flags else False
    if not macro_defined:
        result["missing"].append("backend_macro")
    backend_signals = detect_backend_signals(product, source, shared, flags) if shared else []
    if not backend_signals:
        result["missing"].append("backend_signal")

    seams = detect_live_seams(source, shared)
    closure = extract_link_closure(product)

    imm_ok, imm_missing, imm_detail = apply_source_pin_overrides(
        source, immutable_pins, source_pin_overrides)
    result["missing"].extend(imm_missing)

    src = {}
    if os.path.isfile(proc_h):
        src["plugin_processor_h_sha256"] = sha256_file(proc_h)
    if os.path.isfile(proc_cpp):
        src["plugin_processor_cpp_sha256"] = sha256_file(proc_cpp)
    if os.path.isfile(live_header):
        src["jam_live_interface_h_sha256"] = sha256_file(live_header)
    src["src_tree_hash"] = sha256_tree(os.path.join(source, "src"))
    src["facade_definitions_present"] = facade_defs
    src["path"] = source
    src.update({k: v for k, v in git_info(source).items() if k != "path"})
    src["immutable_pins_ok"] = imm_ok
    src["immutable_pins"] = imm_detail
    src["source_pin_overrides_applied"] = bool(source_pin_overrides)

    prod = {"path": product, "product_source_dir": pdir,
            "source_matches_product": source_matches,
            "link_closure_source": closure["source"]}
    for key, path in (("shared_archive", shared), ("nam_archive", nam),
                      ("assets_archive", assets), ("jam_archive", jamcore),
                      ("build_ninja", os.path.join(product, "build.ninja"))):
        prod[key] = path
        if path and os.path.isfile(path):
            prod[key + "_sha256"] = sha256_file(path)
    prod["link_archives"] = [
        {"path": p, "sha256": sha256_file(p) if os.path.isfile(p) else None}
        for p in closure["archives"]]
    prod["link_libs"] = closure["libs"]
    prod["link_wl_flags"] = closure["wl_flags"]

    result["identity"] = {
        "source": src,
        "product": prod,
        "facade_symbols": {"submitJamCommand": sym_submit, "readJamLiveState": sym_read},
        "backend": {"macro_defined": macro_defined, "signals": backend_signals},
        "live_seams": seams,
    }
    result["flags"] = flags
    result["paths"] = {"shared_archive": shared, "nam_archive": nam,
                       "assets_archive": assets, "jam_archive": jamcore,
                       "sysroot_lib": sysroot_lib or "",
                       "link_closure": closure}
    result["ok"] = len(result["missing"]) == 0
    return result
