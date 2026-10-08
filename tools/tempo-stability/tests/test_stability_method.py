#!/usr/bin/env python3
"""TRACK-008 independent spec-conformance tests for the confirmation-gated
median (CGM). This is a second, independent implementation of the method
described in docs/research/tempo-stability/PROTOCOL.md section 3.2, used to
exercise the declared semantics (startup fallback, post-ready jitter, outlier
robustness, missing/invalid/gap intervals, clock/rate behaviour). It also parses
the committed C++ header and asserts the frozen constants match the protocol.
Run:  python3 tools/tempo-stability/tests/test_stability_method.py
"""
import math
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

RING_CAPACITY = 4
CONFIRM_UPDATES = 3
AGREEMENT = 0.02
MIN_INTERVAL = 60.0 / 240.0
MAX_INTERVAL = 60.0 / 40.0


class CgmReference:
    """Independent Python implementation of the predeclared CGM method."""

    def __init__(self, rate=48000.0):
        self.rate = rate if (math.isfinite(rate) and rate > 0) else 48000.0
        self.reset()

    def reset(self):
        self.ring = []
        self.have_prev_derived = False
        self.prev_derived = 0.0
        self.stable = 0
        self.confirmed = False
        self._prev_sample = None

    @staticmethod
    def _median4(ring):
        a = sorted(ring)
        return 0.5 * (a[1] + a[2])

    def _accept(self, interval):
        if len(self.ring) < RING_CAPACITY:
            self.ring.append(interval)
        else:
            self.ring = self.ring[1:] + [interval]
        if len(self.ring) == RING_CAPACITY:
            derived = 60.0 / self._median4(self.ring)
            if self.have_prev_derived:
                if abs(derived - self.prev_derived) <= AGREEMENT * self.prev_derived:
                    self.stable += 1
                else:
                    self.stable = 0
            else:
                self.have_prev_derived = True
                self.stable = 0
            self.prev_derived = derived
            if self.stable >= CONFIRM_UPDATES:
                self.confirmed = True

    def beat(self, sample, base_bpm):
        """Emit a beat at `sample`; returns the emitted bpmCandidate."""
        if self._prev_sample is None:
            self._prev_sample = sample
        elif sample <= self._prev_sample:
            self.reset()
            self._prev_sample = sample
        else:
            interval = (sample - self._prev_sample) / self.rate
            if not math.isfinite(interval) or interval < MIN_INTERVAL or interval > MAX_INTERVAL:
                self.reset()
            else:
                self._accept(interval)
            self._prev_sample = sample
        return self.derived_bpm if self.confirmed else base_bpm

    def gap_reset(self):
        self.reset()

    @property
    def derived_bpm(self):
        if len(self.ring) == RING_CAPACITY:
            return 60.0 / self._median4(self.ring)
        return 0.0


K = 24000  # 0.5 s at 48 kHz -> 120 BPM


