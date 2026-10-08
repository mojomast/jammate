#!/usr/bin/env python3
"""RT-004 NAM architecture coverage - fail-closed validator and summariser.

Reads the per-run outputs written by run_nam_arch.sh (probe log, per-case CSV,
findings JSON, exit status), validates them against predeclared.json, and
writes summary.json, summary.md, models/*.json (weight-free model identity),
source-pin.txt and manifest.sha256 under docs/research/nam-architecture-probe/.

Status per (variant, model), never relabelled:
  measured-clean     model loaded and activation witnessed, all 26 cases present,
                     every heap alloc/free/lock/trylock/cond/unlock count zero
                     (dry and NAM). no-op free(NULL) is reported separately.
  measured-findings  as above, but at least one positive count. Kept as a
                     finding; never suppressed or used to change the matrix.
  unmeasured         probe ran cleanly, but the model did not load/activate
                     (explicit UNMEASURED, loaded=0 or no activation witness).
                     Only the 18 dry rows are present. Never counted as clean.
  failed             any validation error: non-zero exit, missing model,
                     malformed JSON/CSV, missing/duplicate/unexpected rows,
                     non-finite values, identity or sourcepin mismatch.

Exit status: 0 only when every run is measured (clean or findings);
3 when any run is unmeasured; 4 when any run failed; 2 on usage errors.
"""
import argparse
import csv
import hashlib
import json
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

EXPECTED_HEADER = (
    "tag,rate,block,drums,warm_blocks,duration_s,"
    "cold_cxxnew,cold_cxxnewarr,cold_cxxdel,cold_cxxdelarr,cold_cxxdelsized,"
    "cold_malloc,cold_calloc,cold_realloc,cold_free,cold_noopfree,"
    "cold_lock,cold_trylock,cold_cond,cold_unlock,"
    "cold_alloc_overflow,cold_lock_overflow,"
    "warm_cxxnew,warm_cxxnewarr,warm_cxxdel,warm_cxxdelarr,warm_cxxdelsized,"
    "warm_malloc,warm_calloc,warm_realloc,warm_free,warm_noopfree,"
    "warm_lock,warm_trylock,warm_cond,warm_unlock,"
    "warm_alloc_overflow,warm_lock_overflow,"
    "warm_alloc_cxx_total,warm_alloc_c_total,"
    "process_wall_ms,total_wall_ms,drum_active_blocks,voice_events,out_rms"
).split(",")

DRY_TAGS = {"dry": 0, "dry+drums": 1}
NAM_TAGS = {"nam": 0, "nam+drums": 1}
DRY_RATES = (44100, 48000, 96000)
DRY_BLOCKS = (64, 128, 512)
NAM_RATES = (48000, 96000)
NAM_BLOCKS = (128, 512)
class Failed(Exception):
    """Validation error: the run cannot be trusted."""


class Unmeasured(Exception):
    """Probe ran, but the NAM model did not load/activate (explicit, not clean)."""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise Failed(f"malformed or unreadable JSON {path}: {e}")


def as_int(v, col):
    try:
        n = int(v)
    except (TypeError, ValueError):
        raise Failed(f"column {col} is not an integer: {v!r}")
    if n < 0:
        raise Failed(f"column {col} is negative: {n}")
    return n


def as_finite(v, col):
    try:
        x = float(v)
    except (TypeError, ValueError):
        raise Failed(f"column {col} is not a number: {v!r}")
    if not math.isfinite(x):
        raise Failed(f"column {col} is non-finite: {v!r}")
    return x


# ---------------------------------------------------------------------------
# Model identity (weight-free)
# ---------------------------------------------------------------------------

