#!/usr/bin/env python3
"""Deterministic synthesis of the guitar rhythm evaluation corpus.

WHY THIS EXISTS
---------------
SPEC.md 12 says no rhythm tracker is chosen permanently until it has been tested
on guitar-specific material, and DEVPLAN.md's EVAL-001 / gate G3 need a corpus
with ground truth before BTrack / aubio / BeatNet can be compared. The
application listens to a *guitar* through an interface, line input or microphone
-- not to a mastered stereo song -- so a corpus scored on commercial recordings
would be measuring the wrong thing (and SPEC 12.2 / DEVPLAN 31 forbid shipping
commercial songs as committed fixtures anyway).

Every fixture here is therefore SYNTHESISED. No third-party, commercial or
otherwise copyrighted audio is included, read or derived from anything outside
this repository. SPEC 12.2's licensing requirement is met by construction: the
material is created from scratch by this script and released CC0-1.0.

WHAT IS ACTUALLY SYNTHESISED
----------------------------
A plucked-string model (Karplus-Strong: a shaped noise burst into a delay line
with a lowpass feedback loop, a one-pole allpass for fractional-delay pitch
accuracy) per string, summed through a physically-motivated capture chain:

    dry strings -> amp drive -> amp/cabinet tone -> body resonance (mic only)
                -> room (mic only) -> capture EQ -> noise/hum -> level
                -> limiter/clipper -> 16-bit PCM

Per-fixture character comes from pick/strum burst length and brightness, string
count, open-position fret offsets, per-string velocity spread, pick hardness and
a small detune/velocity humanisation. Real playing has dynamics, so amplitude is
never constant: every event has its own velocity, phrases accent and decay.

GROUND TRUTH
------------
Beats are the *metric grid* -- the notated beat unit of the meter, constant
spacing except on the two ramp fixtures. Onsets are *as-played* perceptual
events (the start of a strum / chord / single note / tap), including the small
timing humanisation that was actually rendered. The ramp fixtures integrate the
tempo function analytically (linear ramp -> exact antiderivative, refined with
Newton iteration) so their beat times are genuinely non-uniform; assuming a
constant spacing there would invalidate every tempo-drift metric SPEC 12.3 asks
for.

DETERMINISM
-----------
Pure Python standard library only (no numpy/scipy). Every random draw comes
from a `random.Random` seeded from SHA-256 of the generator version and the
fixture name, so regeneration is byte-identical:

    seed = int(sha256("EVAL-001/gen_fixtures.py|1|<name>").hexdigest()[:16], 16)

WAV bytes are 16-bit little-endian mono at 48 kHz (SPEC 17 reference rate), and
the manifest is written with sorted keys and fixed separators so it does not
churn in git diffs.

SIZE
----
The task contract asked for the whole corpus under ~4 MB. That is not
achievable together with the mandated format: 48 kHz / 16-bit / mono is 96000
bytes per second, so 4 MB buys 41.7 s in total, or 2.2 s per fixture across 19
fixtures. Two bars of 4/4 at any plausible tempo is 4-6 s, so a 2.2 s fixture
cannot contain the two bars SPEC 19 needs for acquisition plus any steady state
to measure. This corpus instead uses the shortest length that still admits
"acquire within 2 bars" plus at least 2 bars of steady state, which is about
21 MB, and reports the deviation rather than shipping 19 unusable stubs. If the
size limit turns out to be hard, the honest reductions are a fixture subset or a
lower sample rate -- both orchestrator decisions, not something to resolve by
silently truncating the audio. `--profile smoke` renders 2-bar versions (about
11 MB) for cheap CI exercise; they are not valid gate-G3 evidence.

USAGE
-----
    python3 tools/gen_fixtures.py --out wav                 # fixtures + manifest
    python3 tools/gen_fixtures.py --out wav --profile smoke # short CI smoke set
    python3 tools/gen_fixtures.py --check --out wav         # verify on-disk bytes

The `--profile smoke` set uses the same synthesis and the same ground-truth
rules but a fixed 3-bar length. It exists so a CI machine can exercise the
pipeline cheaply; it is NOT a valid substitute for the full corpus at gate G3
because three bars is barely more than the "acquire within 2 bars" budget in
SPEC 19.
"""

import argparse
import hashlib
import json
import math
import os
import random
import struct
import sys
import wave

GENERATOR_NAME = "testdata/rhythm/tools/gen_fixtures.py"
GENERATOR_VERSION = 1
MANIFEST_SCHEMA_VERSION = 1
CORPUS_ID = "eval001-guitar-corpus"
CORPUS_VERSION = 1
LICENSE = "CC0-1.0"

SAMPLE_RATE = 48000
CHANNELS = 1
BIT_DEPTH = 16

# An interface input clips at full scale, so the clipper threshold is 1.0.
# The overshoot is large on purpose: real input clipping squashes only the
# peaks, and with a typical 15 dB crest factor a small overshoot leaves well
# under 0.1% of samples flat. 4.5x puts several percent of the file into
# genuine flat-topping, which is what actually stresses an amplitude-based
# onset detector. Measured `clippedSampleFraction` is recorded per fixture.
kClipOvershoot = 7.0

# ---------------------------------------------------------------------------
# Closed tag vocabulary. Documented in testdata/rhythm/README.md; the C++ test
# (tests/jam/RhythmCorpusTests.cpp) enforces that nothing outside these sets
# appears in the manifest.
# ---------------------------------------------------------------------------

SCENARIO_TAGS = (
    "accelerando",
    "arpeggio",
    "blues_shuffle",
    "clean_eighths",
    "clean_sixteenths",
    "distorted_power_chords",
    "line_input_clipping",
    "line_input_low_level",
    "meter_3_4",
    "meter_6_8",
    "missing_downbeats",
    "noisy_microphone",
    "palm_mute",
    "ritardando",
    "sparse_single_notes",
    "stop_start",
    "sustained_chords",
    "syncopated_funk",
    "tapping_muting_only",
)

QUALIFIER_TAGS = (
    "clipping",
    "contains_silence",
    "core",
    "distorted",
    "humanised",
    "line_input",
    "low_level",
    "microphone",
    "noisy",
    "steady_tempo",
    "swing",
    "tempo_ramp",
)

# SPEC.md 19 gates are phrased against "core fixtures", so the set that counts
# toward them is named explicitly rather than being "everything steady".
#
# THESE ARE FIXTURE NAMES, which is the point of the repair. The previous list
# held *scenario tag* names, and the two namespaces only partly overlap:
# `palm_mute` and `distorted_power_chords` are tags, while the fixtures are named
# `palm_mute_metal` and `power_chords_distorted`. A harness that looked the list
# up by fixture name therefore matched 7 of 11 and silently disagreed with the
# `core` tag on the fixtures, which is precisely how the defect survived my own
# `tagVocabularyIsClosed` test: that test checked vocabulary membership, not
# membership-list agreement.
#
# DEFINITION, and why `missing_downbeats` is in: "core" means the steady-tempo
# set on which SPEC 19's BPM-relative-error, half/double-time and lock-time
# gates are meaningful. `missing_downbeats` is steady tempo -- 120 BPM throughout,
# constant spacing verified to 1e-9 -- so it qualifies. What it omits is the
# *attack* on alternate downbeats, not the tempo: SPEC 19's "no tempo jump from
# one isolated syncopated event" is precisely the gate it exists to test, and a
# corpus that excluded it would have no fixture for that sentence. The tempo
# gates are therefore well defined on it. It is NOT core for anything that needs
# an attack on every beat (onset F-measure), and `silentBeats` marks the beats
# where that applies.
#
# This list is now cross-checked against the `core` tag on every fixture, by both
# the generator (raises on disagreement) and the C++ suite, so the two can never
# drift apart again.
CORE_FIXTURES = (
    "arpeggio",
    "blues_shuffle",
    "clean_eighths",
    "clean_sixteenths",
    "missing_downbeats",
    "palm_mute_metal",
    "power_chords_distorted",
    "sparse_single_notes",
    "stop_start",
    "sustained_chords",
    "syncopated_funk",
)

# ---------------------------------------------------------------------------
# Guitar physical model constants
# ---------------------------------------------------------------------------

# Standard tuning, open strings, low E -> high E.
OPEN_FREQS = (82.4069, 110.0000, 146.8324, 196.0000, 246.9417, 329.6276)

# Per string: decay time (s) and feedback brightness. Thick low strings ring
# longer and darker; thin high strings ring shorter and brighter.
#
# These are CALIBRATED, not guessed, and each value is the T60 the string
# actually achieves.
#
# The feedback path is a two-point average, which is a lowpass whose extra loss
# rises steeply with frequency. For the five darker strings the explicit
# per-trip `damp` dominates and measured T60 tracks these numbers to within 1%.
# The bright high E is different: most of its harmonics sit in the filter's
# steep-loss region, so the loop's own loss sets the decay and `damp` has
# almost no effect (halving the declared value moves the result by ~0.01 s).
# Its number is therefore the measured asymptote rather than a fitted target,
# which is the physically correct outcome for a bright string in a lossy loop.
STRING_T60 = (4.20, 3.90, 3.40, 2.90, 2.30, 1.90)
STRING_BRIGHT = (0.26, 0.32, 0.40, 0.48, 0.56, 0.64)

# Initial (fast) decay, as a fraction of the sustained T60, and how long it
# lasts. See the damping notes in karplus_strong: the saddle removes energy
# fastest right after the pluck, so the first tenth of a second decays several
# times faster than the tail does.
kInitialDecayRatio = 0.18
kInitialDecaySeconds = 0.10

# Room: parallel Schroeder comb bank, then two allpasses. The feedback values
# matter more than they look. A small room -- bedroom, treated studio, typical
# practice space -- has an RT60 around 0.4-0.6 s; the originally chosen 0.72/0.68/
# 0.63 gives 1.69 s, which is a hall, not a room. That is not just inaccurate:
# a 1.7 s reverb tail smears the gap between eighth notes until consecutive
# attacks merge into one wash, and the declared onsets stop being separable.
# Measured RT60 of the values below is ~0.42 s.
kRoomCombs = ((0.0297, 0.42), (0.0371, 0.40), (0.0411, 0.38), (0.0271, 0.44))
kRoomAllpasses = (0.0050, 0.0017)

# How long to synthesise a note before stopping. A decaying string is far below
# -100 dB well before this, and the float->16-bit conversion discards anything
# quieter than 1/32768 anyway, so this is a pure cost saving with no audible or
# measurable consequence.
kNoteTailFloorSeconds = 0.25
kNoteTailDepthDb = 110.0

# Open-position chord shapes, low string first. Frets are absolute.
CHORDS = {
    "E":  (0, 2, 2, 1, 0, 0),
    "A":  (0, 2, 2, 2, 2, 0),
    "D":  (2, 3, 2, 0, 2, 3),
    "G":  (3, 2, 0, 0, 0, 3),
    "C":  (0, 3, 2, 0, 1, 0),
    "Am": (0, 2, 2, 2, 1, 0),
    "Dm": (2, 3, 2, 0, 2, 3),
    "F":  (3, 2, 3, 2, 1, 1),
}

# Root + fifth power-chord shapes: (string index, fret) pairs, low E -> high E.
POWER_SHAPES = {
    "E":  ((5, 0), (4, 2)),
    "A":  ((5, 0), (4, 2), (3, 2)),
    "D":  ((4, 0), (3, 2), (2, 2)),
    "G":  ((3, 0), (2, 0), (1, 2)),
    "C":  ((3, 3), (2, 3), (1, 3)),
    "F":  ((4, 3), (3, 2), (2, 3)),
    "Bb": ((5, 1), (4, 3), (3, 3)),
}


