"""Adversarial repro tests for the correction-round review findings.

Every test reproduces a specific false-pass hole with a concrete malicious or
malformed input and asserts the validator now fails closed. All fixtures are
TEST-ONLY (temp dirs); none is a real hardware run.
"""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402
import ingest_functional as ifn  # noqa: E402
import validate_evidence as ve  # noqa: E402


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-adv-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_validator(self, extra=None):
        argv = ["--session", self.tmp,
                "--out", os.path.join(self.tmp, "report.json")] + (extra or [])
        with contextlib.redirect_stdout(io.StringIO()):
            code = ve.main(argv)
        report = dl.load_json_strict(os.path.join(self.tmp, "report.json"))
        return code, report

    def load(self, name):
        return dl.load_json_strict(os.path.join(self.tmp, "measurements", name))

    def save(self, name, record):
        dl.write_json(os.path.join(self.tmp, "measurements", name), record)

    def rules(self, report):
        return [e["rule"] for e in report["hard_errors"]]


class F1CanonicalTests(Base):
    def test_target_ms_tamper_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"]["target_ms"] = 100.0
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("params-target-ms-tampered", self.rules(report))

    def test_target_ms_null_on_asio128_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"]["target_ms"] = None
        rec["analysis"]["target_met"] = None
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("params-target-ms-tampered", self.rules(report))

    def test_expected_rate_tamper_is_hard_error(self):
        interfaces = [fx.asio_interface("iface-asio-128", 128)]
        session = fx.make_session(self.tmp, interfaces)
        wav = os.path.join(self.tmp, "raw", "rate.wav")
        fx.clean_two_channel(wav, rate=44100)
        rec = fx.latency_record(session, self.tmp, "asio_48k_128",
                                interfaces[0], "raw/rate.wav")
        # The record was ingested against the canonical 48 kHz expectation and
        # is already invalid; tamper the params to claim 44.1 kHz acceptance.
        rec["analysis"]["params"]["expected_rate"] = 44100
        self.save("latency.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("params-expected-rate-tampered", self.rules(report))

    def test_expected_channels_tamper_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"]["expected_channels"] = 1
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("params-expected-channels-tampered", self.rules(report))


class F2MeasuredValueTests(Base):
    def test_measured_true_null_value_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("play-clean_strumming.json")
        rec["callback"]["p99_ms"]["value"] = None
        self.save("play-clean_strumming.json", rec)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 1)
        self.assertIn("measured-value-type", self.rules(report))

    def test_all_null_play_does_not_pass(self):
        interfaces = [fx.asio_interface("iface-asio-128", 128)]
        session = fx.make_session(self.tmp, interfaces)
        rec = fx.play_record(session, self.tmp, "clean_strumming", interfaces[0],
                             useful_lock="unknown",
                             start_requested_s=None, join_heard_s=None,
                             stop_s=None, lock_window_bars=None,
                             time_to_lock_s=None, dropouts=None,
                             callback_p50_ms=None, callback_p99_ms=None,
                             callback_deadline_misses=None,
                             analysis_overruns=None)
        self.save("play.json", rec)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 2)
        self.assertFalse(report["gates"]["play_trials"]["pass"])
        self.assertEqual(report["cells"]["clean_strumming"]["status"],
                         "unmeasured")


class F3MonitoringTests(Base):
    def test_direct_hardware_monitoring_fails_latency(self):
        iface = fx.asio_interface("iface-asio-128", 128,
                                  monitoring="direct-hardware")
        session = fx.make_session(self.tmp, [iface])
        wav = os.path.join(self.tmp, "raw", "clean.wav")
        fx.clean_two_channel(wav)
        rec = fx.latency_record(session, self.tmp, "asio_48k_128", iface,
                                "raw/clean.wav")
        self.assertFalse(rec["analysis"]["valid"])
        self.assertIn("monitoring-not-software-app", rec["analysis"]["reasons"])
        self.save("latency.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertEqual(report["cells"]["asio_48k_128"]["status"], "fail")

    def test_monitoring_identity_mismatch_is_hard_error(self):
        session = fx.complete_matrix(self.tmp)
        spath = os.path.join(self.tmp, "session.json")
        for iface in session["interfaces"]:
            if iface["id"] == "iface-asio-128":
                iface["monitoring"] = "direct-hardware"
        dl.write_json(spath, session)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("interface-identity-mismatch", self.rules(report))


class F4ReceiptTests(Base):
    def test_unparseable_functional_receipt_is_hard_error(self):
        iface = fx.asio_interface("iface-asio-128", 128)
        session = fx.make_session(self.tmp, [iface])
        rec = fx.functional_record(session, self.tmp, "silent_input", iface,
                                   as_receipt=False)
        self.save("functional.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("receipt-not-parseable", self.rules(report))

    def test_zero_byte_functional_receipt_rejected_at_ingest(self):
        iface = fx.asio_interface("iface-asio-128", 128)
        fx.make_session(self.tmp, [iface])
        empty = os.path.join(self.tmp, "raw", "empty.json")
        open(empty, "w").close()
        code = ifn.main(["--session", self.tmp, "--condition", "silent_input",
                         "--outcome", "pass", "--receipt", "raw/empty.json"])
        self.assertEqual(code, 1)

    def test_receipt_attested_play_does_not_gate(self):
        iface = fx.asio_interface("iface-asio-128", 128)
        session = fx.make_session(self.tmp, [iface])
        rec = fx.play_record(session, self.tmp, "clean_strumming", iface,
                             as_receipt=False)
        self.save("play.json", rec)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 2)
        self.assertFalse(report["gates"]["play_trials"]["pass"])

    def test_receipt_metric_mismatch_is_hard_error(self):
        iface = fx.asio_interface("iface-asio-128", 128)
        session = fx.make_session(self.tmp, [iface])
        rec = fx.play_record(session, self.tmp, "clean_strumming", iface)
        # Point one field at a *different* parseable receipt carrying 9.9 ms.
        ref, _doc = fx.receipt_json(self.tmp, session, iface, "play-trial",
                                    {"callback": {"p99_ms": 9.9}},
                                    "other-receipt.json")
        rec["callback"]["p99_ms"]["receipt"] = ref
        self.save("play.json", rec)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 1)
        self.assertIn("receipt-metric-mismatch", self.rules(report))


