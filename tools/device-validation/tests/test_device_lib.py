"""Unit tests for device_lib: WAV IO, analysis and fail-closed JSON."""
import math
import os
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402


class TempDir(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-lib-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def path(self, name):
        return os.path.join(self.tmp, name)


class WavIoTests(TempDir):
    def test_pcm16_roundtrip(self):
        left = [0.0, 0.5, -0.5, 1.0, -1.0]
        right = [0.25, -0.25, 0.0, 0.0, 0.75]
        p = self.path("a.wav")
        dl.write_wav(p, [left, right], 48000, "pcm16")
        wav = dl.read_wav(p)
        self.assertEqual(wav.sample_rate, 48000)
        self.assertEqual(wav.channels, 2)
        self.assertEqual(wav.format_code, 1)
        self.assertEqual(wav.frames, 5)
        self.assertEqual(wav.clip_counts, [1, 0])
        self.assertAlmostEqual(wav.samples[0][1], 0.5, places=4)
        self.assertAlmostEqual(wav.samples[1][4], 0.75, places=4)

    def test_float32_roundtrip_preserves_nonfinite(self):
        p = self.path("f.wav")
        dl.write_wav(p, [[0.5, float("nan"), float("inf")]], 48000, "float32")
        wav = dl.read_wav(p)
        self.assertEqual(wav.format_code, 3)
        self.assertEqual(wav.nonfinite_counts[0], 2)
        self.assertTrue(math.isnan(wav.samples[0][1]))

    def test_24bit_supported(self):
        p = self.path("b24.wav")
        raw = bytearray()
        frames = [(0,), (8388607,), (-8388608,)]
        for (v,) in frames:
            raw += struct.pack("<i", v)[:3]
        fmt = struct.pack("<HHIIHH", 1, 1, 48000, 48000 * 3, 3, 24)
        chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt
                  + b"data" + struct.pack("<I", len(raw)) + bytes(raw))
        with open(p, "wb") as fh:
            fh.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)
        wav = dl.read_wav(p)
        self.assertEqual(wav.bits, 24)
        self.assertAlmostEqual(wav.samples[0][1], 8388607 / 8388608.0, places=6)

    def test_not_riff_rejected(self):
        p = self.path("bad.wav")
        with open(p, "wb") as fh:
            fh.write(b"not a wav at all")
        with self.assertRaises(dl.WavError):
            dl.read_wav(p)

    def test_truncated_rejected(self):
        p = self.path("trunc.wav")
        dl.write_wav(p, [[0.0] * 100], 48000, "pcm16")
        with open(p, "r+b") as fh:
            fh.seek(0, os.SEEK_END)
            fh.truncate(20)
        with self.assertRaises(dl.WavError):
            dl.read_wav(p)


class JsonTests(TempDir):
    def test_strict_rejects_nonfinite(self):
        p = self.path("j.json")
        with open(p, "w") as fh:
            fh.write('{"x": Infinity}')
        with self.assertRaises(ValueError):
            dl.load_json_strict(p)

    def test_write_rejects_nonfinite(self):
        with self.assertRaises(ValueError):
            dl.write_json(self.path("o.json"), {"x": float("nan")})

    def test_roundtrip(self):
        p = self.path("ok.json")
        dl.write_json(p, {"b": 1, "a": [1, 2]})
        self.assertEqual(dl.load_json_strict(p), {"a": [1, 2], "b": 1})


