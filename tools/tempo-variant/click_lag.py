#!/usr/bin/env python3
"""Response-lag summary for the imposed 126 -> 132 tempo step (TRACK-005).

Reads the raw per-block variant method log written by tempo-variant-click
(`--mode step`), keeps the emitted-beat trajectory (compact), and measures how
long after the imposed change the variant's derived BPM first reaches the new
tempo within +/-2 %. All values are causal: the variant only reads intervals of
beats it has already emitted, so the lag is a real availability lag, not a
future-informed one.

    tools/tempo-variant/click_lag.py <raw-step.csv> <rate> <step-seconds> \
        <step-to-bpm> <out-beats.csv> <out-summary.csv>
"""
import csv
import sys


def main() -> int:
    raw, rate, step_from, step_to, step_seconds, out_beats, out_summary = sys.argv[1:8]
    step_seconds = float(step_seconds)
    step_to = float(step_to)
    tol = 0.02 * step_to

    beats = []
    with open(raw, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            if row["beatEvent"] != "1":
                continue
            beats.append(row)

    with open(out_beats, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, lineterminator="\n", fieldnames=["beatIndex", "blockIndex",
                                           "eventSeconds", "availabilitySeconds",
                                           "intervalSeconds", "intervalState",
                                           "ringCount", "ready", "baseBpm",
                                           "variantBpm"])
        w.writeheader()
        for i, r in enumerate(beats):
            w.writerow({
                "beatIndex": i, "blockIndex": r["blockIndex"],
                "eventSeconds": r["eventSeconds"],
                "availabilitySeconds": r["blockEndSeconds"],
                "intervalSeconds": r["intervalSeconds"],
                "intervalState": r["intervalState"], "ringCount": r["ringCount"],
                "ready": r["ready"], "baseBpm": r["baseBpm"],
                "variantBpm": r["variantBpm"],
            })

    def first_within(field):
        hit_time = ""
        hit_beats = ""
        for i, r in enumerate(beats):
            t = float(r["eventSeconds"])
            if t < step_seconds:
                continue
            if abs(float(r[field]) - step_to) <= tol:
                hit_time = t
                break
        if hit_time != "":
            # count beats from the first beat at/after the step
            after = [r for r in beats if float(r["eventSeconds"]) >= step_seconds]
            n = 0
            for r in after:
                n += 1
                if abs(float(r[field]) - step_to) <= tol:
                    break
            hit_beats = n
        return hit_time, hit_beats

    v_time, v_beats = first_within("variantBpm")
    b_time, b_beats = first_within("baseBpm")
    v_lag = "" if v_time == "" else f"{float(v_time) - step_seconds:.6f}"
    b_lag = "" if b_time == "" else f"{float(b_time) - step_seconds:.6f}"

    with open(out_summary, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["rate", "stepSeconds", "stepFromBpm", "stepToBpm",
                    "variantWithin2PctSeconds", "variantLagSeconds",
                    "variantLagBeats", "baseWithin2PctSeconds", "baseLagSeconds",
                    "baseLagBeats", "variantFinalBpm", "baseFinalBpm"])
        w.writerow([
            rate, f"{step_seconds:.6f}",
            f"{float(step_from):.6f}", f"{step_to:.6f}",
            v_time, v_lag, v_beats, b_time, b_lag, b_beats,
            beats[-1]["variantBpm"] if beats else "",
            beats[-1]["baseBpm"] if beats else "",
        ])

    print(f"rate {rate}: variant lag {v_lag} s ({v_beats} beats); "
          f"base {'never' if b_time == '' else b_lag + ' s'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
