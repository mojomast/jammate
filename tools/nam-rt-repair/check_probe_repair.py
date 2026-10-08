#!/usr/bin/env python3
"""RT-003 machine-verified check over the captured real-processor probe runs.

Compares the patched-NAM probe artifacts against the pinned-upstream control
run (identical probe binary, identical read-only SharedCode/assets; only
libnam_core.a differs) and asserts:

  * the patched NAM callback cases have zero C/C++ allocations, frees and
    lock/trylock/cond operations (cold and warm);
  * the control NAM cases reproduce the RT-002 positive allocation baseline;
  * both runs' dry/built-in cases are zero-allocation/lock;
  * the built-in drum render (active blocks, voice events, output RMS) is
    unchanged between the two runs;
  * both runs' scene/timer checks pass.

Writes a JSON verdict and exits non-zero on any failure.
"""
import argparse
import csv
import json
import math
import sys

NAM_TAGS = {"nam", "nam+drums"}
DRY_TAGS = {"dry", "dry+drums"}


def load_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def to_int(row, key):
    value = int(row[key])
    if value < 0:
        raise ValueError(f"negative counter {key}")
    return value


def alloc_total(row, prefix):
    return sum(to_int(row, f"{prefix}_{k}") for k in
               ("cxxnew", "cxxnewarr")) + \
        sum(to_int(row, f"{prefix}_{k}") for k in ("malloc", "calloc", "realloc"))


def free_total(row, prefix):
    return sum(to_int(row, f"{prefix}_{k}") for k in
               ("cxxdel", "cxxdelarr", "cxxdelsized", "free"))


def case_key(row):
    return (row["tag"], float(row["rate"]), int(row["block"]))


def expected_cases():
    return {(tag, rate, block) for tag in DRY_TAGS
            for rate in (44100, 48000, 96000) for block in (64, 128, 512)} | \
           {(tag, rate, block) for tag in NAM_TAGS
            for rate in (48000, 96000) for block in (128, 512)}


