"""Dependency-free metric math for a declared BeatNet observation series.

These metrics are a *research* scorer, deliberately separate from and **not
comparable to the C++ harness** (``tools/rhythm-eval``). In particular:

- ``impliedBpm`` is the median inter-beat interval of the predicted beats, not
  the harness's locked-tempo estimate, and there is no acquisition metric here.
- The scorer takes the predicted series at face value; it does not verify that
  BeatNet produced it. That is declared metadata plus, separately, the WAV hash.

Definitions
-----------
- Beat event matching: greedy nearest-unused match within a tolerance
  (default 0.07 s, the corpus convention). ``tp`` = matched, ``fp`` = predicted
  and unmatched, ``fn`` = truth and unmatched.
- Phase: signed ``predicted - truth`` in milliseconds over matched pairs;
  p95 is nearest-rank on absolute phase.
- Implied BPM: 60 / median inter-beat interval of the predicted beats.
- Availability: for each observation that carries a causal-availability time,
  ``available - event`` in milliseconds. ``mode == online`` carries none, so
  every availability summary is ``None`` rather than a zero.
"""

from __future__ import annotations

import math


def match_beats(predicted, truth, tolerance_seconds: float):
    """Greedy nearest-unused matching. Returns (tp, fp, fn, matches)."""
    pred = sorted(float(p) for p in predicted)
    used = [False] * len(pred)
    matches = []
    for t in truth:
        best_i = -1
        best_d = tolerance_seconds
        for i, p in enumerate(pred):
            if used[i]:
                continue
            d = abs(p - t)
            if d <= best_d:
                best_d = d
                best_i = i
        if best_i >= 0:
            used[best_i] = True
            matches.append((float(t), pred[best_i]))
    tp = len(matches)
    fn = len(truth) - tp
    fp = len(pred) - tp
    return tp, fp, fn, matches


def prf(tp: int, fp: int, fn: int) -> dict:
    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall = tp / (tp + fn) if (tp + fn) else 0.0
    f = (2 * precision * recall / (precision + recall)) if (precision + recall) else 0.0
    return {"precision": precision, "recall": recall, "fMeasure": f}


def _percentile(values, q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(0, min(len(ordered) - 1, math.ceil(q * len(ordered)) - 1))
    return ordered[rank]


def phase_stats_ms(matches) -> dict:
    if not matches:
        return {
            "phaseMatchedBeats": 0,
            "phaseMeanMs": None,
            "phaseMeanAbsMs": None,
            "phaseP95AbsMs": None,
        }
    signed = [(p - t) * 1000.0 for t, p in matches]
    absolute = [abs(v) for v in signed]
    return {
        "phaseMatchedBeats": len(matches),
        "phaseMeanMs": sum(signed) / len(signed),
        "phaseMeanAbsMs": sum(absolute) / len(absolute),
        "phaseP95AbsMs": _percentile(absolute, 0.95),
    }


def implied_bpm(beats) -> float | None:
    series = sorted(float(b) for b in beats)
    if len(series) < 2:
        return None
    intervals = [b - a for a, b in zip(series, series[1:]) if b > a]
    if not intervals:
        return None
    intervals.sort()
    n = len(intervals)
    median = intervals[n // 2] if n % 2 else 0.5 * (intervals[n // 2 - 1] + intervals[n // 2])
    return 60.0 / median if median > 0 else None


def relative_error(value: float | None, reference: float | None) -> float | None:
    if value is None or reference in (None, 0):
        return None
    return abs(value - reference) / abs(reference)


def availability_stats_ms(observations) -> dict:
    """Causal availability latency for observations that declare it.

    ``observations`` is a list of dicts with float ``event`` and either a float
    ``available`` or ``None``. ``None`` entries are excluded, never treated as 0.
    """
    latencies = []
    for obs in observations:
        available = obs.get("available")
        event = obs.get("event")
        if available is None or event is None:
            continue
        latencies.append((float(available) - float(event)) * 1000.0)
    if not latencies:
        return {
            "availabilitySamples": 0,
            "availabilityMeanMs": None,
            "availabilityMaxMs": None,
        }
    return {
        "availabilitySamples": len(latencies),
        "availabilityMeanMs": sum(latencies) / len(latencies),
        "availabilityMaxMs": max(latencies),
    }


def score_fixture(ground_truth_beats, predicted_beats, tolerance_seconds: float,
                  nominal: float | None = None, observations=None) -> dict:
    tp, fp, fn, matches = match_beats(predicted_beats, ground_truth_beats, tolerance_seconds)
    result = {"truthBeats": len(ground_truth_beats), "predictedBeats": len(predicted_beats),
              "truePositives": tp, "falsePositives": fp, "falseNegatives": fn}
    result.update(prf(tp, fp, fn))
    result.update(phase_stats_ms(matches))
    bpm = implied_bpm(predicted_beats)
    result["impliedBpm"] = bpm
    result["bpmRelativeError"] = relative_error(bpm, nominal)
    if observations is not None:
        result.update(availability_stats_ms(observations))
    return result
