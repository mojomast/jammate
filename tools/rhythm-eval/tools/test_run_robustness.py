#!/usr/bin/env python3
"""Tests for EVAL-005 `run_robustness.py` (stdlib unittest, no third party).

They cover the aggregation contract independently of any tracker build:

  * pair mapping: every derived clip is paired with its declared same-parent
    `pairedBaseline`, and a mismatched / non-baseline pair is a hard error;
  * missing semantics: a missing BPM lock, phase match or acquisition lock is a
    missing value, never zero and never a silent pass;
  * fixture-missing cases: a backend result that omits a derived clip fails
    validation and is reported in coverage;
  * CSV/JSON consistency: the two serialisations agree value-for-value;
  * the noise rule: silence metrics on a noise clip are not assessed;
  * an ACTUAL run of the script via subprocess over a synthetic corpus, and (when
    a built CLI is present) the same script driving the real derived corpus.

Run:  python3 tools/rhythm-eval/tools/test_run_robustness.py
"""

import csv
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
SCRIPT = REPO / "tools" / "rhythm-eval" / "tools" / "run_robustness.py"


def load_module():
    spec = importlib.util.spec_from_file_location("run_robustness", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


RR = load_module()


# ---------------------------------------------------------------------------
# Synthetic corpus builders
# ---------------------------------------------------------------------------

def metric(name, **over):
    m = {
        "name": name, "core": False, "steady": True, "ramp": False,
        "hasNominalBpm": True, "beatToleranceSeconds": 0.07,
        "predictedBeats": 5, "truthBeats": 10, "detectionMeasured": True,
        "truePositives": 5, "falsePositives": 0, "falseNegatives": 5,
        "precision": 1.0, "recall": 0.5, "fMeasure": 0.6667,
        "acquired": True, "acquisitionSeconds": 0.5, "acquisitionBeats": 1.0,
        "acquisitionBars": 0.25,
        "hasBpmLock": True, "lockedBpm": 125.0, "bpmRelativeError": 0.01,
        "halfTimeLock": False, "doubleTimeLock": False, "halfDoubleTimeError": False,
        "falseBeatsInTrueSilence": 0, "trueSilenceSeconds": 0.3,
        "trueSilenceFractionOfDuration": 0.06, "falseBeatsInTrueSilencePerSecond": 0.0,
        "trueSilenceMeasured": True, "falseBeatCoverage": "Measured",
        "falseBeatMetricInformative": True,
        "silenceAccelerationMeasured": False,
        "silenceAccelerationInsufficientEvidence": True,
        "silenceSpansEvaluated": 0, "silenceSpansInsufficientEvidence": 1,
        "maxSilenceTempoIncreaseBpm": 0.0,
        "falseBeatsInUnplayedBeatWindows": 0, "unplayedBeatWindowsSeconds": 0.0,
        "falseBeatsInUnplayedBeatWindowsPerSecond": 0.0,
        "falseBeatsOffGridInUnplayedBeatWindows": 0,
        "falseBeatsOffGridInUnplayedBeatWindowsPerSecond": 0.0,
        "timingDiagnosticsMeasured": True, "beatsReportedByBackend": 5,
        "beatsStampAtBlockStart": 0, "beatsRejectedNonCausal": 0,
        "rateMismatchBlocks": 0, "causalAvailabilityMeanSeconds": 0.01,
        "causalAvailabilityMaxSeconds": 0.02,
        "hasRecovery": False, "recoverySeconds": 0.0, "recoveryBeats": 0.0,
        "hasSyncopation": False, "syncopationTempoStdDevBpm": 0.0,
        "syncopationTempoCv": 0.0, "syncopationMaxDeviationBpm": 0.0,
        "syncopationMaxDeviationFraction": 0.0, "syncopationMaxStepBpm": 0.0,
        "syncopationMaxStepFraction": 0.0,
        "hasRamp": False, "rampLocalTempoRelErrorMean": 0.0,
        "rampLocalTempoRelErrorWorst": 0.0,
        "phaseMeasured": True, "phaseMatchedBeats": 5, "phaseMeanMs": -2.0,
        "phaseMeanAbsMs": 9.0, "phaseP50AbsMs": 6.0, "phaseP95AbsMs": 20.0,
        "phaseMeanBeats": -0.004, "phaseMeanAbsBeats": 0.018,
        "phaseP95AbsBeats": 0.04,
        "cpuSeconds": 0.01, "allocationCount": 100,
    }
    m.update(over)
    return m


def fixture(name, parent, pair, kind, transformation, frames, spans=None, tags=None):
    return {
        "name": name,
        "parentFixture": parent,
        "pairedBaseline": pair,
        "transformation": transformation,
        "scenarioTags": tags or ["derived", "core_parent"],
        "durationSeconds": 5.0,
        "truncation": {"frames": frames},
        "trueSilenceSpans": spans if spans is not None else [[0.0, 0.35]],
    }


def synthetic_manifest():
    """One parent (clean) with baseline + noise + tempo_step, one parent (funk)
    with baseline + level. Enough to exercise every status path."""
    fixtures = [
        fixture("clean__baseline", "clean", "clean__baseline", "baseline",
                {"kind": "baseline"}, 240000),
        fixture("clean__noise_snr10", "clean", "clean__baseline", "noise",
                {"kind": "noise", "snrDb": 10.0}, 240000),
        fixture("clean__noise_snr0", "clean", "clean__baseline", "noise",
                {"kind": "noise", "snrDb": 0.0}, 240000),
        fixture("clean__tempo_step_1.25", "clean", "clean__baseline", "tempo_step",
                {"kind": "tempo_step", "ratio": 1.25, "outputSeconds": 4.45}, 213645),
        fixture("funk__baseline", "funk", "funk__baseline", "baseline",
                {"kind": "baseline"}, 240000),
        fixture("funk__level_-20db", "funk", "funk__baseline", "level",
                {"kind": "level", "gainDb": -20.0}, 240000),
    ]
    return {
        "schemaVersion": 1,
        "corpus": {"id": "synthetic", "baseManifestSha256": "abc"},
        "fixtures": fixtures,
    }


def results(backend, metrics_by_name):
    return {
        "schemaVersion": 2,
        "backend": backend,
        "blockFrames": 128,
        "legacyBlockStampedBeats": False,
        "variants": [{
            "label": "uncompensated",
            "latencyCompensationSeconds": 0.0,
            "fixtures": list(metrics_by_name.values()),
        }],
    }


# ---------------------------------------------------------------------------
# Unit-level contract tests
# ---------------------------------------------------------------------------

class ParameterTokenTest(unittest.TestCase):
    def test_tokens(self):
        self.assertEqual(RR.parameter_token({"kind": "noise", "snrDb": 20.0}), "snrDb=20")
        self.assertEqual(RR.parameter_token({"kind": "level", "gainDb": -20.0}), "gainDb=-20")
        self.assertEqual(RR.parameter_token({"kind": "clipping", "thresholdFraction": 0.5}),
                         "thresholdFraction=0.5")
        self.assertEqual(RR.parameter_token({"kind": "drop_onset", "everyKth": 2}), "everyKth=2")
        self.assertEqual(RR.parameter_token({"kind": "tempo_step", "ratio": 1.25}), "ratio=1.25")
        self.assertEqual(RR.parameter_token({"kind": "baseline"}), "window=5.0s")


class MetadataPairingTest(unittest.TestCase):
    def test_detects_mismatched_baseline(self):
        m = synthetic_manifest()
        m["fixtures"][1]["pairedBaseline"] = "funk__baseline"  # wrong parent
        _by, _meta, errors = RR.build_metadata(m)
        self.assertTrue(any("MISMATCHED BASELINE" in e for e in errors), errors)

    def test_detects_non_baseline_pair(self):
        m = synthetic_manifest()
        m["fixtures"][1]["pairedBaseline"] = "clean__noise_snr0"  # not a baseline
        _by, _meta, errors = RR.build_metadata(m)
        self.assertTrue(any("is not a baseline" in e for e in errors), errors)

    def test_clean_manifest(self):
        _by, meta, errors = RR.build_metadata(synthetic_manifest())
        self.assertEqual(errors, [])
        self.assertEqual(len(meta), 6)
        self.assertEqual(meta["clean__noise_snr10"]["pairedBaseline"], "clean__baseline")

    def test_detects_duplicate_rows(self):
        row = {"backend": "b", "parent": "p", "perturbation": "noise",
               "param": "snrDb=0", "metric": "detection.fMeasure"}
        dupes = RR.deduplicate_check([row, dict(row)])
        self.assertEqual(len(dupes), 1)


class MissingSemanticsTest(unittest.TestCase):
    def test_missing_bpm_lock_is_missing(self):
        spec = RR.METRIC_BY_NAME["bpm.bpmRelativeError"]
        value, status, _ = RR.extract_metric(spec, metric("x", hasBpmLock=False))
        self.assertIsNone(value)
        self.assertEqual(status, "missing")

    def test_missing_phase_is_missing(self):
        spec = RR.METRIC_BY_NAME["phase.phaseMeanAbsMs"]
        value, status, _ = RR.extract_metric(spec, metric("x", phaseMeasured=False))
        self.assertIsNone(value)
        self.assertEqual(status, "missing")

    def test_missing_acquisition_is_missing(self):
        spec = RR.METRIC_BY_NAME["acquisition.acquisitionBars"]
        value, status, _ = RR.extract_metric(spec, metric("x", acquired=False))
        self.assertIsNone(value)
        self.assertEqual(status, "missing")

    def test_zero_is_not_treated_as_unset(self):
        spec = RR.METRIC_BY_NAME["bpm.bpmRelativeError"]
        value, status, _ = RR.extract_metric(spec, metric("x", bpmRelativeError=0.0))
        self.assertEqual(value, 0.0)
        self.assertEqual(status, "measured")


class BuildRowsTest(unittest.TestCase):
    def _rows(self):
        m = synthetic_manifest()
        _by, meta, errors = RR.build_metadata(m)
        self.assertEqual(errors, [])
        base = metric("clean__baseline")
        noise = metric("clean__noise_snr10", fMeasure=0.4,
                       falseBeatsInTrueSilencePerSecond=1.0)
        backend = {
            "backend": "btrack",
            "fixturesByName": {"clean__baseline": base, "clean__noise_snr10": noise},
        }
        return RR.build_rows(meta, [backend])

    def test_pair_mapping_and_difference(self):
        rows = self._rows()
        f = [r for r in rows if r["fixture"] == "clean__noise_snr10"
             and r["metric"] == "detection.fMeasure"][0]
        self.assertEqual(f["baselineFixture"], "clean__baseline")
        self.assertAlmostEqual(f["difference"], round(0.4 - 0.6667, 9))

    def test_noise_silence_not_assessed_and_no_difference(self):
        rows = self._rows()
        s = [r for r in rows if r["fixture"] == "clean__noise_snr10"
             and r["metric"] == "silence.falseBeatsInTrueSilencePerSecond"][0]
        self.assertEqual(s["status"], "not_assessed_noise")
        self.assertIsNone(s["difference"])

    def test_bpm_normalization_requires_steady_clip(self):
        # a tempo_step clip is not steady; normalisation must be missing.
        spec = RR.METRIC_BY_NAME["bpm.spec2pctNormalized"]
        value, status, _ = RR.extract_metric(spec, metric("x", steady=False))
        self.assertIsNone(value)
        self.assertEqual(status, "missing")


# ---------------------------------------------------------------------------
# End-to-end script run (synthetic, subprocess)
# ---------------------------------------------------------------------------

class ScriptEndToEndTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="eval005-test-"))
        self.manifest = self.tmp / "manifest.json"
        self.manifest.write_text(json.dumps(synthetic_manifest()), encoding="utf-8")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _write_results(self, backend, omit=()):
        m = synthetic_manifest()
        by_name = {}
        for f in m["fixtures"]:
            if f["name"] in omit:
                continue
            # make the perturbed fMeasure differ from baseline
            fm = 0.6667 if f["transformation"]["kind"] == "baseline" else 0.4
            by_name[f["name"]] = metric(f["name"], fMeasure=fm)
        path = self.tmp / ("%s-results.json" % backend)
        path.write_text(json.dumps(results(backend, by_name)), encoding="utf-8")
        return path

    def _run(self, btrack_path, aubio_path, out):
        cmd = [sys.executable, str(SCRIPT),
               "--derived-manifest", str(self.manifest),
               "--backend", "btrack=%s" % btrack_path,
               "--backend", "aubio=%s" % aubio_path,
               "--out", str(out)]
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_real_run_and_csv_json_consistency(self):
        b = self._write_results("btrack")
        a = self._write_results("aubio")
        out = self.tmp / "out"
        completed = self._run(b, a, out)
        self.assertEqual(completed.returncode, 0, completed.stderr)

        for name in ("degradation.json", "degradation.csv", "degradation.md", "coverage.csv"):
            self.assertTrue((out / name).is_file(), name)

        doc = json.loads((out / "degradation.json").read_text())
        self.assertTrue(doc["validation"]["ok"], doc["validation"]["errors"])
        self.assertEqual(doc["validation"]["duplicateRows"], [])

        # CSV and JSON agree value-for-value.
        csv_rows = {}
        with (out / "degradation.csv").open() as fh:
            for r in csv.DictReader(fh):
                key = (r["backend"], r["parent"], r["perturbation"], r["param"], r["metric"])
                csv_rows[key] = r
        self.assertEqual(len(csv_rows), len(doc["rows"]))
        for r in doc["rows"]:
            key = (r["backend"], r["parent"], r["perturbation"], r["param"], r["metric"])
            cr = csv_rows[key]
            for col, jcol in (("value", "value"), ("baselineValue", "baselineValue"),
                              ("difference", "difference")):
                cval = cr[col]
                jval = r[jcol]
                if jval is None:
                    self.assertEqual(cval, "", (key, col))
                elif isinstance(jval, str):
                    self.assertEqual(cval, jval, (key, col))
                else:
                    self.assertAlmostEqual(float(cval), float(jval), places=9, msg=(key, col))

        # Every declared baseline is one of the two baselines.
        for r in doc["rows"]:
            self.assertTrue(r["baselineFixture"].endswith("__baseline"),
                            r["baselineFixture"])

        # Coverage present for both backends.
        self.assertEqual(set(doc["coverage"]), {"btrack", "aubio"})
        for c in doc["coverage"].values():
            self.assertEqual(c["fixturesPresent"], 6)

    def test_missing_fixture_is_a_hard_error(self):
        b = self._write_results("btrack", omit=("clean__noise_snr0",))
        a = self._write_results("aubio")
        out = self.tmp / "out-missing"
        completed = self._run(b, a, out)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("missing", completed.stderr.lower())
        doc = json.loads((out / "degradation.json").read_text())
        self.assertFalse(doc["validation"]["ok"])
        self.assertTrue(any("clean__noise_snr0" in e for e in doc["validation"]["errors"]))

    def test_mismatched_baseline_is_a_hard_error(self):
        m = synthetic_manifest()
        m["fixtures"][1]["pairedBaseline"] = "funk__baseline"
        self.manifest.write_text(json.dumps(m), encoding="utf-8")
        b = self._write_results("btrack")
        a = self._write_results("aubio")
        out = self.tmp / "out-mismatch"
        completed = self._run(b, a, out)
        self.assertNotEqual(completed.returncode, 0)
        doc = json.loads((out / "degradation.json").read_text())
        self.assertTrue(any("MISMATCHED BASELINE" in e for e in doc["validation"]["errors"]))


