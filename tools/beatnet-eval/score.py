#!/usr/bin/env python3
"""Score a *declared real* BeatNet run against the repository rhythm corpus.

This is a research scorer, not the C++ harness. It refuses any observations
document that does not carry real-run provenance (see ``beatnet_eval.io_contract``).
If no real BeatNet output exists, this script does nothing but fail loudly -- it
never invents beats or scores.

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


def _fixture_observations(doc: dict) -> dict:
    return {fx["name"]: fx for fx in doc["fixtures"]}


def score(doc: dict, manifest: dict, tolerance: float) -> dict:
    io_contract.validate_observations(doc)
    observed = _fixture_observations(doc)
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

    def mean(key):
        vals = [r[key] for r in results if r.get(key) is not None]
        return sum(vals) / len(vals) if vals else None

    aggregate = {
        "fixturesScored": len(results),
        "fMeasureMean": mean("fMeasure"),
        "precisionMean": mean("precision"),
        "recallMean": mean("recall"),
        "bpmRelativeErrorMean": mean("bpmRelativeError"),
        "phaseMeanAbsMs": mean("phaseMeanAbsMs"),
        "availabilityMeanMs": mean("availabilityMeanMs"),
    }
    return {
        "scorer": "tools/beatnet-eval/score.py (research scorer; NOT the C++ G3 harness)",
        "toleranceSeconds": tolerance,
        "provenance": doc["provenance"],
        "availabilitySemantics": doc["availability_semantics"],
        "fixtures": results,
        "aggregate": aggregate,
    }


def _markdown(score_doc: dict) -> str:
    prov = score_doc["provenance"]
    lines = [
        "# BeatNet research score (real run)",
        "",
        f"- source: {prov.get('source')} commit `{prov.get('repo_commit')}` "
        f"model {prov.get('model')} mode `{prov.get('mode')}` / {prov.get('inference_model')}",
        f"- device: {prov.get('device')}; raw output sha256 `{prov.get('raw_output_sha256')}`",
        f"- availability semantics: `{score_doc['availabilitySemantics']}`",
        f"- tolerance: {score_doc['toleranceSeconds']} s",
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
    agg = score_doc["aggregate"]
    lines += [
        "",
        f"Mean F {agg['fMeasureMean']:.4f}, precision {agg['precisionMean']:.4f}, "
        f"recall {agg['recallMean']:.4f} over {agg['fixturesScored']} fixtures.",
        "",
    ]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Score a verified real BeatNet run (research scorer)")
    ap.add_argument("--observations", required=True, help="verified BeatNet observations JSON")
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
        print(f"score.py: refusing input: {exc}", file=sys.stderr)
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
