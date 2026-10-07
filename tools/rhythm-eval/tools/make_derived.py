#!/usr/bin/env python3
"""Derived robustness perturbations for the guitar rhythm evaluation corpus.

EVAL-003 / DEVPLAN.md 14. The base corpus (`testdata/rhythm/`) already contains
whole-fixture examples of every SPEC.md 12.2 required case. What it cannot do is
compare two backends under *identical* degradation, because two fixtures are two
different performances. This tool holds the audio constant and varies one thing,
so a tracker comparison produces a **degradation curve** instead of a pass/fail
against a different recording.

What this tool does NOT do
--------------------------
It does not generate new music and it does not re-score anything. It reads a
finished base fixture, applies one documented, deterministic transformation, and
writes the result plus a manifest into `testdata/rhythm/derived/`. The base
corpus is read-only here; see `README.md` in the derived directory.

Ground-truth semantics -- the part that is easy to get wrong
------------------------------------------------------------
A perturbation is classified by how it relates the *audio timeline* to the
*score timeline*, and the manifest records the classification explicitly:

  amplitude-only   noise, level scaling, hard clipping. The audio changes but no
                   event moves and the clip length does not change. `beats` and
                   `onsets` are inherited from the unperturbed parent baseline
                   **bit-for-bit** (same values, not merely close). Recorded as
                   `truthTransform = "inherit"`.

  evidence edit    dropping every K-th onset / injecting an offbeat burst /
                   carving a silent span. `beats` (the metric grid) is unchanged
                   because the player's pulse did not move; `onsets` changes
                   because attacks were physically added or removed. Recorded as
                   `truthTransform = "edit-onsets"`.

  global delay     offset every onset by a fixed number of milliseconds, length
                   preserving: the samples are delayed by N and the tail is
                   dropped. This is a real audio delay, so the as-played beat
                   times move too. `beats` and `onsets` are shifted by exactly
                   N/sampleRate. Recorded as `truthTransform = "shift"`.

  silence pad      leading / trailing digital silence appended at full scale-
                   level zero. Leading silence shifts every event time; trailing
                   silence leaves event times alone but extends the clip.
                   `truthTransform = "shift"` / `"append-silence"`.

  time warp        a tempo *step* at a known bar. This is a coherent tape-speed
                   warp applied to BOTH the audio and the event times:
                   phi(t) = t for t <= Ts and Ts + (t - Ts)/r for t > Ts.
                   Re-labelling the beats without moving the audio would be a
                   lie (the audio would no longer contain the claimed onsets) and
                   is not done. Recorded as `truthTransform = "warp"`.

The rule enforced in code: a perturbation may never keep the audio a pure
time-translate of the parent while claiming the event times are unchanged, and
may never claim a shift it did not apply. `tests/jam/RhythmDerivedTests.cpp`
re-derives each claim from the committed WAV bytes and manifest.

Determinism
-----------
Every random draw is seeded from `sha256("EVAL-003|<version>|<derivedName>")`,
so the output depends only on the committed inputs and this file. Running the
tool twice produces byte-identical WAVs and manifest; `--check` proves it by
regenerating into a temporary directory and comparing SHA-256 against the
committed manifest.

Disk discipline (hard constraint, DEVPLAN G3)
---------------------------------------------
The base corpus is 21 MB and the disk budget for derived audio is <= 15 MB.
Every derived clip is truncated to a 5.0 s window (`--window`), long enough to
acquire, show a tempo step and lose/keep phase, short enough that the whole grid
fits. The window and the parent frame range are recorded per file. Where a
fuller sweep was possible on paper, values were dropped and are listed in
`task-notes/EVAL-003.md` rather than silently omitted.

Standard library only: wave, struct, math, random, json, hashlib, argparse.
numpy is deliberately NOT used (it is not guaranteed on the build hosts).
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

# ---------------------------------------------------------------------------
# Format and conventions
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))

SAMPLE_RATE = 48000
CHANNELS = 1
BIT_DEPTH = 16
SAMPLE_WIDTH = BIT_DEPTH // 8

GENERATOR_NAME = "tools/rhythm-eval/tools/make_derived.py"
GENERATOR_VERSION = "1"
CORPUS_ID = "eval003-derived-perturbations"

#: The one 16-bit quantization rule, identical to the base generator so that an
#: unperturbed truncation of a parent is byte-identical to the parent's frames.
def float_to_pcm16(samples):
    frames = bytearray()
    for v in samples:
        if v > 1.0:
            v = 1.0
        elif v < -1.0:
            v = -1.0
        # round-half-away-from-zero: exactly reproducible across platforms.
        frames += struct.pack("<h", int(math.floor(v * 32767.0 + 0.5)))
    return bytes(frames)


def pcm16_to_float(raw):
    # Mirror float_to_pcm16 so read -> write is the identity on base bytes.
    count = len(raw) // 2
    return [s / 32767.0 for s in struct.unpack("<%dh" % count, raw)]


def db_to_lin(db):
    return 10.0 ** (db / 20.0)


def lin_to_db(v):
    return 20.0 * math.log10(v) if v > 1e-12 else float("-inf")


def rms(samples):
    if not samples:
        return 0.0
    acc = 0.0
    for v in samples:
        acc += v * v
    return math.sqrt(acc / len(samples))


def peak(samples):
    p = 0.0
    for v in samples:
        a = abs(v)
        if a > p:
            p = a
    return p


def seed_for(name):
    key = "EVAL-003|%s|%s" % (GENERATOR_VERSION, name)
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()
    return int(digest[:16], 16)


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
# Closed tag vocabulary (derived-only; the base vocabulary is untouched)
# ---------------------------------------------------------------------------
#
# The first tag of every derived fixture is the perturbation kind. The remaining
# tags come from DERIVED_QUALIFIERS. The base corpus's `tagVocabulary.core`
# membership is deliberately NOT spread to derived files: a derived clip is a
# probe *of* a core fixture, not itself one of the fixtures SPEC 19's ">= 95% of
# core fixtures" denominator counts. Consumers group derived clips by
# `parentFixture`.

DERIVED_PERTURBATIONS = (
    "baseline",
    "noise",
    "level",
    "clipping",
    "onset_offset",
    "leading_silence",
    "trailing_silence",
    "silence_gap",
    "drop_onset",
    "syncopation_burst",
    "tempo_step",
)

DERIVED_QUALIFIERS = (
    "derived",
    "amplitude_only",
    "evidence_edit",
    "global_delay",
    "silence_pad",
    "time_warp",
    "paired_baseline",
    "core_parent",
)

# ---------------------------------------------------------------------------
# Perturbation catalogue. This is the single source of truth for what runs and
# how each result is labelled; the manifest `perturbations` block is generated
# from it so documentation cannot drift from behaviour.
# ---------------------------------------------------------------------------

# parent fixture name -> (window start, window seconds)
PARENTS = (
    ("clean_eighths", 0.0, 5.0),
    ("syncopated_funk", 0.0, 5.0),
)

# (kind, parameter-value) grids. Parameters are documented per kind below.
GRID = {
    "clean_eighths": (
        ("baseline", None),
        ("noise", 20.0), ("noise", 10.0), ("noise", 0.0),
        ("level", -20.0), ("level", -40.0), ("level", -60.0),
        ("clipping", 0.5), ("clipping", 0.25), ("clipping", 0.125),
        ("onset_offset", 40.0), ("onset_offset", 80.0),
        ("leading_silence", 0.5),
        ("trailing_silence", 0.5),
        ("silence_gap", 1.0),
        ("drop_onset", 2), ("drop_onset", 4),
        ("syncopation_burst", 1), ("syncopation_burst", 4),
        ("tempo_step", 1.25), ("tempo_step", 0.85),
    ),
    "syncopated_funk": (
        ("baseline", None),
        ("noise", 10.0), ("noise", 0.0),
    ),
}

#: Tempo-step anchor: the step lands on this beat index of the windowed grid.
TEMPO_STEP_BEAT_INDEX = 4

#: Onset-offset perturbation: delay-and-drop-tail, milliseconds.
#: Silence-pad perturbations: seconds of digital silence.
#: Drop-onset replacement window: [-pre, +post] seconds around the onset.
#: Syncopation burst: donor segment length and mix gain.
#: Silence gap: [start, end] seconds, with a raised-cosine edge taper.
DROP_PRE_SECONDS = 0.005
DROP_POST_SECONDS = 0.060
BURST_DONOR_SECONDS = 0.12
BURST_GAIN = 0.55
GAP_EDGE_TAPER_SECONDS = 0.005
BURST_INJECTED_SCALE = 1.0  # injected at full donor amplitude * BURST_GAIN


def perturbation_docs():
    return {
        "baseline": {
            "description": "Unperturbed parent window. The paired reference for "
                           "every other derived clip from the same parent.",
            "parameters": {"windowSeconds": "see truncation"},
            "truthTransform": "inherit",
        },
        "noise": {
            "description": "Additive white Gaussian noise at a target SNR relative "
                           "to the window RMS. Reveals where the onset detector "
                           "stops working, not whether it survives a single level.",
            "parameters": {"snrDb": "signal RMS / noise RMS in dB, [20, 10, 0]"},
            "truthTransform": "inherit",
            "note": "Noise raises the floor inside regions the parent declares "
                    "true-silent; trueSilenceSpans is inherited structurally and "
                    "must not be used to score silence on this clip.",
        },
        "level": {
            "description": "Whole-window gain, no added noise. Reveals an absolute "
                           "level floor (a backend that needs a minimum amplitude).",
            "parameters": {"gainDb": "[-20, -40, -60]"},
            "truthTransform": "inherit",
        },
        "clipping": {
            "description": "Drive the window to full scale then hard-clip at a "
                           "fraction of full scale. Reveals whether distorted "
                           "transients destroy the tracker.",
            "parameters": {"thresholdFraction": "clip ceiling as a fraction of "
                                                "full scale, [0.5, 0.25, 0.125]"},
            "truthTransform": "inherit",
        },
        "onset_offset": {
            "description": "Delay every sample by a fixed number of ms and drop "
                           "the tail so the clip length is preserved. A real audio "
                           "delay, so event times move with it.",
            "parameters": {"offsetMs": "[40, 80]"},
            "truthTransform": "shift",
        },
        "leading_silence": {
            "description": "Prepend digital silence. Tests acquisition when the "
                           "performance starts late.",
            "parameters": {"silenceSeconds": "[0.5]"},
            "truthTransform": "shift",
        },
        "trailing_silence": {
            "description": "Append digital silence. Tests that the tracker does "
                           "not invent beats after the playing stops.",
            "parameters": {"silenceSeconds": "[0.5]"},
            "truthTransform": "append-silence",
        },
        "silence_gap": {
            "description": "Carve a raised-cosine-tapered silent span out of the "
                           "middle. The metric grid is unbroken (a stop, not a "
                           "tempo change); onsets inside the span are removed. "
                           "Maps to SPEC 19 'silence does not create false "
                           "acceleration'.",
            "parameters": {"gapSeconds": "[1.0]"},
            "truthTransform": "edit-onsets",
        },
        "drop_onset": {
            "description": "Remove the attack of every K-th onset by interpolating "
                           "across a short window around it. Reveals whether the "
                           "tracker holds phase when evidence is missing.",
            "parameters": {"everyKth": "[2, 4]", "windowSeconds": 0.065},
            "truthTransform": "edit-onsets",
            "note": "The window is shorter than one beat, never reaches the next "
                    "onset, and leaves the tails of earlier notes in place, so no "
                    "whole sustained note is masked. The local noise floor inside "
                    "the window is interpolated (smoothed) -- a bounded, "
                    "documented change.",
        },
        "syncopation_burst": {
            "description": "Overlay a real onset copied from the parent at an "
                           "offbeat position, isolated (1) or across two bars (4). "
                           "Maps to SPEC 19 'no tempo jump from one isolated "
                           "syncopated event'.",
            "parameters": {"burstCount": "[1, 4]", "gain": 0.55},
            "truthTransform": "edit-onsets",
        },
        "tempo_step": {
            "description": "Coherent tape-speed time warp applied to BOTH audio "
                           "and event times at a known bar: phi(t) = t for t <= Ts, "
                           "Ts + (t - Ts)/r after. r>1 speeds up, r<1 slows down. "
                           "The whole source window is consumed and no event is "
                           "dropped or padded; the output duration is the natural "
                           "phi(window) and therefore differs from the baseline "
                           "(shorter when r>1, longer when r<1).",
            "parameters": {"ratio": "[1.25, 0.85]",
                           "stepBeatIndex": TEMPO_STEP_BEAT_INDEX},
            "truthTransform": "warp",
            "note": "Tape-speed resampling raises pitch as it raises tempo. The "
                    "measured quantity is onset timing, not pitch; this is "
                    "documented rather than pitch-corrected.",
        },
    }


# ---------------------------------------------------------------------------
# WAV I/O
# ---------------------------------------------------------------------------

def read_parent_fixture(base_dir, filename, expected_name):
    path = os.path.join(base_dir, filename)
    with wave.open(path, "rb") as w:
        if w.getnchannels() != CHANNELS or w.getsampwidth() != SAMPLE_WIDTH \
                or w.getframerate() != SAMPLE_RATE:
            raise SystemExit("%s: unexpected WAV format for %s"
                             % (path, expected_name))
        frames = w.getnframes()
        raw = w.readframes(frames)
    return path, pcm16_to_float(raw), frames


def write_wav(path, samples):
    raw = float_to_pcm16(samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(SAMPLE_WIDTH)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(raw)
    return len(raw)


# ---------------------------------------------------------------------------
# Truth windowing and transforms
# ---------------------------------------------------------------------------

#: The corpus convention: a beat is "silent" when no onset lies within this
#: many seconds of it.
ONSET_WINDOW_SECONDS = 0.030


def window_events(times, start, end):
    return [t - start for t in times if (start - 1e-9) <= t < (end - 1e-9)]


def silent_beats_and_spans(beats, onsets):
    silent = []
    for i, t in enumerate(beats):
        if not any(abs(o - t) <= ONSET_WINDOW_SECONDS for o in onsets):
            silent.append(i)
    spans = []
    for i in silent:
        lo = beats[i] - ONSET_WINDOW_SECONDS
        hi = beats[i] + ONSET_WINDOW_SECONDS
        if spans and lo <= spans[-1][1]:
            spans[-1][1] = max(spans[-1][1], hi)
        else:
            spans.append([lo, hi])
    return silent, spans


def coalesce_spans(spans):
    out = []
    for s in sorted((list(s) for s in spans), key=lambda x: x[0]):
        if out and s[0] <= out[-1][1] + 1e-9:
            out[-1][1] = max(out[-1][1], s[1])
        else:
            out.append([s[0], s[1]])
    return out


def transform_true_spans(spans, kind, transform, duration):
    """Carry structural true-silence regions through the same timeline change
    the audio received, so `trueSilenceSpans` names where the silence actually
    is in the derived clip."""
    if kind in ("onset_offset", "leading_silence"):
        if kind == "onset_offset":
            off = transform["offsetSeconds"]
        else:
            off = transform["silenceSeconds"]
        moved = [[a + off, b + off] for a, b in spans]
        if kind == "leading_silence":
            moved.append([0.0, transform["silenceSeconds"]])
        return coalesce_spans(moved)
    if kind == "trailing_silence":
        return coalesce_spans(spans
                              + [[duration - transform["silenceSeconds"],
                                  duration]])
    if kind == "tempo_step":
        Ts = transform["stepTimeSeconds"]
        r = transform["ratio"]

        def phi(t):
            return t if t <= Ts else Ts + (t - Ts) / r

        return coalesce_spans([[phi(a), phi(b)] for a, b in spans])
    return spans


def intersect_spans(spans, start, end):
    out = []
    for s in spans:
        a = max(float(s[0]), start)
        b = min(float(s[1]), end)
        if b > a + 1e-9:
            out.append([a - start, b - start])
    return out


def resample_linear(samples, out_count, source_positions):
    """out[i] = samples[source_positions[i]] with linear interpolation."""
    n = len(samples)
    out = []
    for pos in source_positions:
        if pos <= 0.0:
            out.append(samples[0] if n else 0.0)
            continue
        if pos >= n - 1:
            out.append(samples[n - 1] if n else 0.0)
            continue
        i0 = int(math.floor(pos))
        frac = pos - i0
        out.append(samples[i0] * (1.0 - frac) + samples[i0 + 1] * frac)
    return out


# ---------------------------------------------------------------------------
# Perturbation implementations
# ---------------------------------------------------------------------------
#
# Each returns a dict:
#   audio      list[float]                 the transformed window
#   beats      list[float]                 transformed metric grid (clip-relative)
#   onsets     list[float]                 transformed as-played attacks
#   truth_transform, timing_affects_truth, transform{...}, extra_* fields

def p_baseline(ctx):
    return dict(audio=ctx["base"], beats=list(ctx["beats"]),
                onsets=list(ctx["onsets"]),
                truth_transform="inherit", timing_affects_truth=False,
                transform={"kind": "baseline"})


def p_noise(ctx, snr_db):
    base = ctx["base"]
    target_rms = rms(base) / (10.0 ** (snr_db / 20.0))
    rng = random.Random(ctx["seed"])
    raw = [rng.gauss(0.0, 1.0) for _ in range(len(base))]
    cur = rms(raw)
    scale = (target_rms / cur) if cur > 1e-12 else 0.0
    audio = [x + scale * n for x, n in zip(base, raw)]
    return dict(audio=audio, beats=list(ctx["beats"]), onsets=list(ctx["onsets"]),
                truth_transform="inherit", timing_affects_truth=False,
                transform={"kind": "noise", "snrDb": snr_db,
                           "targetNoiseRms": target_rms},
                extra={"declaredSnrDb": snr_db})


def p_level(ctx, gain_db):
    g = db_to_lin(gain_db)
    audio = [x * g for x in ctx["base"]]
    return dict(audio=audio, beats=list(ctx["beats"]), onsets=list(ctx["onsets"]),
                truth_transform="inherit", timing_affects_truth=False,
                transform={"kind": "level", "gainDb": gain_db},
                extra={"declaredGainDb": gain_db})


def p_clipping(ctx, threshold):
    base = ctx["base"]
    p = peak(base)
    drive = (1.0 / p) if p > 1e-12 else 0.0
    audio = []
    for x in base:
        v = x * drive
        if v > threshold:
            v = threshold
        elif v < -threshold:
            v = -threshold
        audio.append(v)
    return dict(audio=audio, beats=list(ctx["beats"]), onsets=list(ctx["onsets"]),
                truth_transform="inherit", timing_affects_truth=False,
                transform={"kind": "clipping", "thresholdFraction": threshold,
                           "driveGain": drive},
                extra={"declaredThreshold": threshold})


def p_onset_offset(ctx, offset_ms):
    n = int(round(offset_ms / 1000.0 * SAMPLE_RATE))
    base = ctx["base"]
    audio = [0.0] * n + base[:len(base) - n]
    shift = n / float(SAMPLE_RATE)
    beats = [b + shift for b in ctx["beats"]]
    onsets = [o + shift for o in ctx["onsets"]]
    return dict(audio=audio, beats=beats, onsets=onsets,
                truth_transform="shift", timing_affects_truth=True,
                transform={"kind": "onset_offset", "offsetMs": offset_ms,
                           "offsetSeconds": shift, "offsetSamples": n})


def p_leading_silence(ctx, seconds):
    n = int(round(seconds * SAMPLE_RATE))
    audio = [0.0] * n + list(ctx["base"])
    beats = [b + seconds for b in ctx["beats"]]
    onsets = [o + seconds for o in ctx["onsets"]]
    return dict(audio=audio, beats=beats, onsets=onsets,
                truth_transform="shift", timing_affects_truth=True,
                transform={"kind": "leading_silence", "silenceSeconds": seconds,
                           "silenceSamples": n})


def p_trailing_silence(ctx, seconds):
    n = int(round(seconds * SAMPLE_RATE))
    audio = list(ctx["base"]) + [0.0] * n
    return dict(audio=audio, beats=list(ctx["beats"]), onsets=list(ctx["onsets"]),
                truth_transform="append-silence", timing_affects_truth=False,
                transform={"kind": "trailing_silence",
                           "silenceSeconds": seconds, "silenceSamples": n,
                           "durationDeltaSeconds": seconds})


def p_silence_gap(ctx, gap_seconds):
    base = list(ctx["base"])
    n = len(base)
    # Centre the gap so it lands on complete beats of the windowed grid rather
    # than clipping the acquisition region. Start at the first beat >= 40% in.
    gap_start = None
    for b in ctx["beats"]:
        if b >= 0.4 * (n / float(SAMPLE_RATE)):
            gap_start = b
            break
    if gap_start is None:
        gap_start = 0.4 * (n / float(SAMPLE_RATE))
    gap_end = min(gap_start + gap_seconds, (n / float(SAMPLE_RATE)) - 0.05)
    a = int(round(gap_start * SAMPLE_RATE))
    z = int(round(gap_end * SAMPLE_RATE))
    taper = int(round(GAP_EDGE_TAPER_SECONDS * SAMPLE_RATE))
    for i in range(a, min(z, n)):
        pos = i - a
        remaining = z - i
        # Muted in the middle, raised-cosine fade to and from full scale at the
        # two edges so carving the gap does not itself inject a click.
        g = 0.0
        if taper > 0 and pos < taper:
            g = 0.5 * (1.0 + math.cos(math.pi * pos / float(taper)))
        if taper > 0 and remaining < taper:
            g = max(g, 0.5 * (1.0 + math.cos(
                math.pi * remaining / float(taper))))
        base[i] *= g
    muted = (a / float(SAMPLE_RATE), min(z, n) / float(SAMPLE_RATE))
    beats = list(ctx["beats"])
    onsets = [o for o in ctx["onsets"] if not (muted[0] < o < muted[1])]
    return dict(audio=base, beats=beats, onsets=onsets,
                truth_transform="edit-onsets", timing_affects_truth=True,
                transform={"kind": "silence_gap", "gapSeconds": gap_seconds,
                           "gapSpanSeconds": [muted[0], muted[1]]},
                extra={"mutedSpanSeconds": [muted[0], muted[1]]})


def p_drop_onset(ctx, every_k):
    base = list(ctx["base"])
    n = len(base)
    order = sorted(range(len(ctx["onsets"])), key=lambda i: ctx["onsets"][i])
    dropped_idx = [i for i in range(len(order)) if (i + 1) % every_k == 0]
    dropped_times = [ctx["onsets"][order[i]] for i in dropped_idx]
    pre = int(round(DROP_PRE_SECONDS * SAMPLE_RATE))
    post = int(round(DROP_POST_SECONDS * SAMPLE_RATE))
    replaced = []
    for t in dropped_times:
        a = int(round(t * SAMPLE_RATE)) - pre
        z = int(round(t * SAMPLE_RATE)) + post
        if a < 0:
            a = 0
        if z > n - 1:
            z = n - 1
        if z - a < 2:
            continue
        # Linear interpolation across the gap: endpoints preserved, no new
        # transient, no injected zeros. Any underlying sustain is carried across.
        v0 = base[a]
        v1 = base[z]
        span = z - a
        for i in range(a + 1, z):
            u = (i - a) / float(span)
            base[i] = v0 * (1.0 - u) + v1 * u
        replaced.append(t)
    onsets = [o for o in ctx["onsets"]
              if not any(abs(o - t) <= 1e-9 for t in replaced)]
    return dict(audio=base, beats=list(ctx["beats"]), onsets=onsets,
                truth_transform="edit-onsets", timing_affects_truth=True,
                transform={"kind": "drop_onset", "everyKth": every_k,
                           "windowSeconds": DROP_PRE_SECONDS + DROP_POST_SECONDS,
                           "droppedOnsets": replaced},
                extra={"droppedCount": len(replaced)})


def p_syncopation_burst(ctx, count):
    base = list(ctx["base"])
    n = len(base)
    beats = ctx["beats"]
    beat_period = (beats[1] - beats[0]) if len(beats) > 1 else 0.5
    # Donor: the first complete onset attack in the window.
    donor_time = ctx["onsets"][0] if ctx["onsets"] else beats[0]
    donor_n = int(round(BURST_DONOR_SECONDS * SAMPLE_RATE))
    d0 = int(round(donor_time * SAMPLE_RATE))
    d1 = min(d0 + donor_n, n)
    donor = base[d0:d1]

    # Offbeat positions = halfway between consecutive beats, in the middle
    # region so both the acquisition and the tail stay clean.
    candidates = []
    for i in range(2, len(beats) - 2):
        candidates.append(beats[i] + 0.5 * beat_period)
    if count == 1:
        chosen = [candidates[len(candidates) // 2]] if candidates else []
    else:
        chosen = candidates[:count]
    injected = []
    for t in chosen:
        a = int(round(t * SAMPLE_RATE))
        if a + len(donor) > n:
            continue
        for k, v in enumerate(donor):
            base[a + k] += BURST_GAIN * v
        injected.append(t)
    onsets = sorted(ctx["onsets"] + injected)
    return dict(audio=base, beats=list(ctx["beats"]), onsets=onsets,
                truth_transform="edit-onsets", timing_affects_truth=True,
                transform={"kind": "syncopation_burst", "burstCount": count,
                           "gain": BURST_GAIN, "injectedOnsets": injected},
                extra={"injectedCount": len(injected)})


def p_tempo_step(ctx, ratio):
    base = ctx["base"]
    D = len(base) / float(SAMPLE_RATE)
    beats = ctx["beats"]
    idx = min(TEMPO_STEP_BEAT_INDEX, len(beats) - 1)
    Ts = beats[idx] if beats else 0.35
    if ratio <= 0.0:
        raise SystemExit("tempo_step ratio must be positive")

    def phi(t):
        return t if t <= Ts else Ts + (t - Ts) / ratio

    # Consume the whole source window and emit its image phi([0, D]): no event is
    # dropped, nothing is padded, and the output is the natural warped length.
    D_out = phi(D)
    out_count = int(round(D_out * SAMPLE_RATE))

    def phi_inv(u):
        return u if u <= Ts else Ts + (u - Ts) * ratio

    source_positions = [phi_inv(i / float(SAMPLE_RATE)) * SAMPLE_RATE
                        for i in range(out_count)]
    audio = resample_linear(base, out_count, source_positions)

    warped_beats = [b for b in (phi(t) for t in beats) if b < D_out - 1e-9]
    warped_onsets = [o for o in (phi(t) for t in ctx["onsets"])
                     if o < D_out - 1e-9]
    return dict(audio=audio, beats=warped_beats, onsets=warped_onsets,
                truth_transform="warp", timing_affects_truth=True,
                transform={"kind": "tempo_step", "ratio": ratio,
                           "stepBeatIndex": idx, "stepTimeSeconds": Ts,
                           "sourceSeconds": D, "outputSeconds": D_out},
                extra={"warpRatio": ratio, "warpAnchorSeconds": Ts})


def apply_perturbation(ctx, kind, param):
    if kind == "baseline":
        return p_baseline(ctx)
    if kind == "noise":
        return p_noise(ctx, param)
    if kind == "level":
        return p_level(ctx, param)
    if kind == "clipping":
        return p_clipping(ctx, param)
    if kind == "onset_offset":
        return p_onset_offset(ctx, param)
    if kind == "leading_silence":
        return p_leading_silence(ctx, param)
    if kind == "trailing_silence":
        return p_trailing_silence(ctx, param)
    if kind == "silence_gap":
        return p_silence_gap(ctx, param)
    if kind == "drop_onset":
        return p_drop_onset(ctx, param)
    if kind == "syncopation_burst":
        return p_syncopation_burst(ctx, param)
    if kind == "tempo_step":
        return p_tempo_step(ctx, param)
    raise SystemExit("unknown perturbation kind: %s" % kind)


# ---------------------------------------------------------------------------
# Naming and measured statistics
# ---------------------------------------------------------------------------

def derived_name(parent, kind, param):
    if kind == "baseline":
        return "%s__baseline" % parent
    if kind == "noise":
        return "%s__noise_snr%gdb" % (parent, param)
    if kind == "level":
        return "%s__level_%gdb" % (parent, param)
    if kind == "clipping":
        return "%s__clip_%g" % (parent, param)
    if kind == "onset_offset":
        return "%s__offset_%gms" % (parent, param)
    if kind == "leading_silence":
        return "%s__lead_silence_%gs" % (parent, param)
    if kind == "trailing_silence":
        return "%s__trail_silence_%gs" % (parent, param)
    if kind == "silence_gap":
        return "%s__silence_gap_%gs" % (parent, param)
    if kind == "drop_onset":
        return "%s__drop_every%d" % (parent, param)
    if kind == "syncopation_burst":
        return "%s__syncop_burst%d" % (parent, param)
    if kind == "tempo_step":
        return "%s__tempo_step_%g" % (parent, param)
    raise SystemExit("cannot name perturbation kind: %s" % kind)


def scenario_tags(kind, parent_is_core):
    tags = [kind, "derived"]
    if kind in ("noise", "level", "clipping"):
        tags.append("amplitude_only")
    elif kind in ("drop_onset", "silence_gap", "syncopation_burst"):
        tags.append("evidence_edit")
    elif kind == "onset_offset":
        tags.append("global_delay")
    elif kind in ("leading_silence", "trailing_silence"):
        tags.append("silence_pad")
    elif kind == "tempo_step":
        tags.append("time_warp")
    if kind == "baseline":
        tags.append("paired_baseline")
    if parent_is_core:
        tags.append("core_parent")
    return tags


def measure_signal(samples):
    p = peak(samples)
    r = rms(samples)
    dc = sum(samples) / len(samples) if samples else 0.0
    return {
        "peakDbfs": round(lin_to_db(p), 6) if p > 0 else None,
        "rmsDbfs": round(lin_to_db(r), 6) if r > 0 else None,
        "dcOffset": round(dc, 9),
        "frames": len(samples),
    }


def clipping_fraction_int16(samples, threshold):
    ceiling = int(math.floor(threshold * 32767.0 + 0.5))
    count = 0
    for v in samples:
        q = int(math.floor(v * 32767.0 + 0.5))
        if abs(abs(q) - ceiling) <= 1:
            count += 1
    return count / float(len(samples)) if samples else 0.0


def measured_snr_db(samples, baseline):
    sig = rms(baseline)
    noise = rms([a - b for a, b in zip(samples, baseline)])
    if noise <= 1e-12:
        return None
    return 20.0 * math.log10(sig / noise)


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------

def meter_of(fx):
    return {
        "numerator": fx["meter"]["numerator"],
        "denominator": fx["meter"]["denominator"],
        "beatUnit": fx["meter"].get("beatUnit", "quarter"),
        "beatsPerBar": fx["meter"]["beatsPerBar"],
    }


def canonical_json(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True) + "\n"


def build(base_dir, out_dir, window_override=None, verbose=True):
    base_manifest_path = os.path.join(base_dir, "manifest.json")
    with open(base_manifest_path, "r") as fh:
        base_manifest = json.load(fh)
    fixtures_by_name = {f["name"]: f for f in base_manifest["fixtures"]}
    base_manifest_sha = sha256_of(base_manifest_path)

    os.makedirs(out_dir, exist_ok=True)

    entries = []
    for parent_name, w_start, w_seconds in PARENTS:
        if window_override is not None:
            w_seconds = window_override
        if parent_name not in fixtures_by_name:
            raise SystemExit("base manifest has no fixture %r" % parent_name)
        parent = fixtures_by_name[parent_name]
        _, pcm, parent_frames = read_parent_fixture(
            base_dir, parent["file"], parent_name)

        w_end = w_start + w_seconds
        start_frame = int(round(w_start * SAMPLE_RATE))
        end_frame = min(int(round(w_end * SAMPLE_RATE)), parent_frames)
        base_window = pcm[start_frame:end_frame]

        parent_beats = window_events(parent["beats"], w_start, w_end)
        parent_onsets = window_events(parent["onsets"], w_start, w_end)
        if not parent_beats:
            raise SystemExit("%s: window contains no beats" % parent_name)

        base_truth_spans = intersect_spans(
            parent.get("trueSilenceSpans", []), w_start, w_end)

        ctx = dict(base=base_window, beats=parent_beats, onsets=parent_onsets,
                   seed=0, parent=parent_name, window=(w_start, w_end))

        for kind, param in GRID.get(parent_name, ()):
            name = derived_name(parent_name, kind, param)
            ctx["seed"] = seed_for(name)
            result = apply_perturbation(ctx, kind, param)

            out_path = os.path.join(out_dir, name + ".wav")
            write_wav(out_path, result["audio"])
            # `bytes` is the committed file size including the 44-byte RIFF
            # header, matching the base corpus's accounting, not the payload.
            nbytes = os.path.getsize(out_path)
            sha = sha256_of(out_path)
            samples = result["audio"]
            beats = [round(b, 9) for b in result["beats"]]
            onsets = [round(o, 9) for o in result["onsets"]]
            silent, silence_spans = silent_beats_and_spans(beats, onsets)

            duration = len(samples) / float(SAMPLE_RATE)
            stats = measure_signal(samples)
            stats["clippedSampleFraction"] = round(
                clipping_fraction_int16(samples, result["transform"].get(
                    "thresholdFraction", 1.0)), 9)
            if kind == "noise":
                stats["measuredSnrDb"] = round(
                    measured_snr_db(samples, base_window), 6)

            true_spans = transform_true_spans(
                [list(s) for s in base_truth_spans], kind, result["transform"],
                duration)
            if kind == "silence_gap":
                gap = result["transform"]["gapSpanSeconds"]
                true_spans = coalesce_spans(true_spans + [[gap[0], gap[1]]])

            entry = {
                "name": name,
                "file": name + ".wav",
                "sha256": sha,
                "bytes": nbytes,
                "sampleRate": SAMPLE_RATE,
                "channels": CHANNELS,
                "bitDepth": BIT_DEPTH,
                "durationSeconds": round(duration, 6),
                "meter": meter_of(parent),
                "subdivision": parent.get("subdivision", {"perBeat": 1}),
                "beats": beats,
                "onsets": onsets,
                "signal": stats,
                "downbeats": list(range(0, len(beats),
                                        parent["meter"]["beatsPerBar"])),
                "silentBeats": silent,
                "silenceSpans": [[round(a, 6), round(b, 6)]
                                 for a, b in silence_spans],
                "trueSilenceSpans": [[round(a, 6), round(b, 6)]
                                     for a, b in true_spans],
                "scenarioTags": scenario_tags(kind, "core" in parent["scenarioTags"]),
                "notes": perturbation_docs()[kind]["description"],
                "license": "CC0-1.0",
                "provenance": (
                    "Derived by %s v%s (Python standard library only) from the "
                    "committed base fixture %s (sha256 %s). No third-party, "
                    "commercial or otherwise copyrighted audio was used."
                    % (GENERATOR_NAME, GENERATOR_VERSION, parent_name,
                       parent["sha256"])),
                "parentFixture": parent_name,
                "parentFile": parent["file"],
                "parentSha256": parent["sha256"],
                "pairedBaseline": derived_name(parent_name, "baseline", None),
                "truncation": {
                    "sourceStartSeconds": round(w_start, 6),
                    "sourceEndSeconds": round(w_end, 6),
                    "sourceStartFrame": start_frame,
                    "sourceEndFrame": end_frame,
                    "frames": end_frame - start_frame,
                },
                "groundTruth": {
                    "policy": result["truth_transform"],
                    "timingAffectsTruth": result["timing_affects_truth"],
                },
                "transformation": result["transform"],
            }
            for key in ("declaredSnrDb", "declaredGainDb", "declaredThreshold",
                        "warpRatio", "warpAnchorSeconds", "mutedSpanSeconds",
                        "injectedCount", "droppedCount"):
                if key in result.get("extra", {}):
                    entry[key] = result["extra"][key]

            if kind == "tempo_step":
                entry["tempoProfile"] = "tempo-step"
                entry["bpmStart"] = parent.get("nominalBpm")
                entry["bpmEnd"] = round(parent.get("nominalBpm", 0.0)
                                        * param, 6)
                entry["rampStartSeconds"] = result["transform"]["stepTimeSeconds"]
                entry["rampEndSeconds"] = duration
            else:
                entry["tempoProfile"] = parent.get("tempoProfile", "constant")
                if "nominalBpm" in parent:
                    entry["nominalBpm"] = parent["nominalBpm"]
                if "bpmStart" in parent:
                    entry["bpmStart"] = parent["bpmStart"]
                    entry["bpmEnd"] = parent["bpmEnd"]
                    entry["rampStartSeconds"] = parent["rampStartSeconds"]
                    entry["rampEndSeconds"] = parent["rampEndSeconds"]

            entries.append(entry)
            if verbose:
                print("  %-42s %8d B  beats=%2d onsets=%2d %s"
                      % (name, nbytes, len(beats), len(onsets),
                         result["truth_transform"]))

    total_bytes = sum(e["bytes"] for e in entries)
    total_duration = sum(e["durationSeconds"] for e in entries)

    manifest = {
        "schemaVersion": 1,
        "corpus": {
            "id": CORPUS_ID,
            "description": (
                "Derived robustness perturbations of the EVAL-001 guitar corpus. "
                "Same audio as the parent base fixture, one controlled change at "
                "a time, for degradation curves rather than pass/fail."),
            "baseCorpusId": base_manifest.get("corpus", {}).get("id", ""),
            "baseManifestSha256": base_manifest_sha,
            "profile": "derived",
        },
        "conventions": {
            "beatToleranceSeconds": base_manifest.get("conventions", {}).get(
                "beatToleranceSeconds", 0.07),
            "beats": (
                "Metric grid, clip-relative. Amplitude-only perturbations inherit "
                "the parent window's grid exactly; timing perturbations transform "
                "it as recorded in `groundTruth`."),
            "onsets": "As-played perceptual attacks, clip-relative.",
            "silentBeats": "Indices into `beats` with no onset within +/-30 ms.",
            "silenceSpans": (
                "Coalesced +/-30 ms windows around silentBeats. NOT a description "
                "of silence."),
            "trueSilenceSpans": (
                "Structural near-silent regions, inherited from the parent and "
                "adjusted only for carved silence. NOT re-measured against added "
                "noise; see the `noise` perturbation note."),
            "derivedSemantics": (
                "See the `perturbations` block. `groundTruth.policy` is one of "
                "inherit | edit-onsets | shift | append-silence | warp; "
                "`groundTruth.timingAffectsTruth` is true exactly when the "
                "audio timeline moved under the score."),
            "windowSeconds": PARENTS[0][2],
        },
        "tagVocabulary": {
            "perturbation": list(DERIVED_PERTURBATIONS),
            "qualifier": list(DERIVED_QUALIFIERS),
        },
        "perturbations": perturbation_docs(),
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
            "language": "python3",
            "dependencies": ("standard library only (wave, struct, math, random, "
                             "json, hashlib, argparse)"),
            "seedPolicy": ("seed = int(sha256('EVAL-003|<version>|<derivedName>')"
                           ".hexdigest()[:16], 16)"),
            "command": ("python3 tools/rhythm-eval/tools/make_derived.py "
                        "--out testdata/rhythm/derived"),
        },
        "totalBytes": total_bytes,
        "totalDurationSeconds": round(total_duration, 6),
        "fixtures": entries,
    }

    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w") as fh:
        fh.write(canonical_json(manifest))

    if verbose:
        print("derived: %d clips, %d bytes (%.2f MB), %.2f s"
              % (len(entries), total_bytes, total_bytes / (1024.0 * 1024.0),
                 total_duration))
    return manifest, manifest_path


def check(base_dir, out_dir, verbose=True):
    """Regenerate into a scratch directory and compare against the committed
    manifest. Byte determinism is the assertion, not a claim."""
    import tempfile
    committed_path = os.path.join(out_dir, "manifest.json")
    if not os.path.exists(committed_path):
        raise SystemExit("no committed manifest at %s" % committed_path)
    with open(committed_path, "r") as fh:
        committed = json.load(fh)

    with tempfile.TemporaryDirectory(prefix="ev003-check-") as tmp:
        manifest, _ = build(base_dir, tmp, verbose=False)
        if canonical_json(manifest) != canonical_json(committed):
            # Report the first differing fixture, not just "not equal".
            a = {f["name"]: f["sha256"] for f in committed["fixtures"]}
            b = {f["name"]: f["sha256"] for f in manifest["fixtures"]}
            for name in sorted(set(a) | set(b)):
                if a.get(name) != b.get(name):
                    print("MISMATCH %s: committed=%s regenerated=%s"
                          % (name, a.get(name), b.get(name)))
            raise SystemExit("derived manifest is not reproducible")
    if verbose:
        print("check OK: derived set is byte-reproducible")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", default=os.path.join(REPO_ROOT, "testdata/rhythm"),
                    help="base corpus directory (read-only)")
    ap.add_argument("--out",
                    default=os.path.join(REPO_ROOT, "testdata/rhythm/derived"),
                    help="output directory for derived WAVs and manifest")
    ap.add_argument("--window", type=float, default=None,
                    help="override the derived window length in seconds "
                         "(default: per-parent, 5.0 s)")
    ap.add_argument("--check", action="store_true",
                    help="regenerate into a scratch dir and compare to the "
                         "committed manifest; nonzero exit on any difference")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    if args.check:
        return check(args.base, args.out, verbose=not args.quiet)
    build(args.base, args.out, window_override=args.window,
          verbose=not args.quiet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
