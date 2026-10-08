#!/usr/bin/env python3
"""Deterministic unit tests for the TRACK-005 click response-lag analysis.

Run by tools/tempo-variant/run-corpus.sh. They pin the reviewer-required
semantics: the PRIMARY lag uses the causal availability clock and, for the
variant, a hit must be READY; a fallback base value in band is not a hit; the
event-time number is secondary; only the first hit is reported; and the
lag-in-beats definition counts emitted beats from the reference through the hit.

    python3 tools/tempo-variant/tests/test_click_lag.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import click_lag  # noqa: E402


def row(event, avail, ready, variant, base):
    return {"event": event, "avail": avail, "ready": ready,
            "variant": variant, "base": base}


class ClickLagTests(unittest.TestCase):
    def test_primary_uses_availability_and_requires_ready(self):
        rows = [
            row(11.9, 11.95, True, 126.0, 123.0),
            row(12.0, 12.05, False, 132.0, 123.0),   # in band but NOT ready
            row(12.5, 12.6, True, 132.5, 123.0),     # first ready in-band hit
            row(13.0, 13.1, True, 132.4, 129.0),
        ]
        r = click_lag.analyze(rows, nominal=12.0, anchor=12.0048, step_to=132.0)
        self.assertAlmostEqual(r["variantFirstWithinAvailabilitySeconds"], 12.6)
        self.assertAlmostEqual(r["variantFirstWithinEventSeconds"], 12.5)
        self.assertAlmostEqual(r["variantLagSecondsFromNominal"], 0.6)
        self.assertAlmostEqual(r["variantEventLagSecondsFromNominal"], 0.5)
        # beats at/after nominal through the hit: indices 1 and 2 -> 2
        self.assertEqual(r["variantExecLagBeatsFromNominal"], 2)

    def test_readiness_false_fallback_not_counted(self):
        rows = [
            row(12.2, 12.3, False, 132.1, 123.0),   # fallback in band, not ready
            row(12.8, 12.9, True, 111.0, 123.0),    # ready but out of band
        ]
        r = click_lag.analyze(rows, nominal=12.0, anchor=12.0, step_to=132.0)
        self.assertEqual(r["variantFirstWithinAvailabilitySeconds"], "")

    def test_out_of_band_later_does_not_change_first_hit(self):
        rows = [
            row(12.1, 12.2, True, 132.4, 123.0),
            row(12.7, 12.8, True, 140.0, 123.0),   # leaves the band later
        ]
        r = click_lag.analyze(rows, nominal=12.0, anchor=12.0, step_to=132.0)
        self.assertAlmostEqual(r["variantFirstWithinAvailabilitySeconds"], 12.2)
        self.assertFalse(r["variantTrailingWithinToEnd"])
        self.assertAlmostEqual(r["variantLastWithinAvailabilitySeconds"], 12.2)

    def test_lag_from_anchor_and_beats(self):
        rows = [
            row(12.0, 12.05, True, 126.0, 123.0),
            row(12.45, 12.5, True, 132.4, 123.0),
        ]
        r = click_lag.analyze(rows, nominal=12.0, anchor=12.0, step_to=132.0)
        self.assertAlmostEqual(r["variantLagSecondsFromAnchor"], 0.5)
        self.assertEqual(r["variantLagBeatsFromAnchor"], 2)

    def test_base_uses_availability_without_readiness(self):
        rows = [row(12.3, 12.45, False, 123.0, 132.01)]
        r = click_lag.analyze(rows, nominal=12.0, anchor=12.0, step_to=132.0)
        self.assertAlmostEqual(r["baseFirstWithinAvailabilitySeconds"], 12.45)
        self.assertAlmostEqual(r["baseLagSecondsFromNominal"], 0.45)
        self.assertEqual(r["baseLagBeatsFromNominal"], 1)

    def test_constant_train_has_no_anchor(self):
        # clickStepInfo reports -1 for a constant train; anchor lag is omitted.
        rows = [row(12.1, 12.2, True, 126.01, 126.0)]
        r = click_lag.analyze(rows, nominal=12.0, anchor=-1.0, step_to=126.0)
        self.assertEqual(r["variantLagSecondsFromAnchor"], "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
