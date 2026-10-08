"""Trace and import-manifest validation tests (fail-closed boundaries)."""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from glock.integrity import (IntegrityError, load_json_strict, safe_resolve,  # noqa: E402
                             sha256_file)
from glock.manifest import (load_import_manifest, validate_manifest)  # noqa: E402
from glock.trace import load_trace, validate_trace  # noqa: E402
from tests import synth  # noqa: E402


class TempDir(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def write_json(self, name, obj, allow_nan=True):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(obj, fh, allow_nan=allow_nan)
        return path

    def make_wav_entry(self, rec_id="r1", classification="synthetic",
                       annotation_kind="file", annotation_source="generated"):
        wav = synth.write_silence_wav(os.path.join(self.dir, rec_id + ".wav"))
        info_sha = sha256_file(wav)
        ann = None
        if annotation_kind == "file":
            ann_file = self.write_json(rec_id + ".ann.json", {
                "schema": "guitar-lock-eval/annotation/1.0",
                "source": annotation_source, "license": "test",
                "tempo_profile": "constant", "nominal_bpm": 120.0,
                "meter": {"numerator": 4, "denominator": 4, "beats_per_bar": 4},
                "beats": [i * 0.5 for i in range(16)], "onsets": [],
                "true_silence_spans": [],
            })
            ann = {"kind": "file", "path": os.path.basename(ann_file),
                   "sha256": sha256_file(ann_file)}
        return {
            "id": rec_id, "audio_path": os.path.basename(wav),
            "audio_sha256": info_sha, "sample_rate": 48000.0, "channels": 1,
            "sample_width_bytes": 2, "frames": 384000, "duration_seconds": 8.0,
            "classification": classification, "license": "test",
            "ownership": "test", "provenance": "synthetic-test",
            "representative": True, "parent_id": None, "tags": ["core"],
            "annotation": ann,
        }

    def write_manifest(self, recordings):
        return self.write_json("import.json", {
            "schema": "guitar-lock-eval/import-manifest/1.0",
            "task": "EVAL-GUITAR-009", "created_utc": "2026-01-01T00:00:00Z",
            "recordings": recordings})

    # --- paths -------------------------------------------------------------
    def test_safe_resolve_rejects_escape(self):
        with self.assertRaises(IntegrityError):
            safe_resolve(self.dir, "../outside.wav")
        with self.assertRaises(IntegrityError):
            safe_resolve(self.dir, "/etc/passwd")

    def test_json_rejects_nan(self):
        path = self.write_json("nan.json", {"x": float("nan")})
        with self.assertRaises(IntegrityError):
            load_json_strict(path)

    def test_json_rejects_duplicate_keys(self):
        path = os.path.join(self.dir, "dup.json")
        with open(path, "w") as fh:
            fh.write('{"a": 1, "a": 2}')
        with self.assertRaises(IntegrityError):
            load_json_strict(path)

    # --- manifest ----------------------------------------------------------
    def test_real_without_annotation_fails(self):
        entry = self.make_wav_entry(classification="real", annotation_kind=None)
        path = self.write_manifest([entry])
        manifest = load_import_manifest(path, verify_files=False)
        hard = [f for f in validate_manifest(manifest) if f.severity == "hard"]
        self.assertTrue(any(f.code == "real_without_annotation" for f in hard))

    def test_synthetic_mislabelled_real_fails(self):
        entry = self.make_wav_entry(classification="real", annotation_source="generated")
        path = self.write_manifest([entry])
        manifest = load_import_manifest(path, verify_files=False)
        hard = [f for f in validate_manifest(manifest) if f.severity == "hard"]
        self.assertTrue(any(f.code == "synthetic_mislabelled_real" for f in hard))

    def test_human_real_is_valid(self):
        entry = self.make_wav_entry(classification="real", annotation_source="human")
        path = self.write_manifest([entry])
        manifest = load_import_manifest(path, verify_files=False)
        hard = [f for f in validate_manifest(manifest) if f.severity == "hard"]
        self.assertEqual([f.code for f in hard], [])

    def test_audio_path_escape_rejected(self):
        entry = self.make_wav_entry()
        entry["audio_path"] = "../escape.wav"
        path = self.write_manifest([entry])
        with self.assertRaises(IntegrityError):
            load_import_manifest(path, verify_files=False)

    def test_annotation_nan_rejected(self):
        entry = self.make_wav_entry()
        ann_path = os.path.join(self.dir, "r1.ann.json")
        with open(ann_path, "w") as fh:
            fh.write('{"schema": null, "source": "human", "license": "x", '
                     '"tempo_profile": "constant", "nominal_bpm": 120.0, '
                     '"meter": {"numerator": 4, "denominator": 4, "beats_per_bar": 4}, '
                     '"beats": [NaN], "onsets": [], "true_silence_spans": []}')
        entry["annotation"] = {"kind": "file", "path": "r1.ann.json",
                               "sha256": sha256_file(ann_path)}
        path = self.write_manifest([entry])
        with self.assertRaises(IntegrityError):
            load_import_manifest(path, verify_files=False)

    def test_audio_sha_mismatch_fails(self):
        entry = self.make_wav_entry()
        entry["audio_sha256"] = "a" * 64
        path = self.write_manifest([entry])
        manifest = load_import_manifest(path, verify_files=False)
        hard = [f for f in validate_manifest(manifest) if f.severity == "hard"]
        self.assertTrue(any(f.code == "audio_sha_mismatch" for f in hard))

    def test_derived_parent_missing_fails(self):
        entry = self.make_wav_entry(classification="derived")
        entry["parent_id"] = "nope"
        path = self.write_manifest([entry])
        manifest = load_import_manifest(path, verify_files=False)
        hard = [f for f in validate_manifest(manifest) if f.severity == "hard"]
        self.assertTrue(any(f.code == "derived_parent_missing" for f in hard))

    def test_load_hard_fails_closed(self):
        entry = self.make_wav_entry(classification="real", annotation_kind=None)
        path = self.write_manifest([entry])
        with self.assertRaises(IntegrityError):
            load_import_manifest(path, verify_files=True)


class TraceTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def write_json(self, name, obj):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(obj, fh)
        return path

    def base_trace(self, **over):
        t = {
            "schema": "guitar-lock-eval/trace/1.0", "recording_id": "r1",
            "backend": "synthetic-test", "backend_kind": "real",
            "parent_backend": None, "derivation": None,
            "audio_sha256": "0" * 64, "sample_rate": 48000.0, "block_frames": 128,
            "source": {"tool": "synthetic-test", "tool_sha256": "0" * 64},
            "receipt": {"measured": True, "wall_utc": "2026-01-01T00:00:00Z",
                        "audio_seconds": 8.0},
            "beats": [{"event_seconds": 0.5, "horizon_seconds": 0.51,
                       "reported_sample": 24000, "reported_bpm": 120.0,
                       "bpm_phase_valid": True}],
            "tempo_samples": [{"event_seconds": 0.5, "horizon_seconds": 0.51,
                               "bpm": 120.0, "phase_valid": True}],
        }
        t.update(over)
        return t

    def test_bad_schema_rejected(self):
        path = self.write_json("t.json", self.base_trace(schema="wrong"))
        with self.assertRaises(IntegrityError):
            load_trace(path)

    def test_event_after_horizon_rejected(self):
        t = self.base_trace(beats=[{"event_seconds": 1.0, "horizon_seconds": 0.5}])
        path = self.write_json("t.json", t)
        trace = load_trace(path)
        findings = validate_trace(trace, synth.make_recording(rec_id="r1"))
        self.assertTrue(any(f.code == "trace_event_after_horizon" for f in findings))

    def test_non_monotonic_beats_rejected(self):
        t = self.base_trace(beats=[
            {"event_seconds": 1.0, "horizon_seconds": 1.1},
            {"event_seconds": 0.5, "horizon_seconds": 0.6}])
        path = self.write_json("t.json", t)
        trace = load_trace(path)
        findings = validate_trace(trace, synth.make_recording(rec_id="r1"))
        self.assertTrue(any(f.code == "trace_beats_not_monotonic" for f in findings))

    def test_receipt_before_horizon_rejected(self):
        t = self.base_trace(receipt={"measured": True, "wall_utc": "x",
                                     "audio_seconds": 0.1})
        path = self.write_json("t.json", t)
        trace = load_trace(path)
        findings = validate_trace(trace, synth.make_recording(rec_id="r1"))
        self.assertTrue(any(f.code == "trace_receipt_before_horizon" for f in findings))

    def test_unmeasured_receipt_must_be_null(self):
        t = self.base_trace(receipt={"measured": False, "wall_utc": "x",
                                     "audio_seconds": 1.0})
        path = self.write_json("t.json", t)
        trace = load_trace(path)
        findings = validate_trace(trace, synth.make_recording(rec_id="r1"))
        self.assertTrue(any(f.code == "trace_unmeasured_receipt_not_null" for f in findings))

    def test_derived_without_parent_rejected(self):
        t = self.base_trace(backend_kind="derived", parent_backend=None, derivation=None)
        path = self.write_json("t.json", t)
        with self.assertRaises(IntegrityError):
            load_trace(path)

    def test_audio_sha_mismatch_flagged(self):
        path = self.write_json("t.json", self.base_trace(audio_sha256="b" * 64))
        trace = load_trace(path)
        findings = validate_trace(trace, synth.make_recording(rec_id="r1"))
        self.assertTrue(any(f.code == "trace_audio_sha_mismatch" for f in findings))


if __name__ == "__main__":
    unittest.main()
