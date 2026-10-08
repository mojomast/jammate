#!/usr/bin/env python3
"""Build the per-core-fixture acquisition reason tables from the diagnostic
artifacts (TRACK-004).

Reads <artifact>/<backend>/block<B>/fixtures.csv and the bounded silence-gate
experiment fixtures, joins them by fixture name, and writes:

    <artifact>/core-reasons.csv   machine table
    <artifact>/core-reasons.md    same table for the research note

Only the 11 SPEC 19 core fixtures are emitted. Missing evidence stays missing
(an empty cell), never zero.

    tools/tracker-diagnostics/make_reason_table.py [artifact-dir]
"""
import csv
import os
import sys

ARTIFACT = sys.argv[1] if len(sys.argv) > 1 else "docs/research/tracker-acquisition"
BLOCKS = [128, 512]


def read_fixtures(path):
    if not os.path.exists(path):
        return {}
    with open(path, newline="", encoding="utf-8") as fh:
        return {r["name"]: r for r in csv.DictReader(fh)}


def read_experiment(name):
    path = os.path.join(ARTIFACT, "experiments", "silence-gate-off", name, "fixtures.csv")
    return read_fixtures(path)


def main() -> int:
    rows = []
    for backend in ("btrack", "aubio"):
        by_block = {b: read_fixtures(os.path.join(ARTIFACT, backend, f"block{b}", "fixtures.csv"))
                    for b in BLOCKS}
        off = read_experiment(f"{backend}-block128")
        names = sorted({n for b in BLOCKS for n in by_block[b]
                        if by_block[b][n].get("core") == "1"})
        for name in names:
            base = by_block[128].get(name, {})
            for block in BLOCKS:
                r = by_block[block].get(name)
                if not r:
                    continue
                ro = off.get(name, {})
                rows.append({
                    "backend": backend,
                    "blockFrames": block,
                    "fixture": name,
                    "acquired": r["scorerAcquired"],
                    "acquisitionBars": r["scorerAcqBars"],
                    "lockStartEventSeconds": r["lockStartEvent"],
                    "lockConfirmEventSeconds": r["lockConfirmEvent"],
                    "lockConfirmAvailabilitySeconds": r["lockConfirmAvail"],
                    "longestMatchRun": r["longestMatchRun"],
                    "longestTempoRun": r["longestTempoRun"],
                    "clauseAgreeing": r.get("clauseAgreeing", ""),
                    "clauseOutsideBand": r.get("clauseOutsideBand", ""),
                    "clauseMissingSample": r.get("clauseMissingSample", ""),
                    "clausePhaseInvalid": r.get("clausePhaseInvalid", ""),
                    "clauseBpmInvalid": r.get("clauseBpmInvalid", ""),
                    "medianBpm": r["medianBpm"],
                    "medianBpmError": r["medianBpmError"],
                    "ratioToTruth": r["ratioToTruth"],
                    "reason": r["reason"],
                    "silenceOffAcquired": ro.get("scorerAcquired", ""),
                    "silenceOffAcquisitionBars": ro.get("scorerAcqBars", ""),
                })

    header = ["backend", "blockFrames", "fixture", "acquired", "acquisitionBars",
              "lockStartEventSeconds", "lockConfirmEventSeconds",
              "lockConfirmAvailabilitySeconds", "longestMatchRun", "longestTempoRun",
              "clauseAgreeing", "clauseOutsideBand", "clauseMissingSample",
              "clausePhaseInvalid", "clauseBpmInvalid",
              "medianBpm", "medianBpmError", "ratioToTruth", "reason",
              "silenceOffAcquired", "silenceOffAcquisitionBars"]
    csv_path = os.path.join(ARTIFACT, "core-reasons.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=header, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)

    lines = ["# Per-core-fixture acquisition reasons (TRACK-004)", "",
             "`silenceOff*` columns are the bounded adapter-config experiment "
             "(gate disabled); they are NOT default evidence. Empty cells are "
             "missing measurements (failed lock, no nominal BPM, no match).", "",
             "Tempo-clause counts are over the longest forward-advancing positional "
             "match run: agreeing / outside-band / missing-sample / phase-invalid / "
             "BPM-invalid. A numeric-band reason is only claimed when that is the "
             "sole failing clause type.", "",
             "| backend | block | fixture | acq | bars | lock start (s) | lock confirm (s) | "
             "confirm avail (s) | match/tempo run | clause a/out/miss/phase/bpm | "
             "median BPM | BPM err | ratio | reason | acq gate-off |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        def f(x):
            try:
                return f"{float(x):.3f}"
            except (TypeError, ValueError):
                return ""
        clause = (f"{r['clauseAgreeing']}/{r['clauseOutsideBand']}/"
                  f"{r['clauseMissingSample']}/{r['clausePhaseInvalid']}/"
                  f"{r['clauseBpmInvalid']}")
        lines.append(
            f"| {r['backend']} | {r['blockFrames']} | {r['fixture']} | {r['acquired']} | "
            f"{f(r['acquisitionBars'])} | {f(r['lockStartEventSeconds'])} | "
            f"{f(r['lockConfirmEventSeconds'])} | {f(r['lockConfirmAvailabilitySeconds'])} | "
            f"{r['longestMatchRun']}/{r['longestTempoRun']} | {clause} | {f(r['medianBpm'])} | "
            f"{f(r['medianBpmError'])} | {f(r['ratioToTruth'])} | {r['reason']} | "
            f"{r['silenceOffAcquired']} |")
    with open(os.path.join(ARTIFACT, "core-reasons.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    print(f"wrote {csv_path} and {os.path.join(ARTIFACT, 'core-reasons.md')} "
          f"({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
