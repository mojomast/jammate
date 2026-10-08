"""CLI end-to-end test using pre-generated synthetic traces (no real backend).

This proves the one-command wiring, the fail-closed behaviour and the output
files. The traces are labelled synthetic-test and are not evidence.
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from glock.cli import main  # noqa: E402
from glock.integrity import sha256_file  # noqa: E402
from tests import synth  # noqa: E402


def trace_dict(rec, bpm, backend="btrack"):
    ann = rec.annotation
    beats, tempo = [], []
    for t in ann.beats:
        beats.append({"event_seconds": t, "horizon_seconds": t + 0.01,
                      "reported_sample": int(t * 48000), "reported_bpm": bpm,
                      "bpm_phase_valid": True})
        tempo.append({"event_seconds": t, "horizon_seconds": t + 0.01,
                      "bpm": bpm, "phase_valid": True})
    return {
        "schema": "guitar-lock-eval/trace/1.0", "recording_id": rec.id,
        "backend": backend, "backend_kind": "real", "parent_backend": None,
        "derivation": None, "audio_sha256": rec.declared["audio_sha256"],
        "sample_rate": rec.declared["sample_rate"], "block_frames": 128,
        "source": {"tool": "synthetic-test", "tool_sha256": "ab" * 32},
        "receipt": {"measured": True, "wall_utc": "2026-01-01T00:00:00Z",
                    "audio_seconds": ann.beats[-1] + 0.01},
        "beats": beats, "tempo_samples": tempo,
    }


class CliTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name
        self.manifest = os.path.join(self.dir, "import.json")
        self.recordings = []
        entries = []
        for i in range(2):
            rid = f"r{i}"
            wav = synth.write_silence_wav(os.path.join(self.dir, rid + ".wav"),
                                          seconds=4.0)
            ann = synth.make_annotation(120.0, n_beats=16)
            ann_path = os.path.join(self.dir, rid + ".ann.json")
            with open(ann_path, "w") as fh:
                json.dump({
                    "schema": "guitar-lock-eval/annotation/1.0", "source": "generated",
                    "license": "test", "tempo_profile": "constant",
                    "nominal_bpm": 120.0,
                    "meter": {"numerator": 4, "denominator": 4, "beats_per_bar": 4},
                    "beats": ann.beats, "onsets": ann.onsets,
                    "true_silence_spans": [],
                }, fh)
            entries.append({
                "id": rid, "audio_path": rid + ".wav",
                "audio_sha256": sha256_file(wav), "sample_rate": 48000.0,
                "channels": 1, "sample_width_bytes": 2, "frames": 192000,
                "duration_seconds": 4.0, "classification": "synthetic",
                "license": "test", "ownership": "test", "provenance": "synthetic-test",
                "representative": False, "parent_id": None, "tags": ["core"],
                "annotation": {"kind": "file", "path": rid + ".ann.json",
                               "sha256": sha256_file(ann_path)},
            })
        with open(self.manifest, "w") as fh:
            json.dump({"schema": "guitar-lock-eval/import-manifest/1.0",
                       "task": "EVAL-GUITAR-009", "created_utc": "x",
                       "recordings": entries}, fh)

    def tearDown(self):
        self._tmp.cleanup()

    def _write_traces(self, traces_dir, backends=("btrack",), bpm=120.0):
        from glock.manifest import load_import_manifest
        manifest = load_import_manifest(self.manifest, verify_files=True)
        recs = {r.id: r for r in manifest.recordings}
        for backend in backends:
            os.makedirs(os.path.join(traces_dir, backend), exist_ok=True)
            for rid, rec in recs.items():
                with open(os.path.join(traces_dir, backend, rid + ".json"), "w") as fh:
                    json.dump(trace_dict(rec, bpm, backend), fh)

    def test_diagnostic_run_exits_zero_and_writes(self):
        traces = os.path.join(self.dir, "traces")
        self._write_traces(traces)
        out_json = os.path.join(self.dir, "results.json")
        out_md = os.path.join(self.dir, "summary.md")
        rc = main(["evaluate", "--manifest", self.manifest, "--traces-dir", traces,
                   "--backends", "btrack", "--diagnostic-ok",
                   "--json-out", out_json, "--summary-md", out_md, "--workdir",
                   os.path.join(self.dir, "work")])
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.isfile(out_json))
        self.assertTrue(os.path.isfile(out_md))
        meta = json.load(open(out_json))
        self.assertFalse(meta["gate_pass"])
        labels = [p["label"] for p in meta["populations"]]
        self.assertIn("gate", labels)
        self.assertIn("diagnostic", labels)

    def test_missing_trace_fails_closed(self):
        traces = os.path.join(self.dir, "traces")
        self._write_traces(traces)
        os.remove(os.path.join(traces, "btrack", "r1.json"))
        rc = main(["evaluate", "--manifest", self.manifest, "--traces-dir", traces,
                   "--backends", "btrack", "--diagnostic-ok",
                   "--workdir", os.path.join(self.dir, "work")])
        self.assertEqual(rc, 2)

    def test_gate_fail_without_diagnostic_ok_exits_one(self):
        traces = os.path.join(self.dir, "traces")
        self._write_traces(traces)
        rc = main(["evaluate", "--manifest", self.manifest, "--traces-dir", traces,
                   "--backends", "btrack",
                   "--workdir", os.path.join(self.dir, "work")])
        self.assertEqual(rc, 1)

    def test_validate_command(self):
        self.assertEqual(main(["validate", "--manifest", self.manifest]), 0)


if __name__ == "__main__":
    unittest.main()
