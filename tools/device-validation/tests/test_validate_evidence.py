"""Validator tests: gate aggregation, anti-fabrication and synthetic exclusion.

The "complete matrix" fixtures are constructed in a temp directory purely to
exercise the aggregation logic; they are never committed or presented as a
real hardware run. The synthetic test proves the opposite direction: even a
fully well-formed synthetic matrix cannot pass a physical gate.
"""
import copy
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402
import validate_evidence as ve  # noqa: E402


class ValidatorBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-val-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_validator(self, session_dir, extra=None):
        argv = ["--session", session_dir, "--out",
                os.path.join(session_dir, "report.json")]
        argv += extra or []
        code = ve.main(argv)
        report = dl.load_json_strict(os.path.join(session_dir, "report.json"))
        return code, report

    def populate_latency_cells(self, session, synthetic=False, delay=96):
        interfaces = session["interfaces"]
        wav = os.path.join(self.tmp, "clean.wav")
        fx.clean_two_channel(wav, delay=delay)
        rel = os.path.relpath(wav, self.tmp)
        # place wav inside session raw/
        raw = os.path.join(self.tmp, "raw", "clean.wav")
        shutil.copy(wav, raw)
        rel = "raw/clean.wav"
        by_id = {i["id"]: i for i in interfaces}
        for cond, iface_id in (("asio_48k_64", "iface-asio-64"),
                               ("asio_48k_128", "iface-asio-128"),
                               ("asio_48k_256", "iface-asio-256"),
                               ("wasapi_low_latency", "iface-wasapi")):
            iface = by_id[iface_id]
            rec = fx.latency_record(session, self.tmp, cond, iface, rel,
                                    synthetic=synthetic)
            fx.write_record(self.tmp, "latency-%s.json" % cond, rec)

    def populate_functional_and_play(self, session, synthetic=False):
        interfaces = session["interfaces"]
        iface = {i["id"]: i for i in interfaces}["iface-asio-128"]
        receipt = fx.text_receipt(self.tmp, "log.txt")
        for cond in dl.FUNCTIONAL_CONDITIONS:
            rec = fx.functional_record(session, self.tmp, cond, iface, receipt,
                                       synthetic=synthetic)
            fx.write_record(self.tmp, "functional-%s.json" % cond, rec)
        for cond in dl.PLAY_CONDITIONS:
            rec = fx.play_record(session, self.tmp, cond, iface, receipt,
                                 synthetic=synthetic)
            fx.write_record(self.tmp, "play-%s.json" % cond, rec)

    def make_complete_session(self, synthetic=False):
        interfaces = [fx.asio_interface("iface-asio-64", 64),
                      fx.asio_interface("iface-asio-128", 128),
                      fx.asio_interface("iface-asio-256", 256),
                      fx.wasapi_interface()]
        session = fx.make_session(self.tmp, interfaces, synthetic=synthetic)
        self.populate_latency_cells(session, synthetic=synthetic)
        self.populate_functional_and_play(session, synthetic=synthetic)
        return session


class EmptyMatrixTests(ValidatorBase):
    def test_empty_matrix_never_passes(self):
        session = fx.make_session(self.tmp, [fx.asio_interface("iface-asio-128", 128)])
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertEqual(code, 2)
        self.assertFalse(report["overall_pass"])
        self.assertTrue(report["matrix_empty"])
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertTrue(report["awaiting_physical_evidence"])


class SyntheticExclusionTests(ValidatorBase):
    def test_synthetic_matrix_cannot_pass_physical_gates(self):
        self.make_complete_session(synthetic=True)
        code, report = self.run_validator(
            self.tmp, ["--gate", "all", "--allow-synthetic-selftest"])
        self.assertEqual(code, 2)
        self.assertFalse(report["overall_pass"])
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertFalse(report["gates"]["play_trials"]["pass"])
        self.assertTrue(report["gates"]["synthetic_selftest"]["pass"])
        self.assertTrue(report["matrix_empty"])

    def test_synthetic_rejected_without_allow_flag(self):
        self.make_complete_session(synthetic=True)
        code, report = self.run_validator(self.tmp, ["--gate", "all"])
        self.assertEqual(code, 1)
        self.assertTrue(report["hard_errors"])


class CompleteMatrixTests(ValidatorBase):
    def test_complete_physical_matrix_passes_all_gates(self):
        self.make_complete_session(synthetic=False)
        code, report = self.run_validator(self.tmp, ["--gate", "all"])
        self.assertEqual(code, 0, msg=report["hard_errors"])
        self.assertTrue(report["overall_pass"])
        self.assertTrue(report["gates"]["hardware_matrix"]["pass"])
        self.assertTrue(report["gates"]["monitoring_latency"]["pass"])
        self.assertTrue(report["gates"]["play_trials"]["pass"])

    def test_hardware_only_gate_ignores_play(self):
        interfaces = [fx.asio_interface("iface-asio-64", 64),
                      fx.asio_interface("iface-asio-128", 128),
                      fx.asio_interface("iface-asio-256", 256),
                      fx.wasapi_interface()]
        session = fx.make_session(self.tmp, interfaces)
        self.populate_latency_cells(session)
        self.populate_functional_and_play(session)
        # remove play records: hardware gate still passes
        for name in os.listdir(os.path.join(self.tmp, "measurements")):
            if name.startswith("play-"):
                os.remove(os.path.join(self.tmp, "measurements", name))
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertEqual(code, 0, msg=report["hard_errors"])
        self.assertTrue(report["gates"]["hardware_matrix"]["pass"])
        self.assertFalse(report["gates"]["play_trials"]["pass"])


