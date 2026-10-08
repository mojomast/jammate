"""Gate tests: fail closed on empty / missing / unrepresentative populations."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from glock.gate import evaluate_gate  # noqa: E402
from glock.scoring import Criteria, score_useful_lock  # noqa: E402
from tests import synth  # noqa: E402

CRIT = Criteria()


def real_recs(n):
    return [synth.make_recording(rec_id=f"r{i}", classification="real",
                                 ownership="test", license="test",
                                 annotation=synth.make_annotation(120.0, n_beats=16),
                                 tags=["core"]) for i in range(n)]


def score_all(recs, n_useful):
    scores = {}
    for i, rec in enumerate(recs):
        trace = (synth.perfect_trace(nominal_bpm=120.0, n_beats=16)
                 if i < n_useful else synth.perfect_trace(nominal_bpm=120.0,
                                                          n_beats=16, bpm=60.0))
        scores[rec.id] = score_useful_lock(rec, trace, CRIT)
    return scores


class GateTests(unittest.TestCase):
    def test_empty_population_fails_closed(self):
        res = evaluate_gate("btrack", [], {}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertTrue(any("empty gate population" in r for r in res.fail_closed))

    def test_missing_trace_fails_closed(self):
        recs = real_recs(1)
        res = evaluate_gate("btrack", recs, {}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertTrue(any("no trace" in r for r in res.fail_closed))

    def test_unrepresentative_excluded_but_empty_then_fails(self):
        recs = real_recs(1)
        recs[0].representative = False
        res = evaluate_gate("btrack", recs, score_all(recs, 1), CRIT)
        self.assertFalse(res.gate_pass)
        self.assertEqual(res.population, [])

    def test_all_useful_passes(self):
        recs = real_recs(10)
        res = evaluate_gate("btrack", recs, score_all(recs, 10), CRIT)
        self.assertTrue(res.gate_pass)
        self.assertEqual(res.fraction, 1.0)

    def test_95_percent_boundary(self):
        recs = real_recs(20)
        res = evaluate_gate("btrack", recs, score_all(recs, 19), CRIT)
        self.assertAlmostEqual(res.fraction, 0.95)
        self.assertTrue(res.gate_pass)

    def test_90_percent_fails(self):
        recs = real_recs(20)
        res = evaluate_gate("btrack", recs, score_all(recs, 18), CRIT)
        self.assertAlmostEqual(res.fraction, 0.90)
        self.assertFalse(res.gate_pass)

    def test_zero_nominal_not_gate_eligible(self):
        ann = synth.make_annotation(120.0)
        ann.tempo_profile = "linear-ramp"
        ann.nominal_bpm = None
        rec = synth.make_recording(rec_id="ramp", classification="real",
                                   annotation=ann, tags=["core"])
        res = evaluate_gate("btrack", [rec], {}, CRIT)
        self.assertFalse(res.gate_pass)
        self.assertFalse(res.eligibility["ramp"]["eligible"])


if __name__ == "__main__":
    unittest.main()
