"""Probe tests: the probe must run offline and report, not fabricate."""

import os
import sys
import unittest

EVAL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, EVAL_DIR)
import probe  # noqa: E402


class ProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = probe.probe(beatnet_dir=None, timeout=30.0)

    def test_report_shape(self):
        self.assertEqual(self.report["schema"], "beatnet-eval/probe/v1")
        self.assertIn("version", self.report["python"])
        self.assertEqual(len(self.report["imports"]), len(probe.MODULES))
        self.assertFalse(self.report["ran_benchmark"])

    def test_importable_flag_is_consistent_with_report(self):
        expected = all(r["importable"] for r in self.report["imports"])
        self.assertEqual(self.report["beatnet_importable"], expected)
        if not expected:
            self.assertTrue(self.report["blockers"])

    def test_main_exit_matches_flag(self):
        code = probe.main(["--json"])
        self.assertEqual(code, 0 if self.report["beatnet_importable"] else 2)


if __name__ == "__main__":
    unittest.main()
