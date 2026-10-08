#!/usr/bin/env python3
"""RT-005 fail-closed validator for the activation-repair processor probe runs.

Reads one run directory per (variant, model) produced by
run_activation_probe.sh and asserts the three-archive comparison:

  * structural validity of every run (exit 0, self-check, exact 18 dry + 8 NAM
    case matrix, warm_blocks 128, finite values, hashes matching the
    predeclaration, actual archives/binaries hashed when present);
  * the RT-005 `new` variant is allocation/free/lock-free on EVERY model;
  * the RT-003 `lstm_only` variant retains the a2_wavenet_max finding and is
    zero on lstm (proving RT-003 did not cover the activation path);
  * the pinned `original` variant is positive on a2_wavenet_max and lstm;
  * the previously clean architectures are zero in all three variants;
  * every dry/built-in case is zero in every run.

Writes summary.json and summary.md, plus an `all_pass` verdict. Exits 0 when
every check passes, 3 when any run is invalid/positive where it should be zero.
"""
import argparse
import csv
import json
import math
import os
import sys

ALLOC_KINDS = ("cxxnew", "cxxnewarr", "malloc", "calloc", "realloc")
FREE_KINDS = ("cxxdel", "cxxdelarr", "cxxdelsized", "free")
LOCK_KINDS = ("lock", "trylock", "cond", "unlock")
REQUIRED_STATUS = ("variant", "architecture_id", "model_file", "warm_blocks",
                   "timeout_s", "binary", "binary_sha256", "nam_archive",
                   "nam_archive_sha256", "model_sha256", "exit")
REQUIRED_FINDINGS = ("selfcheck_pass", "args_ok", "dry_cases", "dry_all_zero",
                     "nam_cases")


def sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def to_int(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError(f"negative counter {key}")
    return value


def total(row, prefix, kinds):
    return sum(to_int(row, f"{prefix}_{k}") for k in kinds)


def load_status(path):
    d = {}
    for line in open(path):
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            d[k] = v
    return d


def nam_rows(rows):
    return [r for r in rows if r["tag"] in ("nam", "nam+drums")]


def dry_rows(rows):
    return [r for r in rows if r["tag"] in ("dry", "dry+drums")]


def check_run(variant, arch, run_dir, pre, models_dir, errors):
    label = f"{variant}/{arch['id']}"
    status_path = os.path.join(run_dir, "exit-status.txt")
    csv_path = os.path.join(run_dir, "cases.csv")
    findings_path = os.path.join(run_dir, "findings.json")
    if not (os.path.isfile(status_path) and os.path.isfile(csv_path)
            and os.path.isfile(findings_path)):
        errors.append(f"{label}: missing run artifacts")
        return None

    st = load_status(status_path)
    for key in REQUIRED_STATUS:
        if key not in st:
            errors.append(f"{label}: exit-status missing {key}")
    if st.get("variant") != variant:
        errors.append(f"{label}: status variant {st.get('variant')!r} != {variant!r}")
    if st.get("architecture_id") != arch["id"]:
        errors.append(f"{label}: status architecture_id mismatch")
    if st.get("model_file") != arch["file"]:
        errors.append(f"{label}: status model_file mismatch")
    if st.get("exit") != "0":
        errors.append(f"{label}: probe exit {st.get('exit')!r} != 0")

    want = pre["variants"][variant]
    if st.get("binary_sha256") != want["binary_sha256"]:
        errors.append(f"{label}: binary sha mismatch")
    if st.get("nam_archive_sha256") != want["nam_archive_sha256"]:
        errors.append(f"{label}: NAM archive sha mismatch")
    if st.get("model_sha256") != arch["sha256"]:
        errors.append(f"{label}: model sha mismatch")
    if st.get("warm_blocks") != str(pre["matrix"]["warm_blocks"]):
        errors.append(f"{label}: warm_blocks not protocol")
    if st.get("timeout_s") != str(pre["matrix"]["timeout_s"]):
        errors.append(f"{label}: timeout not protocol")

    # Actual files on disk, when present, must hash to the predeclared identity.
    for disk, expected_sha, what in (
            (want["binary"], want["binary_sha256"], "binary"),
            (want["nam_archive"], want["nam_archive_sha256"], "NAM archive"),
            (want["shared_archive"], want["shared_archive_sha256"], "shared archive"),
            (os.path.join(models_dir, arch["file"]), arch["sha256"], "model")):
        if os.path.isfile(disk):
            if sha256(disk) != expected_sha:
                errors.append(f"{label}: on-disk {what} sha != predeclared")
        elif what in ("binary", "NAM archive"):
            errors.append(f"{label}: on-disk {what} missing: {disk}")

    try:
        findings = json.load(open(findings_path))
    except Exception as e:
        errors.append(f"{label}: cannot read findings: {e}")
        return None
    for key in REQUIRED_FINDINGS:
        if key not in findings:
            errors.append(f"{label}: findings missing {key}")
    if findings.get("selfcheck_pass") is not True:
        errors.append(f"{label}: selfcheck_pass not true")
    if findings.get("args_ok") is not True:
        errors.append(f"{label}: args_ok not true")
    if findings.get("dry_cases") != pre["matrix"]["dry_cases"]:
        errors.append(f"{label}: dry_cases != {pre['matrix']['dry_cases']}")
    if findings.get("dry_all_zero") is not True:
        errors.append(f"{label}: dry_all_zero not true")
    if findings.get("nam_cases") != pre["matrix"]["nam_cases_per_model"]:
        errors.append(f"{label}: nam_cases != {pre['matrix']['nam_cases_per_model']}")

    try:
        rows = list(csv.DictReader(open(csv_path, newline="")))
    except Exception as e:
        errors.append(f"{label}: cannot read CSV: {e}")
        return None

    dn, nn = dry_rows(rows), nam_rows(rows)
    if len(dn) != pre["matrix"]["dry_cases"]:
        errors.append(f"{label}: dry row count {len(dn)} != {pre['matrix']['dry_cases']}")
    if len(nn) != pre["matrix"]["nam_cases_per_model"]:
        errors.append(f"{label}: NAM row count {len(nn)} != {pre['matrix']['nam_cases_per_model']}")

    expected_nam = {(tag, rate, block)
                    for tag in pre["matrix"]["tags"]
                    for rate in pre["matrix"]["rates"]
                    for block in pre["matrix"]["blocks"]}
    got_nam = {(r["tag"], int(r["rate"]), int(r["block"])) for r in nn}
    if got_nam != expected_nam:
        errors.append(f"{label}: NAM case matrix differs from protocol")

    for r in dn:
        if (total(r, "warm", ALLOC_KINDS) or total(r, "cold", ALLOC_KINDS)
                or total(r, "warm", FREE_KINDS) or total(r, "cold", FREE_KINDS)
                or total(r, "warm", LOCK_KINDS) or total(r, "cold", LOCK_KINDS)):
            errors.append(f"{label}: dry case {r['tag']}/{r['rate']}/{r['block']} is not zero")
    for r in rows:
        try:
            if not math.isfinite(float(r["out_rms"])):
                errors.append(f"{label}: non-finite out_rms")
        except (KeyError, ValueError):
            errors.append(f"{label}: invalid out_rms")

    warm_alloc = sum(total(r, "warm", ALLOC_KINDS) for r in nn)
    warm_free = sum(total(r, "warm", FREE_KINDS) for r in nn)
    warm_lock = sum(total(r, "warm", LOCK_KINDS) for r in nn)
    cold_alloc = sum(total(r, "cold", ALLOC_KINDS) for r in nn)
    overflow = sum(to_int(r, "warm_alloc_overflow") + to_int(r, "warm_lock_overflow")
                   for r in rows)

    clean = (warm_alloc == 0 and warm_free == 0 and warm_lock == 0
             and cold_alloc == 0 and overflow == 0)
    return {
        "variant": variant, "architecture_id": arch["id"], "file": arch["file"],
        "status": "measured-clean" if clean else "measured-findings",
        "nam_warm_alloc_total": warm_alloc, "nam_warm_free_total": warm_free,
        "nam_warm_lock_total": warm_lock, "nam_cold_alloc_total": cold_alloc,
        "capture_overflow": overflow,
        "out_rms": {f"{r['tag']}|{int(r['rate'])}|{int(r['block'])}": float(r["out_rms"])
                    for r in nn},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", required=True)
    ap.add_argument("--predeclared", required=True)
    ap.add_argument("--models-dir", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--summary-md")
    args = ap.parse_args()

    pre = json.load(open(args.predeclared))
    errors = []
    runs = []
    for variant in pre["matrix"]["variants"]:
        for arch in pre["architectures"]:
            run_dir = os.path.join(args.runs, variant, arch["id"])
            rec = check_run(variant, arch, run_dir, pre, args.models_dir, errors)
            if rec is not None:
                runs.append(rec)

    by = {(r["variant"], r["architecture_id"]): r for r in runs}
    expected = pre["expected"]

    # Positive controls: the exact RT-004 warm allocation total.
    for arch_id, variants in expected["positive_models"].items():
        for v in variants:
            r = by.get((v, arch_id))
            if r is None:
                errors.append(f"missing run {v}/{arch_id}")
                continue
            if r["nam_warm_alloc_total"] != expected["positive_warm_alloc_total"]:
                errors.append(f"{v}/{arch_id}: warm alloc {r['nam_warm_alloc_total']} "
                              f"!= positive control {expected['positive_warm_alloc_total']}")
            if r["nam_warm_free_total"] != expected["positive_warm_alloc_total"]:
                errors.append(f"{v}/{arch_id}: warm free {r['nam_warm_free_total']} != positive control")
            if r["nam_warm_lock_total"] != 0:
                errors.append(f"{v}/{arch_id}: positive run has lock ops")

    # Zero expectations.
    for arch_id, variants in expected["zero_models"].items():
        for v in variants:
            r = by.get((v, arch_id))
            if r is None:
                errors.append(f"missing run {v}/{arch_id}")
                continue
            if (r["nam_warm_alloc_total"] != 0 or r["nam_warm_free_total"] != 0
                    or r["nam_warm_lock_total"] != 0 or r["nam_cold_alloc_total"] != 0):
                errors.append(f"{v}/{arch_id}: expected zero, got alloc={r['nam_warm_alloc_total']} "
                              f"free={r['nam_warm_free_total']} lock={r['nam_warm_lock_total']} "
                              f"cold={r['nam_cold_alloc_total']}")
            if r["capture_overflow"] != 0:
                errors.append(f"{v}/{arch_id}: expected clean but capture overflow "
                              f"{r['capture_overflow']}")

    # The new archive must be zero everywhere, independent of the table above.
    for r in runs:
        if r["variant"] == "new" and r["nam_warm_alloc_total"] != 0:
            errors.append(f"new/{r['architecture_id']}: repaired archive allocates")

    # The repaired archive must not change the processor output level: every NAM
    # case's mean warm out_rms must equal the other two archives' value.
    for arch in pre["architectures"]:
        ref = by.get(("new", arch["id"]))
        if ref is None:
            continue
        for v in ("original", "lstm_only"):
            other = by.get((v, arch["id"]))
            if other is None:
                continue
            for key, val in ref["out_rms"].items():
                if abs(val - other["out_rms"][key]) > 1.0e-6:
                    errors.append(f"{arch['id']}: out_rms differs new vs {v} at {key}: "
                                  f"{val} vs {other['out_rms'][key]}")

    result = {"checks_errors": errors, "runs": runs, "all_pass": not errors}
    json.dump(result, open(args.out_json, "w"), indent=2)

    if args.summary_md:
        with open(args.summary_md, "w") as f:
            f.write("| variant | architecture | status | warm alloc | warm free | warm lock | cold alloc |\n")
            f.write("|---|---|---|---|---|---|---|\n")
            for r in runs:
                f.write(f"| {r['variant']} | {r['architecture_id']} | {r['status']} | "
                        f"{r['nam_warm_alloc_total']} | {r['nam_warm_free_total']} | "
                        f"{r['nam_warm_lock_total']} | {r['nam_cold_alloc_total']} |\n")

    for r in runs:
        print(f"  {r['variant']:9s} {r['architecture_id']:22s} {r['status']:16s} "
              f"warm_alloc={r['nam_warm_alloc_total']:8d} free={r['nam_warm_free_total']:8d} "
              f"lock={r['nam_warm_lock_total']}")
    if errors:
        for e in errors:
            print(f"  ERROR  {e}", file=sys.stderr)
    print(f"RT-005 activation probe verification: {'PASS' if result['all_pass'] else 'FAIL'} "
          f"({len(runs)} runs, {len(errors)} errors)")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