def describe_config(arch, cfg, version):
    if arch == "LSTM":
        return {k: cfg.get(k) for k in ("input_size", "hidden_size", "num_layers")}
    if arch == "WaveNet":
        layers = []
        for layer in cfg.get("layers", []):
            act = layer.get("activation")
            if isinstance(act, dict):
                act = act.get("type", act)
            entry = {
                "channels": layer.get("channels"),
                "kernel_size": layer.get("kernel_size"),
                "dilation_count": len(layer.get("dilations", [])),
                "activation": act,
                "condition_size": layer.get("condition_size"),
            }
            slim = layer.get("slimmable")
            if isinstance(slim, dict):
                entry["slimmable_allowed_channels"] = (slim.get("kwargs") or {}).get("allowed_channels")
            layers.append(entry)
        out = {"layer_arrays": len(layers), "layers": layers, "head_scale": cfg.get("head_scale")}
        if "condition_dsp" in cfg:
            cd = cfg["condition_dsp"]
            out["condition_dsp"] = {"architecture": cd.get("architecture"), "version": cd.get("version")}
        return out
    if arch == "SlimmableContainer":
        subs = []
        for s in cfg.get("submodels", []):
            m = s.get("model", {})
            subs.append({
                "max_value": s.get("max_value"),
                "architecture": m.get("architecture"),
                "version": m.get("version"),
                "config": describe_config(m.get("architecture"), m.get("config", {}), m.get("version")),
            })
        return {"submodels": subs, "default_active_submodel_index": len(subs) - 1 if subs else None}
    return {"note": f"architecture {arch!r} not summarised"}


def describe_model(path):
    d = load_json(path)
    arch = d.get("architecture")
    weights = d.get("weights")
    return {
        "architecture": arch,
        "version": d.get("version"),
        "sample_rate": d.get("sample_rate"),
        "weight_count": len(weights) if isinstance(weights, list) else None,
        "config": describe_config(arch, d.get("config", {}), d.get("version")),
    }


# ---------------------------------------------------------------------------
# Source pin
# ---------------------------------------------------------------------------

def check_source_pin(pre):
    lines = [f"revision={pre['source_pin']['revision']}"]
    for rel, expected in pre["source_pin"]["files"].items():
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):
            raise Failed(f"missing pinned source {rel}")
        actual = sha256_file(path)
        if actual != expected:
            raise Failed(f"{rel} sha256 {actual} != predeclared {expected}")
        try:
            pinned = subprocess.run(
                ["git", "-C", REPO, "show", f"{pre['source_pin']['revision']}:{rel}"],
                check=True, capture_output=True).stdout
        except (OSError, subprocess.CalledProcessError):
            raise Failed(f"cannot read {rel} at revision {pre['source_pin']['revision']}")
        if hashlib.sha256(pinned).hexdigest() != expected:
            raise Failed(f"{rel} at revision {pre['source_pin']['revision']} differs from predeclared pin")
        lines.append(f"{rel} sha256={actual} pinned_revision_identical=yes")
    return lines


def check_archive_pins(pre):
    """Confirm each variant's probe binary and archives match the predeclared identity."""
    out = {}
    for name, b in pre["binaries"].items():
        if not os.path.isfile(b["path"]):
            raise Failed(f"probe binary missing: {b['path']}")
        if sha256_file(b["path"]) != b["sha256"]:
            raise Failed(f"probe binary {name} sha256 mismatch")
        pin_file = os.path.join(os.path.dirname(b["path"]), "source-pin.txt")
        if os.path.isfile(pin_file):
            with open(pin_file, "r", encoding="utf-8") as f:
                pin = dict(l.strip().split("=", 1) for l in f if "=" in l and not l.startswith("src/"))
            if pin.get("nam_archive_sha256") != b["nam_archive_sha256"]:
                raise Failed(f"{name}: recorded NAM archive sha {pin.get('nam_archive_sha256')} != predeclared")
            if pin.get("shared_archive_sha256") != b["shared_archive_sha256"]:
                raise Failed(f"{name}: recorded shared archive sha mismatch")
        out[name] = {"binary_sha256": b["sha256"], "nam_archive_sha256": b["nam_archive_sha256"],
                     "shared_archive_sha256": b["shared_archive_sha256"]}
    return out


# ---------------------------------------------------------------------------
# Per-run validation
# ---------------------------------------------------------------------------

