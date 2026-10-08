#!/usr/bin/env python3
"""RT-005 fail-closed validator for the activation-repair processor probe runs.

Reads one run directory per (variant, model) produced by
run_activation_probe.sh and asserts the three-archive comparison.

Allocation accounting is authoritative:
  * the warm allocation totals `warm_alloc_cxx_total` / `warm_alloc_c_total`
    fold the nothrow/aligned `new` forms that have no plain columns, so they are
    the authoritative warm zero test; the plain `cxxnew`/`malloc`/... columns are
    only a lower bound and must satisfy `regular <= total` per family;
  * cold has no authoritative total columns, so the cold zero test sums the known
    regular forms and the cold overflow counters;
  * warm and cold free (`cxxdel`, `cxxdelarr`, `cxxdelsized`, `free`) and
    lock (`lock`, `trylock`, `cond`, `unlock`) counters and all four
    alloc/lock overflow counters must be zero for a zero-expected run;
  * `noopfree` (free(NULL)) is deliberately excluded: it is not a heap
    operation and is reported separately by the probe.
A missing authoritative column is a hard failure (a run cannot be clean if the
pinned fields are absent).

The validator also fails closed on any unexpected/stale run directory, model
directory or extra file under the runs tree, and on an inexact case matrix; no
run is silently excluded.

Verdicts:
  * `new` is allocation/free/lock-free on EVERY model;
  * `lstm_only` retains the a2_wavenet_max finding and is zero on lstm;
  * `original` is positive on a2_wavenet_max and lstm;
  * previously clean architectures are zero in all three variants;
  * every dry/built-in case is zero in every run;
  * NAM mean warm out_rms is unchanged across the three archives.
"""
import argparse
import csv
import json
import math
import os
import sys

ALLOC_CXX = ("cxxnew", "cxxnewarr")
ALLOC_C = ("malloc", "calloc", "realloc")
ALLOC_REG = ALLOC_CXX + ALLOC_C
FREE = ("cxxdel", "cxxdelarr", "cxxdelsized", "free")
LOCK = ("lock", "trylock", "cond", "unlock")
OVERFLOW = ("alloc_overflow", "lock_overflow")
NOOP = ("noopfree",)
PREFIXES = ("cold", "warm")
AUTHORITATIVE = ("warm_alloc_cxx_total", "warm_alloc_c_total")
BASE_COLUMNS = ("tag", "rate", "block", "drums", "warm_blocks", "out_rms")
ALLOWED_RUN_FILES = {"cases.csv", "findings.json", "exit-status.txt", "probe.log"}

REQUIRED_STATUS = ("variant", "architecture_id", "model_file", "warm_blocks",
                   "timeout_s", "binary", "binary_sha256", "nam_archive",
                   "nam_archive_sha256", "model_sha256", "exit")
REQUIRED_FINDINGS = ("selfcheck_pass", "args_ok", "dry_cases", "dry_all_zero",
                     "nam_cases", "nam_cases_with_alloc")


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
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k] = v
    return d


def nam_rows(rows):
    return [r for r in rows if r["tag"] in ("nam", "nam+drums")]


def dry_rows(rows):
    return [r for r in rows if r["tag"] in ("dry", "dry+drums")]


def required_columns():
    cols = set(BASE_COLUMNS) | set(AUTHORITATIVE)
    for p in PREFIXES:
        for k in ALLOC_REG + FREE + LOCK + OVERFLOW + NOOP:
            cols.add(f"{p}_{k}")
    return cols


