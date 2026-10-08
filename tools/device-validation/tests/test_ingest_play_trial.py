"""Play-trial and functional ingest tests: measured vs operator-reported."""
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402
import ingest_play_trial as ipt  # noqa: E402
import ingest_functional as ifn  # noqa: E402
import validate_evidence as ve  # noqa: E402


class PlayTrialTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-play-")
        self.iface = fx.asio_interface("iface-asio-128", 128)
        self.session = fx.make_session(self.tmp, [self.iface])
        self.receipt = fx.text_receipt(self.tmp, "trace.json")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_timing_without_receipt_rejected(self):
        values = self._values()
        with self.assertRaises(dl.DeviceValidationError):
            ipt.make_record(self.session, self.tmp, "clean_strumming", self.iface,
                            values, {})

    def test_useful_lock_yes_without_receipt_rejected(self):
        values = self._values()
        values.pop("start_requested_s", None)
        values.pop("join_heard_s", None)
        values.pop("stop_s", None)
        with self.assertRaises(dl.DeviceValidationError):
            ipt.make_record(self.session, self.tmp, "clean_strumming", self.iface,
                            values, {})

    def test_measured_with_receipt_sets_flags(self):
        rec = fx.play_record(self.session, self.tmp, "clean_strumming",
                             self.iface, self.receipt)
        self.assertTrue(rec["timing"]["join_heard_s"]["measured"])
        self.assertEqual(rec["timing"]["join_heard_s"]["provenance"],
                         "instrumented-raw")
        self.assertTrue(rec["callback"]["p99_ms"]["measured"])
        self.assertEqual(rec["useful_lock"]["provenance"], "operator-report")

    def test_unmeasured_value_is_hard_error(self):
        rec = fx.play_record(self.session, self.tmp, "clean_strumming",
                             self.iface, self.receipt)
        rec["callback"]["p50_ms"]["measured"] = False  # value still present
        fx.write_record(self.tmp, "play.json", rec)
        code = ve.main(["--session", self.tmp, "--gate", "play"])
        report = dl.load_json_strict(os.path.join(self.tmp, "report.json"))
        self.assertEqual(code, 1)
        self.assertIn("unmeasured-value", [e["rule"] for e in report["hard_errors"]])

    def test_play_yes_without_measured_window_is_hard_error(self):
        rec = fx.play_record(self.session, self.tmp, "clean_strumming",
                             self.iface, self.receipt)
        rec["useful_lock"]["window_bars"] = {"value": None, "measured": False}
        fx.write_record(self.tmp, "play.json", rec)
        code = ve.main(["--session", self.tmp, "--gate", "play"])
        report = dl.load_json_strict(os.path.join(self.tmp, "report.json"))
        self.assertEqual(code, 1)
        self.assertIn("play-lock-window", [e["rule"] for e in report["hard_errors"]])

    def _values(self):
        return {
            "style": "Rock", "jam_mode": "follow", "intensity": 0.5,
            "complexity": 0.4, "fill_amount": 0.3, "follow_tightness": 0.5,
            "meter": "4/4", "tempo_bpm": 120.0,
            "start_requested_s": 1.0, "join_heard_s": 3.0, "stop_s": 10.0,
            "useful_lock": "yes", "lock_window_bars": 2, "time_to_lock_s": 1.5,
            "dropouts": 0, "callback_p50_ms": 1.0, "callback_p99_ms": 2.0,
            "callback_deadline_misses": 0, "analysis_overruns": 0,
            "join_sensible": "yes", "stayed_stable": "yes", "overreacted": "no",
            "fills_musical": "yes", "push_pull": "yes",
            "recovered_tap_resync": "yes", "operator": "unit", "notes": None,
        }


class FunctionalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-fn-")
        self.iface = fx.asio_interface("iface-asio-128", 128)
        fx.make_session(self.tmp, [self.iface])

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_receipt_required(self):
        with self.assertRaises(SystemExit):
            ifn.main(["--session", self.tmp, "--condition", "silent_input",
                      "--outcome", "pass"])

    def test_functional_ok_and_hashed(self):
        receipt = fx.text_receipt(self.tmp, "log.txt")
        code = ifn.main(["--session", self.tmp, "--condition", "silent_input",
                         "--outcome", "pass", "--receipt", receipt])
        self.assertEqual(code, 0)
        rec = dl.load_json_strict(os.path.join(
            self.tmp, "measurements", "functional-silent_input.json"))
        self.assertTrue(dl.is_hex64(rec["receipt"]["sha256"]))
        self.assertTrue(rec["measured"])


if __name__ == "__main__":
    unittest.main()
