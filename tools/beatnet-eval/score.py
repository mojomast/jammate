#!/usr/bin/env python3
"""Score a *declared* BeatNet run against the repository rhythm corpus.

The ``provenance`` block is self-reported metadata. This scorer checks its
format/consistency and separately verifies that each fixture's
``input_wav_sha256`` matches the committed corpus WAV, but it **cannot prove**
that BeatNet produced the beats. Treat every number here as unverified until a
separately executed driver and raw output exist.

It also refuses any observations document whose declared metadata is
incomplete or inconsistent (see ``beatnet_eval.io_contract``). If no BeatNet
output exists, this script does nothing but fail loudly -- it never invents
beats or scores.

Usage:
    python3 tools/beatnet-eval/score.py --observations OBS.json \
        --corpus testdata/rhythm --out /tmp/beatnet-score
"""

from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "beatnet_eval"))
import corpus as corpus_mod  # noqa: E402
import io_contract  # noqa: E402
import metrics  # noqa: E402

SCORER_NOTE = (
    "Research scorer; metrics are separate from and NOT comparable to the C++ "
    "G3 harness (impliedBpm is a median interval, not a locked tempo; no "
    "acquisition metric). Provenance is self-reported metadata, not proof, so "
    "every score is declared and unverified. "
    "No verified BeatNet evidence exists until a driver run and raw output are "
    "independently recorded."
)


def _fixture_observations(doc: dict) -> dict:
    return {fx["name"]: fx for fx in doc["fixtures"]}


def score(doc: dict, manifest: dict, tolerance: float) -> dict:
    io_contract.validate_observations(doc)
    observed = _fixture_observations(doc)
    manifest_names = [fx["name"] for fx in corpus_mod.fixtures(manifest)]
    manifest_set = set(manifest_names)

    results = []
    for fixture in corpus_mod.fixtures(manifest):
        name = fixture["name"]
        if name not in observed:
            continue
        obs = observed[name]
        wav_sha = corpus_mod.fixture_sha256(fixture)
        if obs["input_wav_sha256"] != wav_sha:
            raise io_contract.InputContractError(
                f"fixture {name!r}: input_wav_sha256 does not match the committed corpus "
                f"({obs['input_wav_sha256']} != {wav_sha}); refusing to score a different input")
        predicted = [float(b["time"]) for b in obs.get("beats", [])]
        result = metrics.score_fixture(
            corpus_mod.ground_truth_beats(fixture),
            predicted,
            tolerance,
            nominal=corpus_mod.nominal_bpm(fixture),
            observations=obs.get("beats", []),
        )
        result["name"] = name
        result["core"] = name in corpus_mod.core_names(manifest)
        results.append(result)

    if not results:
        raise io_contract.InputContractError("no fixture in the observations matched the corpus manifest")

    missing = [n for n in manifest_names if n not in observed]
    unknown = [n for n in observed if n not in manifest_set]

    def mean(key):
        vals = [r[key] for r in results if r.get(key) is not None]
        return sum(vals) / len(vals) if vals else None

    aggregate = {
        "fixturesScored": len(results),
        "manifestFixtures": len(manifest_names),
        "missingFixtures": missing,
        "unknownFixtures": unknown,
        "coverageComplete": not missing and not unknown,
        "fMeasureMean": mean("fMeasure"),
        "precisionMean": mean("precision"),
        "recallMean": mean("recall"),
        "bpmRelativeErrorMean": mean("bpmRelativeError"),
        "phaseMeanAbsMs": mean("phaseMeanAbsMs"),
        "availabilityMeanMs": mean("availabilityMeanMs"),
    }
    return {
        "scorer": "tools/beatnet-eval/score.py",
        "note": SCORER_NOTE,
        "provenanceIsSelfReported": True,
        "toleranceSeconds": tolerance,
        "provenance": doc["provenance"],
        "availabilitySemantics": doc["availability_semantics"],
        "fixtures": results,
        "aggregate": aggregate,
    }


