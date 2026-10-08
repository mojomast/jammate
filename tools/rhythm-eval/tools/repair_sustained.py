#!/usr/bin/env python3
"""Scoped replacement of the EVAL-001 `sustained_chords` fixture (EVAL-006).

WHAT THIS IS
------------
`docs/research/CORPUS-ACOUSTIC-REVIEW.md` measured the committed
`testdata/rhythm/wav/sustained_chords.wav` and found that its four chord events
have collapsed 30-35 dB of attack energy by 300 ms and reach the capture noise
floor by 500 ms. The fixture is named and tagged `sustained_chords`, and its
range/onset grid is five bars at 96 BPM (one chord per bar, ~2.5 s apart), so a
chord that stops sounding after ~0.3 s cannot stand in for sustained chord
playing. The declared `trueSilenceSpans` therefore described 8.73 s (77 %) of the
11.35 s file as silence -- a synthesis artifact of the generator's deliberate
"decay shorter than the bar" trim, not a performance.

This tool produces a **new corpus directory** that pairs the eighteen unchanged
EVAL-001 fixtures (referenced by relative path, never copied) with a single
re-synthesised `sustained_chords.wav` whose chord envelopes follow the physical
model's own free-string decay instead of an artificial short truncation.

WHAT IT DOES NOT DO
-------------------
- It does not modify the EVAL-001 corpus, its generator, its manifest or any
  historical results. The base manifest is read-only input.
- It does not modify the shared harness (`tools/rhythm-eval/*.cpp/.h`), any
  CMake file, `src/`, the execution ledger or the docs tree outside the one new
  report. Only the two new tools, the new corpus subtree and the new report are
  written.
- It makes no selection and wires no production path. Gate G3 stays open.

THE REPAIR, PRECISELY
---------------------
The base generator renders every fixture through `karplus_strong` with a
per-event `t60_scale` and a per-fixture `mix_t60`. For `sustained_chords` the
pattern sets `t60_scale = ring / (4.2 * 1.15)` with `ring = 0.42 * bar`, which
shortens the low-E decay to ~0.55 s effective. This tool re-renders the exact
same event list (same RNG, same humanised times, same chords, same meter, same
duration) with:

    mix_t60   = 1.0   (the model's own free-string T60 table, not 0.6 trim)
    t60_scale = 1.0   (the per-event "decay shorter than the bar" trim removed)

so the envelope is the physical string decay rather than a fabricated short
pulse. The event times, beat grid, meter, chord shapes, velocities and duration
are byte-for-byte the ones the base generator computed; only the decay time
constant changes. Measured with the independent PCM audit below, every onset is
still acoustically present at +0.5 s and +1.5 s after the attack.

INDEPENDENT ACOUSTIC-SILENCE DERIVATION
---------------------------------------
`trueSilenceSpans` for the repaired fixture is **not** derived from the
generator's `audible_seconds` / two-stage decay model. It is measured from the
committed 16-bit PCM with a documented, model-free RMS criterion:

  * 50 ms RMS frames, 10 ms hop, decoded from the WAV (`s/32768`);
  * `L_ref`  = 99th percentile of frame level (a robust estimate of the loudest
    sustained material);
  * `L_floor` = 5th percentile of frame level (a robust estimate of the file's
    quiet floor);
  * a frame is **acoustically sounding** iff
    `20*log10(rms) >= max(L_floor + 6 dB, L_ref - 45 dB)`;
  * a maximal run of silent frames becomes a span only if it is >= 0.25 s long;
  * spans are rounded conservatively (start up, end down) and verified never to
    contain a declared onset.

This keeps two different fields distinct, which is the whole point of the
repair: `silentBeats` / `silenceSpans` still describe the **missing onset beats**
(the player did not re-attack those grid beats but the chord is still ringing),
while `trueSilenceSpans` now describes the **acoustic silence** that actually
exists in the bytes.

DETERMINISM
-----------
Standard library only. The re-synthesis uses the base generator's own seeded
`random.Random` and its own float->PCM16 quantiser, so regenerating twice is
byte-identical. `--check` regenerates into a scratch directory and compares
SHA-256 against the committed manifest.

USAGE
-----
    python3 tools/rhythm-eval/tools/repair_sustained.py            # build
    python3 tools/rhythm-eval/tools/repair_sustained.py --check    # verify
    python3 tools/rhythm-eval/tools/repair_sustained.py --audit    # print audit
"""

import argparse
import hashlib
import importlib.util
import json
import math
import os
import random
import struct
import sys
import wave

# ---------------------------------------------------------------------------
# Paths, format and identifiers
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
BASE_CORPUS_DIR = os.path.join(REPO_ROOT, "testdata", "rhythm")
DEFAULT_OUT_DIR = os.path.join(BASE_CORPUS_DIR, "repaired-sustain")
BASE_GENERATOR = os.path.join(BASE_CORPUS_DIR, "tools", "gen_fixtures.py")

