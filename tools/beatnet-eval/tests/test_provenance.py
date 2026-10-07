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

    def test_license_is_cc_by_4_and_unresolved(self):
        lic = self.doc["beatnet"]["license"]
        self.assertEqual(lic["spdx"], "CC-BY-4.0")
        self.assertEqual(lic["sha256"], "7e7170e3cebf88a9f60c7b8421418323c09304da1af4d5e90f4da1dc1c8a2661")
        self.assertTrue(lic["only_stated_term"])
        self.assertTrue(lic["no_separate_weight_terms_found"])
        self.assertEqual(lic["weights_scope"], "unresolved")
        self.assertEqual(lic["redistribution_review"], "unresolved")

    def test_license_makes_no_blanket_incompatibility_claim(self):
        concern = self.doc["beatnet"]["license"]["concern"].lower()
        self.assertIn("unresolved", concern)
        for phrase in ("not compatible", "is incompatible", "incompatible with",
                       "cannot be combined", "violates"):
            self.assertNotIn(phrase, concern)
        self.assertTrue(self.doc["beatnet"]["license"]["facts"])

    def test_weight_hashes_verified_at_pinned_commit_url(self):
        models = self.doc["beatnet"]["models"]
        template = models["sha256_source_url_template"]
        self.assertIn(provenance.UPSTREAM_COMMIT, template)
        self.assertNotIn("/main/", template)
        self.assertTrue(models["sha256_verified_at_pinned_commit"])
        hashes = {m["name"]: m["sha256"] for m in models["files"]}
        self.assertEqual(hashes, provenance.MODEL_WEIGHT_SHA256)

    def test_blockers_are_measured_not_guessed(self):
        ids = {b["id"] for b in self.doc["dependency_blockers"]}
        self.assertEqual(ids, {"B1", "B2", "B3", "B4"})
        for blocker in self.doc["dependency_blockers"]:
            self.assertTrue(blocker["measured"], f"{blocker['id']} has no measured evidence")

    def test_verdict_flags_no_numbers(self):
        self.assertIn("No BeatNet accuracy", self.doc["verdict"])


if __name__ == "__main__":
    unittest.main()
