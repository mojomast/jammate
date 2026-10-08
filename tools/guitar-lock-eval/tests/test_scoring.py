"""Scorer-only tests: synthetic traces demonstrate the useful-lock verdict.

Nothing here is real-guitar evidence; every object is built in memory and
labelled synthetic-test.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from glock.scoring import Criteria, score_useful_lock  # noqa: E402
from tests import synth  # noqa: E402

CRIT = Criteria()


class UsefulLockTests(unittest.TestCase):
    def test_perfect_lock_is_useful(self):
        rec = synth.make_recording()
        score = score_useful_lock(rec, synth.perfect_trace(), CRIT)
        self.assertTrue(score.acquired)
        self.assertTrue(score.acquired_within_window)
        self.assertTrue(score.tempo_correct)
        self.assertTrue(score.phase_usable)
        self.assertTrue(score.useful_lock)
        self.assertFalse(score.false_lock)
        self.assertLessEqual(score.acquisition_bars, 2.0)

    def test_half_time_is_separate_not_useful(self):
        rec = synth.make_recording(nominal_bpm=120.0, annotation=synth.make_annotation(120.0))
        trace = synth.perfect_trace(nominal_bpm=120.0, bpm=60.0)
        score = score_useful_lock(rec, trace, CRIT)
        self.assertTrue(score.half_time_lock)
        self.assertFalse(score.double_time_lock)
        self.assertFalse(score.tempo_correct)
        self.assertFalse(score.useful_lock)
        # A positional grid with wrong tempo is a false lock, not no lock.
        self.assertTrue(score.false_lock_phase_ok_tempo_wrong)

    def test_double_time_is_separate_not_useful(self):
        rec = synth.make_recording()
        trace = synth.perfect_trace(nominal_bpm=120.0, bpm=240.0)
        score = score_useful_lock(rec, trace, CRIT)
        self.assertTrue(score.double_time_lock)
        self.assertFalse(score.half_time_lock)
        self.assertFalse(score.useful_lock)

    def test_phase_wrong_tempo_ok_is_false_lock(self):
        rec = synth.make_recording()
        trace = synth.offset_beats_trace(first_correct_index=32)  # all half-beat off
        score = score_useful_lock(rec, trace, CRIT)
        self.assertFalse(score.acquired)
        self.assertTrue(score.false_lock_tempo_ok_phase_wrong)
        self.assertTrue(score.false_lock)
        self.assertFalse(score.useful_lock)

    def test_lock_after_two_bars_is_not_useful(self):
        rec = synth.make_recording(annotation=synth.make_annotation(120.0, n_beats=32))
        # first 12 beats (3 bars) are half a beat off, then a correct lock
        trace = synth.offset_beats_trace(first_correct_index=12)
        score = score_useful_lock(rec, trace, CRIT)
        self.assertTrue(score.acquired)
        self.assertFalse(score.acquired_within_window)
        self.assertGreater(score.acquisition_bars, 2.0)
        self.assertFalse(score.useful_lock)

    def test_no_beats_is_no_lock(self):
        rec = synth.make_recording()
        trace = synth.perfect_trace()
        trace.beats = []
        trace.tempo_samples = []
        score = score_useful_lock(rec, trace, CRIT)
        self.assertFalse(score.acquired)
        self.assertTrue(score.no_lock)

    def test_holdover_reported_separately(self):
        rec = synth.make_recording(
            annotation=synth.make_annotation(120.0, true_silence_spans=[[1.0, 1.5]]))
        trace = synth.perfect_trace()
        score = score_useful_lock(rec, trace, CRIT)
        self.assertTrue(score.acquired)
        self.assertTrue(score.holdover)
        # Holdover does not silently change the useful-lock verdict.
        self.assertTrue(score.useful_lock)

    def test_zero_nominal_bpm_ramp_has_no_tempo_correct(self):
        ann = synth.make_annotation(120.0)
        ann.tempo_profile = "linear-ramp"
        ann.nominal_bpm = None
        rec = synth.make_recording(annotation=ann)
        score = score_useful_lock(rec, synth.perfect_trace(), CRIT)
        self.assertFalse(score.tempo_correct)
        self.assertFalse(score.useful_lock)


if __name__ == "__main__":
    unittest.main()
