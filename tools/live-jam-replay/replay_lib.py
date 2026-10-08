#!/usr/bin/env python3
"""Shared fail-closed helpers for the EVAL-LIVE-001 replay build/run scripts.

Nothing here mutates the product build or any shared source. It only reads the
product's Ninja metadata, hashes source/archive inputs and resolves symbols, so
the replay can reuse the already-built product archives and JUCE objects
without a full rebuild of shared dependencies.
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


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_tree(root):
    """Deterministic content hash over a directory tree (relative path + bytes)."""
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
    """Exact FLAGS/INCLUDES/DEFINES of the product's PluginProcessor.cpp TU."""
    ninja = os.path.join(product, "build.ninja")
    if not os.path.isfile(ninja):
        raise PreflightError(f"missing product build.ninja: {ninja}")
    vals = parse_ninja_rule_vals(ninja, "PluginProcessor.cpp.o")
    if vals is None:
        raise PreflightError("could not find the PluginProcessor.cpp compile rule in build.ninja")
    return vals


def product_source_dir(product, flags):
    """The source root the product archive was compiled from, from its -I flags."""
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
    if rc != 0:
        return ""
    return out


def detect_backend_signals(product, source, archive):
    signals = []
    ninja = os.path.join(product, "build.ninja")
    if os.path.isfile(ninja):
        text = open(ninja, "r", encoding="utf-8", errors="replace").read()
        for token in ("JAM_ENABLE_BTRACK", "JAM_LIVE", "BTrack", "BTRACK", "jam_btrack", "jam-btrack"):
            if token in text:
                signals.append(f"ninja:{token}")
    if os.path.isfile(archive):
        syms = archive_symbols(archive)
        for needle in ("jam::", "BTrack", "Backend"):
            if needle == "jam::":
                continue
            if needle in syms:
                signals.append(f"symbol:{needle}")
        if "GuitarCompanionProcessor::submitJamCommand" in syms:
            signals.append("symbol:live_facade")
    # source-side backend file evidence
    for pat in ("src/btrack/*.cpp", "src/jam/*Backend*.cpp", "src/jam/*btrack*"):
        if glob.glob(os.path.join(source, pat)):
            signals.append(f"source:{pat}")
    return sorted(set(signals))


def preflight(source, product, sysroot_lib=None):
    """Fail-closed readiness report. `missing` is non-empty iff not live-ready."""
    result = {"ok": False, "missing": [], "identity": {}, "flags": {},
              "paths": {}}
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

    backend_signals = detect_backend_signals(product, source, shared) if shared else []
    if not backend_signals:
        result["missing"].append("backend_signal")

    # Identity: hashes of the frozen inputs and the product archives.
    src = {}
    if os.path.isfile(proc_h):
        src["plugin_processor_h_sha256"] = sha256_file(proc_h)
    if os.path.isfile(proc_cpp):
        src["plugin_processor_cpp_sha256"] = sha256_file(proc_cpp)
    if os.path.isfile(live_header):
        src["jam_live_interface_h_sha256"] = sha256_file(live_header)
    src["src_tree_hash"] = sha256_tree(os.path.join(source, "src"))
    src["facade_definitions_present"] = facade_defs
    src.update({"path": source})
    src.update({k: v for k, v in git_info(source).items() if k != "path"})

    prod = {"path": product, "product_source_dir": pdir,
            "source_matches_product": source_matches}
    for key, path in (("shared_archive", shared), ("nam_archive", nam),
                      ("assets_archive", assets), ("jam_archive", jamcore),
                      ("build_ninja", os.path.join(product, "build.ninja"))):
        prod[key] = path
        if path and os.path.isfile(path):
            prod[key + "_sha256"] = sha256_file(path)

    result["identity"] = {
        "source": src,
        "product": prod,
        "facade_symbols": {"submitJamCommand": sym_submit, "readJamLiveState": sym_read},
        "backend": {"signals": backend_signals},
    }
    result["flags"] = flags
    result["paths"] = {"shared_archive": shared, "nam_archive": nam,
                       "assets_archive": assets, "jam_archive": jamcore,
                       "sysroot_lib": sysroot_lib or ""}
    result["ok"] = len(result["missing"]) == 0
    return result