class F5MalformedParamsTests(Base):
    def test_empty_params_is_hard_error_without_crash(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"] = {}
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("analysis-params-invalid", self.rules(report))
        self.assertNotIn("validator-crash", self.rules(report))

    def test_banana_method_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"]["method"] = "banana"
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("analysis-params-invalid", self.rules(report))

    def test_missing_channel_key_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["analysis"]["params"].pop("loopback_channel")
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("analysis-params-invalid", self.rules(report))


class F6AggregationTests(Base):
    def test_fail_not_masked_by_pass(self):
        session = fx.complete_matrix(self.tmp)
        iface = [i for i in session["interfaces"] if i["id"] == "iface-asio-128"][0]
        bad = os.path.join(self.tmp, "raw", "bad.wav")
        fx.clean_two_channel(bad, delay=624)  # 13 ms > 12 ms target
        rec = fx.latency_record(session, self.tmp, "asio_48k_128", iface,
                                "raw/bad.wav")
        self.assertEqual(rec["analysis"]["valid"], True)
        self.save("latency-asio_48k_128-regression.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertNotEqual(code, 0)
        cell = report["cells"]["asio_48k_128"]
        self.assertEqual(cell["status"], "fail")
        self.assertTrue(cell["conflicts"])
        self.assertGreaterEqual(len(cell["observations"]), 2)


class SyntheticSessionTests(Base):
    def test_synthetic_session_records_forced_out_even_if_false(self):
        fx.complete_matrix(self.tmp, synthetic=True)
        # Flip every record's own flag to false; the session is still synthetic.
        for name in os.listdir(os.path.join(self.tmp, "measurements")):
            rec = self.load(name)
            rec["synthetic"] = False
            self.save(name, rec)
        code, report = self.run_validator(
            ["--gate", "hardware", "--allow-synthetic-selftest"])
        self.assertEqual(code, 2)
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertTrue(report["matrix_empty"])
        # Without the allow flag it is a hard error.
        code2, report2 = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code2, 1)
        self.assertIn("synthetic", self.rules(report2))


class IdentityTests(Base):
    def test_non_hex_source_sha_is_hard_error(self):
        fx.complete_matrix(self.tmp, source_sha="not-a-git-sha")
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("session-source-sha", self.rules(report))

    def test_absolute_raw_path_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["identity"]["raw_wav"]["path"] = "/etc/hostname"
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("raw-path-not-contained", self.rules(report))

    def test_dotdot_raw_path_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec["identity"]["raw_wav"]["path"] = "../escape.wav"
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("raw-path-not-contained", self.rules(report))

    def test_missing_required_key_is_hard_error(self):
        fx.complete_matrix(self.tmp)
        rec = self.load("latency-asio_48k_128.json")
        rec.pop("analysis")
        self.save("latency-asio_48k_128.json", rec)
        code, report = self.run_validator(["--gate", "hardware"])
        self.assertEqual(code, 1)
        self.assertIn("missing-key", self.rules(report))


class CallbackDeadlineTests(Base):
    def _single_play(self, **over):
        iface = fx.asio_interface("iface-asio-128", 128)
        session = fx.make_session(self.tmp, [iface])
        rec = fx.play_record(session, self.tmp, "clean_strumming", iface, **over)
        self.save("play.json", rec)

    def test_p99_over_70_percent_blocks_play(self):
        self._single_play(callback_p99_ms=2.0)  # 70% of 128/48000 = 1.867 ms
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 2)
        self.assertEqual(report["cells"]["clean_strumming"]["status"], "fail")
        self.assertFalse(report["gates"]["callback_deadline"]["pass"])

    def test_p99_missing_blocks_play(self):
        self._single_play(callback_p99_ms=None)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 2)
        self.assertEqual(report["cells"]["clean_strumming"]["status"],
                         "unmeasured")
        self.assertFalse(report["gates"]["play_trials"]["pass"])
        self.assertFalse(report["gates"]["callback_deadline"]["pass"])

    def test_dropouts_unmeasured_blocks_play(self):
        self._single_play(dropouts=None)
        code, report = self.run_validator(["--gate", "play"])
        self.assertEqual(code, 2)
        self.assertEqual(report["cells"]["clean_strumming"]["status"],
                         "unmeasured")
        self.assertFalse(report["gates"]["play_trials"]["pass"])


if __name__ == "__main__":
    unittest.main()
