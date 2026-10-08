#!/usr/bin/env python3
"""Compare a baseline and a tempo-variant tracker-diagnostics run (TRACK-005).

Consumes the UNMODIFIED tracker-diagnostics artefacts (fixtures.csv) plus the
per-fixture metric JSON from tools/tempo-variant/FixtureMetricsDump.

Corrections over the first revision:
  * every measurement carries its measured flag; an unmeasured value is an EMPTY
    cell, never a default 0 (lockedBpm / phase / silence-acceleration included);
  * all FOUR fixture sets (base/var fixtures + base/var metrics) must agree
    exactly, contain no duplicates and have the expected fixture count, and
    every per-fixture metric record must be present, or the run fails;
  * bpm-error cells are validated non-negative;
  * acquisition is reported BOTH as acquired-anytime and as the SPEC 19
    within-two-bars count, with the late acquisitions distinguished;
  * the beat-event series equality is asserted via the sha256 of every
    beats/<fixture>.csv.

    compare_runs.py <label> <base-dir> <var-dir> <base-metrics.json> \
        <var-metrics.json> <out-csv> <out-md> [expected-count]
"""
import csv
import hashlib
import json
import os
import sys


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_fixtures(path: str):
    with open(path, newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    return rows


def load_metrics(path: str):
    with open(path, encoding="utf-8") as fh:
        return list(json.load(fh)["fixtures"])


def fail(msg: str):
    print("ERROR: " + msg, file=sys.stderr)
    raise SystemExit(1)


def check_names(label, names, expected_names):
    if names != expected_names:
        # preserve duplicates: compare lists exactly, not sets
        fail(f"{label} fixture list differs from the reference order")
    if len(names) != len(set(names)):
        fail(f"{label} fixture list contains duplicates")
    if len(names) != expected:
        fail(f"{label} has {len(names)} fixtures, expected {expected}")


def acquired_within2(row: dict) -> bool:
    if row.get("scorerAcquired") != "1":
        return False
    if row.get("scorerAcqBars", "") == "":
        return False
    return float(row["scorerAcqBars"]) <= 2.0


def acquired_late(row: dict) -> bool:
    return row.get("scorerAcquired") == "1" and not acquired_within2(row)


def num_or_empty(measured: bool, value):
    return value if measured else ""


def main() -> int:
    global expected
    label, base_dir, var_dir, base_metrics_p, var_metrics_p, out_csv, out_md = sys.argv[1:8]
    expected = int(sys.argv[8]) if len(sys.argv) > 8 else 19

    base_rows = load_fixtures(os.path.join(base_dir, "fixtures.csv"))
    var_rows = load_fixtures(os.path.join(var_dir, "fixtures.csv"))
    base_m = load_metrics(base_metrics_p)
    var_m = load_metrics(var_metrics_p)

    base_fx_names = [r["name"] for r in base_rows]
    var_fx_names = [r["name"] for r in var_rows]
    base_m_names = [m["name"] for m in base_m]
    var_m_names = [m["name"] for m in var_m]

    check_names("baseline", base_fx_names, base_fx_names)
    check_names("variant", var_fx_names, base_fx_names)
    check_names("baseline-metrics", base_m_names, base_fx_names)
    check_names("variant-metrics", var_m_names, base_fx_names)

    base = {r["name"]: r for r in base_rows}
    var = {r["name"]: r for r in var_rows}
    bm = {m["name"]: m for m in base_m}
    vm = {m["name"]: m for m in var_m}

    beats_base = os.path.join(base_dir, "beats")
    beats_var = os.path.join(var_dir, "beats")

    header = ["fixture", "core", "steady",
              "baseAcq", "varAcq", "baseAcqWithin2", "varAcqWithin2",
              "baseAcqLate", "varAcqLate",
              "baseAcqBars", "varAcqBars",
              "baseLockStartEvent", "varLockStartEvent",
              "baseLockConfirmEvent", "varLockConfirmEvent",
              "baseLockConfirmAvail", "varLockConfirmAvail",
              "baseReason", "varReason",
              "baseMedianBpmMeasured", "varMedianBpmMeasured",
              "baseMedianBpm", "varMedianBpm",
              "baseBpmErrMeasured", "varBpmErrMeasured",
              "baseBpmErr", "varBpmErr",
              "baseHasBpmLock", "varHasBpmLock", "baseHasNominalBpm", "varHasNominalBpm",
              "baseLockedBpm", "varLockedBpm",
              "basePhaseMeasured", "varPhaseMeasured",
              "basePhaseMatched", "varPhaseMatched",
              "basePhaseAbsMs", "varPhaseAbsMs",
              "baseTrueSilenceMeasured", "varTrueSilenceMeasured",
              "baseFalseBeatsSilence", "varFalseBeatsSilence",
              "baseSilenceAccelMeasured", "varSilenceAccelMeasured",
              "baseSilenceAccelBpm", "varSilenceAccelBpm",
              "beatsShaEqual"]

    rows = []
    beat_ok = True
    for n in base_fx_names:
        b = base[n]
        v = var[n]
        fbb = os.path.join(beats_base, n + ".csv")
        fbv = os.path.join(beats_var, n + ".csv")
        equal = (os.path.exists(fbb) and os.path.exists(fbv)
                 and sha256(fbb) == sha256(fbv))
        beat_ok = beat_ok and equal
        bmi = bm[n]
        vmi = vm[n]

        # Measured flags straight from the harness record.
        b_bpm_m = b.get("bpmMeasured") == "1"
        v_bpm_m = v.get("bpmMeasured") == "1"
        b_errm = b.get("medianBpmErrorMeasured") == "1"
        v_errm = v.get("medianBpmErrorMeasured") == "1"
        b_err = b.get("medianBpmError", "")
        v_err = v.get("medianBpmError", "")
        if b_errm and b_err != "" and float(b_err) < 0:
            fail(f"{n}: baseline bpm error is negative")
        if v_errm and v_err != "" and float(v_err) < 0:
            fail(f"{n}: variant bpm error is negative")

        b_lock_m = bool(bmi.get("hasBpmLock"))
        v_lock_m = bool(vmi.get("hasBpmLock"))
        b_ph_m = bool(bmi.get("phaseMeasured"))
        v_ph_m = bool(vmi.get("phaseMeasured"))
        b_sa_m = bool(bmi.get("silenceAccelerationMeasured"))
        v_sa_m = bool(vmi.get("silenceAccelerationMeasured"))
        b_ts_m = bool(bmi.get("trueSilenceMeasured"))
        v_ts_m = bool(vmi.get("trueSilenceMeasured"))

        rows.append({
            "fixture": n, "core": b["core"], "steady": b["steady"],
            "baseAcq": b["scorerAcquired"], "varAcq": v["scorerAcquired"],
            "baseAcqWithin2": "1" if acquired_within2(b) else "0",
            "varAcqWithin2": "1" if acquired_within2(v) else "0",
            "baseAcqLate": "1" if acquired_late(b) else "0",
            "varAcqLate": "1" if acquired_late(v) else "0",
            "baseAcqBars": b.get("scorerAcqBars", ""), "varAcqBars": v.get("scorerAcqBars", ""),
            "baseLockStartEvent": b.get("lockStartEvent", ""),
            "varLockStartEvent": v.get("lockStartEvent", ""),
            "baseLockConfirmEvent": b.get("lockConfirmEvent", ""),
            "varLockConfirmEvent": v.get("lockConfirmEvent", ""),
            "baseLockConfirmAvail": b.get("lockConfirmAvail", ""),
            "varLockConfirmAvail": v.get("lockConfirmAvail", ""),
            "baseReason": b["reason"], "varReason": v["reason"],
            "baseMedianBpmMeasured": "1" if b_bpm_m else "0",
            "varMedianBpmMeasured": "1" if v_bpm_m else "0",
            "baseMedianBpm": num_or_empty(b_bpm_m, b.get("medianBpm", "")),
            "varMedianBpm": num_or_empty(v_bpm_m, v.get("medianBpm", "")),
            "baseBpmErrMeasured": "1" if b_errm else "0",
            "varBpmErrMeasured": "1" if v_errm else "0",
            "baseBpmErr": num_or_empty(b_errm, b_err),
            "varBpmErr": num_or_empty(v_errm, v_err),
            "baseHasBpmLock": "1" if b_lock_m else "0",
            "varHasBpmLock": "1" if v_lock_m else "0",
            "baseHasNominalBpm": "1" if bmi.get("hasNominalBpm") else "0",
            "varHasNominalBpm": "1" if vmi.get("hasNominalBpm") else "0",
            "baseLockedBpm": num_or_empty(b_lock_m, bmi.get("lockedBpm", "")),
            "varLockedBpm": num_or_empty(v_lock_m, vmi.get("lockedBpm", "")),
            "basePhaseMeasured": "1" if b_ph_m else "0",
            "varPhaseMeasured": "1" if v_ph_m else "0",
            "basePhaseMatched": num_or_empty(b_ph_m, bmi.get("phaseMatchedBeats", "")),
            "varPhaseMatched": num_or_empty(v_ph_m, vmi.get("phaseMatchedBeats", "")),
            "basePhaseAbsMs": num_or_empty(b_ph_m, bmi.get("phaseMeanAbsMs", "")),
            "varPhaseAbsMs": num_or_empty(v_ph_m, vmi.get("phaseMeanAbsMs", "")),
            "baseTrueSilenceMeasured": "1" if b_ts_m else "0",
            "varTrueSilenceMeasured": "1" if v_ts_m else "0",
            "baseFalseBeatsSilence": num_or_empty(b_ts_m, bmi.get("falseBeatsInTrueSilence", "")),
            "varFalseBeatsSilence": num_or_empty(v_ts_m, vmi.get("falseBeatsInTrueSilence", "")),
            "baseSilenceAccelMeasured": "1" if b_sa_m else "0",
            "varSilenceAccelMeasured": "1" if v_sa_m else "0",
            "baseSilenceAccelBpm": num_or_empty(b_sa_m, bmi.get("maxSilenceTempoIncreaseBpm", "")),
            "varSilenceAccelBpm": num_or_empty(v_sa_m, vmi.get("maxSilenceTempoIncreaseBpm", "")),
            "beatsShaEqual": "1" if equal else "0",
        })

    with open(out_csv, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=header, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    base_agg = json.load(open(os.path.join(base_dir, "summary.json")))["aggregate"]
    var_agg = json.load(open(os.path.join(var_dir, "summary.json")))["aggregate"]

    core = [r for r in rows if r["core"] == "1"]
    def count(rs, key):
        return sum(1 for r in rs if r[key] == "1")

    gained = [r["fixture"] for r in rows if r["baseAcq"] == "0" and r["varAcq"] == "1"]
    lost = [r["fixture"] for r in rows if r["baseAcq"] == "1" and r["varAcq"] == "0"]
    core_gained = [r["fixture"] for r in core if r["baseAcqWithin2"] == "0" and r["varAcqWithin2"] == "1"]
    core_lost = [r["fixture"] for r in core if r["baseAcqWithin2"] == "1" and r["varAcqWithin2"] == "0"]

    lines = []
    lines.append(f"# Baseline vs tempo-variant — {label}")
    lines.append("")
    lines.append(f"- beat-event series exact equality (sha256 of every "
                 f"beats/*.csv): **{'YES' if beat_ok else 'NO'}**")
    lines.append(f"- fixtures: {len(rows)} (expected {expected}); "
                 f"acquisition-anytime flips: {len(gained)} gained / {len(lost)} lost")
    lines.append(f"- **SPEC 19 core within 2 bars**: {count(core,'baseAcqWithin2')} -> "
                 f"{count(core,'varAcqWithin2')} of {len(core)} "
                 f"(gained {len(core_gained)}, lost {len(core_lost)})")
    lines.append(f"- all-19 acquisition-anytime: {count(rows,'baseAcq')} -> "
                 f"{count(rows,'varAcq')}")
    lines.append(f"- core gained: {', '.join(core_gained) if core_gained else '(none)'}")
    lines.append(f"- core lost: {', '.join(core_lost) if core_lost else '(none)'}")
    late = [r["fixture"] for r in rows if r["varAcqLate"] == "1"]
    lines.append(f"- variant acquired but LATE (>2 bars): "
                 f"{', '.join(late) if late else '(none)'}")
    lines.append("")
    lines.append("## Aggregate (event clock; from the unmodified CLI summary.json)")
    lines.append("")
    lines.append("| metric | baseline | variant |")
    lines.append("|---|---|---|")
    for k in ("acquisitionCoreWithin2Bars", "acquisitionCoreEvaluated",
              "bpmRelErrorMeanSteady", "bpmRelErrorMedianSteady",
              "bpmRelErrorWorstCore", "fMeasureMean", "precisionMean", "recallMean",
              "halfDoubleErrorRateCore",
              "falseBeatsInTrueSilencePerSecondWorstInformative",
              "maxSilenceTempoIncreaseBpm", "silenceAccelerationEvaluatedFixtures",
              "phaseMeasuredFixtures"):
        lines.append(f"| {k} | {base_agg.get(k)} | {var_agg.get(k)} |")
    lines.append("")
    lines.append("## Per-fixture acquisition (event start / causal confirm availability)")
    lines.append("")
    lines.append("| fixture | core | base acq (w2/late) | var acq (w2/late) | "
                 "base confirm ev/avail | var confirm ev/avail | base reason | var reason |")
    lines.append("|---|---|---|---|---|---|---|---|")
    for r in rows:
        lines.append(
            f"| {r['fixture']} | {r['core']} | "
            f"{r['baseAcq']}({r['baseAcqWithin2']}/{r['baseAcqLate']}) | "
            f"{r['varAcq']}({r['varAcqWithin2']}/{r['varAcqLate']}) | "
            f"{r['baseLockConfirmEvent']}/{r['baseLockConfirmAvail']} | "
            f"{r['varLockConfirmEvent']}/{r['varLockConfirmAvail']} | "
            f"{r['baseReason']} | {r['varReason']} |")
    lines.append("")

    with open(out_md, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines).rstrip("\n") + "\n")

    print(f"{label}: beat equality={'YES' if beat_ok else 'NO'} "
          f"core within2 {count(core,'baseAcqWithin2')}->{count(core,'varAcqWithin2')} "
          f"anytime {count(rows,'baseAcq')}->{count(rows,'varAcq')} "
          f"(+{len(gained)}/-{len(lost)})")
    return 0 if beat_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