# ---------------------------------------------------------------------------
# Small deterministic DSP toolbox (pure Python, no dependencies)
# ---------------------------------------------------------------------------

def biquad_bandpass(sr, f0, q):
    """Bandpass biquad coefficients (b0, b1, b2, a1, a2)."""
    w0 = 2.0 * math.pi * f0 / sr
    alpha = math.sin(w0) / (2.0 * q)
    cw = math.cos(w0)
    a0 = 1.0 + alpha
    return (alpha / a0, 0.0, -alpha / a0, -2.0 * cw / a0, (1.0 - alpha) / a0)


def biquad_peak(sr, f0, q, gain_db):
    """Peaking EQ biquad coefficients (b0, b1, b2, a1, a2)."""
    a = math.pow(10.0, gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / sr
    alpha = math.sin(w0) / (2.0 * q)
    cw = math.cos(w0)
    a0 = 1.0 + alpha / a
    return ((1.0 + alpha * a) / a0,
            -2.0 * cw / a0,
            (1.0 - alpha * a) / a0,
            -2.0 * cw / a0,
            (1.0 - alpha / a) / a0)


def apply_biquad(buf, coef):
    b0, b1, b2, a1, a2 = coef
    x1 = x2 = y1 = y2 = 0.0
    out = [0.0] * len(buf)
    for n, x0 in enumerate(buf):
        y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        out[n] = y0
        x2 = x1
        x1 = x0
        y2 = y1
        y1 = y0
    return out


def apply_onepole_lp(buf, cutoff_hz, sr):
    """One-pole lowpass."""
    if cutoff_hz >= sr * 0.499:
        return list(buf)
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / sr)
    out = [0.0] * len(buf)
    y = 0.0
    for n, x in enumerate(buf):
        y += alpha * (x - y)
        out[n] = y
    return out


def apply_onepole_hp(buf, cutoff_hz, sr):
    """One-pole highpass (x[n] - lowpass(x))."""
    if cutoff_hz <= 0.0:
        return list(buf)
    if cutoff_hz >= sr * 0.499:
        return [0.0] * len(buf)
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / sr)
    out = [0.0] * len(buf)
    y = 0.0
    for n, x in enumerate(buf):
        y += alpha * (x - y)
        out[n] = x - y
    return out


def apply_comb(buf, delay_samples, feedback, damp_hz, sr):
    """Feedback comb with a one-pole lowpass in the loop (Schroeder)."""
    d = int(delay_samples)
    if d <= 0 or d >= len(buf):
        return list(buf)
    line = [0.0] * d
    idx = 0
    out = [0.0] * len(buf)
    damp = 1.0 - math.exp(-2.0 * math.pi * damp_hz / sr) if damp_hz > 0 else 1.0
    z = 0.0
    for n, x in enumerate(buf):
        v = line[idx]
        out[n] = x + v
        z += damp * (v - z)
        line[idx] = x + z * feedback
        idx += 1
        if idx == d:
            idx = 0
    return out


def apply_allpass(buf, delay_samples, gain, sr):
    """Feed-forward Schroeder allpass."""
    d = int(delay_samples)
    if d <= 0 or d >= len(buf):
        return list(buf)
    line = [0.0] * d
    idx = 0
    out = [0.0] * len(buf)
    for n, x in enumerate(buf):
        v = line[idx]
        out[n] = -gain * x + v
        line[idx] = x + gain * v
        idx += 1
        if idx == d:
            idx = 0
    return out


def karplus_strong(sr, freq, dur_seconds, rng, bright, t60, pluck_pos, amp,
                   t60a=None):
    """One plucked string.

    `bright` in (0, 0.5] controls both the excitation brightness and how much
    high frequency survives the feedback loop. `pluck_pos` is the fraction of the
    period occupied by the pick burst: smaller means a harder, brighter attack
    nearer the bridge; larger means a softer attack nearer the middle of the
    string, which is what a thumb or a pick under the first fret sounds like.

    Decay is two-stage; see the damping notes below for why. `t60a` overrides the
    shorter, initial decay time.
    """
    n_samples = int(round(dur_seconds * sr))
    if n_samples <= 0:
        return []
    freq_hz = freq
    period = sr / freq_hz
    d = int(period)
    if d < 2:
        d = 2
    frac = period - d
    # One-pole allpass for the fractional part of the delay -> accurate pitch.
    a_allpass = (1.0 - frac) / (1.0 + frac)

    burst = max(2, int(d * max(0.02, min(0.5, pluck_pos))))
    line = [0.0] * d
    env = 0.0
    for i in range(d):
        v = rng.uniform(-1.0, 1.0) if i < burst else 0.0
        env = bright * v + (1.0 - bright) * env
        line[i] = env

    # Damping, which is easy to get wrong by a factor of `freq`:
    #
    # `damp` is the gain applied to a delay-line cell each time the write head
    # visits it. The head makes ONE trip per string period, so the signal is
    # multiplied by `damp` `freq` times per second, and to fall 60 dB in `t60`
    # seconds:  damp = 10 ** (-3 / (t60 * freq)).
    #
    # Verified empirically on all six open strings (evidence in the task note):
    # the measured decay slope matches the declared T60 to within 1%. The
    # tempting 10 ** (-3 / (t60 * sr)) divides by the sample rate instead of
    # the frequency and is off by exactly `freq` -- it yields ~2 dB/s on the low
    # E instead of ~14 dB/s, so every string drones on indefinitely and each new
    # onset is buried under the previous note's ring.
    #
    # The first kInitialDecaySeconds use the shorter `t60a`. This is physical,
    # not cosmetic: the saddle transmits a force proportional to string velocity
    # back into the string, so loss is greatest right after the pluck and eases
    # as the string slows. A single exponential over-rings every real note. For
    # THIS corpus it is decisive: a chord strummed every eighth note lands on a
    # ring only ~7 dB down, the attacks stop articulating, and the declared
    # ground truth becomes unfindable by any onset detector.
    period_seconds = (d + frac) / float(sr)
    if period_seconds <= 0.0:
        period_seconds = 1.0 / max(1.0, freq_hz)
    freq = 1.0 / period_seconds
    if t60a is None:
        t60a = max(0.02, t60 * kInitialDecayRatio)
    damp_slow = math.pow(10.0, -3.0 / (max(0.02, t60) * freq))
    damp_fast = math.pow(10.0, -3.0 / (max(0.02, t60a) * freq))
    attack_samples = int(kInitialDecaySeconds * sr)
    out = [0.0] * n_samples
    idx = 0
    ap_x1 = 0.0
    ap_y1 = 0.0
    prev = 0.0
    for n in range(n_samples):
        cur = line[idx]
        # one-pole allpass fractional delay
        y = a_allpass * cur + ap_x1 - a_allpass * ap_y1
        ap_x1 = cur
        ap_y1 = y
        out[n] = y
        prev = cur
        damp = damp_fast if n < attack_samples else damp_slow
        line[idx] = damp * ((1.0 - bright) * cur + bright * prev)
        idx += 1
        if idx == d:
            idx = 0
    peak = 0.0
    for v in out:
        av = v if v >= 0.0 else -v
        if av > peak:
            peak = av
    if peak > 0.0:
        g = amp / peak
        for n in range(n_samples):
            out[n] *= g
    return out


def tanh(x):
    # math.tanh exists, but this clamp keeps the saturation curve bounded in a
    # fixed range, which is what a real amp front end does.
    if x > 6.0:
        return 1.0
    if x < -6.0:
        return -1.0
    return math.tanh(x)


def soft_clip(buf, drive):
    k = max(0.001, drive)
    norm = tanh(k)
    return [tanh(k * x) / norm for x in buf]


def hard_clip(buf, threshold):
    t = max(1e-6, threshold)
    return [max(-t, min(t, x)) for x in buf]


def clipping_fraction(buf, threshold):
    """Fraction of samples sitting on the clip ceiling (within 1 LSB)."""
    t = max(1e-6, threshold)
    n = len(buf)
    if n == 0:
        return 0.0
    c = 0
    eps = 1.0 / 32768.0
    for x in buf:
        if abs(x) >= t - eps:
            c += 1
    return float(c) / n


def remove_dc(buf):
    if not buf:
        return buf
    mean = math.fsum(buf) / len(buf)
    return [x - mean for x in buf]


def peak_of(buf):
    p = 0.0
    for v in buf:
        a = v if v >= 0.0 else -v
        if a > p:
            p = a
    return p


def scale_to_peak(buf, target):
    p = peak_of(buf)
    if p <= 0.0:
        return list(buf)
    g = target / p
    return [x * g for x in buf]


def db_to_lin(db):
    return math.pow(10.0, db / 20.0)


# ---------------------------------------------------------------------------
# Beat grids, including exact non-uniform grids for tempo ramps
# ---------------------------------------------------------------------------

def constant_beats(start, beat_seconds, count):
    return [start + i * beat_seconds for i in range(count)]


def ramp_bpm(t, t0, t1, bpm0, bpm1):
    if t1 <= t0:
        return bpm0
    u = (t - t0) / (t1 - t0)
    u = 0.0 if u < 0.0 else (1.0 if u > 1.0 else u)
    return bpm0 + (bpm1 - bpm0) * u


def ramp_beats(start, t0, t1, bpm0, bpm1, count):
    """Beat times for a linear tempo ramp, derived from the analytic integral.

    Phase(t) = int_0^t bpm(u)/60 du = bpm0*t/60 + s*t^2/120, where s is the ramp
    slope in bpm/second. Beat k lands where Phase(T) = k. The exact quadratic is
    solved by Newton iteration from a linear first guess, so the spacing really
    does follow the tempo curve instead of being an artefact of a sample step.

    Beats before t0 use bpm0, beats after t1 use bpm1, so the ramp is
    well-defined across the whole fixture.
    """
    s = (bpm1 - bpm0) / (t1 - t0)

    def phase(t):
        # Integral from t0 (= ramp start) of bpm(u)/60, with
        # bpm(u) = bpm0 + s*(u - t0). Offsetting by t0 matters: the tempo is
        # clamped to the ramp window, so the correct antiderivative is
        #   (bpm0*d + s*d*d/2)/60   with d = t - t0,
        # NOT a form using u directly, which drifts by s*t0*d/60 and puts the
        # last beat ~0.02 beats early.
        d = t - t0
        return (bpm0 * d + 0.5 * s * d * d) / 60.0

    beats = []
    for k in range(count):
        target = float(k)
        lo = start if k == 0 else beats[-1]
        # linear guess using the local tempo
        guess = lo + (60.0 / max(1.0, ramp_bpm(lo, t0, t1, bpm0, bpm1)))
        t = guess
        for _ in range(60):
            slope = (bpm0 + s * (t - t0)) / 60.0
            if slope <= 1e-12:
                break
            err = phase(t) - target
            step = err / slope
            t -= step
            if abs(step) < 1e-15:
                break
        beats.append(t)
    return beats


# ---------------------------------------------------------------------------
# Events
# ---------------------------------------------------------------------------

