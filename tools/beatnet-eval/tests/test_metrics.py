"""Unit tests for the dependency-free metric math.

These use small hand-written series to exercise arithmetic. They are not
BeatNet observations and produce no BeatNet evidence.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "beatnet_eval"))
import metrics  # noqa: E402


class MatchBeatsTest(unittest.TestCase):
    def test_perfect(self):
        tp, fp, fn, matches = metrics.match_beats([1.0, 2.0, 3.0], [1.0, 2.0, 3.0], 0.07)
        self.assertEqual((tp, fp, fn), (3, 0, 0))
        self.assertEqual(metrics.prf(tp, fp, fn)["fMeasure"], 1.0)

    def test_within_tolerance_matches(self):
        tp, fp, fn, _ = metrics.match_beats([1.03], [1.0], 0.07)
        self.assertEqual((tp, fp, fn), (1, 0, 0))

    def test_outside_tolerance_is_a_miss_and_extra(self):
        tp, fp, fn, _ = metrics.match_beats([1.2], [1.0], 0.07)
        self.assertEqual((tp, fp, fn), (0, 1, 1))

    def test_missing_and_extra(self):
        tp, fp, fn, _ = metrics.match_beats([1.0, 2.0, 4.0], [1.0, 2.0, 3.0], 0.07)
        self.assertEqual((tp, fp, fn), (2, 1, 1))

    def test_phase_stats(self):
        _, _, _, matches = metrics.match_beats([1.02, 2.0, 3.0], [1.0, 2.0, 3.0], 0.07)
        stats = metrics.phase_stats_ms(matches)
        self.assertEqual(stats["phaseMatchedBeats"], 3)
        self.assertAlmostEqual(stats["phaseMeanAbsMs"], 20.0 / 3.0, places=4)

    def test_phase_none_without_match(self):
        stats = metrics.phase_stats_ms([])
        self.assertIsNone(stats["phaseMeanMs"])

    def test_implied_bpm(self):
        self.assertAlmostEqual(metrics.implied_bpm([0.0, 0.5, 1.0]), 120.0)
        self.assertIsNone(metrics.implied_bpm([1.0]))

    def test_availability_excludes_none(self):
        stats = metrics.availability_stats_ms(
            [{"time": 1.0, "available": 1.02}, {"time": 2.0, "available": None}]
        )
        self.assertEqual(stats["availabilitySamples"], 1)
        self.assertAlmostEqual(stats["availabilityMeanMs"], 20.0, places=6)
        self.assertAlmostEqual(stats["availabilityMaxMs"], 20.0, places=6)

    def test_availability_none_when_absent(self):
        stats = metrics.availability_stats_ms([{"time": 1.0, "available": None}])
        self.assertEqual(stats["availabilitySamples"], 0)
        self.assertIsNone(stats["availabilityMeanMs"])

    def test_relative_error(self):
        self.assertAlmostEqual(metrics.relative_error(102.0, 100.0), 0.02)
        self.assertIsNone(metrics.relative_error(None, 100.0))
        self.assertIsNone(metrics.relative_error(100.0, None))


if __name__ == "__main__":
    unittest.main()