class TestCgmSpec(unittest.TestCase):
    def test_startup_fallback_and_confirmation_timing(self):
        t = CgmReference(48000.0)
        base = 100.0
        for i in range(4):
            self.assertEqual(t.beat(i * K, base), base, "pre-ring forwards base")
        self.assertEqual(len(t.ring), 3)
        self.assertEqual(t.beat(4 * K, base), base, "first full-ring forwards base")
        self.assertEqual(len(t.ring), 4)
        self.assertFalse(t.confirmed)
        self.assertEqual(t.stable, 0)
        self.assertEqual(t.beat(5 * K, base), base)
        self.assertEqual(t.stable, 1)
        self.assertEqual(t.beat(6 * K, base), base)
        self.assertEqual(t.stable, 2)
        e = t.beat(7 * K, base)
        self.assertTrue(t.confirmed)
        self.assertAlmostEqual(e, 120.0, places=6)

    def test_jitter_resets_counter(self):
        t = CgmReference()
        base = 100.0
        for i in range(6):
            t.beat(i * K, base)
        self.assertEqual(t.stable, 1)
        t.beat(5 * K + 28800, base)       # first 0.6 s keeps median 0.5 -> agrees
        self.assertEqual(t.stable, 2)
        e = t.beat(5 * K + 2 * 28800, base)  # second 0.6 s shifts median -> jump
        self.assertEqual(t.stable, 0)
        self.assertFalse(t.confirmed)
        self.assertEqual(e, base)

    def test_median_robust_to_single_outlier(self):
        t = CgmReference()
        base = 100.0
        t.beat(0, base)
        t.beat(K, base)
        t.beat(2 * K, base)
        t.beat(3 * K, base)
        t.beat(3 * K + 14400, base)   # 0.30 s newest; ring median still 0.5
        self.assertAlmostEqual(t.derived_bpm, 120.0, places=6)

    def test_first_beat_missing(self):
        t = CgmReference()
        t.beat(5000, 100.0)
        self.assertEqual(len(t.ring), 0)
        t.beat(5000 + K, 100.0)
        self.assertEqual(len(t.ring), 1)

    def test_duplicate_and_non_monotonic_missing(self):
        t = CgmReference()
        t.beat(0, 100.0)
        t.beat(K, 100.0)
        self.assertEqual(len(t.ring), 1)
        t.beat(K, 100.0)                 # duplicate -> reset
        self.assertEqual(len(t.ring), 0)
        t.beat(2 * K, 100.0)
        self.assertEqual(len(t.ring), 1)
        t.beat(K + 100, 100.0)           # backwards -> reset
        self.assertEqual(len(t.ring), 0)

    def test_gap_and_malformed_reset(self):
        t = CgmReference()
        t.beat(0, 100.0)
        t.beat(K, 100.0)
        t.beat(2 * K, 100.0)
        self.assertEqual(len(t.ring), 2)
        t.beat(2 * K + 2 * 48000, 100.0)  # > 1.50 s -> gap reset
        self.assertEqual(len(t.ring), 0)
        t.beat(3 * K, 100.0)
        t.beat(3 * K + 4800, 100.0)       # < 0.25 s -> malformed reset
        self.assertEqual(len(t.ring), 0)

    def test_sample_clock_and_rate(self):
        t = CgmReference(44100.0)
        t.beat(0, 100.0)
        t.beat(22050, 100.0)             # 0.5 s at 44.1 kHz
        self.assertEqual(len(t.ring), 1)
        t2 = CgmReference(-1.0)          # invalid rate falls back to 48 kHz
        t2.beat(0, 100.0)
        t2.beat(24000, 100.0)
        self.assertEqual(len(t2.ring), 1)

    def test_boundary_interval_accepted(self):
        t = CgmReference()
        t.beat(0, 100.0)
        t.beat(12000, 100.0)             # exactly 0.25 s -> 240 BPM
        self.assertEqual(len(t.ring), 1)
        self.assertAlmostEqual(t.derived_bpm, 0.0)  # ring not full yet
        for i in range(2, 8):
            t.beat(i * 12000, 100.0)
        self.assertTrue(t.confirmed)
        self.assertAlmostEqual(t.derived_bpm, 240.0, places=6)


class TestCommittedConstants(unittest.TestCase):
    def test_header_constants_match_protocol(self):
        header = (ROOT / "tools/tempo-stability/TempoStable.h").read_text()
        def value(name):
            m = re.search(r"inline constexpr (?:std::size_t|int|double)\s+%s\s*=\s*([^;]+);" % name,
                          header)
            self.assertIsNotNone(m, "missing constant %s" % name)
            expr = m.group(1).strip()
            expr = expr.split("//")[0].strip()
            return eval(expr, {"__builtins__": {}}, {"kMaxBpm": 240.0, "kMinBpm": 40.0})
        self.assertEqual(value("kRingCapacity"), 4)
        self.assertEqual(value("kConfirmUpdates"), 3)
        self.assertAlmostEqual(value("kAgreementFraction"), 0.02)
        self.assertAlmostEqual(value("kMinIntervalSeconds"), 0.25)
        self.assertAlmostEqual(value("kMaxIntervalSeconds"), 1.5)

    def test_plugin_id_is_unique_and_honest(self):
        src = (ROOT / "tools/tempo-stability/TempoStable.cpp").read_text()
        self.assertIn('return "btrack-tempo-stable";', src)
        plugin = (ROOT / "tools/tempo-stability/TempoStablePlugin.cpp").read_text()
        self.assertIn("JAM_TEMPO_STABILITY_LOG_DIR", plugin)
        self.assertIn("jam::BTrackBackend", plugin)


if __name__ == "__main__":
    unittest.main(verbosity=2)
