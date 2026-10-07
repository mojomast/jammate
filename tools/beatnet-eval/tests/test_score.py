"""End-to-end scorer tests on a LABELED SYNTHETIC unit fixture.

These predicted series are hand-built to exercise the scorer's arithmetic and
its corpus-hash guard. They are NOT BeatNet observations and are not evidence
about BeatNet. The test names say so on purpose.
"""

import os
import sys
import unittest

EVAL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ROOT = os.path.dirname(os.path.dirname(EVAL_DIR))
sys.path.insert(0, os.path.join(EVAL_DIR, "beatnet_eval"))
import corpus  # noqa: E402
import io_contract  # noqa: E402
import score as score_mod  # noqa: E402

H = "b" * 64


def _doc_for(fixture, predicted_beats, semantics="online_batch"):
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
        "fixtures": [{
            "name": fixture["name"],
            "input_wav_sha256": fixture["sha256"],
            "beats": predicted_beats,
        }],
    }


class ScoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = corpus.load_manifest(os.path.join(REPO_ROOT, "testdata", "rhythm"))
        cls.fixture = corpus.fixture_by_name(cls.manifest, "clean_eighths")

    def test_synthetic_unit_fixture_scores_and_matches_corpus_hash(self):
        truth = corpus.ground_truth_beats(self.fixture)
        # UNIT TEST ONLY: take every second truth beat as a fake prediction.
        predicted = [{"time": t, "available": None} for t in truth[::2]]
        result = score_mod.score(_doc_for(self.fixture, predicted), self.manifest, 0.07)
        self.assertEqual(result["aggregate"]["fixturesScored"], 1)
        self.assertGreater(result["fixtures"][0]["fMeasure"], 0.0)

    def test_wav_hash_mismatch_is_refused(self):
        doc = _doc_for(self.fixture, [{"time": 0.35, "available": None}])
        doc["fixtures"][0]["input_wav_sha256"] = H
        with self.assertRaises(io_contract.InputContractError):
            score_mod.score(doc, self.manifest, 0.07)

    def test_unverified_doc_is_refused(self):
        doc = _doc_for(self.fixture, [{"time": 0.35, "available": None}])
        doc["provenance"]["source"] = "my-own-tracker"
        with self.assertRaises(io_contract.InputContractError):
            score_mod.score(doc, self.manifest, 0.07)


if __name__ == "__main__":
    unittest.main()