def _markdown(score_doc: dict) -> str:
    prov = score_doc["provenance"]
    agg = score_doc["aggregate"]
    lines = [
        "# BeatNet research score (DECLARED run — unverified)",
        "",
        "> The provenance below is self-reported metadata. It is **not** proof that",
        "> BeatNet produced these beats. Interpret the numbers only alongside a",
        "> separately executed driver run and raw output. These metrics are separate",
        "> from and not comparable to the C++ G3 harness (impliedBpm is a median",
        "> interval, not a locked tempo; no acquisition metric).",
        "",
        f"- declared source: {prov.get('source')} commit `{prov.get('repo_commit')}` "
        f"model {prov.get('model')} mode `{prov.get('mode')}` / {prov.get('inference_model')}",
        f"- declared device: {prov.get('device')}; raw output sha256 `{prov.get('raw_output_sha256')}`",
        f"- availability semantics: `{score_doc['availabilitySemantics']}`",
        f"- tolerance: {score_doc['toleranceSeconds']} s",
        "",
        f"**Coverage:** scored {agg['fixturesScored']} of {agg['manifestFixtures']} "
        f"manifest fixtures; coverageComplete={agg['coverageComplete']}.",
        "",
    ]
    if agg["missingFixtures"]:
        lines.append(f"- **missing (in manifest, absent from run):** {', '.join(agg['missingFixtures'])}")
    if agg["unknownFixtures"]:
        lines.append(f"- **unknown (in run, absent from manifest):** {', '.join(agg['unknownFixtures'])}")
    lines += [
        "",
        "| fixture | core | F | P | R | implied BPM | BPM rel err | mean abs phase (ms) | avail mean (ms) |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for r in score_doc["fixtures"]:
        def fmt(v, p=3):
            return "n/a" if v is None else f"{v:.{p}f}"
        lines.append(
            f"| {r['name']} | {'yes' if r['core'] else 'no'} | {fmt(r['fMeasure'])} | "
            f"{fmt(r['precision'])} | {fmt(r['recall'])} | {fmt(r['impliedBpm'],1)} | "
            f"{fmt(r['bpmRelativeError'])} | {fmt(r['phaseMeanAbsMs'],1)} | "
            f"{fmt(r['availabilityMeanMs'],1)} |"
        )
    lines += [
        "",
        f"Mean over scored fixtures only: F {fmt(agg['fMeasureMean'],4)}, "
        f"precision {fmt(agg['precisionMean'],4)}, recall {fmt(agg['recallMean'],4)} "
        f"({agg['fixturesScored']}/{agg['manifestFixtures']} fixtures — missing fixtures are not averaged in).",
        "",
    ]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Score a declared BeatNet run (metadata self-reported)")
    ap.add_argument("--observations", required=True, help="declared BeatNet observations JSON")
    ap.add_argument("--corpus", default="testdata/rhythm", help="corpus directory with manifest.json")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--tolerance", type=float, default=None, help="beat tolerance seconds")
    args = ap.parse_args(argv)

    manifest = corpus_mod.load_manifest(args.corpus)
    tolerance = args.tolerance
    if tolerance is None:
        tolerance = (manifest.get("conventions") or {}).get("beatToleranceSeconds", 0.07)

    with open(args.observations, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    try:
        result = score(doc, manifest, tolerance)
    except io_contract.InputContractError as exc:
        print(f"score.py: input metadata invalid: {exc}", file=sys.stderr)
        return 2

    os.makedirs(args.out, exist_ok=True)
    json_path = os.path.join(args.out, "beatnet-score.json")
    md_path = os.path.join(args.out, "beatnet-score.md")
    with open(json_path, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=2, sort_keys=True)
        fh.write("\n")
    with open(md_path, "w", encoding="utf-8") as fh:
        fh.write(_markdown(result))
    print(f"wrote {json_path} and {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