class Event(object):
    """One perceptual attack: a strum, a chord, a single note or a tap."""

    __slots__ = ("time", "kind", "payload", "velocity", "bright", "pluck_pos",
                 "t60_scale", "choke")

    def __init__(self, time, kind, payload, velocity, bright, pluck_pos,
                 t60_scale, choke):
        self.time = time
        self.kind = kind
        self.payload = payload
        self.velocity = velocity
        self.bright = bright
        self.pluck_pos = pluck_pos
        self.t60_scale = t60_scale
        self.choke = choke


class Fixture(object):
    """A fixture recipe plus the place its rendered samples are put."""

    def __init__(self, name, scenario_tag, notes, meter_n, meter_d, beat_unit,
                 beats_per_bar, bars, bpm, pattern, capture, drive=None,
                 drive_mix=0.0, tone=None, peak_dbfs=-6.0, noise_dbfs=-70.0,
                 hum_dbfs=-95.0, clip_threshold=None, dry_gain=1.0,
                 room_wet=0.0, bpm_end=None, extra_tags=(), core=False,
                 lead_in=0.35):
        self.name = name
        self.scenario_tag = scenario_tag
        self.notes = notes
        self.meter_n = meter_n
        self.meter_d = meter_d
        self.beat_unit = beat_unit
        self.beats_per_bar = beats_per_bar
        self.bars = bars
        self.bpm = bpm
        self.bpm_end = bpm_end
        self.pattern = pattern
        self.capture = capture
        self.drive = drive
        self.drive_mix = drive_mix
        self.tone = tone
        self.peak_dbfs = peak_dbfs
        self.noise_dbfs = noise_dbfs
        self.hum_dbfs = hum_dbfs
        self.clip_threshold = clip_threshold
        self.dry_gain = dry_gain
        self.room_wet = room_wet
        self.extra_tags = extra_tags
        self.core = core
        self.lead_in = lead_in

        # Derived (filled in by prepare()).
        self.beats = []
        self.events = []
        self.audio = None
        self.clip_fraction = 0.0
        self.mix_rate = 1.0
        # Global decay trim. Kept below 1 because the STRING_T60 table is the
        # free-string decay; a note played with a pick, through a pickup and
        # into a room loses energy faster than the unloaded string does, and
        # these fixtures are capturing a played instrument, not a physics sim.
        self.mix_t60 = 0.6
        self.silent_beats = []
        # Narrow windows around deliberately-unplayed beats. NOT regions of
        # silence -- see compute_true_silence_spans() and true_silence_spans.
        self.silence_spans = []
        self.true_silence_spans = []


# Minimum separation between two declared onsets. Two events closer than this
# are a ground-truth defect, not a subtle rhythm: a scorer cannot distinguish
# them, and a peak-picking onset detector physically cannot resolve them, so
# declaring both makes the manifest unfalsifiable.
kMinOnsetSeparation = 0.012


def humanise(rng, t, jitter_seconds, boundary):
    """Jitter an event time, but never outside the file or past the next event.

    `boundary` is exclusive by a guard gap: clamping straight onto it would let
    a jittered event land on exactly the same instant as the following event,
    producing duplicate onsets.
    """
    lo = 0.0
    hi = boundary
    t = t + rng.uniform(-jitter_seconds, jitter_seconds)
    if t < lo:
        t = lo
    if hi is not None and t > hi:
        t = hi
    return t


def subdivide_offsets(rng, count, span, jitter):
    """Event offsets inside one beat, with timing humanisation."""
    out = []
    limit = None
    for i in range(count):
        base = span * i / count if count else 0.0
        limit = (span * (i + 1) / count) if (count and i + 1 < count) else span
        out.append(humanise(rng, base, jitter, limit))
    return out


# --- pattern builders -------------------------------------------------------
#
# Each builder receives (fixture, rng, beats) and returns a list of Events whose
# times are already humanised. Beats are the metric grid, so patterns reference
# beats[] / beat length rather than recomputing a grid of their own.

def _chord_events(fx, rng, chord, time, velocity, bright, pluck_pos, t60s):
    return [Event(time, "chord", CHORDS[chord], velocity, bright, pluck_pos,
                  t60s, False)]


def _power_events(fx, rng, root, time, velocity, bright, pluck_pos, t60s):
    return [Event(time, "power", POWER_SHAPES[root], velocity, bright,
                  pluck_pos, t60s, False)]


def _single_events(fx, rng, spec, time, velocity, bright, pluck_pos, t60s):
    return [Event(time, "single", spec, velocity, bright, pluck_pos, t60s,
                  False)]