def scan_runs(runs_dir, pre):
    """Fail closed on unexpected/stale dirs or extra files under the runs tree."""
    errors = []
    if not os.path.isdir(runs_dir):
        return [f"runs directory missing: {runs_dir}"]
    variants = set(pre["matrix"]["variants"])
    ids = {a["id"] for a in pre["architectures"]}
    for entry in sorted(os.listdir(runs_dir)):
        p = os.path.join(runs_dir, entry)
        if os.path.isfile(p):
            errors.append(f"unexpected file in runs root: {entry}")
            continue
        if entry not in variants:
            errors.append(f"unexpected variant directory: {entry}")
            continue
        for sub in sorted(os.listdir(p)):
            q = os.path.join(p, sub)
            if not os.path.isdir(q):
                errors.append(f"unexpected file in {entry}: {sub}")
                continue
            if sub not in ids:
                errors.append(f"unexpected model directory: {entry}/{sub}")
                continue
            for f in sorted(os.listdir(q)):
                if f not in ALLOWED_RUN_FILES:
                    errors.append(f"unexpected file: {entry}/{sub}/{f}")
                    continue
                if not os.path.isfile(os.path.join(q, f)):
                    errors.append(f"unexpected non-file: {entry}/{sub}/{f}")
    return errors


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
        with open(csv_path, newline="") as f:
            reader = csv.DictReader(f)
            fields = reader.fieldnames or []
            rows = list(reader)
    except Exception as e:
        errors.append(f"{label}: cannot read CSV: {e}")
        return None

    # A missing authoritative/pinned column is a hard failure: unknown schema
    # must never let a run be reported clean.
    missing = sorted(required_columns() - set(fields))
    if missing:
        errors.append(f"{label}: CSV missing required columns: {missing}")

    try:
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

        # Dry rows must be completely zero, including authoritative warm totals
        # and all cold/warm overflow counters.
        for r in dn:
            if (total(r, "warm", ALLOC_REG) or total(r, "cold", ALLOC_REG)
                    or total(r, "warm", FREE) or total(r, "cold", FREE)
                    or total(r, "warm", LOCK) or total(r, "cold", LOCK)
                    or total(r, "warm", OVERFLOW) or total(r, "cold", OVERFLOW)
                    or to_int(r, "warm_alloc_cxx_total") or to_int(r, "warm_alloc_c_total")):
                errors.append(f"{label}: dry case {r['tag']}/{r['rate']}/{r['block']} is not zero")

        for r in rows:
            if not math.isfinite(float(r["out_rms"])):
                errors.append(f"{label}: non-finite out_rms")

        warm_cxx_reg = sum(to_int(r, k) for r in nn for k in
                           (f"warm_{x}" for x in ALLOC_CXX))
        warm_c_reg = sum(to_int(r, k) for r in nn for k in
                         (f"warm_{x}" for x in ALLOC_C))
        warm_cxx_tot = sum(to_int(r, "warm_alloc_cxx_total") for r in nn)
        warm_c_tot = sum(to_int(r, "warm_alloc_c_total") for r in nn)
        # Lower-bound consistency: nothrow/aligned `new` have no plain column.
        if warm_cxx_reg > warm_cxx_tot:
            errors.append(f"{label}: warm C++ regular {warm_cxx_reg} > authoritative total {warm_cxx_tot}")
        if warm_c_reg > warm_c_tot:
            errors.append(f"{label}: warm C regular {warm_c_reg} > authoritative total {warm_c_tot}")

        acc = {
            "nam_warm_alloc_auth": warm_cxx_tot + warm_c_tot,
            "nam_warm_alloc_cxx_auth": warm_cxx_tot,
            "nam_warm_alloc_c_auth": warm_c_tot,
            "nam_warm_alloc_reg": warm_cxx_reg + warm_c_reg,
            "nam_cold_alloc_reg": sum(total(r, "cold", ALLOC_REG) for r in nn),
            "nam_warm_free": sum(total(r, "warm", FREE) for r in nn),
            "nam_cold_free": sum(total(r, "cold", FREE) for r in nn),
            "nam_warm_lock": sum(total(r, "warm", LOCK) for r in nn),
            "nam_cold_lock": sum(total(r, "cold", LOCK) for r in nn),
            "nam_warm_overflow": sum(total(r, "warm", OVERFLOW) for r in nn),
            "nam_cold_overflow": sum(total(r, "cold", OVERFLOW) for r in nn),
            "nam_warm_noopfree": sum(to_int(r, "warm_noopfree") for r in nn),
            "nam_cold_noopfree": sum(to_int(r, "cold_noopfree") for r in nn),
            "findings_nam_cases_with_alloc": findings.get("nam_cases_with_alloc"),
        }
        clean = (acc["nam_warm_alloc_auth"] == 0 and acc["nam_warm_alloc_reg"] == 0
                 and acc["nam_cold_alloc_reg"] == 0 and acc["nam_warm_free"] == 0
                 and acc["nam_cold_free"] == 0 and acc["nam_warm_lock"] == 0
                 and acc["nam_cold_lock"] == 0 and acc["nam_warm_overflow"] == 0
                 and acc["nam_cold_overflow"] == 0)
        acc["status"] = "measured-clean" if clean else "measured-findings"
        acc["out_rms"] = {f"{r['tag']}|{int(r['rate'])}|{int(r['block'])}":
                          float(r["out_rms"]) for r in nn}
        return acc
    except (KeyError, ValueError) as e:
        errors.append(f"{label}: malformed counter: {e}")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", required=True)
    ap.add_argument("--predeclared", required=True)
    ap.add_argument("--models-dir", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--summary-md")
    args = ap.parse_args()

    pre = json.load(open(args.predeclared))
    errors = scan_runs(args.runs, pre)
    runs = []
    for variant in pre["matrix"]["variants"]:
        for arch in pre["architectures"]:
            run_dir = os.path.join(args.runs, variant, arch["id"])
            rec = check_run(variant, arch, run_dir, pre, args.models_dir, errors)
            if rec is not None:
                rec["variant"] = variant
                rec["architecture_id"] = arch["id"]
                rec["file"] = arch["file"]
                runs.append(rec)

    by = {(r["variant"], r["architecture_id"]): r for r in runs}
    expected = pre["expected"]

    def zero_ok(rec, label):
        bad = []
        for k in ("nam_warm_alloc_auth", "nam_warm_alloc_reg", "nam_cold_alloc_reg",
                  "nam_warm_free", "nam_cold_free", "nam_warm_lock", "nam_cold_lock",
                  "nam_warm_overflow", "nam_cold_overflow"):
            if rec[k] != 0:
                bad.append(f"{k}={rec[k]}")
        if rec["findings_nam_cases_with_alloc"] != 0:
            bad.append(f"findings_nam_cases_with_alloc={rec['findings_nam_cases_with_alloc']}")
        if bad:
            errors.append(f"{label}: expected zero but " + " ".join(bad))

    # Positive controls: exact RT-004 warm allocation total in the authoritative
    # counter; overflow is allowed (and recorded) but the counters stay exact.
    for arch_id, variants in expected["positive_models"].items():
        for v in variants:
            r = by.get((v, arch_id))
            if r is None:
                errors.append(f"missing run {v}/{arch_id}")
                continue
            if r["nam_warm_alloc_auth"] != expected["positive_warm_alloc_total"]:
                errors.append(f"{v}/{arch_id}: authoritative warm alloc "
                              f"{r['nam_warm_alloc_auth']} != positive control "
                              f"{expected['positive_warm_alloc_total']}")
            if r["nam_warm_free"] != expected["positive_warm_alloc_total"]:
                errors.append(f"{v}/{arch_id}: warm free {r['nam_warm_free']} != "
                              f"positive control {expected['positive_warm_alloc_total']}")
            if r["nam_warm_lock"] != 0 or r["nam_cold_lock"] != 0:
                errors.append(f"{v}/{arch_id}: positive run has lock ops")
            if not (isinstance(r["findings_nam_cases_with_alloc"], int)
                    and r["findings_nam_cases_with_alloc"] > 0):
                errors.append(f"{v}/{arch_id}: positive run findings aggregate "
                              f"nam_cases_with_alloc not positive")

    # Zero expectations: authoritative + regular + free + lock + overflow all zero.
    for arch_id, variants in expected["zero_models"].items():
        for v in variants:
            r = by.get((v, arch_id))
            if r is None:
                errors.append(f"missing run {v}/{arch_id}")
                continue
            zero_ok(r, f"{v}/{arch_id}")

    # The new archive must be zero everywhere, independent of the table above.
    for r in runs:
        if r["variant"] == "new" and r["nam_warm_alloc_auth"] != 0:
            errors.append(f"new/{r['architecture_id']}: repaired archive allocates "
                          f"({r['nam_warm_alloc_auth']})")
        if r["variant"] == "new" and r["nam_warm_alloc_reg"] != 0:
            errors.append(f"new/{r['architecture_id']}: repaired regular allocates")

    # The repaired archive must not change the processor output level.
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

    result = {"runs_dir": args.runs, "predeclared": args.predeclared,
              "checks_errors": errors, "runs": runs, "all_pass": not errors}
    json.dump(result, open(args.out_json, "w"), indent=2)

    if args.summary_md:
        with open(args.summary_md, "w") as f:
            f.write("| variant | architecture | status | warm alloc (auth) | warm free | warm lock | cold alloc |\n")
            f.write("|---|---|---|---|---|---|---|\n")
            for r in runs:
                f.write(f"| {r['variant']} | {r['architecture_id']} | {r['status']} | "
                        f"{r['nam_warm_alloc_auth']} | {r['nam_warm_free']} | "
                        f"{r['nam_warm_lock']} | {r['nam_cold_alloc_reg']} |\n")

    for r in runs:
        print(f"  {r['variant']:9s} {r['architecture_id']:22s} {r['status']:16s} "
              f"warm_auth={r['nam_warm_alloc_auth']:8d} (cxx={r['nam_warm_alloc_cxx_auth']:7d} "
              f"c={r['nam_warm_alloc_c_auth']:7d}) free={r['nam_warm_free']:8d} "
              f"lock={r['nam_warm_lock']} cold_alloc={r['nam_cold_alloc_reg']}")
    if errors:
        for e in errors:
            print(f"  ERROR  {e}", file=sys.stderr)
    print(f"RT-005 activation probe verification: {'PASS' if result['all_pass'] else 'FAIL'} "
          f"({len(runs)} runs, {len(errors)} errors)")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
