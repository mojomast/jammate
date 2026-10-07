"""End-to-end scorer tests on LABELED SYNTHETIC unit fixtures.

The predicted series are hand-built to exercise the scorer's arithmetic, its
corpus-hash guard, and its coverage reporting. They are NOT BeatNet observations
and are not evidence about BeatNet. Note deliberately that the metadata gate
accepts this document when its declared provenance is well-formed: the gate
checks metadata/format, not authenticity, which is exactly why the scorer labels
its output unverified.
"""

import hashlib
import json
import os
import sys
import tempfile
import unittest

EVAL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ROOT = os.path.dirname(os.path.dirname(EVAL_DIR))
sys.path.insert(0, os.path.join(EVAL_DIR, "beatnet_eval"))
import corpus  # noqa: E402
import io_contract  # noqa: E402
import score as score_mod  # noqa: E402

H = "b" * 64


def _doc_for(fixtures, semantics="online_batch"):
    return {
        "schema": io_contract.SCHEMA,
        "provenance": {
            "source": "BeatNet",
            "repo_commit": "81cedd4beeb7235262db80969a0c9ce9a48a0ed4",
            "mode": "online" if semantics == "online_batch" else "realtime",
            "inference_model": "PF",
            "device": "cpu",
            "raw_output_sha256": H,
            "driver_sha256": H,
        },
        "availability_semantics": semantics,
        "fixtures": fixtures,
    }


class ScoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = corpus.load_manifest(os.path.join(REPO_ROOT, "testdata", "rhythm"))
        cls.fixture = corpus.fixture_by_name(cls.manifest, "clean_eighths")

    def _one_fixture(self):
        truth = corpus.ground_truth_beats(self.fixture)
        # UNIT TEST ONLY: every second truth beat is used as a fake prediction.
        predicted = [{"time": t, "available": None} for t in truth[::2]]
        return {"name": self.fixture["name"], "input_wav_sha256": self.fixture["sha256"],
                "beats": predicted}

    def test_metadata_gate_accepts_synthetic_document_but_labels_it_unverified(self):
        result = score_mod.score(_doc_for([self._one_fixture()]), self.manifest, 0.07)
        self.assertEqual(result["aggregate"]["fixturesScored"], 1)
        self.assertTrue(result["provenanceIsSelfReported"])
        self.assertIn("unverified", result["note"].lower())
        md = score_mod._markdown(result)
        self.assertIn("DECLARED", md)
        self.assertIn("unverified", md.lower())

    def test_coverage_reports_missing_and_is_incomplete(self):
        result = score_mod.score(_doc_for([self._one_fixture()]), self.manifest, 0.07)
        agg = result["aggregate"]
        self.assertFalse(agg["coverageComplete"])
        self.assertEqual(agg["fixturesScored"] + len(agg["missingFixtures"]), agg["manifestFixtures"])
        self.assertIn("clean_eighths", [f["name"] for f in result["fixtures"]])
        self.assertNotIn("clean_eighths", agg["missingFixtures"])
        self.assertIn("blues_shuffle", agg["missingFixtures"])
        self.assertEqual(agg["unknownFixtures"], [])
        md = score_mod._markdown(result)
        self.assertIn("missing (in manifest, absent from run)", md)

    def test_unknown_fixture_is_reported(self):
        fake = dict(self._one_fixture())
        fake["name"] = "not_in_manifest"
        real = self._one_fixture()
        doc = _doc_for([real, fake])
        result = score_mod.score(doc, self.manifest, 0.07)
        self.assertEqual(result["aggregate"]["unknownFixtures"], ["not_in_manifest"])
        self.assertFalse(result["aggregate"]["coverageComplete"])

    def test_wav_hash_mismatch_is_rejected(self):
        fx = self._one_fixture()
        fx["input_wav_sha256"] = H
        with self.assertRaises(io_contract.InputContractError):
            score_mod.score(_doc_for([fx]), self.manifest, 0.07)

    def test_non_beatnet_source_rejected(self):
        doc = _doc_for([self._one_fixture()])
        doc["provenance"]["source"] = "my-own-tracker"
        with self.assertRaises(io_contract.InputContractError):
            score_mod.score(doc, self.manifest, 0.07)

    def test_per_chunk_availability_must_not_precede_event(self):
        fx = self._one_fixture()
        fx["beats"] = [{"time": 0.5, "available": 0.4}]
        with self.assertRaises(io_contract.InputContractError):
            score_mod.score(_doc_for([fx], semantics="per_chunk"), self.manifest, 0.07)

    def test_per_chunk_latency_reaches_scored_output(self):
        fx = self._one_fixture()
        fx["beats"] = [{"time": 0.5, "available": 0.52},
                       {"time": 1.0, "available": 1.04}]
        result = score_mod.score(_doc_for([fx], semantics="per_chunk"), self.manifest, 0.07)
        measured = result["fixtures"][0]
        self.assertEqual(measured["availabilitySamples"], 2)
        self.assertAlmostEqual(measured["availabilityMeanMs"], 30)
        self.assertAlmostEqual(measured["availabilityMaxMs"], 40)
        self.assertAlmostEqual(result["aggregate"]["availabilityMeanMs"], 30)

    def test_manifest_loader_checks_actual_audio_bytes(self):
        with tempfile.TemporaryDirectory() as root:
            fixture = dict(self.fixture, file="input.wav")
            audio = b"synthetic hash-test bytes, not BeatNet input"
            fixture["sha256"] = hashlib.sha256(audio).hexdigest()
            wav_path = os.path.join(root, "input.wav")
            with open(wav_path, "wb") as fh:
                fh.write(audio)
            with open(os.path.join(root, "manifest.json"), "w") as fh:
                json.dump({"fixtures": [fixture]}, fh)
            corpus.load_manifest(root)
            with open(wav_path, "ab") as fh:
                fh.write(b"corrupted")
            with self.assertRaisesRegex(ValueError, "actual WAV sha256"):
                corpus.load_manifest(root)


if __name__ == "__main__":
    unittest.main()
