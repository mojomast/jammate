"""Adversarial tests for the d64a598 review findings F1-F4.

Each test reproduces the reviewer's finding and pins the correction:
  F1 real classification must not survive a declared generated provenance marker;
  F2 a trace must be bound to the requested backend and be a real backend kind,
     enforced both in the CLI and inside the gate itself;
  F3 the derived candidate records a real producer hash, and all-zero
     placeholders are rejected;
  F4 the phase window comes from criteria.acquisition_window_bars.
"""
import json
import os
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from glock.adapters import derive_beat_interval_trace, producer_sha256  # noqa: E402
from glock.cli import main  # noqa: E402
from glock.gate import evaluate_gate  # noqa: E402
from glock.integrity import sha256_file  # noqa: E402
from glock.manifest import (gate_eligibility, provenance_contradiction,  # noqa: E402
                            validate_manifest)
from glock.scoring import Criteria, score_useful_lock  # noqa: E402
from glock.trace import parse_trace, validate_trace, valid_tool_hash  # noqa: E402
from tests import synth  # noqa: E402

CRIT = Criteria()

GENERATED_PROVENANCE = ("Synthesised from scratch by a generator v1; this is "
                        "generated audio, not a human performance")


def _real_rec_with_wav(dirpath, rec_id="r1", provenance="field recording"):
    wav = synth.write_silence_wav(os.path.join(dirpath, rec_id + ".wav"),
                                  seconds=8.0)
    rec = synth.make_recording(rec_id=rec_id, classification="real",
                               ownership="me", license="cc-by",
                               annotation=synth.make_annotation(120.0, n_beats=16),
                               tags=["core"], provenance=provenance)
    rec.audio_path_resolved = wav
    rec.declared = {"audio_sha256": sha256_file(wav), "sample_rate": 48000.0,
                    "channels": 1, "sample_width_bytes": 2,
                    "frames": 384000, "duration_seconds": 8.0}
    return rec, wav