def lock_total(row, prefix):
    return sum(to_int(row, f"{prefix}_{k}") for k in
               ("lock", "trylock", "cond", "unlock"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--patched-csv", required=True)
    ap.add_argument("--baseline-csv", required=True)
    ap.add_argument("--patched-findings", required=True)
    ap.add_argument("--baseline-findings", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    patched = load_rows(args.patched_csv)
    baseline = load_rows(args.baseline_csv)
    pf = json.load(open(args.patched_findings))
    bf = json.load(open(args.baseline_findings))

    checks = {}
    details = {}

    # --- scene + overall probe checks ----------------------------------------
    scene_keys = {"full_exit_ok", "noaudio_short_exit_ok", "noaudio_long_exit_ok",
                  "selfcheck_pass", "scene_audio_restored",
                  "scene_audio_restore_before_fallback", "scene_audio_dispatch_within_bound",
                  "scene_audio_stop_not_preset", "scene_audio_signal_zero",
                  "scene_audio_editor_null", "scene_noaudio_short_not_restored",
                  "scene_noaudio_long_restored", "scene_timer_attribution_discriminated"}
    for label, data in (("patched", pf), ("baseline", bf)):
        fields = data.get("checks", {})
        checks[f"{label}_scene_checks_pass"] = (set(fields) == scene_keys
                                               and all(v is True for v in fields.values()))
    for label, rows in (("patched", patched), ("baseline", baseline)):
        keys = [case_key(row) for row in rows]
        checks[f"{label}_exact_case_matrix"] = (len(keys) == 26
                                                and len(set(keys)) == 26
                                                and set(keys) == expected_cases())

    # --- NAM callback rows ----------------------------------------------------
    def nam_rows(rows):
        return [r for r in rows if r["tag"] in NAM_TAGS]

    pn, bn = nam_rows(patched), nam_rows(baseline)
    checks["nam_case_count_8"] = len(pn) == 8 and len(bn) == 8
    checks["patched_nam_zero_alloc_free_lock"] = all(
        alloc_total(r, "cold") == 0 and free_total(r, "cold") == 0
        and alloc_total(r, "warm") == 0 and free_total(r, "warm") == 0
        and to_int(r, "warm_alloc_cxx_total") == 0
        and lock_total(r, "cold") == 0 and lock_total(r, "warm") == 0
        for r in pn)
    checks["baseline_nam_positive_alloc"] = all(
        alloc_total(r, "warm") > 0 for r in bn)
    checks["patched_nam_zero_not_merely_unmeasured"] = (
        pf["full"].get("nam_cases_with_alloc") == 0
        and pf["full"].get("nam_cases") == 8)
    checks["baseline_nam_reproduces_rt002_2_per_sample"] = (
        abs(bf["full"].get("nam_alloc_per_host_sample_48000", -1) - 2.0) < 1e-9)

    # --- dry/built-in rows ----------------------------------------------------
    def dry_rows(rows):
        return [r for r in rows if r["tag"] in DRY_TAGS]

    checks["dry_cases_18_both"] = len(dry_rows(patched)) == 18 \
        and len(dry_rows(baseline)) == 18
    checks["patched_dry_zero"] = all(
        alloc_total(r, "cold") == 0 and alloc_total(r, "warm") == 0
        and free_total(r, "cold") == 0 and free_total(r, "warm") == 0
        and lock_total(r, "cold") == 0 and lock_total(r, "warm") == 0
        for r in dry_rows(patched))
    checks["baseline_dry_zero"] = all(
        alloc_total(r, "cold") == 0 and alloc_total(r, "warm") == 0
        and free_total(r, "cold") == 0 and free_total(r, "warm") == 0
        and lock_total(r, "cold") == 0 and lock_total(r, "warm") == 0
        for r in dry_rows(baseline))

    # --- drum render unchanged (active blocks, voices, RMS) -------------------
    def drum_key(r):
        return (float(r["rate"]), int(r["block"]))

    pd = {drum_key(r): r for r in dry_rows(patched) if r["drums"] == "1"}
    bd = {drum_key(r): r for r in dry_rows(baseline) if r["drums"] == "1"}
    rms_ok = True
    for k, r in pd.items():
        b = bd.get(k)
        if b is None:
            rms_ok = False
            break
        if (int(r["drum_active_blocks"]) != 256
                or int(r["drum_active_blocks"]) != int(b["drum_active_blocks"])
                or int(r["voice_events"]) != int(b["voice_events"])
                or not math.isfinite(float(r["out_rms"]))
                or not math.isfinite(float(b["out_rms"]))
                or abs(float(r["out_rms"]) - float(b["out_rms"])) > 1e-6):
            rms_ok = False
            break
    checks["drum_render_identical_and_active"] = rms_ok
    checks["no_lock_wait_regression"] = all(
        lock_total(r, "warm") == 0 for r in patched)

    details["patched_nam_rows"] = [
        {"tag": r["tag"], "rate": r["rate"], "block": r["block"],
         "cold_alloc": alloc_total(r, "cold"), "warm_alloc": alloc_total(r, "warm"),
         "warm_free": to_int(r, "warm_free")} for r in pn]
    details["baseline_nam_rows"] = [
        {"tag": r["tag"], "rate": r["rate"], "block": r["block"],
         "warm_alloc": alloc_total(r, "warm"), "warm_free": to_int(r, "warm_free")}
        for r in bn]

    result = {"checks": checks, "details": details,
              "all_pass": all(checks.values())}
    json.dump(result, open(args.out_json, "w"), indent=2)

    for k, v in checks.items():
        print(f"  {'PASS' if v else 'FAIL'}  {k}")
    print(f"RT-003 probe repair verification: {'PASS' if result['all_pass'] else 'FAIL'}")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