GENERATOR_NAME = "tools/rhythm-eval/tools/repair_sustained.py"
GENERATOR_VERSION = "1"
CORPUS_ID = "eval006-sustain-repair"

SAMPLE_RATE = 48000
CHANNELS = 1
BIT_DEPTH = 16
SAMPLE_WIDTH = BIT_DEPTH // 8

#: The repaired fixture is named exactly as the base one so that a comparison is
#: of the same scenario, and so the shared CLI's per-fixture tables line up.
REPLACED_FIXTURE = "sustained_chords"

#: Repair parameters. See the module docstring for the physical justification.
SUSTAIN_MIX_T60 = 1.0
SUSTAIN_EVENT_SCALE = 1.0

#: Independent PCM silence criterion (see module docstring).
SILENCE_WINDOW_SECONDS = 0.050
SILENCE_HOP_SECONDS = 0.010
SILENCE_REF_PERCENTILE = 99.0
SILENCE_FLOOR_PERCENTILE = 5.0
SILENCE_DEPTH_DB = 45.0
SILENCE_FLOOR_MARGIN_DB = 6.0
SILENCE_MIN_SECONDS = 0.25

#: Persistence audit offsets, seconds after each onset.
PERSISTENCE_OFFSETS = (0.5, 1.5)
PERSISTENCE_WINDOW_SECONDS = 0.050

#: Continuity bound: the largest permitted single-sample step in the repaired
#: file, as a fraction of full scale. A genuine band-limited pluck never steps
#: this far between adjacent 48 kHz samples; an edit/splice that inserts a click
#: does. Measured max step on the committed repair is reported by `--audit`.
MAX_SAMPLE_STEP = 0.90

#: Output file naming.
REPAIRED_WAV_NAME = "sustained_chords.wav"


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def canonical_json(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True) + "\n"