class F1ProvenanceMarkerTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def test_generated_marker_rejected_for_real(self):
        rec, _ = _real_rec_with_wav(self.dir, provenance=GENERATED_PROVENANCE)
        findings = validate_manifest(SimpleNamespace(path="test", recordings=[rec]))
        codes = [f.code for f in findings if f.severity == "hard"]
        self.assertIn("provenance_contradicts_real", codes)
        self.assertEqual(provenance_contradiction(rec), "synthes")
        eligible, reason = gate_eligibility(rec, {"min_annotated_beats": 8})
        self.assertFalse(eligible)
        self.assertIn("generated/synthetic", reason)

    def test_clean_real_provenance_survives(self):
        rec, _ = _real_rec_with_wav(self.dir, provenance="field recording A")
        findings = validate_manifest(SimpleNamespace(path="test", recordings=[rec]))
        hard = [f for f in findings if f.severity == "hard"]
        self.assertEqual([f.code for f in hard], [])
        eligible, _ = gate_eligibility(rec, {"min_annotated_beats": 8})
        self.assertTrue(eligible)

    def test_marker_real_cannot_pass_gate_even_with_perfect_trace(self):
        rec, _ = _real_rec_with_wav(self.dir, provenance=GENERATED_PROVENANCE)
        rec.declared["audio_sha256"] = "0" * 64  # trace identity matches below
        trace = synth.perfect_trace(nominal_bpm=120.0, n_beats=16, backend="btrack")
        trace.recording_id = rec.id
        trace.audio_sha256 = "0" * 64
        score = score_useful_lock(rec, trace, CRIT)
        res = evaluate_gate("btrack", [rec], {rec.id: score}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertEqual(res.population, [])  # not eligible for the gate


class F2BackendBindingTests(unittest.TestCase):
    def test_gate_rejects_mislabelled_backend(self):
        rec = synth.make_recording(rec_id="r1", classification="real")
        trace = synth.perfect_trace(nominal_bpm=120.0, n_beats=16, backend="btrack")
        score = score_useful_lock(rec, trace, CRIT)
        self.assertTrue(score.useful_lock)
        score.backend = "not-a-real-backend"
        score.backend_kind = "derived"
        res = evaluate_gate("btrack", [rec], {rec.id: score}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertTrue(any("not-a-real-backend" in r for r in res.fail_closed))

    def test_gate_rejects_derived_kind_for_gate_backend(self):
        rec = synth.make_recording(rec_id="r1", classification="real")
        score = score_useful_lock(
            rec, synth.perfect_trace(n_beats=16, backend="btrack"), CRIT)
        score.backend_kind = "derived"
        res = evaluate_gate("btrack", [rec], {rec.id: score}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertTrue(any("kind 'derived'" in r for r in res.fail_closed))

    def test_gate_accepts_correct_binding(self):
        rec = synth.make_recording(rec_id="r1", classification="real")
        score = score_useful_lock(
            rec, synth.perfect_trace(n_beats=16, backend="btrack"), CRIT)
        res = evaluate_gate("btrack", [rec], {rec.id: score}, CRIT)
        self.assertTrue(res.gate_pass)

    def test_validate_trace_binds_backend_and_parent(self):
        from glock.trace import Trace, Beat, TempoSample, Receipt
        rec = synth.make_recording(rec_id="r1")
        t = Trace(path="t", schema="guitar-lock-eval/trace/1.0", recording_id="r1",
                  backend="not-a-real-backend", backend_kind="derived",
                  parent_backend="aubio", derivation="x", audio_sha256="0" * 64,
                  sample_rate=48000.0, block_frames=128,
                  source={"tool": "x", "tool_sha256": "ab" * 32},
                  receipt=Receipt(True, "2026-01-01T00:00:00Z", 1.0), beats=[], tempo_samples=[])
        codes = [f.code for f in validate_trace(
            t, rec, expected_backend="btrack", expected_kind="real",
            expected_parent="btrack")]
        self.assertIn("trace_backend_mismatch", codes)
        self.assertIn("trace_backend_kind_mismatch", codes)
        self.assertIn("trace_parent_mismatch", codes)

    def test_cli_traces_dir_mislabelled_backend_fails_closed(self):
        with tempfile.TemporaryDirectory() as d:
            rec, wav = _real_rec_with_wav(d, rec_id="r1")
            ann_path = os.path.join(d, "r1.ann.json")
            with open(ann_path, "w") as fh:
                json.dump({
                    "schema": "guitar-lock-eval/annotation/1.0", "source": "human",
                    "license": "cc-by", "tempo_profile": "constant",
                    "nominal_bpm": 120.0,
                    "meter": {"numerator": 4, "denominator": 4, "beats_per_bar": 4},
                    "beats": [i * 0.5 for i in range(16)], "onsets": [],
                    "true_silence_spans": [],
                }, fh)
            manifest = os.path.join(d, "import.json")
            with open(manifest, "w") as fh:
                json.dump({
                    "schema": "guitar-lock-eval/import-manifest/1.0",
                    "task": "EVAL-GUITAR-009", "created_utc": "x",
                    "recordings": [{
                        "id": "r1", "audio_path": "r1.wav",
                        "audio_sha256": sha256_file(wav), "sample_rate": 48000.0,
                        "channels": 1, "sample_width_bytes": 2, "frames": 384000,
                        "duration_seconds": 8.0, "classification": "real",
                        "license": "cc-by", "ownership": "me",
                        "provenance": "field recording", "representative": True,
                        "parent_id": None, "tags": ["core"],
                        "annotation": {"kind": "file", "path": "r1.ann.json",
                                       "sha256": sha256_file(ann_path)},
                    }],
                }, fh)
            traces = os.path.join(d, "traces", "btrack")
            os.makedirs(traces)
            beats = [{"event_seconds": i * 0.5, "horizon_seconds": i * 0.5 + 0.01,
                      "reported_bpm": 120.0, "bpm_phase_valid": True}
                     for i in range(16)]
            tempo = [{"event_seconds": i * 0.5, "horizon_seconds": i * 0.5 + 0.01,
                      "bpm": 120.0, "phase_valid": True} for i in range(16)]
            with open(os.path.join(traces, "r1.json"), "w") as fh:
                json.dump({
                    "schema": "guitar-lock-eval/trace/1.0", "recording_id": "r1",
                    "backend": "not-a-real-backend", "backend_kind": "derived",
                    "parent_backend": "btrack", "derivation": "x",
                    "audio_sha256": sha256_file(wav), "sample_rate": 48000.0,
                    "block_frames": 128,
                    "source": {"tool": "x", "tool_sha256": "ab" * 32},
                    "receipt": {"measured": True, "wall_utc": "2026-01-01T00:00:00Z",
                                "audio_seconds": 8.0},
                    "beats": beats, "tempo_samples": tempo,
                }, fh)
            rc = main(["evaluate", "--manifest", manifest, "--traces-dir",
                       os.path.join(d, "traces"), "--backends", "btrack",
                       "--workdir", os.path.join(d, "work"), "--diagnostic-ok"])
            self.assertEqual(rc, 2)


class F3ProducerHashTests(unittest.TestCase):
    def test_candidate_records_real_producer_hash(self):
        rec = synth.make_recording()
        trace = synth.perfect_trace()
        d = derive_beat_interval_trace(trace, rec)
        h = d["source"]["tool_sha256"]
        self.assertEqual(h, producer_sha256())
        self.assertTrue(valid_tool_hash(h))
        self.assertNotEqual(h, "0" * 64)

    def test_placeholder_tool_hash_rejected(self):
        rec = synth.make_recording(rec_id="r1")
        data = {
            "schema": "guitar-lock-eval/trace/1.0", "recording_id": "r1",
            "backend": "btrack", "backend_kind": "real", "parent_backend": None,
            "derivation": None, "audio_sha256": "0" * 64, "sample_rate": 48000.0,
            "block_frames": 128,
            "source": {"tool": "x", "tool_sha256": "0" * 64},
            "receipt": {"measured": True, "wall_utc": "x", "audio_seconds": 1.0},
            "beats": [], "tempo_samples": [],
        }
        t = parse_trace(data, "t")
        codes = [f.code for f in validate_trace(t, rec)]
        self.assertIn("trace_tool_identity_placeholder", codes)


class F4PhaseWindowTests(unittest.TestCase):
    def _trace_late_phase_error(self):
        trace = synth.perfect_trace(nominal_bpm=120.0, n_beats=16)
        for i in range(4, 8):
            trace.beats[i].event_seconds += 0.05
            trace.beats[i].horizon_seconds += 0.05
            trace.tempo_samples[i].event_seconds += 0.05
            trace.tempo_samples[i].horizon_seconds += 0.05
        return trace

    def test_phase_window_follows_criteria(self):
        rec = synth.make_recording(annotation=synth.make_annotation(120.0, n_beats=16))
        trace = self._trace_late_phase_error()
        one_bar = Criteria(acquisition_window_bars=1.0, phase_usable_mean_abs_ms=20.0)
        two_bar = Criteria(acquisition_window_bars=2.0, phase_usable_mean_abs_ms=20.0)
        s1 = score_useful_lock(rec, trace, one_bar)
        s2 = score_useful_lock(rec, trace, two_bar)
        self.assertTrue(s1.phase_usable)
        self.assertFalse(s2.phase_usable)
        self.assertLess(s1.phase_mean_abs_ms, s2.phase_mean_abs_ms)
        self.assertTrue(s1.useful_lock)
        self.assertFalse(s2.useful_lock)


if __name__ == "__main__":
    unittest.main()
