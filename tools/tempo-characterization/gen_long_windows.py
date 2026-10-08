#!/usr/bin/env python3
"""TRACK-007: deterministic longer-window synthetic guitar-like matrix (generator).

Reuses the synthesis code of testdata/rhythm/tools/gen_fixtures.py v1 (pinned by
sha256 in PROTOCOL.md): Karplus-Strong open strings, the mic capture chain, the
render path, the 16-bit writer and the EVAL-001 manifest entry layout. Only the
pattern (which strums happen), the window length and the additive SNR noise are
defined here. Nothing is recorded guitar; nothing is copied from any audio.

Usage (scratch output, never committed as WAV):
    python3 tools/tempo-characterization/gen_long_windows.py --out DIR
Writes DIR/manifest.json and DIR/wav/*.wav. Refuses to overwrite an existing DIR.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN_PATH = ROOT / 'testdata/rhythm/tools/gen_fixtures.py'
GEN_SHA256 = '81df59dcac2b48f6861276561fd6dcd8eb892bbad1666257676e2c75682d5be8'

spec = importlib.util.spec_from_file_location('gen_fixtures', GEN_PATH)
gf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gf)

CORPUS_ID = 'track007-long-windows'
GENERATOR_VERSION = 1
GENERATOR_NAME = 'tools/tempo-characterization/gen_long_windows.py'
LICENSE = 'CC0-1.0'
TEMPI = (96.0, 126.0)
BARS = {96.0: 13, 126.0: 17}
RATES = (48000, 44100)
CONTROLS = ('regular', 'sparse', 'gap', 'noise')
BEATS_PER_BAR = 4
LEAD_IN = 0.35
GAP_START = 16.0
GAP_END = 17.0
NOISE_SNR_DB = 0.0
PEAK_DBFS = -6.0
PROG = ('E', 'A', 'D', 'G', 'C', 'Am')
MIN_SECONDS = 32.0


def require(condition, message):
    if not condition:
        raise ValueError(message)


def seed_for(key):
    return int(hashlib.sha256(
        ('%s|%d|%s' % (CORPUS_ID, GENERATOR_VERSION, key)).encode()).hexdigest()[:16], 16)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def strums(fx, rng, beats):
    """One straight downstroke per quarter beat, timing humanised like clean_eighths.

    Returns (beat_index, Event) pairs so the controls can select by metric beat.
    Every beat is drawn before any selection, so kept attacks have identical times.
    """
    out = []
    for b, base in enumerate(beats):
        chord = PROG[(b // fx.beats_per_bar) % len(PROG)]
        boundary = beats[b + 1] if b + 1 < len(beats) else base + fx.beat_seconds
        vel = 1.0 if b % fx.beats_per_bar == 0 else 0.8
        t = gf.humanise(rng, base, 0.008, boundary)
        out.append((b, gf._chord_events(fx, rng, chord, t, vel, 0.46, 0.5, 1.0)[0]))
    return out


def pattern_for(control):
    def pattern(fx, rng, beats):
        pairs = strums(fx, rng, beats)
        if control == 'sparse':
            pairs = [(b, e) for b, e in pairs if b % fx.beats_per_bar in (0, 2)]
        elif control == 'gap':
            pairs = [(b, e) for b, e in pairs if not GAP_START <= e.time < GAP_END]
        return [e for _, e in pairs]
    return pattern


def perf_key(bpm):
    return 'perf|%dbpm' % round(bpm)


def fixture_for(control, bpm, rate):
    bars = BARS[bpm]
    fx = gf.Fixture(
        '%s_%dbpm_%dhz' % (control, round(bpm), rate), 'long_' + control,
        'Solo-guitar-like quarter-note strums at %g BPM, 4/4, %d bars; %s control.'
        % (bpm, bars, control),
        4, 4, 'quarter', BEATS_PER_BAR, bars, bpm, pattern_for(control), 'mic',
        peak_dbfs=PEAK_DBFS, noise_dbfs=-66.0, room_wet=0.16, lead_in=LEAD_IN,
        extra_tags=('steady_tempo', 'long_window'))
    return fx


def add_noise(buf, key):
    sig_power = sum(x * x for x in buf)
    rng = random.Random(seed_for(key))
    gauss = [rng.gauss(0.0, 1.0) for _ in buf]
    gauss_power = sum(x * x for x in gauss)
    scale = math.sqrt(sig_power / gauss_power) * 10.0 ** (-NOISE_SNR_DB / 20.0)
    realised_snr = 10.0 * math.log10(sig_power / sum((scale * g) ** 2 for g in gauss))
    mixed = [s + scale * g for s, g in zip(buf, gauss)]
    return gf.scale_to_peak(mixed, gf.db_to_lin(PEAK_DBFS)), realised_snr


def check_samples(buf, label):
    require(all(math.isfinite(x) for x in buf), label + ': non-finite sample')
    peak = max(abs(x) for x in buf)
    require(peak < 1.0, label + ': full-scale clipping')
    require(peak <= gf.db_to_lin(PEAK_DBFS + 0.05), label + ': peak more than 0.05 dB above target')


def entry(fx, name, rel, wav_path, sha, nbytes, notes, realised=None):
    fx.true_silence_spans = []
    e = gf.entry_for(fx, str(wav_path), rel, sha, nbytes, gf.measure_wav(str(wav_path)))
    e['name'] = name
    e['notes'] = notes
    e['license'] = LICENSE
    e['provenance'] = (
        'Synthesised from scratch by %s v%d. Reuses the Karplus-Strong, capture-chain, '
        'render and writer code of testdata/rhythm/tools/gen_fixtures.py v1 (sha256 %s). '
        'Quarter-note strum pattern is new. No recorded, third-party or commercial audio '
        'was used.' % (GENERATOR_NAME, GENERATOR_VERSION, GEN_SHA256))
    e['trueSilenceSpans'] = []
    e['trueSilenceNote'] = ('No exact-zero acoustic silence is generated (capture noise floor '
                            'present). Event-free gap windows are structural only.')
    if realised is not None:
        e['realisedSnrDb'] = round(realised, 6)
    return e


def generate(out, bars_override=None):
    require(sha256_file(GEN_PATH) == GEN_SHA256, 'gen_fixtures.py pin mismatch')
    out = Path(out)
    require(not out.exists(), 'output exists; choose a new directory: %s' % out)
    (out / 'wav').mkdir(parents=True)
    entries = []
    summary = []
    saved_rate = gf.SAMPLE_RATE
    try:
        for rate in RATES:
            gf.SAMPLE_RATE = rate
            for bpm in TEMPI:
                if bars_override:
                    BARS[bpm] = bars_override
                fixtures = {c: fixture_for(c, bpm, rate) for c in CONTROLS if c != 'noise'}
                regular_buf = None
                for control in ('regular', 'sparse', 'gap'):
                    fx = fixtures[control]
                    gf.prepare(fx)
                    gf.build_events(fx, seed_for(perf_key(bpm)))
                    gf.compute_ground_truth(fx)
                    buf = gf.render(fx, random.Random(seed_for(perf_key(bpm))))
                    check_samples(buf, fx.name)
                    if control == 'regular':
                        regular_buf = buf
                    path, rel = out / 'wav' / (fx.name + '.wav'), 'wav/' + fx.name + '.wav'
                    gf.write_wav(str(path), buf)
                    entries.append(entry(fx, fx.name, rel, path, sha256_file(path), path.stat().st_size,
                                         fx.notes + ' Paired by tempo, rate and performance seed.'))
                    summary.append((fx.name, fx.duration_seconds, len(fx.beats), fx.silent_beats))
                noise_name = 'noise_%dbpm_%dhz' % (round(bpm), rate)
                fx = fixture_for('noise', bpm, rate)
                fx.name = noise_name
                fx.events = fixtures['regular'].events
                fx.beats = fixtures['regular'].beats
                fx.beat_seconds = fixtures['regular'].beat_seconds
                fx.duration_seconds = fixtures['regular'].duration_seconds
                fx.silent_beats = []
                fx.silence_spans = []
                gf.compute_ground_truth(fx)
                noisy, realised = add_noise(regular_buf, 'noise|%dbpm' % round(bpm))
                check_samples(noisy, noise_name)
                path = out / 'wav' / (noise_name + '.wav')
                gf.write_wav(str(path), noisy)
                entries.append(entry(fx, noise_name, 'wav/' + noise_name + '.wav', path,
                                     sha256_file(path), path.stat().st_size,
                                     'Regular matrix clip plus additive white Gaussian noise at a '
                                     'declared SNR of 0 dB over the rendered window RMS. Same '
                                     'underlying audio as the paired regular clip.', realised))
                summary.append((noise_name, fx.duration_seconds, len(fx.beats), fx.silent_beats))
    finally:
        gf.SAMPLE_RATE = saved_rate
    manifest = {
        'schemaVersion': 1,
        'corpus': {'id': CORPUS_ID, 'version': GENERATOR_VERSION,
                   'description': 'TRACK-007 longer-window synthetic guitar-like characterization '
                                  'matrix. Not recorded guitar; not a production gate corpus.'},
        'generator': {'name': GENERATOR_NAME, 'version': GENERATOR_VERSION,
                      'reusesSynthesis': 'testdata/rhythm/tools/gen_fixtures.py v1 sha256 ' + GEN_SHA256,
                      'seedPolicy': 'seed = sha256(corpusId|version|performanceKey); performance key is '
                                    'tempo-only so all rates and controls share one performance.'},
        'conventions': {'beatToleranceSeconds': 0.07,
                        'beats': 'Metric grid (quarter notes) for each fixture.'},
        'fixtures': entries,
        'totalBytes': sum(e['bytes'] for e in entries),
        'totalDurationSeconds': round(sum(e['durationSeconds'] for e in entries), 6),
    }
    for e in entries:
        require(e['durationSeconds'] >= MIN_SECONDS or bars_override, 'window shorter than 32 s: ' + e['name'])
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return manifest, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--bars', type=int, default=None,
                        help='smoke-test only: override bars (not valid evidence)')
    args = parser.parse_args()
    manifest, summary = generate(args.out, args.bars)
    for name, seconds, beats, silent in summary:
        print('%-30s %8.3f s beats=%d silentBeats=%d' % (name, seconds, beats, len(silent)))
    print('fixtures=%d bytes=%d' % (len(manifest['fixtures']), manifest['totalBytes']))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        print('gen_long_windows: %s' % error, file=sys.stderr)
        sys.exit(2)