# ---------------------------------------------------------------------------
# Optional: the real CLI + real derived corpus
# ---------------------------------------------------------------------------

def _find_real_env():
    cli = Path(os.environ.get("EVAL005_CLI",
                              "/home/mojo/projects/build-EVAL-005/cli/rhythm-eval"))
    core = Path(os.environ.get("EVAL005_CORE",
                               "/home/mojo/projects/build-EVAL-005/core"))
    derived = REPO / "testdata" / "rhythm" / "derived" / "manifest.json"
    btrack = core / "librhythm-eval-btrack.so"
    aubio = core / "librhythm-eval-aubio.so"
    if all(p.exists() for p in (cli, btrack, aubio, derived)):
        return cli, btrack, aubio, derived
    return None


@unittest.skipUnless(_find_real_env(), "real EVAL-005 CLI + plugins not built")
class RealCorpusRunTest(unittest.TestCase):
    def test_script_drives_real_cli_over_derived_corpus(self):
        cli, btrack, aubio, derived = _find_real_env()
        with tempfile.TemporaryDirectory(prefix="eval005-real-") as tmp:
            out = Path(tmp) / "out"
            cmd = [sys.executable, str(SCRIPT),
                   "--derived-manifest", str(derived),
                   "--cli", str(cli),
                   "--backend", "btrack", "--backend-lib", "btrack=%s" % btrack,
                   "--backend", "aubio", "--backend-lib", "aubio=%s" % aubio,
                   "--out", str(out)]
            completed = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            doc = json.loads((out / "degradation.json").read_text())
            self.assertTrue(doc["validation"]["ok"], doc["validation"]["errors"])
            self.assertEqual(len(doc["rows"]), 2 * 24 * len(RR.METRICS))
            for c in doc["coverage"].values():
                self.assertEqual(c["fixturesPresent"], 24)


if __name__ == "__main__":
    unittest.main(verbosity=2)
