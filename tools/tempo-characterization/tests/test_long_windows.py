#!/usr/bin/env python3
"""TRACK-007 meaningful checks: pattern/pairing, interval and comparison rules, pins.

Runs without rendering audio or executing trackers (the full run is evidence-checked
by run_long_windows.py itself). Run from the repository root.
"""
import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/tempo-characterization'))
import run_long_windows as run  # noqa: E402
import gen_long_windows as gen  # noqa: E402


def fixture(control, bpm=96.0, rate=48000):
    fx = gen.fixture_for(control, bpm, rate)
    gen.gf.prepare(fx)
    return fx


def events_for(control, bpm=96.0):
    fx = fixture(control, bpm)
    fx.events = fx.pattern(fx, random.Random(gen.seed_for(gen.perf_key(bpm))), fx.beats)
    return fx


class PatternPairingTests(unittest.TestCase):
    def test_seed_is_deterministic_and_tempo_keyed(self):
        self.assertEqual(gen.seed_for('perf|96bpm'), gen.seed_for('perf|96bpm'))
        self.assertNotEqual(gen.seed_for('perf|96bpm'), gen.seed_for('perf|126bpm'))

    def test_sparse_keeps_only_beats_one_and_three_of_each_bar(self):
        regular = events_for('regular')
        sparse = events_for('sparse')
        beat_times = [round(t, 9) for t in regular.beats]
        kept_beats = {beat_times.index(min(beat_times, key=lambda t: abs(t - e.time)))
                      for e in sparse.events}
        self.assertTrue(kept_beats and all(b % 4 in (0, 2) for b in kept_beats))
        self.assertEqual(len(sparse.events), len(regular.events) // 2)

    def test_sparse_and_gap_are_subsets_with_identical_times(self):
        regular = {round(e.time, 12) for e in events_for('regular').events}
        for control in ('sparse', 'gap'):
            times = {round(e.time, 12) for e in events_for(control).events}
            self.assertTrue(times <= regular, control)

    def test_gap_removes_only_the_declared_window(self):
        gap = events_for('gap')
        self.assertFalse([e for e in gap.events if gen.GAP_START <= e.time < gen.GAP_END])
        regular = events_for('regular')
        outside = [e.time for e in regular.events if not gen.GAP_START <= e.time < gen.GAP_END]
        self.assertEqual(len(gap.events), len(outside))

    def test_windows_meet_the_32_second_minimum(self):
        for bpm in gen.TEMPI:
            fx = fixture('regular', bpm)
            self.assertGreaterEqual(fx.duration_seconds, gen.MIN_SECONDS, bpm)

    def test_noise_realises_zero_db_snr_on_a_synthetic_buffer(self):
        rng = random.Random(7)
        buf = [0.25 * rng.uniform(-1, 1) for _ in range(4000)]
        mixed, realised = gen.add_noise(buf, 'test')
        self.assertLess(abs(realised), 0.01)
        self.assertLess(max(abs(x) for x in mixed), 1.0)

    def test_pins_match_the_committed_generator_and_source(self):
        for key, (path, sha) in run.GEN_PINS.items():
            self.assertEqual(run.robustness.sha256_file(Path(path)), sha, key)


class IntervalRuleTests(unittest.TestCase):
    def test_gap_spanning_interval_is_reported_not_an_outlier(self):
        period = 60.0 / 96.0
        events = [i * period for i in range(20)] + [17.2 + i * period for i in range(20)]
        summary = run.interval_summary(events, 'gap', 96)
        self.assertEqual(len(summary['gapSpanning']), 1)
        self.assertFalse([r for r in summary['gapSpanning'] if r['outlier']])
        self.assertEqual(summary['outliers'], 0)

    def test_sparse_expects_two_beats_and_flags_single_beat_steps(self):
        period = 60.0 / 96.0
        events = [i * 2 * period for i in range(8)]
        self.assertEqual(run.interval_summary(events, 'sparse', 96)['outliers'], 0)
        steady = [i * period for i in range(8)]
        self.assertEqual(run.interval_summary(steady, 'sparse', 96)['outliers'], 7)

    def test_outlier_run_length_is_consecutive(self):
        self.assertEqual(run.longest_run([True, False, True, True, False, True]), 2)


class ComparisonRuleTests(unittest.TestCase):
    @staticmethod
    def record(acquired, bars, error):
        return {'fixture': 'x', 'acquired': acquired,
                'acquiredWithinTwoBars': acquired and bars is not None and bars <= 2.0,
                'bpmRelativeError': error}

    def test_acquisition_gain_and_loss(self):
        gained = run.compare(self.record(False, None, 0.01), self.record(True, 1.0, 0.01))
        lost = run.compare(self.record(True, 1.0, 0.01), self.record(False, None, 0.01))
        self.assertTrue(gained['acquisitionGained'] and gained['withinTwoBarsGained'])
        self.assertTrue(lost['acquisitionLost'] and lost['withinTwoBarsLost'])

    def test_bpm_band_is_half_a_point_absolute(self):
        base = self.record(True, 1.0, 0.0100)
        self.assertEqual(run.compare(base, self.record(True, 1.0, 0.0160))['bpmErrorState'], 'regression')
        self.assertEqual(run.compare(base, self.record(True, 1.0, 0.0140))['bpmErrorState'], 'no-change')
        self.assertEqual(run.compare(base, self.record(True, 1.0, 0.0040))['bpmErrorState'], 'gain')
        self.assertEqual(run.compare(base, self.record(True, 1.0, 0.0120))['bpmErrorState'], 'no-change')

    def test_missing_lock_is_not_evaluable_not_zero(self):
        self.assertEqual(run.compare(self.record(True, 1.0, None), self.record(True, 1.0, 0.0))['bpmErrorState'],
                         'not-evaluable')


class ReadinessTests(unittest.TestCase):
    def test_out_of_band_ready_beats_and_runs(self):
        rows = []
        for ready, bpm in [('0', 96.0), ('1', 48.0), ('1', 48.0), ('1', 96.0)]:
            rows.append({'beatEvent': '1', 'ready': ready, 'variantBpm': str(bpm)})
        summary = {'firstReadyEventSeconds': 1.0, 'firstReadyAvailabilitySeconds': 1.1,
                   'fallbackBeats': 1, 'readyBeats': 3, 'readyBpmMin': 48.0,
                   'readyBpmMedian': 48.0, 'readyBpmMax': 96.0}
        readiness = run.variant_readiness(rows, 96.0, summary)
        self.assertEqual(readiness['readyBeatsOutOfBand'], 2)
        self.assertEqual(readiness['longestOutOfBandReadyRun'], 2)
        self.assertEqual(readiness['fallbackBeatsBeforeReady'], 1)


if __name__ == '__main__':
    unittest.main()
