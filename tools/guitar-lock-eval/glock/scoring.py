"""The useful-lock scorer (pure, deterministic, no I/O).

Definition (frozen in protocol/useful-lock-protocol.json before any
measurement):

    useful lock = a sustained lock run of exactly `lock_run_bars` bars acquired
    within `acquisition_window_bars` AND correct BPM AND usable phase.

A `lock run` is consecutive predicted beats, each within
`beat_tolerance_seconds` of a distinct forward-advancing ground-truth beat, each
accompanied by a phase-valid tempo within `bpm_agreement_fraction` of the local
ground-truth BPM. Acquisition is the event time of the run's first beat, exactly
as EVAL-002/TRACK-004 define it; no lock-run semantics are redefined.

Half-time, double-time, holdover and false locks are reported as separate
labels and never folded into the useful-lock verdict.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

from .manifest import Annotation, Recording
from .trace import Beat, TempoSample, Trace


@dataclass
class Criteria:
    bpm_agreement_fraction: float = 0.02
    beat_tolerance_seconds: float = 0.07
    lock_run_bars: int = 1
    acquisition_window_bars: float = 2.0
    phase_usable_mean_abs_ms: float = 70.0
    phase_usable_p95_abs_ms: float = 150.0
    half_double_band_fraction: float = 0.10
    min_gate_fraction: float = 0.95
    min_annotated_beats: int = 8

    @classmethod
    def from_protocol(cls, protocol: dict) -> "Criteria":
        c = protocol.get("criteria", {})
        return cls(
            bpm_agreement_fraction=float(c.get("bpm_agreement_fraction", 0.02)),
            beat_tolerance_seconds=float(c.get("beat_tolerance_seconds", 0.07)),
            lock_run_bars=int(c.get("lock_run_bars", 1)),
            acquisition_window_bars=float(c.get("acquisition_window_bars", 2.0)),
            phase_usable_mean_abs_ms=float(c.get("phase_usable_mean_abs_ms", 70.0)),
            phase_usable_p95_abs_ms=float(c.get("phase_usable_p95_abs_ms", 150.0)),
            half_double_band_fraction=float(c.get("half_double_band_fraction", 0.10)),
            min_gate_fraction=float(c.get("min_gate_fraction", 0.95)),
            min_annotated_beats=int(protocol.get("gate", {}).get("min_annotated_beats", 8)),
        )


@dataclass
class FixtureScore:
    recording_id: str
    backend: str
    backend_kind: str
    classification: str
    core: bool

    predicted_beats: int = 0
    truth_beats: int = 0
    matched_beats: int = 0
    precision: float = 0.0
    recall: float = 0.0
    f_measure: float = 0.0
    detection_measured: bool = False

    acquired: bool = False
    acquisition_bars: float | None = None
    acquisition_seconds: float | None = None
    lock_start_event_seconds: float | None = None
    lock_start_horizon_seconds: float | None = None
    lock_confirm_event_seconds: float | None = None
    lock_confirm_horizon_seconds: float | None = None
    acquired_within_window: bool = False

    has_bpm_lock: bool = False
    locked_bpm: float | None = None
    bpm_relative_error: float | None = None
    tempo_correct: bool = False

    phase_measured: bool = False
    phase_mean_abs_ms: float | None = None
    phase_p95_abs_ms: float | None = None
    phase_usable: bool = False

    useful_lock: bool = False

    holdover: bool = False
    half_time_lock: bool = False
    double_time_lock: bool = False
    false_lock_phase_ok_tempo_wrong: bool = False
    false_lock_tempo_ok_phase_wrong: bool = False
    false_lock: bool = False
    no_lock: bool = False

    longest_match_run: int = 0
    longest_tempo_run: int = 0
    note: str = ""

    def as_dict(self) -> dict:
        return {
            "recording_id": self.recording_id,
            "backend": self.backend,
            "backend_kind": self.backend_kind,
            "classification": self.classification,
            "core": self.core,
            "predicted_beats": self.predicted_beats,
            "truth_beats": self.truth_beats,
            "matched_beats": self.matched_beats,
            "f_measure": self.f_measure,
            "detection_measured": self.detection_measured,
            "acquired": self.acquired,
            "acquisition_bars": self.acquisition_bars,
            "acquisition_seconds": self.acquisition_seconds,
            "lock_start_event_seconds": self.lock_start_event_seconds,
            "lock_start_horizon_seconds": self.lock_start_horizon_seconds,
            "lock_confirm_event_seconds": self.lock_confirm_event_seconds,
            "lock_confirm_horizon_seconds": self.lock_confirm_horizon_seconds,
            "acquired_within_window": self.acquired_within_window,
            "has_bpm_lock": self.has_bpm_lock,
            "locked_bpm": self.locked_bpm,
            "bpm_relative_error": self.bpm_relative_error,
            "tempo_correct": self.tempo_correct,
            "phase_measured": self.phase_measured,
            "phase_mean_abs_ms": self.phase_mean_abs_ms,
            "phase_p95_abs_ms": self.phase_p95_abs_ms,
            "phase_usable": self.phase_usable,
            "useful_lock": self.useful_lock,
            "holdover": self.holdover,
            "half_time_lock": self.half_time_lock,
            "double_time_lock": self.double_time_lock,
            "false_lock_phase_ok_tempo_wrong": self.false_lock_phase_ok_tempo_wrong,
            "false_lock_tempo_ok_phase_wrong": self.false_lock_tempo_ok_phase_wrong,
            "false_lock": self.false_lock,
            "no_lock": self.no_lock,
            "longest_match_run": self.longest_match_run,
            "longest_tempo_run": self.longest_tempo_run,
            "note": self.note,
        }


# ---------------------------------------------------------------------------
# Pure helpers (mirrors of the unmodified EVAL-002 maths)
# ---------------------------------------------------------------------------

def nearest_truth_index(beats: list[float], t: float) -> int:
    if not beats:
        return -1
    lo, hi = 0, len(beats)
    while lo < hi:
        mid = (lo + hi) // 2
        if beats[mid] < t:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return 0
    if lo >= len(beats):
        return len(beats) - 1
    return lo - 1 if (t - beats[lo - 1]) <= (beats[lo] - t) else lo


def local_period_at_index(beats: list[float], i: int, nominal: float | None) -> float:
    n = len(beats)
    if n < 2:
        return 60.0 / nominal if nominal and nominal > 0 else 0.0
    if i + 1 < n:
        return beats[i + 1] - beats[i]
    return beats[i] - beats[i - 1]


def local_bpm_at_index(beats: list[float], i: int, nominal: float | None) -> float:
    p = local_period_at_index(beats, i, nominal)
    return 60.0 / p if p > 0 else 0.0


def local_truth_bpm_at_time(beats: list[float], t: float, nominal: float | None) -> float:
    n = len(beats)
    if n < 2:
        return nominal if nominal and nominal > 0 else 0.0
    if t <= beats[0]:
        idx = 0
    elif t >= beats[-1]:
        idx = n - 2
    else:
        lo, hi = 0, n
        while lo < hi:
            mid = (lo + hi) // 2
            if beats[mid] <= t:
                lo = mid + 1
            else:
                hi = mid
        idx = (lo - 1) if lo - 1 <= n - 2 else n - 2
    p = beats[idx + 1] - beats[idx]
    return 60.0 / p if p > 0 else 0.0


def beat_position_at(beats: list[float], t: float) -> float:
    if not beats:
        return 0.0
    if len(beats) == 1:
        return 0.0
    if t <= beats[0]:
        gap = beats[1] - beats[0]
        return (t - beats[0]) / gap if gap > 0 else 0.0
    if t >= beats[-1]:
        gap = beats[-1] - beats[-2]
        return (len(beats) - 1) + ((t - beats[-1]) / gap if gap > 0 else 0.0)
    lo, hi = 0, len(beats)
    while lo < hi:
        mid = (lo + hi) // 2
        if beats[mid] <= t:
            lo = mid + 1
        else:
            hi = mid
    hi = lo
    lo = hi - 1
    gap = beats[hi] - beats[lo]
    return lo + ((t - beats[lo]) / gap if gap > 0 else 0.0)


def match_beats(predicted: list[float], truth: list[float], tol: float) -> list[int]:
    matches = [-1] * len(predicted)
    if not predicted or not truth:
        return matches
    i = j = 0
    while i < len(predicted) and j < len(truth):
        d = predicted[i] - truth[j]
        if abs(d) <= tol:
            matches[i] = j
            i += 1
            j += 1
        elif d < 0:
            i += 1
        else:
            j += 1
    return matches


def _median(values: list[float]) -> float | None:
    if not values:
        return None
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def _percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    s = sorted(values)
    idx = int(math.floor(max(0.0, min(1.0, p)) * (len(s) - 1) + 0.5))
    return s[min(idx, len(s) - 1)]


def _tempo_near(samples: list[TempoSample], t: float) -> TempoSample | None:
    if not samples:
        return None
    lo, hi = 0, len(samples)
    while lo < hi:
        mid = (lo + hi) // 2
        if samples[mid].event_seconds < t:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return samples[0]
    if lo >= len(samples):
        return samples[-1]
    a, b = samples[lo - 1], samples[lo]
    return a if (t - a.event_seconds) <= (b.event_seconds - t) else b


def _tempo_in_band(sample: TempoSample, truth_bpm: float, frac: float) -> bool:
    return (truth_bpm <= 0.0
            or (sample.phase_valid and sample.bpm > 0.0
                and abs(sample.bpm - truth_bpm) / truth_bpm <= frac))


def _longest_match_run(events: list[float], beats: list[float], tol: float) -> int:
    """Longest run of consecutive predicted events each matching a distinct
    forward-advancing truth beat within tolerance (tempo ignored)."""
    longest = run = 0
    prev_gi = -1
    for t in events:
        gi = nearest_truth_index(beats, t)
        if gi >= 0 and abs(t - beats[gi]) <= tol and gi > prev_gi:
            run += 1
            prev_gi = gi
            longest = max(longest, run)
        else:
            run = 0
            prev_gi = -1
    return longest


def _longest_tempo_run(samples: list[TempoSample], beats: list[float],
                       nominal: float | None, frac: float) -> int:
    longest = run = 0
    for s in samples:
        truth = local_truth_bpm_at_time(beats, s.event_seconds, nominal)
        if _tempo_in_band(s, truth, frac):
            run += 1
            longest = max(longest, run)
        else:
            run = 0
    return longest


# ---------------------------------------------------------------------------
# The scorer
# ---------------------------------------------------------------------------

def score_useful_lock(rec: Recording, trace: Trace, criteria: Criteria) -> FixtureScore:
    ann = rec.annotation
    if ann is None:
        raise ValueError(f"recording {rec.id} has no annotation")

    beats = ann.beats
    tol = criteria.beat_tolerance_seconds
    frac = criteria.bpm_agreement_fraction
    bpb = max(1, ann.beats_per_bar)
    run_length = max(1, bpb * max(1, criteria.lock_run_bars))

    events = [b.event_seconds for b in trace.beats]
    score = FixtureScore(recording_id=rec.id, backend=trace.backend,
                         backend_kind=trace.backend_kind,
                         classification=rec.classification, core=rec.isCore())
    score.predicted_beats = len(events)
    score.truth_beats = len(beats)

    # matching / detection
    matches = match_beats(events, beats, tol)
    matched = sum(1 for m in matches if m >= 0)
    score.matched_beats = matched
    score.detection_measured = len(beats) > 0
    if score.detection_measured:
        precision = matched / len(events) if events else 1.0
        recall = matched / len(beats) if beats else 1.0
        score.precision = precision
        score.recall = recall
        score.f_measure = (2 * precision * recall / (precision + recall)) if (precision + recall) else 0.0

    # lock run (event clock), mirrors EVAL-002 findFirstLockFrom with run = 1 bar
    lock_start_event = None
    lock_confirm_event = None
    lock_start_horizon = None
    lock_confirm_horizon = None
    if beats and len(events) >= run_length:
        for i in range(0, len(events) - run_length + 1):
            ok = True
            prev_gi = -1
            for k in range(run_length):
                t = events[i + k]
                gi = nearest_truth_index(beats, t)
                if gi < 0 or abs(t - beats[gi]) > tol or gi <= prev_gi:
                    ok = False
                    break
                truth_bpm = local_bpm_at_index(beats, gi, ann.nominal_bpm)
                if truth_bpm > 0.0:
                    s = _tempo_near(trace.tempo_samples, t)
                    if s is None or not _tempo_in_band(s, truth_bpm, frac):
                        ok = False
                        break
                prev_gi = gi
            if ok:
                lock_start_event = events[i]
                lock_confirm_event = events[i + run_length - 1]
                lock_start_horizon = trace.beats[i].horizon_seconds
                lock_confirm_horizon = trace.beats[i + run_length - 1].horizon_seconds
                break
    score.acquired = lock_start_event is not None
    if score.acquired:
        score.lock_start_event_seconds = lock_start_event
        score.lock_confirm_event_seconds = lock_confirm_event
        score.lock_start_horizon_seconds = lock_start_horizon
        score.lock_confirm_horizon_seconds = lock_confirm_horizon
        score.acquisition_seconds = max(0.0, lock_start_event - beats[0])
        pos = max(0.0, beat_position_at(beats, lock_start_event))
        score.acquisition_bars = pos / bpb
        score.acquired_within_window = score.acquisition_bars <= criteria.acquisition_window_bars

    # BPM lock over the steady window (mirrors EVAL-002 steadyWindow)
    duration = ann.beats[-1] if beats else 0.0
    if score.acquired and score.acquisition_seconds is not None:
        window_start = beats[0] + score.acquisition_seconds
    else:
        window_start = beats[0] + 0.5 * (beats[-1] - beats[0]) if len(beats) >= 2 else 0.0
    window_end = duration if duration > 0 else (beats[-1] if beats else 0.0)
    in_window = [s.bpm for s in trace.tempo_samples
                 if s.phase_valid and s.bpm > 0.0
                 and window_start <= s.event_seconds <= window_end]
    if not in_window:
        in_window = [s.bpm for s in trace.tempo_samples if s.phase_valid and s.bpm > 0.0]
    locked = _median(in_window)
    if locked is not None:
        score.has_bpm_lock = True
        score.locked_bpm = locked
        if ann.hasNominalBpm():
            score.bpm_relative_error = abs(locked - ann.nominal_bpm) / ann.nominal_bpm
            score.tempo_correct = score.bpm_relative_error <= frac

    # half / double (separate from plain BPM error)
    if ann.hasNominalBpm() and score.has_bpm_lock:
        ratio = locked / ann.nominal_bpm
        b = criteria.half_double_band_fraction
        score.half_time_lock = 0.5 * (1 - b) <= ratio <= 0.5 * (1 + b)
        score.double_time_lock = 2.0 * (1 - b) <= ratio <= 2.0 * (1 + b)

    # phase usability over the acquisition window (criteria-driven, not a
    # hard-coded two bars): matched beats whose fractional beat position is
    # within acquisition_window_bars bars of the first annotated beat.
    phase_window_beats = criteria.acquisition_window_bars * bpb
    abs_ms = []
    for i, m in enumerate(matches):
        if m < 0:
            continue
        t = events[i]
        if beat_position_at(beats, t) > phase_window_beats + 1e-9:
            continue
        near = nearest_truth_index(beats, t)
        if near < 0:
            continue
        abs_ms.append(abs(t - beats[near]) * 1000.0)
    if abs_ms:
        score.phase_measured = True
        score.phase_mean_abs_ms = sum(abs_ms) / len(abs_ms)
        score.phase_p95_abs_ms = _percentile(abs_ms, 0.95)
        score.phase_usable = (score.phase_mean_abs_ms <= criteria.phase_usable_mean_abs_ms
                              and score.phase_p95_abs_ms <= criteria.phase_usable_p95_abs_ms)

    score.useful_lock = bool(score.acquired_within_window and score.tempo_correct
                             and score.phase_usable)

    # false locks / runs
    score.longest_match_run = _longest_match_run(events, beats, tol)
    score.longest_tempo_run = _longest_tempo_run(trace.tempo_samples, beats,
                                                 ann.nominal_bpm, frac)
    if not score.acquired:
        score.false_lock_phase_ok_tempo_wrong = score.longest_match_run >= run_length
        score.false_lock_tempo_ok_phase_wrong = (
            score.longest_tempo_run >= run_length
            and score.longest_match_run < run_length)
    score.false_lock = (score.false_lock_phase_ok_tempo_wrong
                        or score.false_lock_tempo_ok_phase_wrong)
    score.no_lock = not score.acquired and not score.false_lock

    # holdover: the lock is maintained across an annotated true-silence span
    if score.acquired and ann.true_silence_spans and events:
        first = score.lock_start_event_seconds
        last = events[-1]
        score.holdover = any(s >= first - 1e-9 and e <= last + 1e-9
                             for s, e in ann.true_silence_spans)
    return score