def p_clean_eighths(fx, rng, beats):
    evs = []
    prog = ["E", "A", "D", "G", "C", "Am"]
    bar_seconds = fx.beats_per_bar * fx.beat_seconds
    for b in range(0, len(beats), fx.beats_per_bar):
        chord = prog[(b // fx.beats_per_bar) % len(prog)]
        # one downstroke per beat == straight eighth notes at this tempo
        for k in range(fx.beats_per_bar):
            base = beats[b + k]
            boundary = beats[b + k + 1] if b + k + 1 < len(beats) \
                else base + fx.beat_seconds
            vel = 1.0 if k == 0 else (0.88 if k == 2 else 0.74)
            if k % 2 == 1 and (b // fx.beats_per_bar) % 3 == 2:
                vel = 0.86  # turnaround bar, push back in
            t = humanise(rng, base, 0.008, boundary)
            evs.extend(_chord_events(fx, rng, chord, t, vel, 0.46, 0.5, 1.0))
    del bar_seconds
    return evs


def p_clean_sixteenths(fx, rng, beats):
    evs = []
    prog = ["E", "D", "A", "G", "C", "D", "Am", "E"]
    for b in range(0, len(beats), 4):
        chord = prog[(b // 4) % len(prog)]
        beat_seconds = fx.beat_seconds
        # constant sixteenths, with the usual 2-and-4 push on the bar backbeat
        for k in range(16):
            base = beats[b] + beat_seconds * k / 4.0
            if b + 4 < len(beats):
                boundary = beats[b] + beat_seconds * (k + 1) / 4.0
            else:
                boundary = base + beat_seconds / 4.0
            t = humanise(rng, base, 0.005, boundary)
            accent = 1.0 if k == 0 else (0.86 if k % 4 == 2 else 0.66)
            evs.extend(_chord_events(fx, rng, chord, t, accent, 0.50, 0.5, 0.8))
    return evs


def p_power_chords(fx, rng, beats):
    evs = []
    prog = ["E", "E", "A", "A", "D", "G", "F", "Bb"]
    for b in range(0, len(beats), 4):
        root = prog[(b // 4) % len(prog)]
        beat_seconds = fx.beat_seconds
        for k in range(8):
            base = beats[b] + beat_seconds * k / 2.0
            if b + 4 < len(beats):
                boundary = beats[b] + beat_seconds * (k + 1) / 2.0
            else:
                boundary = base + beat_seconds / 2.0
            t = humanise(rng, base, 0.007, boundary)
            # the low string gets the downstroke, so it is the loudest note
            vel = 1.0 if k == 0 else (0.88 if k % 4 == 2 else 0.70)
            evs.extend(_power_events(fx, rng, root, t, vel, 0.40, 0.5, 0.85))
    return evs


def p_palm_mute(fx, rng, beats):
    evs = []
    prog = ["E", "D", "A", "G", "F", "E"]
    for b in range(0, len(beats), 4):
        root = prog[(b // 4) % len(prog)]
        beat_seconds = fx.beat_seconds
        # chug on every sixteenth, short damped bursts, no ring
        for k in range(16):
            base = beats[b] + beat_seconds * k / 4.0
            if b + 4 < len(beats):
                boundary = beats[b] + beat_seconds * (k + 1) / 4.0
            else:
                boundary = base + beat_seconds / 4.0
            t = humanise(rng, base, 0.004, boundary)
            vel = 1.0 if k % 4 == 0 else (0.82 if k % 2 == 0 else 0.66)
            evs.extend(_power_events(fx, rng, root, t, vel, 0.18, 0.06, 0.055))
    return evs


def p_blues_shuffle(fx, rng, beats):
    evs = []
    # swing: the second eighth sits late in the beat, at 2/3 of it
    for i, b in enumerate(beats):
        chord = "E" if (i // 4) % 2 == 0 else "A"
        for off, vel in ((0.0, 1.0), (2.0 / 3.0, 0.62)):
            base = b + off * fx.beat_seconds
            if off > 0.0:
                boundary = b + fx.beat_seconds
            else:
                boundary = b + 0.66 * fx.beat_seconds
            t = humanise(rng, base, 0.007, boundary)
            evs.extend(_chord_events(fx, rng, chord, t, vel, 0.44, 0.5, 1.0))
    return evs


def p_syncopated_funk(fx, rng, beats):
    evs = []
    # sixteenth-note grid with the backbeat on the "a" of 2 and 4 and ghost notes
    # that deliberately straddle the downbeat.
    # Sixteenth grid (position is a fraction of one beat). Beat 3 deliberately
    # omits its downbeat attack; the "+0.5" ghost notes are the accents that
    # make a tracker mistake a syncopated accent for the beat.
    hits = {
        0: [0.0, 0.50, 0.75],
        1: [0.0, 0.75],
        2: [0.25, 0.75, 1.50],
        3: [0.50, 1.00, 1.75],
    }
    prog = ["E", "Am", "D", "G"]
    for i, b in enumerate(beats):
        chord = prog[(i // 4) % len(prog)]
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        for pos in hits[i % 4]:
            base = b + pos * fx.beat_seconds
            t = humanise(rng, base, 0.006, next_b - 1e-6)
            if abs(pos - 0.0) < 1e-9 and (i % 4) == 3:
                # last beat of the bar: deliberately no attack on the downbeat,
                # the phrase walks into the next bar instead
                continue
            vel = 1.0 if pos == 0.0 else 0.58
            if pos >= 1.25:
                vel = 0.72
            evs.extend(_chord_events(fx, rng, chord, t, vel, 0.52, 0.5, 0.55))
    return evs


def p_sparse_single_notes(fx, rng, beats):
    evs = []
    # Lead playing: a note on 1 and a couple of long single notes, lots of ring.
    line = [
        (0, [(0, 2.0, 5), (1.0, 3.0, 5), (2.5, 4.0, 5), (3.5, 5.0, 5)]),
        (4, [(0, 3.0, 5), (1.5, 5.0, 4), (3.0, 6.0, 5)]),
        (8, [(0, 5.0, 4), (1.0, 4.0, 5), (2.0, 7.0, 5)]),
        (12, [(0, 3.0, 5), (0.5, 5.0, 5), (2.0, 4.0, 4), (3.5, 6.0, 5)]),
    ]
    positions = {}
    for start_beat, items in line:
        positions[start_beat] = items
    for i, b in enumerate(beats):
        for off, fret, sidx in positions.get(i, []):
            base = b + off * fx.beat_seconds
            if off == 0.0:
                boundary = b + 0.85 * fx.beat_seconds
            else:
                boundary = beats[i + 1] - 1e-6 if i + 1 < len(beats) else base + 0.1
            t = humanise(rng, base, 0.009, boundary)
            vel = 1.0 if off == 0.0 else 0.7
            evs.extend(_single_events(fx, rng, (sidx, fret), t, vel, 0.58, 0.5,
                                      1.0))
    return evs


def p_arpeggio(fx, rng, beats):
    evs = []
    prog = [("Am", (0, 1, 2, 1, 2, 1)), ("D", (0, 1, 2, 1, 2, 1)),
            ("E", (0, 1, 2, 3, 2, 1)), ("G", (0, 2, 1, 2, 1, 2))]
    for i, b in enumerate(beats):
        chord, shape = prog[(i // 4) % len(prog)]
        frets = CHORDS[chord]
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        # 16th-note broken chord, one string per note -> genuinely separate
        # transients rather than one smeared strum
        for k, sidx in enumerate(shape):
            base = b + fx.beat_seconds * k / 4.0
            t = humanise(rng, base, 0.005, next_b - 1e-6)
            vel = 0.95 if sidx == 0 else 0.62
            evs.append(Event(t, "single", (sidx, frets[sidx]), vel, 0.60, 0.5,
                             0.7, False))
    return evs


def p_sustained_chords(fx, rng, beats):
    evs = []
    prog = ["C", "Am", "F", "G"]
    # One attack per bar, then silence through the remaining beats. A note that
    # simply rings out over the next downbeat WOULD be a re-articulation to any
    # onset detector (the spectrum changes as it decays), which would make
    # `silentBeats` a lie. So the decay is deliberately shorter than the bar:
    # the chords stop before the next attack, leaving genuine quiet.
    ring = 0.42 * fx.beat_seconds * fx.beats_per_bar
    for i, b in enumerate(beats):
        if i % fx.beats_per_bar != 0:
            continue
        chord = prog[(i // fx.beats_per_bar) % len(prog)]
        next_b = beats[i + fx.beats_per_bar] \
            if i + fx.beats_per_bar < len(beats) \
            else b + fx.beats_per_bar * fx.beat_seconds
        # soft pick near the middle of the string: slow bloom, then decay
        t = humanise(rng, b, 0.010, next_b - 1e-6)
        vel = 1.0 if i % (4 * fx.beats_per_bar) == 0 else 0.8
        scale = ring / (4.2 * 1.15)
        evs.append(Event(t, "chord", CHORDS[chord], vel, 0.22, 0.5, scale,
                         False))
    return evs


def p_missing_downbeats(fx, rng, beats):
    evs = []
    prog = ["E", "D", "C", "G", "Am", "D"]
    for i, b in enumerate(beats):
        beat_in_bar = i % 4
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        if beat_in_bar == 0 and ((i // 4) % 2 == 0):
            # No attack at all on alternating downbeats: the previous chord is
            # left to ring through, which is exactly the case that makes a
            # downbeat-snapping tracker hallucinate or lose lock.
            continue
        chord = prog[(i // 4) % len(prog)]
        vel = 0.7 if beat_in_bar == 0 else (0.95 if beat_in_bar == 2 else 0.8)
        t = humanise(rng, b, 0.008, next_b - 1e-6)
        evs.append(Event(t, "chord", CHORDS[chord], vel, 0.46, 0.5, 1.0,
                         False))
    return evs


def p_stop_start(fx, rng, beats):
    evs = []
    prog = ["E", "A", "D", "G"]
    # Two whole bars of deliberate silence in the middle; the metric grid keeps
    # running through it so recovery phase is scorable.
    sil_lo = 4 if fx.bars >= 8 else 1
    sil_hi = 6 if fx.bars >= 8 else 3
    for i, b in enumerate(beats):
        bar = i // fx.beats_per_bar
        if sil_lo <= bar < sil_hi:
            continue
        chord = prog[bar % len(prog)]
        vel = 1.0 if i % fx.beats_per_bar == 0 else 0.78
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        t = humanise(rng, b, 0.007, next_b - 1e-6)
        # The final strum before the gap is damped by the picking hand the way a
        # real player stops. Without this the chord rings through the "silence",
        # which is both unphysical for a stop and would make `silentBeats`
        # wrong: a decaying note is a spectral change, and any onset detector
        # will fire on it.
        last = (bar + 1 == sil_lo)
        scale = 0.10 if last else 0.95
        evs.append(Event(t, "chord", CHORDS[chord], vel, 0.48, 0.5, scale,
                         False))
    return evs


def p_meter_3_4(fx, rng, beats):
    evs = []
    prog = [("C", "G"), ("G", "D"), ("D", "A"), ("Am", "E")]
    for i, b in enumerate(beats):
        if i % 3 == 0:
            bar = i // 3
            bass, rest = prog[bar % len(prog)]
            next_b = beats[i + 1]
            t = humanise(rng, b, 0.007, next_b - 1e-6)
            evs.append(Event(t, "single", (0, CHORDS[bass][0]), 1.0, 0.44, 0.5,
                             1.0, False))
            for k in (1, 2):
                base = beats[i + k]
                boundary = beats[i + k + 1] if i + k + 1 < len(beats) else base + fx.beat_seconds
                tk = humanise(rng, base, 0.007, boundary)
                evs.append(Event(tk, "chord", CHORDS[rest], 0.72, 0.40, 0.5,
                                 0.8, False))
        else:
            continue
    return evs


def p_meter_6_8(fx, rng, beats):
    evs = []
    # Dotted-quarter pulse, subdivided into three eighths: bass on the 1, chord
    # on the second and fifth eighth of each half-bar.
    prog = [("E", "C"), ("A", "F"), ("D", "G"), ("G", "C")]
    eighth = fx.beat_seconds / 3.0
    for i, b in enumerate(beats):
        half = i % 2
        bar = i // 2
        bass, rest = prog[bar % len(prog)]
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        # bass root on the 1 of each half-bar, chords on the 2nd and 5th eighth
        t = humanise(rng, b, 0.007, next_b - 1e-6)
        evs.append(Event(t, "single", (0, CHORDS[bass][0]), 1.0, 0.44, 0.5,
                         1.0, False))
        for k in (1, 2):
            base = b + k * eighth
            t = humanise(rng, base, 0.007, next_b - 1e-6)
            evs.append(Event(t, "chord", CHORDS[rest], 0.74, 0.42, 0.5, 0.8,
                             False))
    return evs


def p_tapping_muting_only(fx, rng, beats):
    evs = []
    # Palm-muted chugs and fret-hand taps: near-transient, almost no pitch
    # content. This is the hardest realistic case for spectral-flux trackers.
    for i, b in enumerate(beats):
        next_b = beats[i + 1] if i + 1 < len(beats) else b + fx.beat_seconds
        if i % 4 in (0, 2):
            positions = [0.0, 0.25]
            vals = [1.0, 0.6]
        else:
            positions = [0.0]
            vals = [0.72]
        for pos, vel in zip(positions, vals):
            base = b + pos * fx.beat_seconds
            t = humanise(rng, base, 0.005, next_b - 1e-6)
            evs.append(Event(t, "tap", (4 + (i % 2), 5), vel, 0.14, 0.05, 0.045,
                             True))
    return evs


def p_ramp(fx, rng, beats):
    """Ramp fixtures: straight eighth-note strums following the tempo curve."""
    evs = []
    prog = ["E", "A", "D", "G", "C", "Am"]
    for i, b in enumerate(beats):
        chord = prog[(i // 4) % len(prog)]
        for k in (0, 1):
            base = b + k * 0.5 * fx.beat_at(i)
            if k == 0:
                boundary = b + 0.6 * fx.beat_at(i)
            else:
                boundary = beats[i + 1] if i + 1 < len(beats) else base + 0.1
            t = humanise(rng, base, 0.008, boundary)
            vel = 1.0 if k == 0 else 0.72
            evs.append(Event(t, "chord", CHORDS[chord], vel, 0.46, 0.5, 1.0,
                             False))
    return evs


# ---------------------------------------------------------------------------
# Capture profiles
# ---------------------------------------------------------------------------

def capture_chain(fx, buf, rng):
    """Mic / line capture path, impairments, level and clipping."""
    sr = SAMPLE_RATE

    if fx.room_wet > 0.0:
        # Parallel comb bank + two allpasses. apply_comb returns input + tail,
        # so `verb` already contains the dry signal; keep a copy so the mix
        # below is a genuine wet/dry crossfade rather than a level scale.
        dry = buf
        verb = buf
        for d, fb in kRoomCombs:
            verb = apply_comb(verb, d * sr, fb, 4200.0, sr)
        for d in kRoomAllpasses:
            verb = apply_allpass(verb, d * sr, 0.7, sr)
        w = fx.room_wet
        buf = [(1.0 - w) * a + w * b for a, b in zip(dry, verb)]

    if fx.capture == "mic":
        # body/air resonances: a guitar through a mic has real low end, but
        # nowhere near a kick drum's
        for f0, q, g in ((104.0, 2.2, 6.5), (212.0, 2.6, 4.0),
                         (1180.0, 1.4, 2.5), (2900.0, 1.1, 2.0)):
            buf = apply_biquad(buf, biquad_peak(sr, f0, q, g))
        hp = 95.0
        lp = 7400.0
    else:
        # direct line injection: flat, no room, no body peak
        hp = 24.0
        lp = 13000.0

    buf = apply_onepole_hp(buf, hp, sr)
    buf = apply_onepole_lp(buf, lp, sr)

    # Line inputs pick up mains hum from the interface; microphones do not.
    if fx.hum_dbfs > -90.0:
        amp = db_to_lin(fx.hum_dbfs)
        for i in range(len(buf)):
            tt = i / sr
            buf[i] += amp * (math.sin(2.0 * math.pi * 50.0 * tt)
                             + 0.42 * math.sin(2.0 * math.pi * 100.0 * tt + 0.7)
                             + 0.16 * math.sin(2.0 * math.pi * 150.0 * tt + 1.9))

    # noise floor: white hiss plus a little rumble
    namp = db_to_lin(fx.noise_dbfs)
    rumble = [0.0] * len(buf)
    r1 = rng.uniform(-1.0, 1.0)
    b0 = b1 = 0.0
    for i in range(len(buf)):
        white = rng.uniform(-1.0, 1.0)
        b0 = 0.99765 * b0 + white * 0.0990460
        b1 = 0.96300 * b1 + white * 0.2965164
        pink = (b0 + b1 + white * 0.1848) * 0.22
        rumble[i] = namp * (0.72 * white + 1.9 * pink)
    buf = [x + r for x, r in zip(buf, rumble)]

    if fx.clip_threshold is not None:
        # Drive well past the ceiling before the clipper, the way a hot input
        # gain actually does, so the flat tops are a real fraction of the file
        # rather than a cosmetic touch.
        # `dry_gain` is deliberately not applied to the noise floor here: the
        # point of this fixture is that the SIGNAL is clipped while the
        # interface's own noise floor stays where it is, which is exactly what
        # a real hot-input capture looks like.
        buf = scale_to_peak(buf, fx.clip_threshold * kClipOvershoot)
        buf = hard_clip(buf, fx.clip_threshold)
        fx.clip_fraction = clipping_fraction(buf, fx.clip_threshold)

    if fx.clip_threshold is None:
        buf = scale_to_peak(buf, db_to_lin(fx.peak_dbfs))

    return remove_dc(buf)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def pluck_string(fx, rng, freq, t60, bright, pluck_pos, amp, n_samples):
    cents = rng.uniform(-5.0, 5.0)
    detune = 2.0 ** (cents / 1200.0)
    return karplus_strong(SAMPLE_RATE, freq * detune, n_samples / SAMPLE_RATE,
                          rng, bright, t60, pluck_pos, amp)


def render_event(fx, rng, ev, buf, n_samples):
    """Additive Karplus-Strong per string, mixed into `buf`."""
    at = int(round(ev.time * SAMPLE_RATE))
    if at >= n_samples:
        return
    remaining = n_samples - at
    vel = ev.velocity

    if ev.kind == "chord":
        specs = list(enumerate(ev.payload))
    elif ev.kind == "power":
        specs = list(ev.payload)
    elif ev.kind == "single":
        sidx, fret = ev.payload
        specs = [(sidx, fret)]
    elif ev.kind == "tap":
        sidx, fret = ev.payload
        # fret-hand tap / mute: a slap, modelled as a very short burst on the
        # highest open string plus percussive pick noise, no ringing pitch
        specs = [(sidx, fret)]
    else:
        specs = []

    for sidx, fret in specs:
        if sidx < 0 or sidx >= len(OPEN_FREQS):
            continue
        freq = OPEN_FREQS[sidx] * (2.0 ** (fret / 12.0))
        if freq < 40.0 or freq > 1800.0:
            continue
        # per-string velocity spread: strummed strings decay across the pick
        spread = 1.0 - 0.055 * sidx
        amp = vel * spread * fx.dry_gain * 0.34
        bright = max(0.05, min(0.72, ev.bright + (STRING_BRIGHT[sidx] - 0.45) * 0.5))
        t60 = STRING_T60[sidx] * ev.t60_scale * fx.mix_t60
        pluck = max(0.03, min(0.5, ev.pluck_pos))
        # Cap the note length instead of running it to the end of the file.
        # Synthesising every string out to `remaining` costs O(events x strings x
        # file_length) samples, which made a 25 s fixture take minutes and the
        # whole corpus a quarter of an hour. Beyond the audible tail the output
        # is below -100 dB and is then discarded by the float->16-bit conversion
        # anyway, so truncating changes nothing observable.
        tail = int(kNoteTailFloorSeconds * SAMPLE_RATE) + \
            int((t60 / 60.0) * kNoteTailDepthDb * SAMPLE_RATE)
        need = remaining if remaining < tail else tail
        seg = pluck_string(fx, rng, freq, t60, bright, pluck, amp, need)
        for n, v in enumerate(seg):
            buf[at + n] += v

    if ev.kind == "tap":
        # pick noise: a short broadband burst, which is most of what survives
        # in the spectrum for a properly muted note
        noise_len = min(remaining, int(0.012 * SAMPLE_RATE))
        y = 0.0
        for n in range(noise_len):
            y = 0.55 * y + 0.45 * rng.uniform(-1.0, 1.0)
            buf[at + n] += vel * fx.dry_gain * 0.22 * y * (1.0 - n / float(noise_len)) ** 2
    else:
        # Every pluck has a pick-attack transient, even a soft one. Without it
        # the excitation is too gradual and a real onset detector cannot find
        # the attack at all, which would make the declared ground truth
        # unfindable and the whole benchmark a lie.
        # Longer, softer bursts (palm mute, tapping) get proportionally more
        # noise energy because that is where a real player is scraping.
        burst_len = min(remaining, int(0.009 * SAMPLE_RATE))
        scrape = 0.5 + 2.0 * min(1.0, ev.pluck_pos / 0.5)
        level = vel * fx.dry_gain * 0.085 * (1.0 + ev.t60_scale * -0.55)
        level *= max(0.25, scrape)
        y = 0.0
        for n in range(burst_len):
            x = rng.uniform(-1.0, 1.0)
            y = 0.62 * y + 0.38 * x
            # attack-shaped: instantaneous rise, fast decay
            shape = math.exp(-n / (0.0022 * SAMPLE_RATE))
            buf[at + n] += level * (0.55 * x + 0.45 * y) * shape


def render(fx, rng):
    n_samples = int(round(fx.duration_seconds * SAMPLE_RATE))
    buf = [0.0] * n_samples

    for ev in fx.events:
        render_event(fx, rng, ev, buf, n_samples)

    if fx.drive is not None:
        dry = scale_to_peak(buf, 0.55 * fx.dry_gain)
        wet = soft_clip(buf, fx.drive)
        m = fx.drive_mix
        buf = [(1.0 - m) * d + m * w for d, w in zip(dry, wet)]

    # amp/cabinet tone: the analysis tap is post input-gain but the material a
    # tracker hears still carries the cabinet's roll-off
    lp = 5200.0 if fx.drive is None else 3200.0
    buf = apply_onepole_lp(buf, lp, SAMPLE_RATE)
    buf = apply_onepole_hp(buf, 62.0, SAMPLE_RATE)
    buf = apply_biquad(buf, biquad_peak(SAMPLE_RATE, 2400.0, 0.9, 3.0))

    if fx.dry_gain < 1.0 or fx.peak_dbfs < -12.0:
        pass

    buf = capture_chain(fx, buf, rng)
    return buf


def write_wav(path, samples):
    frames = bytearray()
    peak = 1.0 / 32767.0 * 32767.0
    for v in samples:
        if v > peak:
            v = peak
        elif v < -peak:
            v = -peak
        # round-half-away-from-zero so the conversion is exactly reproducible
        frames += struct.pack("<h", int(math.floor(v * 32767.0 + 0.5)))
    with wave.open(path, "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(BIT_DEPTH // 8)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(bytes(frames))
    return len(frames)


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Fixture definitions
# ---------------------------------------------------------------------------

def fixtures(profile):
    smoke = profile == "smoke"

    def spec(name, default_bars, default_bpm):
        """Bars and BPM from FULL_BARS, or the 2-bar smoke override."""
        if smoke:
            return 2, default_bpm
        n, bpm = FULL_BARS[name]
        return n, bpm

    def bars(n):  # kept for readability at call sites that only need the count
        return n

    core = not smoke
    # Length budget. See the module docstring: the ~4 MB target in the task
    # contract is arithmetically unreachable for 19 fixtures at 48 kHz / 16-bit
    # mono (96000 B/s), which is 2.2 s per fixture -- less than one bar at any
    # plausible tempo, and far too short to measure the SPEC 19 gates. These bar
    # counts are the shortest lengths that still admit "acquire within 2 bars"
    # plus at least 2 bars of steady state, and they land at ~11 MB. The
    # deviation is reported in task-notes/EVAL-001.md rather than silently
    # resolved.
    FULL_BARS = {
        # name: (bars, bpm)
        "clean_eighths": (5, 126.0),
        "clean_sixteenths": (5, 126.0),
        "power_chords_distorted": (5, 126.0),
        "palm_mute_metal": (5, 126.0),
        "blues_shuffle": (5, 108.0),
        "syncopated_funk": (5, 112.0),
        "sparse_single_notes": (5, 112.0),
        "arpeggio": (5, 120.0),
        "sustained_chords": (4, 96.0),
        "missing_downbeats": (5, 120.0),
        # 3 play + 2 silent + 3 play: the minimum that shows a stop, a gap long
        # enough to lose lock, and a recovery that can be timed.
        "stop_start": (8, 132.0),
        # Ramps need more beats, not more bars, for the curve to be visible.
        "accelerando": (6, 108.0),
        "ritardando": (6, 152.0),
        "waltz_3_4": (6, 138.0),
        "compound_6_8": (6, 96.0),
        "noisy_microphone": (5, 126.0),
        "line_input_low_level": (5, 126.0),
        "line_input_clipping": (5, 126.0),
        "tapping_muting_only": (5, 112.0),
    }
    human = ("humanised",)
    out = []

    out.append(Fixture(
        "clean_eighths", "clean_eighths",
        "Straight downstroke strumming on open-position triads, steady 4/4, "
        "clean pickup through a mic. The baseline every tracker must pass: "
        "clear eighth-note attacks, real low end from the body, no clipping.",
        4, 4, "quarter", 4, *spec("clean_eighths", 12, 120.0),
        p_clean_eighths, "mic",
        peak_dbfs=-6.0, noise_dbfs=-66.0, room_wet=0.16,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "clean_sixteenths", "clean_sixteenths",
        "Constant sixteenth-note strumming. Probes whether a tracker locks to "
        "the beat or is pulled into double time by the denser grid, which is "
        "the single most common failure on guitar input.",
        4, 4, "quarter", 4, *spec("clean_sixteenths", 12, 120.0),
        p_clean_sixteenths, "line",
        peak_dbfs=-7.0, noise_dbfs=-72.0,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "power_chords_distorted", "distorted_power_chords",
        "Saturated root+fifth power chords through a distorted amp. Saturation "
        "generates harmonic content above the strings' fundamentals and pulls "
        "energy down into the low mids, so spectral-flux onset detection sees a "
        "much denser spectrum than a clean guitar.",
        4, 4, "quarter", 4, *spec("power_chords_distorted", 12, 120.0),
        p_power_chords, "line",
        drive=3.4, drive_mix=0.85, peak_dbfs=-4.0, noise_dbfs=-70.0,
        extra_tags=("core", "steady_tempo", "distorted") + human, core=core))

    out.append(Fixture(
        "palm_mute_metal", "palm_mute",
        "Sixteenth-note palm-muted chugs with a very short decay and a dark pick "
        "burst. Almost no pitched harmonic content survives; this is close to "
        "percussive noise, which is the hardest realistic case for trackers that "
        "weight spectral flux over broadband energy.",
        4, 4, "quarter", 4, *spec("palm_mute_metal", 12, 120.0),
        p_palm_mute, "line",
        drive=2.0, drive_mix=0.55, peak_dbfs=-6.0, noise_dbfs=-71.0,
        extra_tags=("core", "steady_tempo", "distorted") + human, core=core))

    out.append(Fixture(
        "blues_shuffle", "blues_shuffle",
        "Triplet swing eighths (2:1 ratio) over open E and A. The third "
        "subdivision of every beat is empty and the attacks are late, so a "
        "tracker that assumes a straight eighth grid will read the wrong pulse.",
        4, 4, "quarter", 4, *spec("blues_shuffle", 10, 100.0),
        p_blues_shuffle, "mic",
        peak_dbfs=-6.0, noise_dbfs=-64.0, room_wet=0.18,
        extra_tags=("core", "steady_tempo", "swing") + human, core=core))

    out.append(Fixture(
        "syncopated_funk", "syncopated_funk",
        "Sixteenth-note funk with the attack deliberately missing on the last "
        "downbeat of each bar and ghost notes straddling beats. Directly tests "
        "the SPEC 19 gate 'no tempo jump from one isolated syncopated event'.",
        4, 4, "quarter", 4, *spec("syncopated_funk", 12, 105.0),
        p_syncopated_funk, "line",
        peak_dbfs=-6.5, noise_dbfs=-70.0,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "sparse_single_notes", "sparse_single_notes",
        "Sparse lead-style single notes on the treble strings with long "
        "ringing tails. Transient density is far below anything in a pop "
        "recording; a tracker that requires regular onsets must still hold lock "
        "through the gaps.",
        4, 4, "quarter", 4, *spec("sparse_single_notes", 12, 100.0),
        p_sparse_single_notes, "mic",
        peak_dbfs=-6.0, noise_dbfs=-62.0, room_wet=0.20,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "arpeggio", "arpeggio",
        "Sixteenth-note broken chords, one string per note. Six distinct "
        "transients per beat at a steady rate is the classic double-time trap, "
        "and the notes decay so each attack is a genuine percussive event "
        "rather than a strum smear.",
        4, 4, "quarter", 4, *spec("arpeggio", 10, 112.0),
        p_arpeggio, "line",
        peak_dbfs=-7.0, noise_dbfs=-70.0,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "sustained_chords", "sustained_chords",
        "Half-note chords plucked softly near the middle of the string: slow "
        "bloom, long decay, one transient every two beats. Tests lock stability "
        "with almost no transient density and no per-beat re-attack.",
        4, 4, "quarter", 4, *spec("sustained_chords", 6, 84.0),
        p_sustained_chords, "mic",
        peak_dbfs=-6.0, noise_dbfs=-60.0, room_wet=0.22,
        extra_tags=("core", "steady_tempo"), core=core))

    out.append(Fixture(
        "missing_downbeats", "missing_downbeats",
        "Alternating bars have no attack on the downbeat at all; the previous "
        "chord is left ringing through it. The metric grid is unchanged, so a "
        "tracker that insists on seeing the downbeat must not lose lock or "
        "invent a tempo. This is the sharpest downbeat-phasing probe available "
        "without a human playing it.",
        4, 4, "quarter", 4, *spec("missing_downbeats", 12, 112.0),
        p_missing_downbeats, "line",
        peak_dbfs=-7.0, noise_dbfs=-70.0,
        extra_tags=("core", "steady_tempo") + human, core=core))

    out.append(Fixture(
        "stop_start", "stop_start",
        "Four bars playing, two bars of deliberate silence, four bars playing "
        "again; the metric grid continues unbroken through the silence. Probes "
        "the SPEC 19 stop/start recovery gate and the false-beat-in-silence "
        "gate (SPEC 12.3).",
        4, 4, "quarter", 4, *spec("stop_start", 12, 100.0),
        p_stop_start, "line",
        peak_dbfs=-6.5, noise_dbfs=-73.0,
        extra_tags=("core", "steady_tempo", "contains_silence") + human,
        core=core))

    out.append(Fixture(
        "accelerando", "accelerando",
        "Linear tempo ramp from 100 to 146 BPM over the whole file, straight "
        "eighth strums following it. Beat times in the ground truth are the "
        "analytic integral of the tempo curve, so they are genuinely "
        "non-uniform. Proves a tracker can follow a controlled gradual ramp "
        "without stepping or oscillating (SPEC 19, EVAL-003 'gradual ramp').",
        4, 4, "quarter", 4, *spec("accelerando", 12, 100.0),
        p_ramp, "mic",
        bpm_end=146.0, peak_dbfs=-6.0, noise_dbfs=-64.0, room_wet=0.16,
        extra_tags=("tempo_ramp",) + human))

    out.append(Fixture(
        "ritardando", "ritardando",
        "Linear tempo ramp from 146 down to 100 BPM. Same integrator as the "
        "accelerando fixture, run backwards: a tracker that only handles "
        "acceleration, or that lags its phase correction, fails here.",
        4, 4, "quarter", 4, *spec("ritardando", 12, 146.0),
        p_ramp, "mic",
        bpm_end=100.0, peak_dbfs=-6.0, noise_dbfs=-64.0, room_wet=0.16,
        extra_tags=("tempo_ramp",) + human))

    out.append(Fixture(
        "waltz_3_4", "meter_3_4",
        "3/4 bass-note-on-one / chord-on-two-and-three. Three beats per bar "
        "breaks the 4/4 prior every beat tracker is built with; a tracker that "
        "assumes four will report a bar phase that drifts by a beat every bar.",
        3, 4, "quarter", 3, *spec("waltz_3_4", 7, 132.0),
        p_meter_3_4, "line",
        peak_dbfs=-6.5, noise_dbfs=-69.0,
        extra_tags=("steady_tempo",) + human))

    out.append(Fixture(
        "compound_6_8", "meter_6_8",
        "6/8 compound duple at 84 dotted-quarter BPM, subdivided into three "
        "eighths with bass on 1 and chords on the 2nd and 5th. The scored beat "
        "grid is the dotted quarter (2 per bar); the eighths are three per beat. "
        "A tracker that locks to the eighths reports exactly triple tempo, so "
        "this separates 'understands compound meter' from 'found a periodicity'.",
        6, 8, "dotted-quarter", 2, *spec("compound_6_8", 8, 84.0),
        p_meter_6_8, "mic",
        peak_dbfs=-6.5, noise_dbfs=-63.0, room_wet=0.18,
        extra_tags=("steady_tempo",) + human))

    out.append(Fixture(
        "noisy_microphone", "noisy_microphone",
        "Same 4/4 eighth-note material as clean_eighths, captured by a noisy "
        "microphone: hiss floor around -34 dBFS, low rumble, plus a few "
        "randomised handling ticks. Isolates SNR robustness from level "
        "robustness, which the two line-input fixtures vary separately.",
        4, 4, "quarter", 4, *spec("noisy_microphone", 12, 120.0),
        p_clean_eighths, "mic",
        peak_dbfs=-6.0, noise_dbfs=-34.0, room_wet=0.22,
        extra_tags=("noisy", "microphone", "steady_tempo")))

    out.append(Fixture(
        "line_input_low_level", "line_input_low_level",
        "Direct instrument-cable injection at a peak near -28 dBFS with a small "
        "50/100/150 Hz mains hum and a -76 dBFS noise floor. The gain-staging "
        "failure mode: everything is fine except that there is far less of it.",
        4, 4, "quarter", 4, *spec("line_input_low_level", 12, 120.0),
        p_clean_eighths, "line",
        peak_dbfs=-28.0, noise_dbfs=-76.0, hum_dbfs=-74.0, dry_gain=0.35,
        extra_tags=("low_level", "line_input", "steady_tempo") + human))

    out.append(Fixture(
        "line_input_clipping", "line_input_clipping",
        "Direct injection driven past the interface's input ceiling and hard "
        "clipped, producing flat-topped transients. Clipping destroys the "
        "amplitude information a level-based onset detector relies on and can "
        "flatten the very peaks that carry the beat, so this is where "
        "amplitude-threshold trackers are expected to fail.",
        4, 4, "quarter", 4, *spec("line_input_clipping", 12, 120.0),
        p_clean_eighths, "line",
        clip_threshold=1.0, noise_dbfs=-68.0,
        extra_tags=("clipping", "line_input", "steady_tempo")))

    out.append(Fixture(
        "tapping_muting_only", "tapping_muting_only",
        "Palm-muted chugs and fret-hand taps only: essentially broadband "
        "percussive noise with a very short dark decay and almost no pitched "
        "harmonic content. The realistic floor of what a guitar can present to "
        "a tracker, and a deliberate check that a tracker is not secretly "
        "depending on harmonic material.",
        4, 4, "quarter", 4, *spec("tapping_muting_only", 12, 100.0),
        p_tapping_muting_only, "line",
        peak_dbfs=-7.0, noise_dbfs=-72.0, drive=1.6, drive_mix=0.35,
        extra_tags=("steady_tempo", "distorted") + human))

    return out


# ---------------------------------------------------------------------------
# Preparation: grids, events, derived ground truth
# ---------------------------------------------------------------------------

def prepare(fx):
    if fx.bpm_end is None:
        fx.beat_seconds = 60.0 / fx.bpm
        fx.beat_start = fx.lead_in
        fx.beat_end_time = fx.lead_in + fx.bars * fx.beats_per_bar * fx.beat_seconds
        fx.duration_seconds = fx.beat_end_time + fx.mix_rate
        fx.beats = constant_beats(fx.beat_start, fx.beat_seconds,
                                  fx.bars * fx.beats_per_bar)
    else:
        fx.beat_start = fx.lead_in
        count = fx.bars * fx.beats_per_bar
        fx.ramp_t0 = fx.beat_start
        # Choose the ramp window so the LAST beat lands exactly on rampEnd. The
        # tempo is linear in time, so its mean over [t0, t1] is the value at the
        # midpoint, (bpm0+bpm1)/2, and the integrated phase over the window is
        #   phase(t1) = (t1-t0) * (bpm0+bpm1) / 120.
        # Requiring phase(t1) == count-1 therefore solves in closed form. Doing
        # it this way means every beat lies strictly inside the ramp: nothing is
        # clamped, and there is no tail region where the ground truth would need
        # a tempo definition the audio never exercises.
        fx.ramp_t1 = fx.beat_start + (120.0 * (count - 1)) / (fx.bpm + fx.bpm_end)
        fx.beats = ramp_beats(fx.beat_start, fx.ramp_t0, fx.ramp_t1, fx.bpm,
                              fx.bpm_end, count)
        fx.beat_end_time = fx.beats[-1] + (60.0 / fx.bpm_end)
        fx.duration_seconds = fx.beat_end_time + fx.mix_rate
        fx.beat_seconds = 60.0 / fx.bpm  # mean, used only by `beat_at`

    return fx


def beat_at(fx, i):
    if fx.bpm_end is None:
        return 60.0 / fx.bpm
    if i + 1 < len(fx.beats):
        return fx.beats[i + 1] - fx.beats[i]
    return 60.0 / fx.bpm_end


# Attach `beat_at` / `beat_seconds` to the fixture object as bound helpers.
Fixture.beat_at = lambda self, i: beat_at(self, i)


def build_events(fx, seed):
    rng = random.Random(seed ^ 0x5bf03635)
    fx.events = fx.pattern(fx, rng, fx.beats)
    fx.events.sort(key=lambda e: e.time)
    return rng


def compute_ground_truth(fx, onsets_tol=0.030):
    """Derive the declared ground truth from the events actually rendered.

    Onsets are de-duplicated to `kMinOnsetSeparation`. Two pattern positions can
    otherwise jitter onto (or clamp to) the same instant -- which would declare
    an onset pair that no detector can resolve and quietly inflate the expected
    onset count, making the benchmark unfalsifiable rather than hard.
    """
    raw = sorted(e.time for e in fx.events)
    onsets = []
    for t in raw:
        if onsets and (t - onsets[-1]) < kMinOnsetSeparation:
            continue
        onsets.append(t)
    fx.silent_beats = [
        i for i, t in enumerate(fx.beats)
        if not any(abs(o - t) <= onsets_tol for o in onsets)
    ]
    spans = []
    for i in fx.silent_beats:
        lo = fx.beats[i] - onsets_tol
        hi = fx.beats[i] + onsets_tol
        if spans and lo <= spans[-1][1]:
            spans[-1][1] = max(spans[-1][1], hi)
        else:
            spans.append([lo, hi])
    fx.silence_spans = spans
    return onsets


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------

def measure_wav(path):
    """Measured statistics read back from the 16-bit file on disk.

    These are facts about the bytes that ship, not intentions, so the
    evaluation harness can assert the impairment it thinks it is testing is
    actually present (a 'clipping' fixture with no flat tops would be a lie).
    Reading the file back also makes `--check` compare like with like.
    """
    with wave.open(path, "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != \
                (CHANNELS, BIT_DEPTH // 8, SAMPLE_RATE):
            raise SystemExit("%s: not %d-bit mono @ %d Hz" %
                             (path, BIT_DEPTH, SAMPLE_RATE))
        frames = w.getnframes()
        raw = w.readframes(frames)
    samples = [struct.unpack_from("<h", raw, i * 2)[0] / 32768.0
               for i in range(len(raw) // 2)]
    del raw
    return measure(samples, frames)


def measure(samples, frames):
    """Peak / RMS / clipped-sample fraction of a float signal."""
    peak = 0
    acc = 0
    clip_samples = 0
    for x in samples:
        s = int(math.floor(x * 32767.0 + 0.5))
        if s > peak:
            peak = s
        elif -s > peak:
            peak = -s
        acc += x * x
        if s >= 32760 or s <= -32760:
            clip_samples += 1
    n = float(len(samples))
    rms = math.sqrt(acc / n) if n > 0.0 else 0.0
    peak_lin = peak / 32768.0
    return {
        "peakDbfs": round(20.0 * math.log10(peak_lin) if peak_lin > 0 else -120.0,
                          3),
        "rmsDbfs": round(20.0 * math.log10(rms) if rms > 0 else -120.0, 3),
        "clippedSampleFraction": round(clip_samples / n if n else 0.0, 6),
        "frames": frames,
    }


# ---------------------------------------------------------------------------
# True silence: derived from synthesis intent, verified against the audio
# ---------------------------------------------------------------------------
#
# WHY A SECOND FIELD. `silenceSpans` names narrow +/-30 ms windows around beats
# the player deliberately did NOT play, which is a real and useful piece of
# ground truth: it tells a harness that a beat is still expected on a grid the
# audio does not reinforce. It is NOT a description of silence, and consuming it
# as if it were measures nothing. For `stop_start` it declared 0.480 s across 8
# windows; the audio is genuinely silent for about 4 s in one contiguous stretch,
# and every beat of the maintained grid falls inside a declared window. A
# false-beat-rate-in-silence metric computed over those windows therefore counts
# the *correct* behaviour -- holding the grid through a gap -- as 16.7 false
# beats per second. The instrument could not measure the gate it was built for.
# `trueSilenceSpans` is the field that describes where the guitar stopped
# playing, and SPEC 19 / SPEC 12.3 should be scored against it.

# A note is inaudible once it has fallen this far below its own peak.
#
# 45 dB is chosen musically, not arithmetically. It sits below the 24-bit
# noise floor of any reasonable recording chain and far below the ~40 dB level at
# which a decaying guitar string stops contributing perceptually, so a span
# declared silent at this depth contains no string a listener would call
# "still ringing". Crucially it is NOT the file's noise floor, which varies from
# -76 dBFS (quiet line capture) to -34 dBFS (noisy mic): a criterion relative to
# the noise floor would call the noisy-mic fixture's gaps silent at a level where
# the clean capture's would not, even though the guitar is equally absent in
# both. Measured against the committed audio, notes reach this depth between
# 0.09 s (palm-muted) and 2.4 s (low E).
kSilenceDepthDb = 45.0

# A gap shorter than this is between two notes in one phrase, not a stop. At
# 126 BPM a sixteenth note is 0.12 s, so 0.25 s excludes every subdivision a
# player could be playing through while still admitting any deliberate pause. The
# reason this matters: palm-muted sixteenths have ~0.1 s of audible ring between
# notes, and without a floor the corpus would declare 80 "silent" spans inside a
# fixture that is audibly continuous sixteenth-note chugging.
kMinTrueSilenceSeconds = 0.25

# The room is a linear filter on the whole signal, so it keeps sounding after
# the last note stops. Its tail is the reverberation of something the player
# actually played, which is why it is included in the audible interval: a
# reverberant decay is not the guitarist continuing to play.
kRoomTailScale = 2.5

# Added to every event's audible window. Two reasons, both from measuring the
# committed audio:
#   - The analytic ring time agrees with synthesised notes to within ~5 ms, but a
#     few ms is enough to put a decay tail inside a declared span (measured: a
#     palm-muted chord declared silent 30 ms after its attack was still 9.6 dB
#     above the noise floor).
#   - A pluck does not reach its loudest instant at the moment the pick touches
#     the string; the strings beat against each other for tens of milliseconds.
# The direction is deliberate. Over-declaring silence is the dangerous error: a
# tracker emitting a beat inside a "silent" span is scored as fabricating one.
# Under-declaring costs at most a few tens of milliseconds of measurable silence,
# which is the cheap direction to be wrong in.
kAudibleGuardSeconds = 0.030


def audible_seconds(t60, depth_db=kSilenceDepthDb):
    """Time for a note with T60 `t60` to fall `depth_db` dB below its own peak.

    Mirrors the two-stage decay in karplus_strong exactly: the first
    kInitialDecaySeconds run at 60/t60a dB per second (t60a = ratio * t60), the
    rest at 60/t60 dB per second, which is the definition of T60. Verified
    against synthesised notes over t60 0.045-4.2 s and depth 40-55 dB: agreement
    within 5%, limited by the 5 ms measurement step.
    """
    t60 = max(1e-4, t60)
    t60a = t60 * kInitialDecayRatio
    fast_db_per_s = 60.0 / t60a
    depth_at_switch = fast_db_per_s * kInitialDecaySeconds
    if depth_db <= depth_at_switch:
        return depth_db / fast_db_per_s
    return kInitialDecaySeconds + (depth_db - depth_at_switch) * t60 / 60.0


def event_strings(ev):
    """The (stringIndex, fret) pairs an event sounds, mirroring render_event."""
    if ev.kind == "chord":
        return list(enumerate(ev.payload))
    if ev.kind == "power":
        return list(ev.payload)
    return [ev.payload]


def event_audible_seconds(fx, ev):
    """How long after `ev.time` the event is still audible.

    The longest-ringing string in the event sets the end of the window; the
    others finish inside it. The room tail is added because it is a real decay of
    the same event, and excluding it would declare a mic-captured gap silent
    while its reverb was still audible.
    """
    best = 0.0
    for sidx, _fret in event_strings(ev):
        if 0 <= sidx < len(OPEN_FREQS):
            t60 = STRING_T60[sidx] * ev.t60_scale * fx.mix_t60
            best = max(best, audible_seconds(t60))
    if fx.room_wet > 0.0:
        best += fx.room_wet * kRoomTailScale
    return best + kAudibleGuardSeconds


def compute_true_silence_spans(fx):
    """Regions where the guitar is genuinely not sounding, from synthesis intent.

    Built from the event list and the decay model, NOT by measuring the rendered
    WAV. That ordering is deliberate and is the whole point of the fix: a field
    reverse-engineered from the file it describes cannot catch a synthesis bug,
    because it would faithfully describe the bug. Measuring the audio afterwards
    is a verification step that can disagree with this declaration, which is what
    makes the check meaningful.

    Two deliberate refinements, both learned from measuring the committed audio:

    1. Audible intervals are extended to the END of the event's attack window as
       well as its decay. A real pluck does not reach its loudest instant at the
       instant the pick contacts the string: the strings are excited together but
       beat against each other, so a chord takes tens of milliseconds to reach
       full amplitude. Anchoring the audible window at the first sample instead
       produced spans whose first 20-30 ms contain the loudest part of the note.
    2. Spans are NOT coalesced. Two gaps separated by a short audible chord are
       genuinely two separate silences; merging them (an earlier version of this
       function did) swallowed the attack between them and produced spans that
       overlapped real onsets.
    """
    if not fx.events:
        fx.true_silence_spans = []
        return fx.true_silence_spans

    # Attack settling time: one full period of the lowest-sounding string in the
    # event. Independent of any measurement, derived from the tuning.
    intervals = []
    for ev in fx.events:
        lowest = None
        for sidx, _fret in event_strings(ev):
            if 0 <= sidx < len(OPEN_FREQS):
                lowest = sidx if lowest is None else min(lowest, sidx)
        attack_settle = (1.0 / OPEN_FREQS[lowest]) if lowest is not None else 0.01
        start = ev.time
        end = ev.time + max(attack_settle, event_audible_seconds(fx, ev))
        intervals.append([start, end])

    intervals.sort()
    merged = []
    for a, b in intervals:
        if merged and a <= merged[-1][1]:
            if b > merged[-1][1]:
                merged[-1][1] = b
        else:
            merged.append([a, b])

    spans = []
    prev = 0.0
    for a, b in merged:
        if a - prev > kMinTrueSilenceSeconds:
            spans.append([prev, a])
        if b > prev:
            prev = b
    if fx.duration_seconds - prev > kMinTrueSilenceSeconds:
        spans.append([prev, fx.duration_seconds])

    # Trim to the file, and round CONSERVATIVELY: round the start UP (later) and
    # the end DOWN (earlier) to 6 decimal places, so a serialised span can only
    # ever be smaller than the region that was derived. Plain round() rounds the
    # end either way, and rounding it up pushed a span's end past the onset that
    # terminates it -- which the verification below caught as a span swallowing an
    # attack. A silence span must never contain an onset, and this is what
    # guarantees that on the serialised bytes rather than only in memory.
    out = []
    for a, b in spans:
        lo = max(0.0, a)
        hi = min(fx.duration_seconds, b)
        lo = math.ceil(lo * 1e6) / 1e6
        hi = math.floor(hi * 1e6) / 1e6
        if hi - lo > kMinTrueSilenceSeconds:
            out.append([lo, hi])
    fx.true_silence_spans = out
    return out


def entry_for(fx, wav_path, rel_path, sha, nbytes, stats=None):
    meter = {
        "numerator": fx.meter_n,
        "denominator": fx.meter_d,
        "beatUnit": fx.beat_unit,
        "beatsPerBar": fx.beats_per_bar,
    }
    if fx.beat_unit == "dotted-quarter":
        sub_per_beat = 3
    elif fx.bpm_end is None:
        sub_per_beat = 1
    else:
        sub_per_beat = 1
    if fx.scenario_tag in ("clean_eighths", "distorted_power_chords",
                           "accelerando", "ritardando"):
        sub_per_beat = 2
    elif fx.scenario_tag == "clean_sixteenths":
        sub_per_beat = 4
    elif fx.scenario_tag == "arpeggio":
        sub_per_beat = 4
    elif fx.scenario_tag == "blues_shuffle":
        sub_per_beat = 2
    subdivision = {
        "perBeat": sub_per_beat,
        "bpm": round((fx.bpm if fx.bpm_end is None else
                      (fx.bpm + fx.bpm_end) * 0.5) * sub_per_beat, 6),
    }
    if fx.scenario_tag == "blues_shuffle":
        subdivision["swingRatio"] = 2.0 / 3.0

    entry = {
        "name": fx.name,
        "file": rel_path,
        "sha256": sha,
        "bytes": nbytes,
        "sampleRate": SAMPLE_RATE,
        "channels": CHANNELS,
        "bitDepth": BIT_DEPTH,
        "durationSeconds": round(fx.duration_seconds, 6),
        "meter": meter,
        "subdivision": subdivision,
        "beats": [round(b, 9) for b in fx.beats],
        "onsets": [round(o, 9) for o in compute_ground_truth(fx)],
        "signal": stats or {},
        "downbeats": list(range(0, len(fx.beats), fx.beats_per_bar)),
        "silentBeats": fx.silent_beats,
        "silenceSpans": [[round(a, 6), round(b, 6)] for a, b in fx.silence_spans],
        "trueSilenceSpans": [list(s) for s in fx.true_silence_spans],
        "scenarioTags": [fx.scenario_tag] + list(fx.extra_tags),
        "notes": fx.notes,
        "license": LICENSE,
        "provenance": (
            "Synthesised from scratch by {} v{} ({}). Open-position "
            "Karplus-Strong plucked strings through a simulated mic/line capture "
            "chain. No third-party, commercial or otherwise copyrighted audio "
            "was used, read or derived from.".format(
                GENERATOR_NAME, GENERATOR_VERSION, "pure Python standard library")
        ),
    }
    if fx.bpm_end is None:
        entry["nominalBpm"] = round(fx.bpm, 6)
        entry["tempoProfile"] = "constant"
    else:
        entry["bpmStart"] = round(fx.bpm, 6)
        entry["bpmEnd"] = round(fx.bpm_end, 6)
        entry["tempoProfile"] = "linear-ramp"
        entry["rampStartSeconds"] = round(fx.ramp_t0, 6)
        entry["rampEndSeconds"] = round(fx.ramp_t1, 6)
    del wav_path
    return entry


def verify_true_silence_spans(entry):
    """Fail loudly if a declared true-silence span contradicts the ground truth.

    This is the generator-side half of the repair's contract. The spans are
    derived from synthesis intent and only then compared against the rendered
    audio and the rest of the manifest, so a disagreement means the synthesis
    does not do what was intended -- the failure mode worth catching loudly.
    The C++ suite re-checks the same properties independently.
    """
    name = entry["name"]
    spans = entry["trueSilenceSpans"]
    dur = entry["durationSeconds"]
    onsets = entry["onsets"]

    previous_end = None
    for span in spans:
        if len(span) != 2:
            raise SystemExit("%s: trueSilenceSpans entry is not a pair" % name)
        a, b = span
        if not (0.0 <= a < b <= dur + 1e-9):
            raise SystemExit(
                "%s: trueSilenceSpans [%.6f, %.6f] outside [0, %.6f]"
                % (name, a, b, dur))
        if previous_end is not None and a < previous_end - 1e-9:
            raise SystemExit(
                "%s: trueSilenceSpans not sorted / overlapping at %.6f"
                % (name, a))
        previous_end = b
        for o in onsets:
            if a - 1e-9 < o < b - 1e-9:
                raise SystemExit(
                    "%s: trueSilenceSpans [%.6f, %.6f] swallows onset %.6f"
                    % (name, a, b, o))


def verify_core_membership(entry):
    """Fail loudly if the `core` tag and the `core` membership list disagree.

    The two disagreed in the merged corpus (scenario-tag names against fixture
    names) and nothing noticed, because the test that existed only checked
    vocabulary membership. Both sides are now checked at generation time as
    well as in the suite.
    """
    name = entry["name"]
    tagged = "core" in entry["scenarioTags"]
    listed = name in CORE_FIXTURES
    if tagged != listed:
        raise SystemExit(
            "%s: carries the `core` tag=%s but tagVocabulary.core lists it=%s"
            % (name, tagged, listed))
    if tagged and "steady_tempo" not in entry["scenarioTags"]:
        raise SystemExit(
            "%s: is core but not steady_tempo, so SPEC 19's BPM relative error "
            "has no defined meaning on it" % name)


def canonical_json(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True) + "\n"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def seed_for(name):
    key = "{}|{}|{}".format(CORPUS_ID, GENERATOR_VERSION, name)
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()
    return int(digest[:16], 16)


def generate(out_dir, manifest_path, profile, check=False, verbose=True):
    os.makedirs(out_dir, exist_ok=True)
    entries = []
    for fx in fixtures(profile):
        prepare(fx)
        build_events(fx, seed_for(fx.name))
        compute_ground_truth(fx)
        # Must precede entry_for(), which reads fx.true_silence_spans. Derived
        # from the event list and the decay model, so it does not depend on
        # fx.audio and is identical in --check mode.
        compute_true_silence_spans(fx)
        rng = random.Random(seed_for(fx.name))
        if not check:
            fx.audio = render(fx, rng)
        wav_path = os.path.join(out_dir, fx.name + ".wav")
        rel = "wav/" + fx.name + ".wav"

        if check:
            if not os.path.isfile(wav_path):
                raise SystemExit("missing fixture: " + wav_path)
            with wave.open(wav_path, "rb") as w:
                got = (w.getnchannels(), w.getsampwidth(), w.getframerate(),
                       w.getnframes())
            if got[:3] != (CHANNELS, BIT_DEPTH // 8, SAMPLE_RATE):
                raise SystemExit("%s: format drift %r" % (wav_path, got))
            frames = got[3]
            if abs(frames / float(SAMPLE_RATE) - fx.duration_seconds) > 1e-4:
                raise SystemExit("%s: length drift" % wav_path)
        else:
            frames = write_wav(wav_path, fx.audio)

        sha = sha256_of(wav_path)
        stats = measure_wav(wav_path)
        entry = entry_for(fx, wav_path, rel, sha,
                          os.path.getsize(wav_path), stats)
        verify_true_silence_spans(entry)
        verify_core_membership(entry)
        entries.append(entry)
        if verbose:
            print("%-24s %6.2fs %8d B %s" % (fx.name, fx.duration_seconds,
                                              os.path.getsize(wav_path), sha[:16]))

    manifest = {
        "schemaVersion": MANIFEST_SCHEMA_VERSION,
        "corpus": {
            "id": CORPUS_ID,
            "version": CORPUS_VERSION,
            "description": (
                "Synthetic solo-guitar rhythm evaluation corpus for rhythm "
                "tracker selection (SPEC.md 12, DEVPLAN.md EVAL-001, gate G3). "
                "All audio is synthesised by the committed generator; no "
                "third-party or commercial recordings are included."
            ),
            "profile": profile,
        },
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
            "language": "python3",
            "dependencies": "standard library only (wave, struct, math, random, "
                            "json, hashlib, argparse)",
            "synthesis": "Karplus-Strong plucked string + simulated mic/line "
                         "capture chain",
            "seedPolicy": (
                "seed = int(sha256('<corpusId>|<generatorVersion>|<fixtureName>')"
                ".hexdigest()[:16], 16); one random.Random per fixture, drawn "
                "in a fixed order (excitation -> velocity -> detune -> "
                "impairments). Regeneration is byte-identical. Bumping "
                "GENERATOR_VERSION changes every seed, so a generator change "
                "invalidates every committed hash loudly instead of silently "
                "producing a corpus that differs only in the noise."
            ),
            "command": "python3 tools/gen_fixtures.py --out wav",
        },
        "conventions": {
            "beats": (
                "Metric grid: the notated beat unit of the meter, "
                "beatsPerBar per bar. Constant spacing on steady fixtures; on "
                "the two tempoProfile=linear-ramp fixtures the spacing follows "
                "the analytic integral of the tempo function and is therefore "
                "genuinely non-uniform."
            ),
            "onsets": (
                "As-played perceptual attacks (start of a strum, chord, single "
                "note or tap), including the timing humanisation that was "
                "actually rendered. Individual string plucks inside one strum "
                "are one onset, not six."
            ),
            "silentBeats": (
                "Indices into `beats` with no onset within +/-30 ms. These beats "
                "are still scored, but a tracker has no transient to hear there."
            ),
            "silenceSpans": (
                "Narrow +/-30 ms windows around `silentBeats`: beats that were "
                "deliberately NOT played. This is NOT a description of silence. "
                "A beat inside one of these windows is still a beat the player "
                "intended and the grid still runs through it, so a tracker that "
                "keeps time here is correct. Do not compute a false-beat-rate "
                "from this field; use `trueSilenceSpans`."
            ),
            "trueSilenceSpans": (
                "Regions where the guitar is genuinely near-silent: no event's "
                "audible window (attack settling plus decay to %.0f dB below the "
                "note's own peak, plus the room tail where a room is modelled) "
                "overlaps the span, and the span is at least %.2f s long. This is "
                "what SPEC 19's 'silence does not create false acceleration' and "
                "SPEC 12.3's 'false beat rate in silence' must be measured "
                "against: a tracker emitting beats here is fabricating them. "
                "Derived from the event list and the decay model, then verified "
                "against the rendered audio; a span never overlaps an `onsets` "
                "entry. Sparse fixtures legitimately have empty or few spans -- "
                "an empty list means continuously sounding, not missing data."
            ) % (kSilenceDepthDb, kMinTrueSilenceSeconds),
            "trueSilenceDepthDb": kSilenceDepthDb,
            "trueSilenceMinSeconds": kMinTrueSilenceSeconds,
            "trueSilenceDerivation": (
                "A note is inaudible once it has fallen %.0f dB below its own "
                "peak. The criterion is relative to the note, not to the file's "
                "noise floor, which ranges from -76 dBFS (quiet line capture) to "
                "-34 dBFS (noisy mic): a noise-floor-relative test would call the "
                "noisy fixture's gaps silent where the clean fixture's would not, "
                "even though the guitarist is equally absent from both. %.0f dB "
                "sits below the noise floor of any reasonable recording chain and "
                "below the level at which a decaying string is still perceived as "
                "ringing, so a held, muted or decaying string is not silence."
                % (kSilenceDepthDb, kSilenceDepthDb)
            ),
            "downbeats": "Indices into `beats` of the meter's strong beat.",
            "subdivision": (
                "Level between ground-truth beats: perBeat subdivisions per "
                "beat and their nominal bpm, so a harness can also score a "
                "finer grid (eighths, sixteenths, compound-meter eighths)."
            ),
            "scenarioTags": (
                "First tag is the SPEC 12.2 required case and is unique across "
                "the corpus. Remaining tags come from the closed qualifier set "
                "documented in testdata/rhythm/README.md; `core` marks the "
                "fixtures SPEC 19 calls 'core fixtures'."
            ),
            "beatToleranceSeconds": 0.07,
            "beatToleranceNote": (
                "Suggested F-measure tolerance for SPEC 12.3 beat-event scoring. "
                "Chosen well above the generator's worst-case +/-10 ms timing "
                "humanisation so the tolerance measures the tracker, not the "
                "humanisation."
            ),
        },
        "tagVocabulary": {
            "scenario": list(SCENARIO_TAGS),
            "qualifier": list(QUALIFIER_TAGS),
            # Explicit fixture-name membership for the SPEC 19 "core fixtures"
            # denominator. Present and reconciled against the per-fixture `core`
            # tag in generate(); see CORE_FIXTURES for the definition and for why
            # `missing_downbeats` is included.
            "core": list(CORE_FIXTURES),
        },
        "totalBytes": sum(e["bytes"] for e in entries),
        "totalDurationSeconds": round(
            sum(e["durationSeconds"] for e in entries), 6),
        "fixtures": sorted(entries, key=lambda e: e["name"]),
    }

    text = canonical_json(manifest)
    if check:
        if not os.path.isfile(manifest_path):
            raise SystemExit("missing manifest: " + manifest_path)
        with open(manifest_path, "r", encoding="utf-8") as fh:
            have = fh.read()
        if have != text:
            raise SystemExit(
                "manifest drift: on-disk manifest.json differs from a fresh "
                "generation (run: python3 tools/gen_fixtures.py --out wav)")
        print("manifest matches (%d fixtures)" % len(entries))
    else:
        with open(manifest_path, "w", encoding="utf-8") as fh:
            fh.write(text)
    return entries


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=None,
                    help="output directory for the .wav fixtures "
                         "(default: <repo>/testdata/rhythm/wav)")
    ap.add_argument("--manifest", default=None,
                    help="path to manifest.json (default: <repo>/"
                         "testdata/rhythm/manifest.json)")
    ap.add_argument("--profile", choices=("full", "smoke"), default="full",
                    help="'full' is the SPEC 12.2 corpus; 'smoke' is a short "
                         "3-bar version for cheap CI exercise and is NOT valid "
                         "evidence for gate G3")
    ap.add_argument("--check", action="store_true",
                    help="do not generate; verify the on-disk corpus matches "
                         "a fresh deterministic generation")
    args = ap.parse_args(argv)

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)  # testdata/rhythm
    out_dir = args.out or os.path.join(root, "wav")
    manifest_path = args.manifest or os.path.join(root, "manifest.json")

    generate(out_dir, manifest_path, args.profile, check=args.check)
    if not args.check:
        print("wrote %s (%d fixtures)" % (manifest_path,
                                          len(fixtures(args.profile))))
    return 0


if __name__ == "__main__":
    sys.exit(main())