#!/usr/bin/env python3
"""Project the raw per-block TempoVariant method logs into a per-beat history.

The variant plugin writes one raw `instance_<N>.csv` per constructed backend;
the tracker-diagnostics CLI constructs one backend per manifest fixture, in
manifest order. This script maps instance index -> fixture name, keeps the
emitted-beat rows (the method only changes on a beat), and writes:

  <out-csv>       per-beat: fixture, instance, blockIndex, eventSeconds,
                  availabilitySeconds (= block end), intervalSeconds,
                  intervalState, ringCount, ready, baseBpm, variantBpm
  <out-summary>   per-fixture readiness summary

    tools/tempo-variant/extract_method_history.py <raw-dir> <corpus-dir> \
        <out-csv> <out-summary>
"""
import csv
import json
import os
import sys


def main() -> int:
    raw_dir, corpus, out_csv, out_summary = sys.argv[1:5]

    with open(os.path.join(corpus, "manifest.json"), "rb") as fh:
        manifest = json.load(fh)
    names = [fx["name"] for fx in manifest["fixtures"]]

    instances = sorted(
        (int(n[len("instance_"):-len(".csv")]), n)
        for n in os.listdir(raw_dir)
        if n.startswith("instance_") and n.endswith(".csv")
    )
    if len(instances) != len(names):
        print(f"ERROR: {len(instances)} logs for {len(names)} fixtures", file=sys.stderr)
        return 1

    beat_header = ["fixture", "instance", "blockIndex", "eventSeconds",
                   "availabilitySeconds", "intervalMeasured", "intervalSeconds",
                   "intervalState", "ringCount", "ready", "baseBpm", "variantBpm"]
    sum_header = ["fixture", "beats", "accepted", "gapReset", "malformedReset",
                  "outOfOrderReset", "firstReadyEventSeconds", "finalVariantBpm",
                  "finalReady"]

    with open(out_csv, "w", newline="", encoding="utf-8") as beat_fh, \
         open(out_summary, "w", newline="", encoding="utf-8") as sum_fh:
        bw = csv.writer(beat_fh, lineterminator="\n")
        sw = csv.writer(sum_fh, lineterminator="\n")
        bw.writerow(beat_header)
        sw.writerow(sum_header)

        for idx, fname in instances:
            if idx >= len(names):
                print(f"ERROR: log index {idx} outside manifest", file=sys.stderr)
                return 1
            name = names[idx]
            beats = accepted = gap = malformed = outoforder = 0
            first_ready = ""
            final_bpm = ""
            final_ready = ""
            with open(os.path.join(raw_dir, fname), newline="", encoding="utf-8") as fh:
                for row in csv.DictReader(fh):
                    if row["beatEvent"] != "1":
                        continue
                    beats += 1
                    state = row["intervalState"]
                    if state == "accepted":
                        accepted += 1
                    elif state == "gap_reset":
                        gap += 1
                    elif state == "malformed_reset":
                        malformed += 1
                    elif state == "out_of_order_reset":
                        outoforder += 1
                    if row["ready"] == "1" and not first_ready:
                        first_ready = row["eventSeconds"]
                    final_bpm = row["variantBpm"]
                    final_ready = row["ready"]
                    bw.writerow([
                        name, idx, row["blockIndex"], row["eventSeconds"],
                        row["blockEndSeconds"], row["intervalMeasured"],
                        row["intervalSeconds"], state,
                        row["ringCount"], row["ready"], row["baseBpm"],
                        row["variantBpm"],
                    ])
            sw.writerow([name, beats, accepted, gap, malformed, outoforder,
                         first_ready, final_bpm, final_ready])

    print(f"wrote {out_csv} and {out_summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
