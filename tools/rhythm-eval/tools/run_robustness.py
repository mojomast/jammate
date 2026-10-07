#!/usr/bin/env python3
"""EVAL-005 robustness degradation curves over the EVAL-003 derived corpus.

This tool CONSUMES two existing things; it does not change either of them:

  * the timing-corrected `rhythm-eval` CLI (EVAL-004), run at a fixed 128-frame
    analysis block with the uncompensated scoring variant, and
  * the deterministic 24-clip derived corpus and its manifest (EVAL-003).

It emits tidy degradation curves -- one row per
(backend, parent, perturbation, parameter, metric) -- each paired against that
clip's declared `pairedBaseline` (the *same-parent, same-truncation* baseline
clip, so the raw comparison is length-commensurable), together with the raw
difference and an explicit measurement status.

Design rules that are enforced here (not merely documented):

  * Missing stays missing. A BPM lock, a phase match or an acquisition lock
    that the CLI reports as absent is emitted as a missing value (empty in CSV,
    null in JSON), never as a zero and never as a silent pass.
  * No invented gate thresholds. Only the documented SPEC 19 "<= 2 % locked BPM
    relative error" number is used anywhere, and only as a clearly labelled
    per-row BPM diagnostic normalisation, only when the value is measured and
    comparable. There is deliberately no rolled-up overall grade.
  * Noise clips inherit structural `trueSilenceSpans` while an added floor fills
    what used to be near-silent, so silence metrics are NOT assessed from a
    noise clip; the raw numbers are still emitted and a metadata warning is
    recorded.
  * The derived clips are 5.0 s windows. They can compare backends to their own
    paired baseline; they can never establish the SPEC 19 absolute ">= 95 % of
    core fixtures acquire within 2 bars" release gate, and they are not in any
    core denominator. Acquisition is therefore reported per clip only.

Run `--help` for the CLI/plugin options. Typical reproduction (the corrected
main-source build roots; see README for pins):

    python3 tools/rhythm-eval/tools/run_robustness.py \
        --cli /home/mojo/projects/build-EVAL-005/main-cli/rhythm-eval \
        --backend btrack --backend aubio \
        --backend-lib btrack=/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so \
        --backend-lib aubio=/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so \
        --out docs/research/robustness

or, consuming already-produced CLI runs:

    python3 tools/rhythm-eval/tools/run_robustness.py \
        --backend btrack=docs/research/robustness/raw/btrack/block128/results.json \
        --backend aubio=docs/research/robustness/raw/aubio/block128/results.json \
        --out docs/research/robustness
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

SCHEMA_VERSION = 1
SPEC_BPM_REL_ERROR = 0.02  # SPEC.md section 19: locked BPM relative error <= 2 %

# Fixed primary configuration (EVAL-004 comparison.md: the primary run is the
# uncompensated 128-frame run; compensation is offered but never selected here).
PRIMARY_BLOCK_FRAMES = 128
PRIMARY_VARIANT_LABEL = "uncompensated"


# ---------------------------------------------------------------------------
# Metric table
# ---------------------------------------------------------------------------
#
# Each metric names a field in the CLI's per-fixture metrics JSON, a display
# kind, and the flags that make its value *measured*. When those flags are not
# all set the value is missing (empty / null), never zero. `category` marks the
# silence metrics that must not be assessed on a noise clip.

class Metric:
    __slots__ = ("name", "key", "kind", "raw_kind", "requires", "category")

    def __init__(self, name, key, kind, requires=(), category=None, raw_kind=None):
        self.name = name
        self.key = key
        self.kind = kind          # "float" | "int" | "bool" | "str"
        # How to read the source field. Usually the same as `kind`, but
        # `bpm.spec2pctWithin` is a boolean OUTPUT derived from a numeric input:
        # the raw relative error must be preserved until the 2 % comparison, so
        # a value of 0.0004 is not collapsed to 1 by a bool cast first.
        self.raw_kind = raw_kind or kind
        self.requires = tuple(requires)
        self.category = category  # None | "silence" | "syncopation" | "ramp"


METRICS = [
    # detection (always measured when the truth has beats: detectionMeasured)
    Metric("detection.predictedBeats", "predictedBeats", "int", ["detectionMeasured"]),
    Metric("detection.truePositives", "truePositives", "int", ["detectionMeasured"]),
    Metric("detection.falsePositives", "falsePositives", "int", ["detectionMeasured"]),
    Metric("detection.falseNegatives", "falseNegatives", "int", ["detectionMeasured"]),
    Metric("detection.precision", "precision", "float", ["detectionMeasured"]),
    Metric("detection.recall", "recall", "float", ["detectionMeasured"]),
    Metric("detection.fMeasure", "fMeasure", "float", ["detectionMeasured"]),

    # acquisition -- per-clip only, never a release gate over 5 s windows
    Metric("acquisition.acquired", "acquired", "bool"),
    Metric("acquisition.acquisitionBars", "acquisitionBars", "float", ["acquired"]),
    Metric("acquisition.acquisitionSeconds", "acquisitionSeconds", "float", ["acquired"]),
    Metric("acquisition.acquisitionBeats", "acquisitionBeats", "float", ["acquired"]),

    # BPM lock -- a missing lock is missing, never 0
    Metric("bpm.hasBpmLock", "hasBpmLock", "bool"),
    Metric("bpm.lockedBpm", "lockedBpm", "float", ["hasBpmLock"]),
    Metric("bpm.bpmRelativeError", "bpmRelativeError", "float", ["hasBpmLock", "hasNominalBpm"]),
    Metric("bpm.spec2pctWithin", "bpmRelativeError", "bool",
           ["hasBpmLock", "hasNominalBpm", "steady"], raw_kind="float"),
    Metric("bpm.spec2pctNormalized", "bpmRelativeError", "float", ["hasBpmLock", "hasNominalBpm", "steady"]),

    # half/double-time
    Metric("halfDouble.halfTimeLock", "halfTimeLock", "bool", ["hasBpmLock", "hasNominalBpm"]),
    Metric("halfDouble.doubleTimeLock", "doubleTimeLock", "bool", ["hasBpmLock", "hasNominalBpm"]),
    Metric("halfDouble.halfDoubleTimeError", "halfDoubleTimeError", "bool", ["hasBpmLock", "hasNominalBpm"]),

    # silence (blocked on noise clips; NoTrueSilence -> missing)
    Metric("silence.trueSilenceMeasured", "trueSilenceMeasured", "bool"),
    Metric("silence.falseBeatCoverage", "falseBeatCoverage", "str"),
    Metric("silence.falseBeatsInTrueSilence", "falseBeatsInTrueSilence", "int",
           ["trueSilenceMeasured"], "silence"),
    Metric("silence.falseBeatsInTrueSilencePerSecond", "falseBeatsInTrueSilencePerSecond", "float",
           ["trueSilenceMeasured"], "silence"),
    Metric("silence.maxSilenceTempoIncreaseBpm", "maxSilenceTempoIncreaseBpm", "float",
           ["silenceAccelerationMeasured"], "silence"),
    Metric("silence.falseBeatsInUnplayedBeatWindowsPerSecond",
           "falseBeatsInUnplayedBeatWindowsPerSecond", "float"),
    Metric("silence.falseBeatsOffGridInUnplayedBeatWindowsPerSecond",
           "falseBeatsOffGridInUnplayedBeatWindowsPerSecond", "float"),

    # backend timing diagnostics (the proxy for "holdover" behaviour)
    Metric("timing.beatsReportedByBackend", "beatsReportedByBackend", "int", ["timingDiagnosticsMeasured"]),
    Metric("timing.beatsStampAtBlockStart", "beatsStampAtBlockStart", "int", ["timingDiagnosticsMeasured"]),
    Metric("timing.beatsRejectedNonCausal", "beatsRejectedNonCausal", "int", ["timingDiagnosticsMeasured"]),
    Metric("timing.rateMismatchBlocks", "rateMismatchBlocks", "int", ["timingDiagnosticsMeasured"]),
    Metric("timing.causalAvailabilityMeanSeconds", "causalAvailabilityMeanSeconds", "float",
           ["timingDiagnosticsMeasured"]),
    Metric("timing.causalAvailabilityMaxSeconds", "causalAvailabilityMaxSeconds", "float",
           ["timingDiagnosticsMeasured"]),

    # phase -- undefined when no predicted beat matched
    Metric("phase.phaseMeasured", "phaseMeasured", "bool"),
    Metric("phase.phaseMeanMs", "phaseMeanMs", "float", ["phaseMeasured"]),
    Metric("phase.phaseMeanAbsMs", "phaseMeanAbsMs", "float", ["phaseMeasured"]),
    Metric("phase.phaseP50AbsMs", "phaseP50AbsMs", "float", ["phaseMeasured"]),
    Metric("phase.phaseP95AbsMs", "phaseP95AbsMs", "float", ["phaseMeasured"]),

    # ramps -- raw proxy only; the SPEC continuity gate is NOT-MEASURED offline
    Metric("ramp.hasRamp", "hasRamp", "bool"),
    Metric("ramp.rampLocalTempoRelErrorMean", "rampLocalTempoRelErrorMean", "float",
           ["hasRamp"], "ramp"),
    Metric("ramp.rampLocalTempoRelErrorWorst", "rampLocalTempoRelErrorWorst", "float",
           ["hasRamp"], "ramp"),

    # syncopation -- raw proxy only; the SPEC isolated-event gate is NOT-MEASURED
    Metric("syncopation.hasSyncopation", "hasSyncopation", "bool"),
    Metric("syncopation.syncopationMaxStepFraction", "syncopationMaxStepFraction", "float",
           ["hasSyncopation"], "syncopation"),
    Metric("syncopation.syncopationMaxDeviationFraction", "syncopationMaxDeviationFraction", "float",
           ["hasSyncopation"], "syncopation"),

    # resources -- raw numbers, no invented normalisation
    Metric("resources.cpuSeconds", "cpuSeconds", "float"),
    Metric("resources.allocationCount", "allocationCount", "int"),
]

METRIC_BY_NAME = {m.name: m for m in METRICS}

# Order perturbations appear in when grouping curves (manifest vocabulary order).
PERTURBATION_ORDER = [
    "baseline", "noise", "level", "clipping", "onset_offset", "leading_silence",
    "trailing_silence", "silence_gap", "drop_onset", "syncopation_burst", "tempo_step",
]

# Parameter key inside `transformation` for each perturbation kind.
PARAM_KEY = {
    "baseline": None,
    "noise": "snrDb",
    "level": "gainDb",
    "clipping": "thresholdFraction",
    "onset_offset": "offsetMs",
    "leading_silence": "silenceSeconds",
    "trailing_silence": "silenceSeconds",
    "silence_gap": "gapSeconds",
    "drop_onset": "everyKth",
    "syncopation_burst": "burstCount",
    "tempo_step": "ratio",
}

# Perturbations whose audio timeline is not length/content-commensurable with the
# paired baseline. The comparison is still emitted but carries an explicit
# caveat, because the raw numbers are measured over a different window.
CAVEAT_BY_KIND = {
    "noise": (
        "trueSilenceSpans is STRUCTURAL only: an added noise floor fills what was "
        "near-silent. Silence metrics are NOT assessed from this clip; raw counts "
        "are shown uncaveated only as counts."
    ),
    "tempo_step": (
        "Duration differs from the paired baseline (tape-speed warp). Detection "
        "counts, acquisition and silence occupancy are NOT length-commensurable; "
        "compare tempo/phase within the clip."
    ),
    "onset_offset": (
        "Content window is shifted by a real sample delay (length preserved). "
        "Absolute detection counts are not commensurable in the same sample window."
    ),
    "leading_silence": (
        "Content window is prepended with silence (length +0.5 s). Absolute "
        "detection counts and acquisition time are not commensurable with the baseline."
    ),
    "trailing_silence": (
        "Duration is +0.5 s longer (silence appended). Absolute counts are not "
        "length-commensurable; detection is over a longer window."
    ),
    "silence_gap": (
        "Edit-onsets perturbation. SPEC 19 'silence does not create false "
        "acceleration' is NOT-MEASURED offline; the tempo proxy is raw only, not a clock gate."
    ),
    "syncopation_burst": (
        "Edit-onsets perturbation. SPEC 19 'no tempo jump from one isolated "
        "syncopated event' is NOT-MEASURED offline; the step proxy is raw only, not a clock gate."
    ),
    "drop_onset": (
        "Edit-onsets perturbation: the metric grid is unchanged but as-played "
        "onsets are removed, so recall/F degrade against an unchanged beat truth."
    ),
}

GLOBAL_CAVEATS = [
    "Derived clips are 5.0 s windows. They can compare a backend to its own paired "
    "baseline; they CANNOT establish the SPEC 19 absolute '>= 95 % of core fixtures "
    "acquire within 2 bars' release gate, and derived clips are not in any core "
    "denominator (they carry 'core_parent', not 'core'). Acquisition is per-clip only.",
    "No gate threshold is invented for detection, CPU, silence or ramp. Only the "
    "documented SPEC 19 locked-BPM <= 2 % number appears, as a labelled per-row "
    "BPM diagnostic normalisation; there is no rolled-up overall grade.",
    "Syncopation-stability and holdover/ramp behaviour are reported as raw backend "
    "proxies only; they are NOT clock gates offline (the Musical Clock/audition is "
    "not in this harness).",
]


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def num_token(value) -> str:
    """Stable human/CSV text for a numeric parameter value."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        if value == int(value):
            return str(int(value))
        return ("%g" % value)
    return str(value)


