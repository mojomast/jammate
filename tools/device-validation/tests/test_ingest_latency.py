"""End-to-end tests for session init, latency ingest and the synthetic harness."""
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402
import init_session as init  # noqa: E402
import ingest_latency as il  # noqa: E402
import make_synthetic_evidence as mse  # noqa: E402
import validate_evidence as ve  # noqa: E402


class InitSessionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-init-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_creates_manifest_and_refuses_nonempty(self):
        out = os.path.join(self.tmp, "sess")
        self.assertEqual(init.main(["--out", out, "--backend", "asio",
                                    "--driver", "Test ASIO", "--os", "windows",
                                    "--sample-rate", "48000", "--block", "128",
                                    "--source-sha", "b" * 40]), 0)
        session = dl.load_json_strict(os.path.join(out, "session.json"))
        self.assertEqual(session["schema"], dl.SESSION_SCHEMA)
        self.assertEqual(session["interfaces"][0]["backend"], "asio")
        self.assertEqual(session["measurements"], [])
        self.assertFalse(session["synthetic"])
        self.assertEqual(init.main(["--out", out]), 2)
        self.assertEqual(init.main(["--out", out, "--force"]), 0)


class IngestLatencyCliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-ing-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _session(self):
        out = os.path.join(self.tmp, "sess")
        init.main(["--out", out, "--backend", "asio", "--driver", "Test ASIO",
                   "--os", "windows", "--interface-id", "iface-asio-128",
                   "--sample-rate", "48000", "--block", "128"])
        return out

    def test_cli_recovers_latency(self):
        out = self._session()
        wav = os.path.join(out, "raw", "clean.wav")
        fx.clean_two_channel(wav, delay=144)
        code = il.main(["--session", out, "--wav", "raw/clean.wav",
                        "--condition", "asio_48k_128"])
        self.assertEqual(code, 0)
        rec = dl.load_json_strict(os.path.join(
            out, "measurements", "latency-asio_48k_128.json"))
        self.assertTrue(rec["analysis"]["valid"])
        self.assertAlmostEqual(rec["analysis"]["physical_roundtrip_latency_ms"],
                               1000.0 * 144 / 48000.0, places=6)
        self.assertTrue(dl.is_hex64(rec["identity"]["raw_wav"]["sha256"]))

    def test_cli_reports_invalid_but_writes_diagnostics(self):
        out = self._session()
        wav = os.path.join(out, "raw", "silent.wav")
        ref = dl.make_click(48000, total_ms=100.0)
        dl.write_wav(wav, [[0.0] * len(ref), ref], 48000, "pcm16")
        code = il.main(["--session", out, "--wav", "raw/silent.wav",
                        "--condition", "asio_48k_128"])
        self.assertEqual(code, 3)
        rec = dl.load_json_strict(os.path.join(
            out, "measurements", "latency-asio_48k_128.json"))
        self.assertFalse(rec["analysis"]["valid"])
        self.assertIn("silent", rec["analysis"]["reasons"])
        self.assertIsNone(rec["analysis"]["physical_roundtrip_latency_ms"])


class SyntheticHarnessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-syn-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_recovered_delay_is_exact_and_gates_fail(self):
        out = os.path.join(self.tmp, "synthetic")
        info = mse.generate(out)
        self.assertAlmostEqual(info["clean_latency_ms"],
                               info["expected_latency_ms"], places=6)
        code = ve.main(["--session", out, "--allow-synthetic-selftest",
                        "--gate", "all", "--out", os.path.join(out, "report.json")])
        report = dl.load_json_strict(os.path.join(out, "report.json"))
        self.assertEqual(code, 2)
        self.assertFalse(report["overall_pass"])
        self.assertFalse(report["gates"]["hardware_matrix"]["pass"])
        self.assertTrue(report["gates"]["synthetic_selftest"]["pass"])
        self.assertEqual(report["hard_errors"], [])

    def test_generated_wavs_hashed_in_session(self):
        out = os.path.join(self.tmp, "synthetic")
        mse.generate(out)
        session = dl.load_json_strict(os.path.join(out, "session.json"))
        self.assertTrue(session["raw_files"])
        for ref in session["raw_files"]:
            resolved = dl.resolve_path(out, ref["path"])
            self.assertTrue(os.path.isfile(resolved))
            self.assertEqual(dl.sha256_file(resolved), ref["sha256"])


if __name__ == "__main__":
    unittest.main()