class AnalysisTests(TempDir):
    def test_quality_flags_clipping_and_nonfinite(self):
        q = dl.channel_quality([0.0, 1.0, -1.0, 2.0, float("nan")], clip_count=2)
        self.assertEqual(q["clipped_samples"], 2)
        self.assertEqual(q["nonfinite_samples"], 1)
        self.assertEqual(q["frames"], 5)

    def test_matched_filter_recovers_known_lag(self):
        rate = 48000
        ref = dl.make_click(rate, offset_ms=5.0, total_ms=120.0)
        delay = 137
        lp = [0.0] * len(ref)
        for i, v in enumerate(ref):
            j = i + delay
            if j < len(ref):
                lp[j] = v * 0.6
        onset = dl.threshold_onsets(ref, rate, -40.0)[0]
        template = ref[onset["sample"]:onset["sample"] + 960]
        scores = dl.matched_filter_scores(template, lp, onset["sample"], -480, 12000)
        best, second, dominance, ambiguous = dl.pick_candidates(scores, 0.5, 960)
        self.assertEqual(best[0], delay)
        self.assertFalse(ambiguous)

    def test_pick_candidates_flags_close_pair(self):
        scores = [(d, 0.9) for d in (100, 1100)]
        best, second, dominance, ambiguous = dl.pick_candidates(scores, 0.5, 960)
        self.assertIsNotNone(best)
        self.assertIsNotNone(second)
        self.assertAlmostEqual(dominance, 1.0, places=6)

    def test_analyze_two_channel_known_delay(self):
        p = self.path("clean.wav")
        delay = fx.clean_two_channel(p, delay=96)
        result = dl.analyze_loopback(p, fx.default_params(), base_dir=self.tmp)
        self.assertTrue(result["valid"])
        self.assertAlmostEqual(result["physical_roundtrip_latency_ms"],
                               1000.0 * delay / 48000.0, places=6)

    def test_analyze_silence_invalid(self):
        p = self.path("silent.wav")
        n = 4800
        dl.write_wav(p, [[0.0] * n, dl.make_click(48000, total_ms=100.0)],
                     48000, "pcm16")
        result = dl.analyze_loopback(p, fx.default_params(), base_dir=self.tmp)
        self.assertFalse(result["valid"])
        self.assertIn("silent", result["reasons"])

    def test_analyze_rate_mismatch_invalid(self):
        p = self.path("rate.wav")
        fx.clean_two_channel(p, rate=44100)
        params = fx.default_params()
        params["expected_rate"] = 48000
        result = dl.analyze_loopback(p, params, base_dir=self.tmp)
        self.assertFalse(result["valid"])
        self.assertIn("sample-rate-mismatch", result["reasons"])

    def test_analyze_clipped_invalid(self):
        p = self.path("clip.wav")
        ref = dl.make_click(48000, offset_ms=5.0, total_ms=120.0)
        lp = [max(-1.0, min(1.0, v * 20.0)) for v in ref]
        dl.write_wav(p, [lp, ref], 48000, "pcm16")
        result = dl.analyze_loopback(p, fx.default_params(), base_dir=self.tmp)
        self.assertFalse(result["valid"])
        self.assertIn("clipped-samples", result["reasons"])

    def test_analyze_nonfinite_invalid(self):
        p = self.path("nan.wav")
        ref = dl.make_click(48000, offset_ms=5.0, total_ms=120.0)
        lp = list(ref)
        lp[500] = float("nan")
        dl.write_wav(p, [lp, ref], 48000, "float32")
        result = dl.analyze_loopback(p, fx.default_params(), base_dir=self.tmp)
        self.assertFalse(result["valid"])
        self.assertIn("nonfinite-samples", result["reasons"])

    def test_analyze_single_channel_absolute(self):
        p = self.path("abs.wav")
        delay = 240
        ref = dl.make_click(48000, offset_ms=5.0, total_ms=120.0)
        lp = [0.0] * len(ref)
        for i, v in enumerate(ref):
            if i + delay < len(lp):
                lp[i + delay] = v
        dl.write_wav(p, [lp, ref], 48000, "pcm16")
        params = fx.default_params(method="single-channel", reference_onset_ms=5.0)
        result = dl.analyze_loopback(p, params, base_dir=self.tmp)
        self.assertTrue(result["valid"])
        # 5.0 ms reference vs onset at ~ 5.0 + 5.0 ms
        self.assertAlmostEqual(result["physical_roundtrip_latency_ms"],
                               1000.0 * delay / 48000.0, delta=1.0)


class InterfaceReasonTests(unittest.TestCase):
    def test_asio_requires_driver_and_windows(self):
        iface = fx.asio_interface("i", 128)
        iface["driver"] = None
        reasons = dl.interface_reasons("asio_48k_128", iface)
        self.assertIn("asio-driver-missing", reasons)

    def test_block_mismatch(self):
        iface = fx.asio_interface("i", 64)
        self.assertIn("interface-block-mismatch",
                      dl.interface_reasons("asio_48k_128", iface))


if __name__ == "__main__":
    unittest.main()