def parse_exit_status(path):
    kv = {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                if "=" in line:
                    k, v = line.rstrip("\n").split("=", 1)
                    kv[k] = v
    except OSError as e:
        raise Failed(f"missing exit status {path}: {e}")
    return kv


def read_csv_rows(path):
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            reader = csv.reader(f)
            header = next(reader, None)
            if header != EXPECTED_HEADER:
                raise Failed("CSV header does not match the probe's contract")
            rows = []
            for lineno, row in enumerate(reader, start=2):
                if len(row) != len(EXPECTED_HEADER):
                    raise Failed(f"CSV line {lineno} has {len(row)} fields, expected {len(EXPECTED_HEADER)}")
                rows.append(dict(zip(EXPECTED_HEADER, row)))
            return rows
    except (OSError, csv.Error, StopIteration) as e:
        raise Failed(f"malformed CSV {path}: {e}")


def expected_case_keys():
    keys = set()
    for r in DRY_RATES:
        for b in DRY_BLOCKS:
            for t in DRY_TAGS:
                keys.add(("dry", t, r, b))
    nam = set()
    for r in NAM_RATES:
        for b in NAM_BLOCKS:
            for t in NAM_TAGS:
                nam.add(("nam", t, r, b))
    return keys, nam


def summarise_row(row, warm_blocks):
    tag = row["tag"]
    rate = as_finite(row["rate"], "rate")
    block = as_int(row["block"], "block")
    drums = as_int(row["drums"], "drums")
    wb = as_int(row["warm_blocks"], "warm_blocks")
    if wb != warm_blocks:
        raise Failed(f"row {tag}/{rate}/{block} warm_blocks {wb} != {warm_blocks}")
    if tag in DRY_TAGS and drums != DRY_TAGS[tag]:
        raise Failed(f"row {tag} drums flag {drums} inconsistent with tag")
    if tag in NAM_TAGS and drums != NAM_TAGS[tag]:
        raise Failed(f"row {tag} drums flag {drums} inconsistent with tag")
    counts = {}
    for c in EXPECTED_HEADER:
        if c.startswith(("cold_", "warm_")) and c != "warm_blocks":
            counts[c] = as_int(row[c], c)
    dur = as_finite(row["duration_s"], "duration_s")
    if abs(dur - wb * block / rate) > 1e-3:
        raise Failed(f"row {tag}/{rate}/{block} duration_s {dur} inconsistent with warm*block/rate")
    proc = as_finite(row["process_wall_ms"], "process_wall_ms")
    total = as_finite(row["total_wall_ms"], "total_wall_ms")
    if proc < 0 or total < 0:
        raise Failed("negative wall time")
    outrms = as_finite(row["out_rms"], "out_rms")
    if outrms < 0:
        raise Failed("negative out_rms")
    as_int(row["drum_active_blocks"], "drum_active_blocks")
    as_int(row["voice_events"], "voice_events")

    def total_of(prefix, names):
        return sum(counts[f"{prefix}_{n}"] for n in names)

    heap_alloc_names = ["cxxnew", "cxxnewarr", "malloc", "calloc", "realloc"]
    heap_free_names = ["cxxdel", "cxxdelarr", "cxxdelsized", "free"]
    lock_names = ["lock", "trylock", "cond", "unlock"]
    warm_alloc = counts["warm_alloc_cxx_total"] + counts["warm_alloc_c_total"]
    warm_free = total_of("warm", heap_free_names)
    return {
        "tag": tag, "rate": int(rate), "block": block, "drums": drums,
        "warm_blocks": wb,
        "cold_alloc": counts["cold_cxxnew"] + counts["cold_cxxnewarr"] + counts["cold_malloc"]
                      + counts["cold_calloc"] + counts["cold_realloc"],
        "cold_free": total_of("cold", heap_free_names),
        "cold_lock_ops": total_of("cold", ["lock", "trylock", "cond", "unlock"]),
        "cold_noopfree": counts["cold_noopfree"],
        "warm_alloc": warm_alloc,
        "warm_free": warm_free,
        "warm_lock_ops": total_of("warm", lock_names),
        "warm_noopfree": counts["warm_noopfree"],
        "warm_alloc_overflow": counts["warm_alloc_overflow"],
        "warm_lock_overflow": counts["warm_lock_overflow"],
        "warm_alloc_per_host_sample": warm_alloc / float(wb * block),
        "out_rms": outrms,
        "drum_active_blocks": int(row["drum_active_blocks"]),
        "voice_events": int(row["voice_events"]),
        "process_wall_ms": proc,
    }


def validate_run(run_dir, pre, variant, model_path, expected_model_sha, bin_sha, models_dir):
    st_path = os.path.join(run_dir, "exit-status.txt")
    st = parse_exit_status(st_path)
    if st.get("variant") != variant:
        raise Failed(f"exit status variant {st.get('variant')!r} != {variant!r}")
    if st.get("model_file") != os.path.basename(model_path):
        raise Failed("exit status model_file mismatch")
    if st.get("binary_sha256") != bin_sha:
        raise Failed("exit status binary sha256 does not match predeclared probe identity")
    if st.get("model_sha256") != expected_model_sha:
        raise Failed("exit status model sha256 does not match predeclared model identity")
    if st.get("exit") != "0":
        raise Failed(f"probe exit status {st.get('exit')}")

    if not os.path.isfile(model_path):
        raise Failed(f"model file missing: {model_path}")
    if sha256_file(model_path) != expected_model_sha:
        raise Failed("model file sha256 does not match predeclared identity")

    warm = int(st.get("warm_blocks", "0"))
    log_path = os.path.join(run_dir, "probe.log")
    try:
        with open(log_path, "r", encoding="utf-8", errors="strict") as f:
            log = f.read()
    except (OSError, UnicodeDecodeError) as e:
        raise Failed(f"probe log unreadable: {e}")

    if "self-check PASS" not in log:
        raise Failed("instrument self-check PASS not found in probe log")

    req = re.findall(r"^\[nam\] requesting async load: (.+)$", log, flags=re.M)
    if len(req) != 1 or req[0] != model_path:
        raise Failed(f"model identity not witnessed in log: requested {req!r}")

    post = re.findall(r'^\[nam\] post-dry state: loaded=([01]) resampling=([01]) error="(.*)"$',
                      log, flags=re.M)
    if len(post) != 1:
        raise Failed("post-dry NAM state line missing or duplicated")
    loaded, resampling, load_error = post[0]
    activation_witness = "model became active during the dry run" in log
    unmeasured_line = "[nam] UNMEASURED" in log

    if loaded == "1" and load_error != "":
        raise Failed("loaded=1 with a non-empty load error")
    if loaded == "1" and not activation_witness:
        raise Failed("model reports loaded but activation was not witnessed in the dry run")
    if loaded == "0" and "[nam] UNMEASURED" not in log:
        raise Failed("loaded=0 without the explicit UNMEASURED marker")

    try:
        findings = load_json(os.path.join(run_dir, "findings.json"))
    except Failed:
        raise
    findings_full = findings.get("full", findings) if isinstance(findings, dict) else findings
    if not isinstance(findings_full, dict):
        raise Failed("findings.json has no full-run object")
    if findings.get("mode") != "full" and "full" not in findings:
        raise Failed("findings.json is not a full run")
    if findings.get("selfcheck_pass") is not True or findings.get("args_ok") is not True:
        raise Failed("findings.json self-check/args flags not true")
    if findings.get("scene_expectation_ok") is not True:
        raise Failed("findings.json scene_expectation_ok is not true")
    if findings.get("dry_cases") != 18:
        raise Failed(f"findings.json dry_cases {findings.get('dry_cases')} != 18")

    rows = read_csv_rows(os.path.join(run_dir, "cases.csv"))
    dry_expected, nam_expected = expected_case_keys()
    seen = set()
    parsed = []
    for row in rows:
        if row["tag"] in NAM_TAGS:
            kind = "nam"
        elif row["tag"] in DRY_TAGS:
            kind = "dry"
        else:
            raise Failed(f"unexpected tag {row['tag']!r}")
        key = (kind, row["tag"], int(as_finite(row["rate"], "rate")), as_int(row["block"], "block"))
        if key in seen:
            raise Failed(f"duplicate case {key}")
        seen.add(key)
        parsed.append((key, summarise_row(row, warm)))

    dry_seen = {k for k, _ in parsed if k[0] == "dry"}
    nam_seen = {k for k, _ in parsed if k[0] == "nam"}
    if dry_seen != dry_expected:
        raise Failed(f"dry case matrix mismatch: missing {sorted(dry_expected - dry_seen)} "
                     f"extra {sorted(dry_seen - dry_expected)}")
    if loaded == "1":
        if nam_seen != nam_expected:
            raise Failed(f"NAM case matrix mismatch: missing {sorted(nam_expected - nam_seen)} "
                         f"extra {sorted(nam_seen - nam_expected)}")
        if findings.get("nam_cases") != 8:
            raise Failed(f"findings.json nam_cases {findings.get('nam_cases')} != 8")
    else:
        if nam_seen:
            raise Failed("NAM rows present although the model did not load")
        if findings.get("nam_cases") != 0:
            raise Failed("findings.json reports NAM cases for an unloaded model")
        if not unmeasured_line:
            raise Failed("unloaded model without UNMEASURED marker")

    nam_rows = [r for k, r in parsed if k[0] == "nam"]
    dry_rows = [r for k, r in parsed if k[0] == "dry"]
    if loaded == "1" and findings.get("nam_cases_with_alloc") != sum(1 for r in nam_rows if r["warm_alloc"] > 0):
        raise Failed("findings.json nam_cases_with_alloc disagrees with CSV")

    nonzero_nam = [r for r in nam_rows if any(r[k] for k in (
        "cold_alloc", "cold_free", "cold_lock_ops", "warm_alloc", "warm_free", "warm_lock_ops",
        "warm_alloc_overflow", "warm_lock_overflow"))]
    nonzero_dry = [r for r in dry_rows if any(r[k] for k in (
        "cold_alloc", "cold_free", "cold_lock_ops", "warm_alloc", "warm_free", "warm_lock_ops",
        "warm_alloc_overflow", "warm_lock_overflow"))]
    noop_total = sum(r["cold_noopfree"] + r["warm_noopfree"] for r in nam_rows + dry_rows)

    if loaded == "0":
        status = "unmeasured"
        reason = "model did not load or activate; explicit UNMEASURED; not clean"
    elif nonzero_nam or nonzero_dry:
        status = "measured-findings"
        reason = "positive heap or lock counts recorded as findings"
    else:
        status = "measured-clean"
        reason = "all dry and NAM heap/lock counts zero (no-op free(NULL) reported separately)"

    call_sites = []
    if status == "measured-findings":
        call_sites = resolve_call_sites(log, os.path.join(
            pre["binaries"][variant]["path"]))

    per_sample = [r["warm_alloc_per_host_sample"] for r in nam_rows]
    rms = [r["out_rms"] for r in nam_rows]
    return {
        "variant": variant,
        "model_file": os.path.basename(model_path),
        "model_sha256": expected_model_sha,
        "status": status,
        "status_reason": reason,
        "loaded": loaded == "1",
        "resampling_at_post_dry": resampling == "1",
        "activation_witnessed_in_dry_run": activation_witness,
        "load_error": load_error,
        "exit": 0,
        "dry_rows": len(dry_rows),
        "nam_rows": len(nam_rows),
        "dry_rows_nonzero": len(nonzero_dry),
        "nam_rows_nonzero": len(nonzero_nam),
        "nam_cases_with_warm_alloc": sum(1 for r in nam_rows if r["warm_alloc"] > 0),
        "nam_warm_alloc_total": sum(r["warm_alloc"] for r in nam_rows),
        "nam_warm_free_total": sum(r["warm_free"] for r in nam_rows),
        "nam_warm_lock_ops_total": sum(r["warm_lock_ops"] for r in nam_rows),
        "nam_cold_alloc_total": sum(r["cold_alloc"] for r in nam_rows),
        "nam_warm_alloc_per_host_sample_min": min(per_sample) if per_sample else None,
        "nam_warm_alloc_per_host_sample_max": max(per_sample) if per_sample else None,
        "nam_out_rms_min": min(rms) if rms else None,
        "nam_out_rms_max": max(rms) if rms else None,
        "noopfree_total_reported_separately": noop_total,
        "per_sample_output_checked": False,
        "call_sites": call_sites,
        "rows": [dict(key=list(k), **r) for k, r in parsed],
    }


def resolve_call_sites(log, binary):
    addrs = []
    for a in re.findall(r"caller_vaddr=(0x[0-9a-fA-F]+)", log):
        if a not in addrs:
            addrs.append(a)
    addrs = addrs[:16]
    if not addrs or not os.path.isfile(binary):
        return []
    try:
        out = subprocess.run(["addr2line", "-f", "-C", "-e", binary] + addrs,
                             check=True, capture_output=True, text=True).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        return [{"vaddr": a, "resolved": False} for a in addrs]
    sites = []
    for i, a in enumerate(addrs):
        fn = out[2 * i] if 2 * i < len(out) else "??"
        loc = out[2 * i + 1] if 2 * i + 1 < len(out) else "??:0"
        sites.append({"vaddr": a, "function": fn, "location": loc})
    return sites


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--predeclared", default=os.path.join(HERE, "predeclared.json"))
    ap.add_argument("--runs", default=os.path.join(REPO, "docs", "research", "nam-architecture-probe", "runs"))
    ap.add_argument("--out", default=os.path.join(REPO, "docs", "research", "nam-architecture-probe"))
    ap.add_argument("--models-dir", default=None,
                    help="override models directory (default: predeclared models_dir)")
    ap.add_argument("--no-write", action="store_true", help="validate only; write nothing")
    args = ap.parse_args(argv)

    pre = load_json(args.predeclared)
    models_dir = args.models_dir or os.path.join(REPO, pre["models_dir"])
    if not os.path.isabs(models_dir):
        models_dir = os.path.join(REPO, models_dir)
    if args.models_dir is None and not os.path.isdir(models_dir):
        # The worktree's submodule may be empty; fall back to the main checkout's models.
        alt = os.path.join("/home/mojo/projects/guitars", pre["models_dir"])
        if os.path.isdir(alt):
            models_dir = alt

    try:
        pin_lines = check_source_pin(pre)
        archive_pins = check_archive_pins(pre)
    except Failed as e:
        print(f"FAILED sourcepin/archive identity: {e}", file=sys.stderr)
        return 4

    identities = {}
    for a in pre["architectures"]:
        path = os.path.join(models_dir, a["file"])
        if not os.path.isfile(path):
            print(f"FAILED model file missing: {path}", file=sys.stderr)
            return 4
        sha = sha256_file(path)
        if sha != a["sha256"]:
            print(f"FAILED model {a['file']} sha256 {sha} != predeclared {a['sha256']}", file=sys.stderr)
            return 4
        ident = describe_model(path)
        if ident["architecture"] != a["expected_architecture"] or ident["version"] != a["expected_version"]:
            print(f"FAILED model {a['file']} architecture/version {ident['architecture']}/{ident['version']} "
                  f"!= predeclared", file=sys.stderr)
            return 4
        identities[a["id"]] = (path, sha, ident)

    runs = []
    worst = 0
    for variant in ("repaired", "original_control"):
        bin_sha = pre["binaries"][variant]["sha256"]
        for a in pre["architectures"]:
            path, sha, ident = identities[a["id"]]
            run_dir = os.path.join(args.runs, variant, a["id"])
            try:
                run = validate_run(run_dir, pre, variant, path, sha, bin_sha, models_dir)
                run["architecture_id"] = a["id"]
                run["model_identity"] = ident
                run["role"] = a["role"]
            except Failed as e:
                run = {"variant": variant, "architecture_id": a["id"], "model_file": a["file"],
                       "model_sha256": sha, "status": "failed", "status_reason": str(e)}
                worst = max(worst, 4)
            except Unmeasured as e:
                run = {"variant": variant, "architecture_id": a["id"], "model_file": a["file"],
                       "model_sha256": sha, "status": "unmeasured", "status_reason": str(e)}
                worst = max(worst, 3)
            if run["status"] == "unmeasured":
                worst = max(worst, 3)
            runs.append(run)
            print(f"{variant:17s} {a['id']:24s} {run['status']:18s} {run.get('status_reason', '')}")

    summary = {
        "task": "RT-004",
        "predeclared_sha256": sha256_file(args.predeclared),
        "source_pin": pin_lines,
        "archive_pins": archive_pins,
        "matrix": pre["matrix"],
        "runs": runs,
        "coverage_complete": all(r["status"] in ("measured-clean", "measured-findings") for r in runs),
        "clean_runs": sum(1 for r in runs if r["status"] == "measured-clean"),
        "findings_runs": sum(1 for r in runs if r["status"] == "measured-findings"),
        "unmeasured_runs": sum(1 for r in runs if r["status"] == "unmeasured"),
        "failed_runs": sum(1 for r in runs if r["status"] == "failed"),
    }

    if args.no_write:
        print("validate-only: nothing written")
        return worst

    os.makedirs(args.out, exist_ok=True)
    models_out = os.path.join(args.out, "models")
    os.makedirs(models_out, exist_ok=True)
    for a in pre["architectures"]:
        _, sha, ident = identities[a["id"]]
        with open(os.path.join(models_out, a["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump({"id": a["id"], "file": a["file"], "sha256": sha, **ident}, f, indent=2)
            f.write("\n")
    with open(os.path.join(args.out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")
    with open(os.path.join(args.out, "source-pin.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(pin_lines) + "\n")
        for name, pins in archive_pins.items():
            for k, v in pins.items():
                f.write(f"{name}.{k}={v}\n")
    write_markdown(os.path.join(args.out, "summary.md"), summary)
    write_manifest(args.out)
    print(f"summary: {os.path.join(args.out, 'summary.json')}")
    print(f"coverage_complete={summary['coverage_complete']} clean={summary['clean_runs']} "
          f"findings={summary['findings_runs']} unmeasured={summary['unmeasured_runs']} "
          f"failed={summary['failed_runs']}")
    return worst


def write_markdown(path, s):
    lines = ["| variant | architecture | model | status | NAM rows | warm alloc total | warm alloc/host-sample (min-max) | warm free | lock ops | noop frees | out RMS (min-max) |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in s["runs"]:
        if r["status"] == "failed":
            lines.append(f"| {r['variant']} | {r['architecture_id']} | {r['model_file']} | **failed** | - | - | - | - | - | - | {r['status_reason']} |")
            continue
        ps_min = r.get("nam_warm_alloc_per_host_sample_min")
        ps_max = r.get("nam_warm_alloc_per_host_sample_max")
        rms_min = r.get("nam_out_rms_min")
        rms_max = r.get("nam_out_rms_max")
        fmt = lambda v: "-" if v is None else f"{v:.4f}"
        lines.append(
            f"| {r['variant']} | {r['architecture_id']} | {r['model_file']} | {r['status']} | "
            f"{r.get('nam_rows', 0)} | {r.get('nam_warm_alloc_total', 0)} | "
            f"{fmt(ps_min)}-{fmt(ps_max)} | {r.get('nam_warm_free_total', 0)} | "
            f"{r.get('nam_warm_lock_ops_total', 0)} | {r.get('noopfree_total_reported_separately', 0)} | "
            f"{fmt(rms_min)}-{fmt(rms_max)} |")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def write_manifest(out):
    entries = []
    for root, _, files in os.walk(out):
        for name in files:
            if name == "manifest.sha256":
                continue
            p = os.path.join(root, name)
            entries.append((os.path.relpath(p, out), sha256_file(p)))
    with open(os.path.join(out, "manifest.sha256"), "w", encoding="utf-8") as f:
        for rel, h in sorted(entries):
            f.write(f"{h}  {rel}\n")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
