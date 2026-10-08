#!/usr/bin/env python3
"""Compare a baseline and a tempo-variant tracker-diagnostics run (TRACK-005).

Consumes the UNMODIFIED tracker-diagnostics artefacts (fixtures.csv) plus the
per-fixture metric JSON from tools/tempo-variant/FixtureMetricsDump, and writes
a per-fixture CSV and a Markdown summary. It asserts the emitted beat-event
series is EXACTLY equal between baseline and variant by comparing the sha256 of
every beats/<fixture>.csv, and reports acquisition on the EVENT clock and the
CAUSAL availability clock separately.

    tools/tempo-variant/compare_runs.py <label> <base-dir> <var-dir> \
        <base-metrics.json> <var-metrics.json> <out-csv> <out-md>
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


def load_fixtures(path: str) -> dict:
    with open(path, newline="", encoding="utf-8") as fh:
        return {r["name"]: r for r in csv.DictReader(fh)}


def load_metrics(path: str) -> dict:
    with open(path, encoding="utf-8") as fh:
        return {m["name"]: m for m in json.load(fh)["fixtures"]}


def num(row: dict, key: str) -> str:
    return row.get(key, "")


def main() -> int:
    label, base_dir, var_dir, base_metrics_p, var_metrics_p, out_csv, out_md = sys.argv[1:8]

    base = load_fixtures(os.path.join(base_dir, "fixtures.csv"))
    var = load_fixtures(os.path.join(var_dir, "fixtures.csv"))
    bm = load_metrics(base_metrics_p)
    vm = load_metrics(var_metrics_p)

    beats_base = os.path.join(base_dir, "beats")
    beats_var = os.path.join(var_dir, "beats")
    names = list(base.keys())
    if list(var.keys()) != names:
        print("ERROR: fixture sets differ", file=sys.stderr)
        return 1

    header = ["fixture", "core", "steady",
              "baseAcq", "varAcq", "baseAcqBars", "varAcqBars",
              "baseLockStartEvent", "varLockStartEvent",
              "baseLockConfirmEvent", "varLockConfirmEvent",
              "baseLockConfirmAvail", "varLockConfirmAvail",
              "baseReason", "varReason",
              "baseMedianBpm", "varMedianBpm", "baseBpmErr", "varBpmErr",
              "baseLockedBpm", "varLockedBpm",
              "basePhaseMatched", "varPhaseMatched",
              "basePhaseAbsMs", "varPhaseAbsMs",
              "baseFalseBeatsSilence", "varFalseBeatsSilence",
              "baseSilenceAccelBpm", "varSilenceAccelBpm",
              "beatsShaEqual"]

    rows = []
    beat_ok = True
    for n in names:
        b = base[n]
        v = var[n]
        fbb = os.path.join(beats_base, n + ".csv")
        fbv = os.path.join(beats_var, n + ".csv")
        equal = (os.path.exists(fbb) and os.path.exists(fbv)
                 and sha256(fbb) == sha256(fbv))
        beat_ok = beat_ok and equal
        bmi = bm.get(n, {})
        vmi = vm.get(n, {})
        rows.append({
            "fixture": n, "core": b["core"], "steady": b["steady"],
            "baseAcq": b["scorerAcquired"], "varAcq": v["scorerAcquired"],
            "baseAcqBars": num(b, "scorerAcqBars"), "varAcqBars": num(v, "scorerAcqBars"),
            "baseLockStartEvent": num(b, "lockStartEvent"),
            "varLockStartEvent": num(v, "lockStartEvent"),
            "baseLockConfirmEvent": num(b, "lockConfirmEvent"),
            "varLockConfirmEvent": num(v, "lockConfirmEvent"),
            "baseLockConfirmAvail": num(b, "lockConfirmAvail"),
            "varLockConfirmAvail": num(v, "lockConfirmAvail"),
            "baseReason": b["reason"], "varReason": v["reason"],
            "baseMedianBpm": num(b, "medianBpm"), "varMedianBpm": num(v, "medianBpm"),
            "baseBpmErr": num(b, "medianBpmError"), "varBpmErr": num(v, "medianBpmError"),
            "baseLockedBpm": bmi.get("lockedBpm", ""), "varLockedBpm": vmi.get("lockedBpm", ""),
            "basePhaseMatched": bmi.get("phaseMatchedBeats", ""),
            "varPhaseMatched": vmi.get("phaseMatchedBeats", ""),
            "basePhaseAbsMs": bmi.get("phaseMeanAbsMs", ""),
            "varPhaseAbsMs": vmi.get("phaseMeanAbsMs", ""),
            "baseFalseBeatsSilence": bmi.get("falseBeatsInTrueSilence", ""),
            "varFalseBeatsSilence": vmi.get("falseBeatsInTrueSilence", ""),
            "baseSilenceAccelBpm": bmi.get("maxSilenceTempoIncreaseBpm", ""),
            "varSilenceAccelBpm": vmi.get("maxSilenceTempoIncreaseBpm", ""),
            "beatsShaEqual": "1" if equal else "0",
        })

    with open(out_csv, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=header, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    base_agg = json.load(open(os.path.join(base_dir, "summary.json")))["aggregate"]
    var_agg = json.load(open(os.path.join(var_dir, "summary.json")))["aggregate"]
    keys = ["acquisitionCoreWithin2Bars", "acquisitionCoreEvaluated",
            "acquisitionCorePassFraction", "bpmRelErrorMeanSteady",
            "bpmRelErrorMedianSteady", "bpmRelErrorWorstCore", "fMeasureMean",
            "precisionMean", "recallMean", "halfDoubleErrorRateCore",
            "falseBeatsInTrueSilencePerSecondWorstInformative",
            "maxSilenceTempoIncreaseBpm", "silenceAccelerationEvaluatedFixtures",
            "phaseMeasuredFixtures"]

    flipped = [r["fixture"] for r in rows if r["baseAcq"] != r["varAcq"]]
    gain = [r["fixture"] for r in rows if r["baseAcq"] == "0" and r["varAcq"] == "1"]
    loss = [r["fixture"] for r in rows if r["baseAcq"] == "1" and r["varAcq"] == "0"]

    lines = []
    lines.append(f"# Baseline vs tempo-variant — {label}\n")
    lines.append(f"- beat-event series exact equality (sha256 of every "
                 f"beats/*.csv): **{'YES' if beat_ok else 'NO'}**\n")
    lines.append(f"- acquisition flips: {len(flipped)} "
                 f"(gained {len(gain)}, lost {len(loss)})\n")
    lines.append(f"- gained: {', '.join(gain) if gain else '(none)'}\n")
    lines.append(f"- lost: {', '.join(loss) if loss else '(none)'}\n")
    lines.append("\n## Aggregate (event clock)\n")
    lines.append("| metric | baseline | variant |")
    lines.append("|---|---|---|")
    for k in keys:
        lines.append(f"| {k} | {base_agg.get(k)} | {var_agg.get(k)} |")
    lines.append("\n## Per-fixture acquisition and tempo\n")
    lines.append("| fixture | core | base acq | var acq | base confirm ev/avail | "
                 "var confirm ev/avail | base BPM | var BPM | base reason | var reason |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        lines.append(
            f"| {r['fixture']} | {r['core']} | {r['baseAcq']} | {r['varAcq']} | "
            f"{r['baseLockConfirmEvent']}/{r['baseLockConfirmAvail']} | "
            f"{r['varLockConfirmEvent']}/{r['varLockConfirmAvail']} | "
            f"{r['baseMedianBpm']} | {r['varMedianBpm']} | "
            f"{r['baseReason']} | {r['varReason']} |"
        )
    lines.append("")

    with open(out_md, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines).rstrip("\n") + "\n")

    print(f"{label}: beat equality={'YES' if beat_ok else 'NO'} "
          f"acq {base_agg['acquisitionCoreWithin2Bars']} -> "
          f"{var_agg['acquisitionCoreWithin2Bars']} "
          f"(+{len(gain)}/-{len(loss)})")
    return 0 if beat_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
