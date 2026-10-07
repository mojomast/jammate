"""The provenance document is the output of record; assert its pinned values."""

import os
import sys
import unittest

EVAL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(EVAL_DIR, "beatnet_eval"))
import provenance  # noqa: E402


class ProvenanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = provenance.load_provenance()

    def test_schema_and_refs(self):
        self.assertEqual(self.doc["schema"], "beatnet-eval/provenance/v1")
        self.assertEqual(self.doc["beatnet"]["spec_cited_repo"]["url"], provenance.SPEC_CITED_REPO)
        self.assertEqual(self.doc["beatnet"]["spec_cited_repo"]["commit"], provenance.SPEC_CITED_COMMIT)
        self.assertEqual(self.doc["beatnet"]["upstream_repo"]["commit"], provenance.UPSTREAM_COMMIT)

    def test_license_is_cc_by_4_and_covers_weights(self):
        lic = self.doc["beatnet"]["license"]
        self.assertEqual(lic["spdx"], "CC-BY-4.0")
        self.assertEqual(lic["sha256"], "7e7170e3cebf88a9f60c7b8421418323c09304da1af4d5e90f4da1dc1c8a2661")
        self.assertTrue(lic["no_separate_weight_terms_found"])
        self.assertIn("model weights", lic["applies_to"])

    def test_weight_hashes_match_constants(self):
        models = {m["name"]: m["sha256"] for m in self.doc["beatnet"]["models"]["files"]}
        self.assertEqual(models, provenance.MODEL_WEIGHT_SHA256)

    def test_blockers_are_measured_not_guessed(self):
        ids = {b["id"] for b in self.doc["dependency_blockers"]}
        self.assertEqual(ids, {"B1", "B2", "B3", "B4"})
        for blocker in self.doc["dependency_blockers"]:
            self.assertTrue(blocker["measured"], f"{blocker['id']} has no measured evidence")

    def test_verdict_flags_no_numbers(self):
        self.assertIn("No BeatNet accuracy", self.doc["verdict"])


if __name__ == "__main__":
    unittest.main()