class MonitoringTargetTests(ValidatorBase):
    def test_over_target_fails_monitoring_gate(self):
        interfaces = [fx.asio_interface("iface-asio-64", 64),
                      fx.asio_interface("iface-asio-128", 128),
                      fx.asio_interface("iface-asio-256", 256),
                      fx.wasapi_interface()]
        session = fx.make_session(self.tmp, interfaces)
        # 13 ms > 12 ms target at 48 kHz = 624 samples
        self.populate_latency_cells(session, delay=624)
        self.populate_functional_and_play(session)
        code, report = self.run_validator(self.tmp, ["--gate", "all"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["gates"]["monitoring_latency"]["pass"])
        self.assertEqual(report["cells"]["asio_48k_128"]["status"], "fail")


class AntiFabricationTests(ValidatorBase):
    def test_edited_latency_value_is_rejected(self):
        self.make_complete_session(synthetic=False)
        path = os.path.join(self.tmp, "measurements", "latency-asio_48k_128.json")
        rec = dl.load_json_strict(path)
        rec["analysis"]["physical_roundtrip_latency_ms"] = 1.0
        dl.write_json(path, rec)
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertEqual(code, 1)
        rules = [e["rule"] for e in report["hard_errors"]]
        self.assertIn("latency-recheck-value", rules)

    def test_tampered_raw_bytes_are_rejected(self):
        self.make_complete_session(synthetic=False)
        raw = os.path.join(self.tmp, "raw", "clean.wav")
        with open(raw, "r+b") as fh:
            fh.seek(0)
            first = fh.read(1)
            fh.seek(0)
            fh.write(bytes([first[0] ^ 0xFF]))
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertEqual(code, 1)
        rules = [e["rule"] for e in report["hard_errors"]]
        self.assertIn("raw-hash-mismatch", rules)

    def test_missing_driver_fails_asio_cell(self):
        interfaces = [fx.asio_interface("iface-asio-128", 128)]
        interfaces[0]["driver"] = None
        session = fx.make_session(self.tmp, interfaces)
        raw = os.path.join(self.tmp, "raw", "clean.wav")
        fx.clean_two_channel(raw)
        rec = fx.latency_record(session, self.tmp, "asio_48k_128",
                                interfaces[0], "raw/clean.wav")
        fx.write_record(self.tmp, "latency-asio_48k_128.json", rec)
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertEqual(report["cells"]["asio_48k_128"]["status"], "fail")

    def test_identity_mismatch_is_hard_error(self):
        self.make_complete_session(synthetic=False)
        path = os.path.join(self.tmp, "measurements", "latency-asio_48k_128.json")
        rec = dl.load_json_strict(path)
        rec["identity"]["interface"]["driver"] = "Sneaky Driver"
        dl.write_json(path, rec)
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertEqual(code, 1)
        rules = [e["rule"] for e in report["hard_errors"]]
        self.assertIn("interface-identity-mismatch", rules)

    def test_functional_fail_cell_not_pass(self):
        interfaces = [fx.asio_interface("iface-asio-128", 128)]
        session = fx.make_session(self.tmp, interfaces)
        receipt = fx.text_receipt(self.tmp, "log.txt")
        for cond in dl.FUNCTIONAL_CONDITIONS:
            outcome = "fail" if cond == "device_disconnect_reconnect" else "pass"
            rec = fx.functional_record(session, self.tmp, cond, interfaces[0],
                                       receipt, outcome=outcome)
            fx.write_record(self.tmp, "functional-%s.json" % cond, rec)
        code, report = self.run_validator(self.tmp, ["--gate", "hardware"])
        self.assertNotEqual(code, 0)
        self.assertEqual(report["cells"]["device_disconnect_reconnect"]["status"],
                         "fail")
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])


class SummaryTests(ValidatorBase):
    def test_summary_markdown_written(self):
        session = fx.make_session(self.tmp, [fx.asio_interface("iface-asio-128", 128)])
        out = os.path.join(self.tmp, "summary.md")
        ve.main(["--session", self.tmp, "--summary-md", out, "--gate", "hardware"])
        text = open(out, encoding="utf-8").read()
        self.assertIn("Device-validation report", text)
        self.assertIn("hardware_matrix", text)


if __name__ == "__main__":
    unittest.main()