def round9(value):
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    return round(float(value), 9)


def json_num(value):
    """Token used identically in JSON and CSV so the two never disagree."""
    if value is None:
        return None
    return json.dumps(round9(value))


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def die(message: str) -> "None":
    print("run_robustness: " + message, file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------------------
# Manifest metadata + pairing validation
# ---------------------------------------------------------------------------

def parameter_token(transformation: dict) -> str:
    kind = transformation.get("kind", "unknown")
    key = PARAM_KEY.get(kind)
    if key is None:
        # baseline: the parameter is the truncation window, if present.
        return "window=5.0s"
    value = transformation.get(key)
    if value is None:
        return key + "=?"
    return "%s=%s" % (key, num_token(value))


# Source-window fields that define "the same truncation". The OUTPUT length
# (`signal.frames` / `durationSeconds`) may legitimately differ for a warp or a
# silence pad; the SOURCE window a clip was cut from must not.
SOURCE_TRUNCATION_FIELDS = ("sourceStartFrame", "sourceEndFrame",
                            "sourceStartSeconds", "sourceEndSeconds")


def build_metadata(manifest: dict):
    fixtures = manifest.get("fixtures", [])
    errors = []

    by_name = {}
    for f in fixtures:
        name = f.get("name")
        if not name:
            errors.append("fixture without a name")
            continue
        if name in by_name:
            errors.append("duplicate fixture name in manifest: %s" % name)
            continue
        by_name[name] = f

    parent_of = {}
    baseline_of = {}
    meta = {}

    for f in fixtures:
        name = f.get("name")
        if not name or name not in by_name:
            continue
        parent = f.get("parentFixture")
        pair = f.get("pairedBaseline")
        if not parent:
            errors.append("fixture %s has no parentFixture" % name)
        if not pair:
            errors.append("fixture %s has no pairedBaseline" % name)
            continue
        if pair not in by_name:
            errors.append("fixture %s pairedBaseline %s not found" % (name, pair))
            continue
        pair_fixture = by_name[pair]
        if pair_fixture.get("parentFixture") != parent:
            errors.append(
                "MISMATCHED BASELINE: %s parent=%s but pairedBaseline %s parent=%s"
                % (name, parent, pair, pair_fixture.get("parentFixture")))
        pair_kind = pair_fixture.get("transformation", {}).get("kind")
        if pair_kind != "baseline":
            errors.append(
                "fixture %s pairedBaseline %s is not a baseline (kind=%s)"
                % (name, pair, pair_kind))
        baseline_of[name] = pair
        meta[name] = {
            "parent": parent,
            "pairedBaseline": pair,
            "kind": f.get("transformation", {}).get("kind", "unknown"),
            "param": parameter_token(f.get("transformation", {})),
            "tags": f.get("scenarioTags", []),
            "durationSeconds": f.get("durationSeconds"),
            "outputFrames": (f.get("signal") or {}).get("frames"),
            "sourceTruncation": {
                key: (f.get("truncation") or {}).get(key)
                for key in SOURCE_TRUNCATION_FIELDS
            },
            "trueSilenceSpans": f.get("trueSilenceSpans", []),
        }

    # Every parent must have exactly one baseline, and every baseline must pair
    # only with itself.
    baselines_by_parent = {}
    for name, m in meta.items():
        if m["kind"] == "baseline":
            baselines_by_parent.setdefault(m["parent"], []).append(name)
    for parent, bases in baselines_by_parent.items():
        if len(bases) != 1:
            errors.append("parent %s has %d baseline clips (expected 1): %s"
                          % (parent, len(bases), bases))
    for name, m in meta.items():
        if m["kind"] == "baseline" and m["pairedBaseline"] != name:
            errors.append("baseline %s does not pair with itself (%s)"
                          % (name, m["pairedBaseline"]))
        if m["kind"] != "baseline" and m["pairedBaseline"] == name:
            errors.append("non-baseline %s pairs with itself" % name)

    # SAME SOURCE TRUNCATION: a clip may only be paired against a baseline cut
    # from exactly the same source window. Output length may differ (warp/pad),
    # but the source window must not.
    for name, m in meta.items():
        if m["kind"] == "baseline":
            continue
        base = meta.get(m["pairedBaseline"])
        if base is None:
            continue
        src = m["sourceTruncation"]
        bsrc = base["sourceTruncation"]
        missing = [k for k in SOURCE_TRUNCATION_FIELDS
                   if src.get(k) is None or bsrc.get(k) is None]
        if missing:
            errors.append(
                "fixture %s lacks source truncation fields %s (cannot prove the "
                "paired baseline is the same window)" % (name, missing))
            continue
        if src != bsrc:
            errors.append(
                "SOURCE TRUNCATION MISMATCH: %s window %s != paired baseline %s "
                "window %s" % (name, src, m["pairedBaseline"], bsrc))

    return by_name, meta, errors


# ---------------------------------------------------------------------------
# Backend results
# ---------------------------------------------------------------------------

def read_backend_results(path: Path):
    """Returns (backend_id, block_frames, variant_label, fixtures_by_name, errors)."""
    data = load_json(path)
    errors = []
    variant_label = PRIMARY_VARIANT_LABEL
    fixtures = data.get("fixtures")
    variants = data.get("variants") or []
    if isinstance(variants, list) and variants:
        # Prefer the uncompensated variant explicitly.
        chosen = None
        for v in variants:
            if v.get("label") == PRIMARY_VARIANT_LABEL:
                chosen = v
                break
        if chosen is None:
            errors.append("results %s has no '%s' variant" % (path, PRIMARY_VARIANT_LABEL))
            chosen = variants[0]
        variant_label = chosen.get("label", PRIMARY_VARIANT_LABEL)
        fixtures = chosen.get("fixtures", fixtures)

    by_name = {}
    for m in fixtures or []:
        name = m.get("name")
        if name is None:
            errors.append("results %s has a fixture with no name" % path)
            continue
        if name in by_name:
            errors.append("results %s has a duplicate fixture name %s "
                          "(silently collapsing would hide a repeated clip)" % (path, name))
            continue
        by_name[name] = m

    declared_backend = data.get("backend")
    if data.get("legacyBlockStampedBeats"):
        errors.append("results %s were produced with --legacy-block-stamped-beats "
                      "(diagnostic defect mode); not valid evidence" % path)
    if data.get("blockFrames") not in (None, PRIMARY_BLOCK_FRAMES):
        errors.append("results %s blockFrames=%s, expected %d"
                      % (path, data.get("blockFrames"), PRIMARY_BLOCK_FRAMES))

    return {
        "path": str(path),
        "sha256": sha256_file(path),
        "declaredBackend": declared_backend,
        "backend": declared_backend,
        "blockFrames": data.get("blockFrames"),
        "variant": variant_label,
        "fixturesByName": by_name,
        "errors": errors,
    }


# ---------------------------------------------------------------------------
# Metric extraction
# ---------------------------------------------------------------------------

def extract_metric(spec: Metric, m: dict):
    """Returns (value, status, reason). `value` is None when missing.

    The source is read according to `spec.raw_kind`, not `spec.kind`, so a
    numeric input to a derived boolean metric keeps its value until the caller
    compares it (see the `bpm.spec2pctWithin` handling in `build_rows`)."""
    for flag in spec.requires:
        if not m.get(flag, False):
            return None, "missing", "requires " + flag
    raw = m.get(spec.key)
    if raw is None:
        return None, "missing", "field absent"
    if spec.raw_kind == "bool":
        return (1 if raw else 0), "measured", ""
    if spec.raw_kind in ("int", "float"):
        if isinstance(raw, bool):
            raw = 1.0 if raw else 0.0
        return round9(raw), "measured", ""
    return raw, "measured", ""


def baseline_status(name, baseline_name, backend):
    if name not in backend["fixturesByName"]:
        return "missing"
    if baseline_name not in backend["fixturesByName"]:
        return "missing"
    return "measured"


def build_rows(manifest_meta, backends):
    rows = []
    for backend in backends:
        for name in sorted(manifest_meta):
            meta = manifest_meta[name]
            m = backend["fixturesByName"].get(name)
            base_m = backend["fixturesByName"].get(meta["pairedBaseline"])
            caveat = CAVEAT_BY_KIND.get(meta["kind"], "")
            if meta["kind"] == "baseline":
                caveat = ""
            if base_m is None:
                caveat = (caveat + " " if caveat else "") + \
                    "paired baseline missing from backend results"

            for spec in METRICS:
                status = "measured"
                reason = ""
                value = None
                base_value = None
                base_status = "measured"

                if m is None:
                    value, status, reason = None, "missing_fixture", "fixture not in results"
                else:
                    value, status, reason = extract_metric(spec, m)
                    # Special cases that are not a plain flag.
                    if spec.name == "bpm.spec2pctWithin" and value is not None:
                        value = 1 if value <= SPEC_BPM_REL_ERROR else 0
                    if spec.name == "bpm.spec2pctNormalized" and value is not None:
                        value = value / SPEC_BPM_REL_ERROR
                    if (spec.category == "silence"
                            and meta["kind"] == "noise"
                            and status == "measured"):
                        status = "not_assessed_noise"
                        reason = ("noise floor makes structural trueSilenceSpans "
                                  "unusable for a silence assessment")
                    if (spec.category in ("syncopation", "ramp")
                            and status == "measured"):
                        status = "measured_proxy_not_gate"
                        reason = "raw backend proxy; SPEC gate NOT-MEASURED offline"

                if base_m is None:
                    base_status = "missing"
                else:
                    base_value, base_status, base_reason = extract_metric(spec, base_m)
                    if spec.name == "bpm.spec2pctWithin" and base_value is not None:
                        base_value = 1 if base_value <= SPEC_BPM_REL_ERROR else 0
                    if spec.name == "bpm.spec2pctNormalized" and base_value is not None:
                        base_value = base_value / SPEC_BPM_REL_ERROR

                # Difference only when BOTH sides are measured; a missing value
                # never becomes a zero difference.
                difference = None
                comparable = (status == "measured" and base_status == "measured"
                              and value is not None and base_value is not None)
                if comparable and spec.kind != "str":
                    difference = round9(value - base_value)

                row = {
                    "backend": backend["backend"],
                    "parent": meta["parent"],
                    "perturbation": meta["kind"],
                    "param": meta["param"],
                    "fixture": name,
                    "baselineFixture": meta["pairedBaseline"],
                    "metric": spec.name,
                    "kind": spec.kind,
                    "value": value,
                    "baselineValue": base_value,
                    "difference": difference,
                    "status": status,
                    "baselineStatus": base_status,
                    "reason": reason,
                    "caveat": caveat,
                }
                rows.append(row)
    return rows


def deduplicate_check(rows):
    seen = set()
    dupes = []
    for r in rows:
        key = (r["backend"], r["parent"], r["perturbation"], r["param"], r["metric"])
        if key in seen:
            dupes.append(key)
        seen.add(key)
    return dupes


# ---------------------------------------------------------------------------
# Coverage
# ---------------------------------------------------------------------------

def compute_coverage(manifest_names, backends, rows):
    coverage = {}
    for backend in backends:
        present = [n for n in manifest_names if n in backend["fixturesByName"]]
        missing = [n for n in manifest_names if n not in backend["fixturesByName"]]
        backend_rows = [r for r in rows if r["backend"] == backend["backend"]]
        measured = sum(1 for r in backend_rows if r["status"] == "measured")
        proxy = sum(1 for r in backend_rows if r["status"] == "measured_proxy_not_gate")
        not_assessed = sum(1 for r in backend_rows if r["status"] == "not_assessed_noise")
        missing_rows = sum(1 for r in backend_rows
                           if r["status"] not in ("measured", "measured_proxy_not_gate",
                                                   "not_assessed_noise"))
        coverage[backend["backend"]] = {
            "fixtures": len(manifest_names),
            "fixturesPresent": len(present),
            "fixturesMissing": missing,
            "metricRows": len(backend_rows),
            "metricRowsMeasured": measured,
            "metricRowsMissing": missing_rows,
            "metricRowsProxyNotGate": proxy,
            "metricRowsNotAssessedNoise": not_assessed,
        }
    return coverage


# ---------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------

CSV_COLUMNS = [
    "backend", "parent", "perturbation", "param", "fixture", "baselineFixture",
    "metric", "kind", "value", "baselineValue", "difference", "status",
    "baselineStatus", "reason", "caveat",
]


def write_csv(rows, path: Path):
    with path.open("w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(CSV_COLUMNS)
        for r in rows:
            out = []
            for col in CSV_COLUMNS:
                v = r.get(col)
                if v is None:
                    out.append("")
                elif col in ("value", "baselineValue", "difference") and r["kind"] != "str":
                    out.append(json_num(v) if not isinstance(v, str) else v)
                elif isinstance(v, bool):
                    out.append("true" if v else "false")
                else:
                    out.append(v)
            w.writerow(out)


def write_json(document, path: Path):
    with path.open("w", encoding="utf-8") as fh:
        json.dump(document, fh, indent=2, sort_keys=False)
        fh.write("\n")


def fmt_cell(row):
    if row is None:
        return "NA"
    v = row["value"]
    if v is None:
        return "NA"
    if row["kind"] == "bool":
        return "yes" if v else "no"
    if row["kind"] == "str":
        return str(v)
    if isinstance(v, float):
        return ("%.4f" % v).rstrip("0").rstrip(".")
    return str(v)


def write_markdown(document, path: Path):
    md = []
    md.append("# Robustness degradation curves — EVAL-005\n")
    md.append("Both real tracker integrations (BTrack and aubio) driven over the "
              "unchanged EVAL-003 deterministic derived corpus at the primary "
              "uncompensated 128-frame setting. Every clip is paired against its "
              "declared `pairedBaseline` (same parent, same 5.0 s truncation).\n")
    md.append("This is a **diagnostic** document: no tracker is selected, no gate "
              "is tuned, and there is no rolled-up overall grade. G3 stays open.\n")

    g = document["generatedFrom"]
    md.append("## Provenance\n")
    md.append("- Derived manifest: `%s` (sha256 `%s`)" % (g["derivedManifest"], g["derivedManifestSha256"]))
    md.append("- Derived corpus id: `%s`; base manifest sha256 `%s`"
              % (g.get("corpusId"), g.get("baseManifestSha256")))
    for b in g["backends"]:
        md.append("- `%s`: results `%s` (sha256 `%s`); blockFrames %s; variant `%s`"
                  % (b["backend"], b["path"], b["sha256"], b["blockFrames"], b["variant"]))
    md.append("")

    md.append("## Reproduction\n")
    md.append("```bash")
    md.append(g["reproduce"].strip())
    md.append("```\n")

    md.append("## Global caveats\n")
    for c in document["caveats"]:
        md.append("- " + c)
    md.append("")

    if document["metadataWarnings"]:
        md.append("## Metadata warnings\n")
        for w in document["metadataWarnings"]:
            md.append("- " + w)
        md.append("")

    v = document["validation"]
    md.append("## Validation\n")
    md.append("- pairs checked: %d; baselines: %d; hard errors: %d; duplicate rows: %d"
              % (v["pairsChecked"], v["baselines"], len(v["errors"]), len(v["duplicateRows"])))
    for e in v["errors"]:
        md.append("  - ERROR: " + e)
    for w in v["warnings"]:
        md.append("  - warning: " + w)
    md.append("")

    md.append("## Coverage\n")
    md.append("| backend | fixtures present/total | rows measured | rows missing | proxy (not gate) | not assessed (noise) |")
    md.append("|---|---|---|---|---|---|")
    for name, c in document["coverage"].items():
        md.append("| %s | %d/%d | %d | %d | %d | %d |"
                  % (name, c["fixturesPresent"], c["fixtures"], c["metricRowsMeasured"],
                     c["metricRowsMissing"], c["metricRowsProxyNotGate"],
                     c["metricRowsNotAssessedNoise"]))
    md.append("")

    # Curves: per backend, parent, perturbation.
    index = {}
    for r in document["rows"]:
        index.setdefault((r["backend"], r["parent"], r["perturbation"]), {}) \
             .setdefault(r["param"], {})[r["metric"]] = r

    display = [
        ("detection.fMeasure", "F"),
        ("detection.precision", "P"),
        ("detection.recall", "R"),
        ("detection.predictedBeats", "pred"),
        ("acquisition.acquired", "acq?"),
        ("acquisition.acquisitionBars", "acqBars"),
        ("bpm.bpmRelativeError", "BPMerr"),
        ("bpm.hasBpmLock", "bpmLock"),
        ("phase.phaseMeanAbsMs", "phaseAbsMs"),
        ("silence.falseBeatsInTrueSilencePerSecond", "sil/s"),
        ("resources.cpuSeconds", "CPU s"),
        ("resources.allocationCount", "allocs"),
    ]

    md.append("## Degradation curves\n")
    md.append("`NA` means the metric is not measured for that clip (missing lock, "
              "no phase match, no lock, or a blocked silence assessment). It is "
              "never a zero-pass.\n")

    def sort_key(kind):
        try:
            return PERTURBATION_ORDER.index(kind)
        except ValueError:
            return len(PERTURBATION_ORDER)

    for (backend, parent, perturbation) in sorted(
            index, key=lambda t: (t[0], t[1], sort_key(t[2]))):
        md.append("### %s — %s — %s\n" % (backend, parent, perturbation))
        params = sorted(index[(backend, parent, perturbation)], key=_param_sort_key)
        header = "| param | " + " | ".join(label for _, label in display) + " |"
        sep = "|" + "---|" * (len(display) + 1)
        md.append(header)
        md.append(sep)
        for p in params:
            cells = []
            for metric, _ in display:
                cells.append(fmt_cell(index[(backend, parent, perturbation)][p].get(metric)))
            md.append("| %s | %s |" % (p, " | ".join(cells)))
        caveat = ""
        sample = next(iter(index[(backend, parent, perturbation)].values()))
        any_row = next(iter(sample.values()))
        caveat = any_row.get("caveat", "")
        if caveat:
            md.append("")
            md.append("> caveat: " + caveat)
        md.append("")

    md.append("## BPM diagnostic normalisation (documented SPEC 19 <= 2 % only)\n")
    md.append("`bpm.spec2pctNormalized` = `bpmRelativeError / 0.02` (1.0 == at the "
              "documented gate, >1 outside); `bpm.spec2pctWithin` is the boolean. "
              "Both are `NA` when there is no measured, comparable BPM lock. This is "
              "the ONLY normalisation in this file; nothing is rolled up into an "
              "overall grade.\n")

    md.append("## Files\n")
    md.append("- `degradation.csv` — tidy long rows, the authoritative raw record.")
    md.append("- `degradation.json` — same rows plus provenance, coverage and validation.")
    md.append("- `degradation.md` — this file.")
    path.write_text("\n".join(md) + "\n", encoding="utf-8")


def _param_sort_key(param: str):
    # sort by the numeric value after '=' when possible, else lexicographic
    if "=" in param:
        tail = param.split("=", 1)[1]
        try:
            return (0, float(tail), param)
        except ValueError:
            return (1, 0.0, param)
    return (1, 0.0, param)


# ---------------------------------------------------------------------------
# CLI invocation (optional)
# ---------------------------------------------------------------------------

def run_cli(cli: Path, derived_dir: Path, out_dir: Path, backend: str, lib: Path):
    run_dir = out_dir / "raw" / backend / ("block%d" % PRIMARY_BLOCK_FRAMES)
    run_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(cli),
        "--corpus", str(derived_dir),
        "--out", str(run_dir),
        "--backend", backend,
        "--backend-lib", str(lib),
        "--block", str(PRIMARY_BLOCK_FRAMES),
    ]
    print("run_robustness: running " + " ".join(cmd), file=sys.stderr)
    env = dict(os.environ)
    completed = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        die("rhythm-eval failed for backend %s (exit %d)" % (backend, completed.returncode))
    return run_dir / "results.json", cmd


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args(argv):
    p = argparse.ArgumentParser(
        description="EVAL-005 paired robustness degradation curves over the "
                    "EVAL-003 derived corpus.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--derived-manifest",
                   default="testdata/rhythm/derived/manifest.json",
                   help="path to the derived corpus manifest")
    p.add_argument("--backend", action="append", default=[],
                   metavar="NAME[=RESULTS_JSON]",
                   help="backend name, optionally with an existing results.json. "
                        "Repeat for both backends. With --cli, NAME alone runs the CLI.")
    p.add_argument("--backend-lib", action="append", default=[],
                   metavar="NAME=PLUGIN.so",
                   help="dlopen plugin for a backend when --cli is used")
    p.add_argument("--cli", default=None,
                   help="path to the rhythm-eval binary; when given, the tool runs "
                        "it per backend instead of consuming existing results")
    p.add_argument("--out", default="docs/research/robustness",
                   help="output directory")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)

    derived_manifest = Path(args.derived_manifest)
    if not derived_manifest.is_file():
        die("derived manifest not found: %s" % derived_manifest)
    manifest = load_json(derived_manifest)
    manifest_names = [f["name"] for f in manifest.get("fixtures", [])]
    by_name, meta, meta_errors = build_metadata(manifest)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Parse backend specs.
    backend_specs = []
    libs = {}
    for spec in args.backend_lib:
        if "=" not in spec:
            die("--backend-lib must be NAME=PLUGIN.so")
        name, lib = spec.split("=", 1)
        libs[name] = lib
    for spec in args.backend:
        if "=" in spec:
            name, path = spec.split("=", 1)
            backend_specs.append({"name": name, "path": Path(path)})
        else:
            backend_specs.append({"name": spec, "path": None})

    if not backend_specs:
        die("at least one --backend is required")

    reproduce_lines = [
        "# EVAL-005 primary run: uncompensated, %d-frame analysis block" % PRIMARY_BLOCK_FRAMES,
        "export PATH=/tmp/opencode/venv/bin:$PATH",
        "export TMPDIR=/home/mojo/projects/build-EVAL-005/tmp",
    ]

    backends = []
    for spec in backend_specs:
        name = spec["name"]
        path = spec["path"]
        if path is None:
            if args.cli is None:
                die("backend %s has no results path and --cli was not given" % name)
            if name not in libs:
                die("backend %s has no --backend-lib" % name)
            results_path, cmd = run_cli(Path(args.cli), derived_manifest.parent,
                                        out_dir, name, Path(libs[name]))
            reproduce_lines.append(" ".join(cmd))
            path = results_path
        if not path.is_file():
            die("results file not found: %s" % path)
        backend = read_backend_results(path)
        declared = backend.get("declaredBackend")
        if declared and declared != name:
            backend["errors"].append(
                "backend label mismatch: requested --backend %s but results declare "
                "'%s'" % (name, declared))
        backend["backend"] = name
        backends.append(backend)

    rows = build_rows(meta, backends)
    duplicate_rows = deduplicate_check(rows)
    coverage = compute_coverage(manifest_names, backends, rows)

    # Validation.
    errors = list(meta_errors)
    warnings = []
    for backend in backends:
        if backend["errors"]:
            errors.extend("%s: %s" % (backend["backend"], e) for e in backend["errors"])
        missing = [n for n in manifest_names if n not in backend["fixturesByName"]]
        if missing:
            errors.append("%s: %d derived fixtures missing from results: %s"
                          % (backend["backend"], len(missing), ", ".join(missing)))
        extra = [n for n in backend["fixturesByName"] if n not in set(manifest_names)]
        if extra:
            warnings.append("%s: %d results fixtures not in the derived manifest: %s"
                            % (backend["backend"], len(extra), ", ".join(sorted(extra))))
    if duplicate_rows:
        errors.append("duplicate metric rows: %s" % (duplicate_rows[:5],))

    # Noise metadata warning (structural trueSilenceSpans).
    noise_names = [n for n, m in meta.items() if m["kind"] == "noise"]
    metadata_warnings = []
    if noise_names:
        metadata_warnings.append(
            "%d noise clips inherit STRUCTURAL trueSilenceSpans from the parent; "
            "the added floor fills what was near-silent. Their silence metrics are "
            "marked not_assessed_noise and MUST NOT be read as SPEC 19 silence results."
            % len(noise_names))
    metadata_warnings.append(
        "Derived clips are 5.0 s windows and carry 'core_parent', not 'core'; they "
        "are excluded from any SPEC 19 core denominator and cannot establish the "
        "absolute acquisition release gate.")

    # Baseline output-length-commensurability report. Source truncation equality
    # is already enforced as a hard error in build_metadata; a differing OUTPUT
    # length is a legitimate warp/pad and is only warned about.
    for name, m in sorted(meta.items()):
        base = meta.get(m["pairedBaseline"])
        if base is None or m["kind"] == "baseline":
            continue
        bf, pf = base.get("outputFrames"), m.get("outputFrames")
        if bf is not None and pf is not None and bf != pf:
            warnings.append(
                "output length differs from paired baseline: %s %d frames vs "
                "baseline %d (caveat: not length-commensurable)"
                % (name, pf, bf))

    document = {
        "schemaVersion": SCHEMA_VERSION,
        "generatedFrom": {
            "derivedManifest": str(derived_manifest),
            "derivedManifestSha256": sha256_file(derived_manifest),
            "corpusId": (manifest.get("corpus") or {}).get("id"),
            "baseManifestSha256": (manifest.get("corpus") or {}).get("baseManifestSha256"),
            "primaryBlockFrames": PRIMARY_BLOCK_FRAMES,
            "primaryVariant": PRIMARY_VARIANT_LABEL,
            "backends": [
                {"backend": b["backend"], "path": b["path"], "sha256": b["sha256"],
                 "blockFrames": b["blockFrames"], "variant": b["variant"]}
                for b in backends
            ],
            "reproduce": "\n".join(reproduce_lines),
        },
        "caveats": GLOBAL_CAVEATS,
        "metadataWarnings": metadata_warnings,
        "validation": {
            "ok": not errors,
            "errors": errors,
            "warnings": warnings,
            "duplicateRows": duplicate_rows,
            "pairsChecked": len(meta),
            "baselines": sum(1 for m in meta.values() if m["kind"] == "baseline"),
        },
        "coverage": coverage,
        "rows": rows,
    }

    write_csv(rows, out_dir / "degradation.csv")
    write_json(document, out_dir / "degradation.json")
    write_markdown(document, out_dir / "degradation.md")

    # Coverage CSV for convenience.
    with (out_dir / "coverage.csv").open("w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["backend", "fixtures", "fixturesPresent", "fixturesMissing",
                    "metricRows", "metricRowsMeasured", "metricRowsMissing",
                    "metricRowsProxyNotGate", "metricRowsNotAssessedNoise"])
        for name, c in coverage.items():
            w.writerow([name, c["fixtures"], c["fixturesPresent"],
                        len(c["fixturesMissing"]), c["metricRows"],
                        c["metricRowsMeasured"], c["metricRowsMissing"],
                        c["metricRowsProxyNotGate"], c["metricRowsNotAssessedNoise"]])

    print("run_robustness: wrote %s, %s, %s, %s"
          % (out_dir / "degradation.json", out_dir / "degradation.csv",
             out_dir / "degradation.md", out_dir / "coverage.csv"))
    print("run_robustness: %d rows, %d pairs, %d hard errors"
          % (len(rows), len(meta), len(errors)))
    if errors:
        for e in errors:
            print("  ERROR: " + e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