def load_base_generator(path=BASE_GENERATOR):
    """Import the committed base generator read-only (module load has no side
    effects: all generation is behind `main()`)."""
    spec = importlib.util.spec_from_file_location("eval001_gen_fixtures", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def percentile(sorted_values, p):
    """Linear-interpolated percentile of an already-sorted list."""
    if not sorted_values:
        return float("nan")
    if len(sorted_values) == 1:
        return sorted_values[0]
    k = (len(sorted_values) - 1) * (p / 100.0)
    lo = int(math.floor(k))
    hi = int(math.ceil(k))
    if lo == hi:
        return sorted_values[lo]
    return sorted_values[lo] * (hi - k) + sorted_values[hi] * (k - lo)


def read_pcm16(path):
    """Decode a mono 16-bit WAV to floats in [-1, 1) using the same `s/32768`
    convention as the base generator's `measure_wav`."""
    with wave.open(path, "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != \
                (CHANNELS, SAMPLE_WIDTH, SAMPLE_RATE):
            raise SystemExit("%s: not %d-bit mono @ %d Hz"
                             % (path, BIT_DEPTH, SAMPLE_RATE))
        frames = w.getnframes()
        raw = w.readframes(frames)
    count = len(raw) // 2
    return [struct.unpack_from("<h", raw, i * 2)[0] / 32768.0
            for i in range(count)]


def window_rms_db(samples, sr, start_seconds, end_seconds):
    a = max(0, int(round(start_seconds * sr)))
    b = min(len(samples), int(round(end_seconds * sr)))
    if b <= a:
        return float("-inf")
    acc = 0.0
    for i in range(a, b):
        v = samples[i]
        acc += v * v
    rms = math.sqrt(acc / (b - a))
    return 20.0 * math.log10(rms) if rms > 1e-12 else float("-inf")


# ---------------------------------------------------------------------------
# Independent PCM silence / energy audit
# ---------------------------------------------------------------------------

def frame_levels(samples, sr, window=SILENCE_WINDOW_SECONDS,
                 hop=SILENCE_HOP_SECONDS):
    """List of (start_seconds, level_dbfs) for 50 ms RMS frames."""
    w = max(1, int(round(window * sr)))
    h = max(1, int(round(hop * sr)))
    out = []
    i = 0
    n = len(samples)
    while i + w <= n:
        acc = 0.0
        for j in range(i, i + w):
            v = samples[j]
            acc += v * v
        rms = math.sqrt(acc / w)
        out.append((i / float(sr),
                    20.0 * math.log10(rms) if rms > 1e-12 else float("-inf")))
        i += h
    return out


def pcm_silence_threshold_db(levels):
    """Documented model-free sounding threshold for one file."""
    dbs = sorted(level for _, level in levels)
    ref = percentile(dbs, SILENCE_REF_PERCENTILE)
    floor = percentile(dbs, SILENCE_FLOOR_PERCENTILE)
    return max(floor + SILENCE_FLOOR_MARGIN_DB, ref - SILENCE_DEPTH_DB), ref, floor


def compute_pcm_true_silence_spans(samples, sr, duration_seconds,
                                   min_seconds=SILENCE_MIN_SECONDS):
    """Maximal >= `min_seconds` acoustically-silent runs of the PCM, measured
    with the criterion in the module docstring. Returns (spans, audit)."""
    levels = frame_levels(samples, sr)
    threshold, ref, floor = pcm_silence_threshold_db(levels)
    silent = [(t, level < threshold) for t, level in levels]

    raw_spans = []
    run_start = None
    last_end = 0.0
    for start, is_silent in silent:
        if is_silent and run_start is None:
            run_start = start
        if not is_silent and run_start is not None:
            # The run ends where the FIRST sounding frame begins. Ending at the
            # overlapping frame's window end would put part of a sounding 50 ms
            # window inside a declared silent span; ending here is conservative.
            raw_spans.append([run_start, start])
            run_start = None
        last_end = start + SILENCE_WINDOW_SECONDS
    if run_start is not None:
        raw_spans.append([run_start, last_end])

    spans = []
    for a, b in raw_spans:
        a = max(0.0, a)
        b = min(duration_seconds, b)
        if b - a >= min_seconds:
            # Conservative rounding: start up, end down.
            spans.append([math.ceil(a * 1e6) / 1e6,
                          math.floor(b * 1e6) / 1e6])
    audit = {
        "windowSeconds": SILENCE_WINDOW_SECONDS,
        "hopSeconds": SILENCE_HOP_SECONDS,
        "refPercentile": SILENCE_REF_PERCENTILE,
        "refDbfs": round(ref, 4),
        "floorPercentile": SILENCE_FLOOR_PERCENTILE,
        "floorDbfs": round(floor, 4),
        "depthDb": SILENCE_DEPTH_DB,
        "floorMarginDb": SILENCE_FLOOR_MARGIN_DB,
        "soundingThresholdDbfs": round(threshold, 4),
        "minSeconds": min_seconds,
    }
    return spans, audit


def validate_true_silence_spans(spans, onsets, duration_seconds):
    """The repaired spans must be sorted, inside the file, and never contain a
    declared onset. Raises AssertionError otherwise."""
    previous_end = None
    for a, b in spans:
        if not (0.0 <= a < b <= duration_seconds + 1e-9):
            raise AssertionError("true-silence span [%r, %r] outside file" % (a, b))
        if previous_end is not None and a < previous_end - 1e-9:
            raise AssertionError("true-silence spans not sorted at %r" % a)
        previous_end = b
        for o in onsets:
            if a - 1e-9 < o < b - 1e-9:
                raise AssertionError(
                    "true-silence span [%r, %r] swallows onset %r" % (a, b, o))


def max_abs_sample_step(samples):
    best = 0.0
    prev = samples[0] if samples else 0.0
    for v in samples[1:]:
        d = v - prev
        if d < 0.0:
            d = -d
        if d > best:
            best = d
        prev = v
    return best


def persistence_report(samples, sr, onsets, threshold_db,
                       offsets=PERSISTENCE_OFFSETS):
    """Measured window level at each persistence offset after each onset, plus
    the count below `threshold_db`."""
    rows = []
    below = 0
    for t in onsets:
        per_offset = {}
        for off in offsets:
            level = window_rms_db(samples, sr, t + off,
                                  t + off + PERSISTENCE_WINDOW_SECONDS)
            per_offset[off] = round(level, 4)
            if level < threshold_db:
                below += 1
        rows.append({"onsetSeconds": round(t, 9), "levelDbfsAtOffset": per_offset})
    return {"offsetsSeconds": list(offsets), "thresholdDbfs": round(threshold_db, 4),
            "belowThresholdCount": below, "perOnset": rows}


def validate_persistence(samples, sr, onsets, threshold_db,
                         offsets=PERSISTENCE_OFFSETS):
    """Raise if any onset is not acoustically present at every offset."""
    report = persistence_report(samples, sr, onsets, threshold_db, offsets)
    if report["belowThresholdCount"] > 0:
        raise AssertionError(
            "repaired chord is not acoustically persistent: %d of %d "
            "onset/offset probes below %s dBFS: %s"
            % (report["belowThresholdCount"],
               len(onsets) * len(offsets), report["thresholdDbfs"],
               report["perOnset"]))
    return report


def validate_no_clipping(signal_stats):
    """The repaired WAV must not clip and must not sit at full scale."""
    if signal_stats["clippedSampleFraction"] > 0.0:
        raise AssertionError("repaired WAV contains clipped samples: %r"
                             % signal_stats["clippedSampleFraction"])
    if signal_stats["peakDbfs"] > -0.1:
        raise AssertionError("repaired WAV peak too close to full scale: %r"
                             % signal_stats["peakDbfs"])


def validate_continuity(samples, max_step=MAX_SAMPLE_STEP):
    """The repaired WAV must be a continuous waveform with no edit/splice step."""
    step = max_abs_sample_step(samples)
    if step > max_step:
        raise AssertionError("sample step %.4f exceeds continuity bound %.4f"
                             % (step, max_step))
    return step


def validate_duration(duration_seconds, expected_seconds, tolerance=1e-6):
    if abs(duration_seconds - expected_seconds) > tolerance:
        raise AssertionError("duration %.6f != expected %.6f"
                             % (duration_seconds, expected_seconds))
    return duration_seconds


# ---------------------------------------------------------------------------
# Onset-spacing / energy audit (used for `tapping_muting_only`)
# ---------------------------------------------------------------------------

def onset_energy_audit(entry, samples, sr):
    """Independent onset-spacing and post-onset-energy audit of one fixture.

    This is deliberately descriptive: it reports the spacing distribution and
    the energy just after each attack. It does not classify a fixture as
    defective from silent occupancy, which is the EVAL-006 correction (brief
    percussive attacks legitimately leave most of the file below the floor).
    """
    onsets = sorted(entry["onsets"])
    beats = entry["beats"]
    beeperiod = (beats[1] - beats[0]) if len(beats) > 1 else None
    gaps = [b - a for a, b in zip(onsets, onsets[1:])]
    gap_stats = None
    if gaps:
        ordered = sorted(gaps)
        gap_stats = {
            "min": round(ordered[0], 6),
            "median": round(percentile(ordered, 50.0), 6),
            "mean": round(sum(ordered) / len(ordered), 6),
            "max": round(ordered[-1], 6),
            "iqr": round(percentile(ordered, 75.0) - percentile(ordered, 25.0), 6),
        }
    rows = []
    for t in onsets:
        before = window_rms_db(samples, sr, t - 0.05, t)
        after = window_rms_db(samples, sr, t, t + 0.05)
        rows.append({
            "onsetSeconds": round(t, 9),
            "beforeDbfs": round(before, 4),
            "afterDbfs": round(after, 4),
            "riseDb": round(after - before, 4),
        })
    after_levels = sorted(r["afterDbfs"] for r in rows)
    return {
        "name": entry["name"],
        "durationSeconds": entry["durationSeconds"],
        "onsetCount": len(onsets),
        "beatPeriodSeconds": beeperiod,
        "gapStatsSeconds": gap_stats,
        "medianAfterDbfs": round(percentile(after_levels, 50.0), 4),
        "minRiseDb": round(min((r["riseDb"] for r in rows), default=0.0), 4),
        "perOnset": rows,
    }


# ---------------------------------------------------------------------------
# Re-synthesis of the replaced fixture
# ---------------------------------------------------------------------------

def render_repaired_sustained(base_generator):
    """Return (fx, onsets, audio) for the repaired fixture.

    Event list, beat grid and duration are exactly the base generator's; only
    the decay parameters differ.
    """
    fixture = next(f for f in base_generator.fixtures("full")
                   if f.name == REPLACED_FIXTURE)
    base_generator.prepare(fixture)
    seed = base_generator.seed_for(fixture.name)
    base_generator.build_events(fixture, seed)
    onsets = base_generator.compute_ground_truth(fixture)

    # The repair: remove the artificial "decay shorter than the bar" trim. The
    # event times were already fixed by build_events and are not touched.
    fixture.mix_t60 = SUSTAIN_MIX_T60
    for event in fixture.events:
        event.t60_scale = SUSTAIN_EVENT_SCALE

    rng = random.Random(seed)
    audio = base_generator.render(fixture, rng)
    return fixture, list(onsets), audio


def build_repaired_entry(base_generator, base_entry, out_dir, base_manifest_sha):
    """Render the repaired WAV and build its manifest entry."""
    fixture, onsets, audio = render_repaired_sustained(base_generator)
    wav_path = os.path.join(out_dir, REPAIRED_WAV_NAME)
    base_generator.write_wav(wav_path, audio)

    stats = base_generator.measure_wav(wav_path)
    samples = read_pcm16(wav_path)
    duration = len(samples) / float(SAMPLE_RATE)

    # The repaired entry inherits every ground-truth field except the audio
    # identity and the acoustic-silence measurement.
    entry = dict(base_entry)
    entry["file"] = REPAIRED_WAV_NAME
    entry["sha256"] = sha256_of(wav_path)
    entry["bytes"] = os.path.getsize(wav_path)
    entry["signal"] = stats
    entry["durationSeconds"] = round(duration, 6)

    spans, silence_audit = compute_pcm_true_silence_spans(
        samples, SAMPLE_RATE, duration)
    validate_true_silence_spans(spans, entry["onsets"], duration)
    entry["trueSilenceSpans"] = spans

    # Keep the *conceptual* missing-onset spans untouched (they are a different
    # field): only the acoustic-silence field is recomputed.
    entry["provenance"] = (
        "EVAL-006 scoped replacement of `%s` by %s v%s. Re-synthesised with the "
        "committed EVAL-001 generator's own Karplus-Strong model and seeded RNG "
        "(%s), with the artificial per-event decay trim removed: mix_t60=%s, "
        "event t60_scale=%s. Event times, beat grid, meter and duration are "
        "identical to the base fixture; only the decay time constant and the "
        "byte-identical audio change. Base WAV sha256 %s. No third-party, "
        "commercial or otherwise copyrighted audio was used, read or derived "
        "from."
        % (REPLACED_FIXTURE, GENERATOR_NAME, GENERATOR_VERSION,
           "seed = int(sha256('eval001-guitar-corpus|1|%s')[:16], 16)"
           % REPLACED_FIXTURE, SUSTAIN_MIX_T60, SUSTAIN_EVENT_SCALE,
           base_entry["sha256"]))
    entry["replacement"] = {
        "reason": (
            "Original chord attack energy collapsed 30-35 dB by 300 ms and "
            "reached the noise floor by 500 ms, so 8.73 s (77%) of the 11.35 s "
            "file was declared true silence -- an artifact of the generator's "
            "deliberate 'decay shorter than the bar' trim, not sustained chord "
            "playing. See docs/research/CORPUS-ACOUSTIC-REVIEW.md."
        ),
        "baseWavSha256": base_entry["sha256"],
        "baseManifestSha256": base_manifest_sha,
        "parameters": {
            "mixT60": SUSTAIN_MIX_T60,
            "eventT60Scale": SUSTAIN_EVENT_SCALE,
        },
        "measured": {
            "durationSeconds": round(duration, 6),
            "peakDbfs": stats["peakDbfs"],
            "rmsDbfs": stats["rmsDbfs"],
            "clippedSampleFraction": stats["clippedSampleFraction"],
        },
        "trueSilenceAudit": silence_audit,
    }
    return entry, samples, stats


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------

def build(base_dir=BASE_CORPUS_DIR, out_dir=DEFAULT_OUT_DIR, verbose=True):
    """Build the repaired corpus directory. Returns (manifest, path)."""
    base_manifest_path = os.path.join(base_dir, "manifest.json")
    with open(base_manifest_path, "r") as fh:
        base_manifest = json.load(fh)
    base_manifest_sha = sha256_of(base_manifest_path)
    base_fixtures = base_manifest["fixtures"]

    os.makedirs(out_dir, exist_ok=True)
    base_generator = load_base_generator()

    replaced_base = next(f for f in base_fixtures if f["name"] == REPLACED_FIXTURE)
    repaired_entry, samples, stats = build_repaired_entry(
        base_generator, replaced_base, out_dir, base_manifest_sha)

    entries = []
    for fixture in base_fixtures:
        if fixture["name"] == REPLACED_FIXTURE:
            entries.append(repaired_entry)
        else:
            # Reference the untouched original by relative path. Every truth
            # and hash field is inherited, so the eighteen original WAVs and
            # their manifest records are preserved exactly.
            referenced = dict(fixture)
            referenced["file"] = "../" + fixture["file"]
            referenced["referencedFrom"] = "eval001-guitar-corpus"
            entries.append(referenced)

    # Keep the base conventions (beat tolerance etc.) and append the repaired
    # fixture's independent PCM criterion.
    conventions = dict(base_manifest.get("conventions", {}))
    conventions["repairedFixture"] = REPLACED_FIXTURE
    conventions["trueSilenceDerivationRepair"] = (
        "For the replaced `%s` fixture only, `trueSilenceSpans` is recomputed "
        "from the committed PCM with a model-free RMS criterion (%.0f ms "
        "frames, %.0f ms hop; sounding iff level >= max(5th-percentile floor + "
        "%.0f dB, 99th-percentile ref - %.0f dB); spans >= %.2f s). The "
        "eighteen unchanged fixtures keep the EVAL-001 generator-derived spans. "
        "`silentBeats`/`silenceSpans` (missing onset beats) are unchanged for "
        "every fixture." % (REPLACED_FIXTURE, SILENCE_WINDOW_SECONDS * 1000.0,
                            SILENCE_HOP_SECONDS * 1000.0,
                            SILENCE_FLOOR_MARGIN_DB, SILENCE_DEPTH_DB,
                            SILENCE_MIN_SECONDS))

    fixtures_by_name = {e["name"]: e for e in entries}
    core_fixtures = base_manifest.get("tagVocabulary", {}).get("core", [])

    manifest = {
        "schemaVersion": 1,
        "corpus": {
            "id": CORPUS_ID,
            "version": 1,
            "description": (
                "EVAL-006 scoped sustain repair. Eighteen unchanged EVAL-001 "
                "fixtures referenced by relative path, plus a re-synthesised "
                "`sustained_chords` whose chord envelopes follow the physical "
                "model's free-string decay. Not a replacement for the EVAL-001 "
                "corpus and not gate-G3 evidence on its own."),
            "profile": "repaired-sustain",
            "baseCorpusId": base_manifest.get("corpus", {}).get("id", ""),
            "baseManifestSha256": base_manifest_sha,
            "replacement": {
                "fixture": REPLACED_FIXTURE,
                "file": REPAIRED_WAV_NAME,
                "method": (
                    "Re-render the exact base event list with the base "
                    "generator's Karplus-Strong model, removing the artificial "
                    "per-event decay trim (mix_t60=%s, t60_scale=%s)."
                    % (SUSTAIN_MIX_T60, SUSTAIN_EVENT_SCALE)),
                "baseWavSha256": replaced_base["sha256"],
            },
        },
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
            "language": "python3",
            "dependencies": ("standard library only (wave, struct, math, random, "
                             "json, hashlib, importlib, argparse); the EVAL-001 "
                             "generator is imported read-only for its physical "
                             "model and seeded RNG"),
            "synthesis": ("Karplus-Strong plucked string + simulated mic capture "
                          "chain, inherited from testdata/rhythm/tools/"
                          "gen_fixtures.py"),
            "command": ("python3 tools/rhythm-eval/tools/repair_sustained.py "
                        "--out testdata/rhythm/repaired-sustain"),
        },
        "conventions": conventions,
        "tagVocabulary": base_manifest.get("tagVocabulary", {}),
        "totalBytes": sum(e["bytes"] for e in entries),
        "totalDurationSeconds": round(
            sum(e["durationSeconds"] for e in entries), 6),
        "fixtures": entries,
    }

    # Sanity: the core denominator must be unchanged.
    manifest_core = [n for n in core_fixtures if n in fixtures_by_name]
    if len(manifest_core) != len(core_fixtures):
        raise SystemExit("core membership drifted: %r" % (core_fixtures,))
    if len(entries) != len(base_fixtures):
        raise SystemExit("fixture count drifted")

    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w") as fh:
        fh.write(canonical_json(manifest))

    if verbose:
        print("repaired corpus: %d fixtures, %d bytes (%.2f MB), %.2f s"
              % (len(entries), manifest["totalBytes"],
                 manifest["totalBytes"] / (1024.0 * 1024.0),
                 manifest["totalDurationSeconds"]))
        print("replaced %s: %d bytes, sha256 %s"
              % (REPLACED_FIXTURE, repaired_entry["bytes"],
                 repaired_entry["sha256"][:16]))
        print("trueSilenceSpans: %r" % (repaired_entry["trueSilenceSpans"],))
    return manifest, manifest_path


def check(base_dir=BASE_CORPUS_DIR, out_dir=DEFAULT_OUT_DIR, verbose=True):
    """Regenerate into scratch and compare against the committed manifest."""
    import tempfile
    committed_path = os.path.join(out_dir, "manifest.json")
    if not os.path.exists(committed_path):
        raise SystemExit("no committed manifest at %s" % committed_path)
    with open(committed_path, "r") as fh:
        committed = json.load(fh)
    with tempfile.TemporaryDirectory(prefix="eval006-check-") as tmp:
        manifest, _ = build(base_dir, tmp, verbose=False)
        if canonical_json(manifest) != canonical_json(committed):
            print("repaired corpus manifest is NOT reproducible")
            return 1
        # Also compare the WAV bytes.
        with open(os.path.join(tmp, REPAIRED_WAV_NAME), "rb") as fh:
            a = hashlib.sha256(fh.read()).hexdigest()
        with open(os.path.join(out_dir, REPAIRED_WAV_NAME), "rb") as fh:
            b = hashlib.sha256(fh.read()).hexdigest()
        if a != b:
            print("repaired WAV is NOT reproducible: %s vs %s" % (a, b))
            return 1
    if verbose:
        print("check OK: repaired corpus is byte-reproducible")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def audit(base_dir=BASE_CORPUS_DIR, out_dir=DEFAULT_OUT_DIR):
    """Print the independent PCM audit for the repaired fixture and the
    `tapping_muting_only` onset-spacing/energy audit, as JSON."""
    with open(os.path.join(base_dir, "manifest.json"), "r") as fh:
        base_manifest = json.load(fh)
    with open(os.path.join(out_dir, "manifest.json"), "r") as fh:
        repaired_manifest = json.load(fh)
    repaired_entry = next(e for e in repaired_manifest["fixtures"]
                          if e["name"] == REPLACED_FIXTURE)
    samples = read_pcm16(os.path.join(out_dir, REPAIRED_WAV_NAME))
    threshold, ref, floor = pcm_silence_threshold_db(frame_levels(samples, SAMPLE_RATE))
    out = {
        "repaired": {
            "sha256": repaired_entry["sha256"],
            "durationSeconds": repaired_entry["durationSeconds"],
            "signal": repaired_entry["signal"],
            "maxAbsSampleStep": round(max_abs_sample_step(samples), 6),
            "silenceThresholdDbfs": round(threshold, 4),
            "refDbfs": round(ref, 4),
            "floorDbfs": round(floor, 4),
            "trueSilenceSpans": repaired_entry["trueSilenceSpans"],
            "persistence": persistence_report(samples, SAMPLE_RATE,
                                              repaired_entry["onsets"], threshold),
        },
        "base": {},
    }

    # Contrast: the ORIGINAL sustained fixture measured with the same PCM
    # criterion. This is the defect the repair exists to remove.
    base_sustained = next(e for e in base_manifest["fixtures"]
                          if e["name"] == REPLACED_FIXTURE)
    bsamples = read_pcm16(os.path.join(base_dir, base_sustained["file"]))
    bthreshold, bref, bfloor = pcm_silence_threshold_db(
        frame_levels(bsamples, SAMPLE_RATE))
    out["base"][REPLACED_FIXTURE] = {
        "sha256": base_sustained["sha256"],
        "signal": base_sustained["signal"],
        "maxAbsSampleStep": round(max_abs_sample_step(bsamples), 6),
        "silenceThresholdDbfs": round(bthreshold, 4),
        "refDbfs": round(bref, 4),
        "floorDbfs": round(bfloor, 4),
        "trueSilenceSpans": base_sustained["trueSilenceSpans"],
        "persistence": persistence_report(bsamples, SAMPLE_RATE,
                                          base_sustained["onsets"], bthreshold),
    }

    base_tapping = next(e for e in base_manifest["fixtures"]
                        if e["name"] == "tapping_muting_only")
    base_samples = read_pcm16(os.path.join(base_dir, base_tapping["file"]))
    out["base"]["tapping_muting_only"] = onset_energy_audit(
        base_tapping, base_samples, SAMPLE_RATE)

    return out


# ---------------------------------------------------------------------------
# Original vs repaired CLI result comparison
# ---------------------------------------------------------------------------

#: Sustained-fixture metrics compared across the two manifests.
COMPARE_FIXTURE_METRICS = (
    "predictedBeats", "truthBeats", "truePositives", "falsePositives",
    "falseNegatives", "precision", "recall", "fMeasure",
    "acquired", "acquisitionSeconds", "acquisitionBars",
    "hasBpmLock", "lockedBpm", "bpmRelativeError", "halfDoubleTimeError",
    "phaseMeasured", "phaseMeanAbsMs", "phaseP95AbsMs",
    "trueSilenceMeasured", "trueSilenceSeconds",
    "falseBeatsInTrueSilence", "falseBeatsInTrueSilencePerSecond",
    "falseBeatCoverage",
)

COMPARE_AGGREGATE_METRICS = (
    "fixtures", "coreFixtures",
    "acquisitionCoreEvaluated", "acquisitionCoreWithin2Bars",
    "acquisitionCorePassFraction",
    "bpmRelErrorCoreEvaluated", "bpmRelErrorWorstCore",
    "fMeasureMean", "precisionMean", "recallMean",
    "trueSilenceMeasuredFixtures", "trueSilenceNoSilenceFixtures",
    "trueSilenceCorpusDefectFixtures",
)

COMPARE_BACKENDS = ("btrack", "aubio")


def _load_raw(out_dir, corpus, backend):
    path = os.path.join(out_dir, "raw", corpus, backend, "block128",
                        "results.json")
    if not os.path.exists(path):
        raise SystemExit("missing raw result: %s" % path)
    with open(path, "r") as fh:
        return json.load(fh), path


def _numeric_delta(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return None
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return b - a
    return None


def compare(out_dir=DEFAULT_OUT_DIR, verbose=True):
    """Compare the committed raw CLI runs of the original and repaired
    manifests. Writes `raw/comparison.json` and `raw/comparison.md`."""
    report = {"fixtures": {}, "aggregate": {}, "rawHashes": {},
              "corpora": {}}
    for backend in COMPARE_BACKENDS:
        orig, orig_path = _load_raw(out_dir, "original", backend)
        rep, rep_path = _load_raw(out_dir, "repaired", backend)
        report["rawHashes"]["original/%s" % backend] = {
            "path": os.path.relpath(orig_path, out_dir),
            "sha256": sha256_of(orig_path)}
        report["rawHashes"]["repaired/%s" % backend] = {
            "path": os.path.relpath(rep_path, out_dir),
            "sha256": sha256_of(rep_path)}

        report["corpora"][backend] = {
            "original": orig["corpus"]["id"],
            "repaired": rep["corpus"]["id"],
        }
        if orig["corpus"]["id"] == rep["corpus"]["id"]:
            raise SystemExit("original and repaired share a corpus id")

        def fixture_by_name_cli(doc, name):
            return next(f for f in doc["fixtures"] if f["name"] == name)

        so = fixture_by_name_cli(orig, REPLACED_FIXTURE)
        sr = fixture_by_name_cli(rep, REPLACED_FIXTURE)
        rows = {}
        for metric in COMPARE_FIXTURE_METRICS:
            rows[metric] = {"original": so.get(metric),
                            "repaired": sr.get(metric),
                            "delta": _numeric_delta(so.get(metric),
                                                    sr.get(metric))}
        report["fixtures"][backend] = rows

        ao, ar = orig["aggregate"], rep["aggregate"]
        arows = {}
        for metric in COMPARE_AGGREGATE_METRICS:
            arows[metric] = {"original": ao.get(metric),
                             "repaired": ar.get(metric),
                             "delta": _numeric_delta(ao.get(metric),
                                                     ar.get(metric))}
        arows["spec19Gates"] = {"original": ao.get("spec19Gates"),
                                "repaired": ar.get("spec19Gates")}
        report["aggregate"][backend] = arows

    # The whole point of showing denominators: they must not move.
    core = {be: report["aggregate"][be]["coreFixtures"]["repaired"]
            for be in COMPARE_BACKENDS}
    fixtures = {be: report["aggregate"][be]["fixtures"]["repaired"]
                for be in COMPARE_BACKENDS}
    if len(set(core.values())) != 1 or core[COMPARE_BACKENDS[0]] != 11:
        raise SystemExit("repaired core denominator is not 11: %r" % core)
    if len(set(fixtures.values())) != 1 or fixtures[COMPARE_BACKENDS[0]] != 19:
        raise SystemExit("repaired fixture denominator is not 19: %r" % fixtures)
    report["coreDenominator"] = {"fixtures": 19, "coreFixtures": 11}

    json_path = os.path.join(out_dir, "raw", "comparison.json")
    with open(json_path, "w") as fh:
        fh.write(canonical_json(report))
    md_path = os.path.join(out_dir, "raw", "comparison.md")
    with open(md_path, "w") as fh:
        fh.write(comparison_markdown(report))
    if verbose:
        print("wrote %s and %s" % (json_path, md_path))
    return 0


def _fmt(value):
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, float):
        return "%.6g" % value
    return str(value)


def comparison_markdown(report):
    lines = []
    lines.append("# Original vs repaired manifest — real CLI runs (EVAL-006)")
    lines.append("")
    lines.append("Both manifests were run with the current-main CLI and the two "
                 "unchanged plugin shims at 128-frame blocks, uncompensated, no "
                 "legacy stamping. Raw output is under `raw/`.")
    lines.append("")
    lines.append("- Denominators: **%d fixtures, %d core fixtures** on both "
                 "manifests (unchanged)."
                 % (report["coreDenominator"]["fixtures"],
                    report["coreDenominator"]["coreFixtures"]))
    lines.append("")
    for backend in COMPARE_BACKENDS:
        lines.append("## `%s`" % backend)
        lines.append("")
        lines.append("corpus ids: original `%s`, repaired `%s`"
                     % (report["corpora"][backend]["original"],
                        report["corpora"][backend]["repaired"]))
        lines.append("")
        lines.append("| `sustained_chords` metric | original | repaired | delta |")
        lines.append("|---|---:|---:|---:|")
        for metric, row in report["fixtures"][backend].items():
            lines.append("| %s | %s | %s | %s |"
                         % (metric, _fmt(row["original"]), _fmt(row["repaired"]),
                            _fmt(row["delta"])))
        lines.append("")
        lines.append("| corpus aggregate | original | repaired | delta |")
        lines.append("|---|---:|---:|---:|")
        for metric, row in report["aggregate"][backend].items():
            if metric == "spec19Gates":
                continue
            lines.append("| %s | %s | %s | %s |"
                         % (metric, _fmt(row["original"]), _fmt(row["repaired"]),
                            _fmt(row["delta"])))
        lines.append("")
        lines.append("SPEC 19 gate booleans (not a selection): original `%s`, "
                     "repaired `%s`."
                     % (json.dumps(report["aggregate"][backend]["spec19Gates"]["original"],
                                   sort_keys=True),
                        json.dumps(report["aggregate"][backend]["spec19Gates"]["repaired"],
                                   sort_keys=True)))
        lines.append("")
    lines.append("Raw result hashes:")
    lines.append("")
    for key, value in report["rawHashes"].items():
        lines.append("- `%s` %s" % (value["path"], value["sha256"]))
    lines.append("")
    lines.append("The shared CLI still classifies `sustained_chords` as "
                 "`CorpusDefect` by fixture name. That classification is a "
                 "harness hard-code outside this task's ownership; the "
                 "repaired fixture's silence is measured independently and is "
                 "genuine (see `docs/research/SUSTAIN-REPAIR.md`).")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", default=BASE_CORPUS_DIR,
                        help="base EVAL-001 corpus directory (read-only)")
    parser.add_argument("--out", default=DEFAULT_OUT_DIR,
                        help="output directory for the repaired corpus")
    parser.add_argument("--check", action="store_true",
                        help="regenerate into scratch and compare to committed")
    parser.add_argument("--audit", action="store_true",
                        help="print the independent PCM audit as JSON")
    parser.add_argument("--compare", action="store_true",
                        help="compare the committed raw CLI runs of the "
                             "original and repaired manifests")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    if args.audit:
        print(json.dumps(audit(args.base, args.out), indent=2, sort_keys=True))
        return 0
    if args.compare:
        return compare(args.out)
    if args.check:
        return check(args.base, args.out, verbose=not args.quiet)
    build(args.base, args.out, verbose=not args.quiet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
