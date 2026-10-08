#!/usr/bin/env python3
"""TRACK-008: post-readiness tempo-stability candidate paired evaluation.

Runs the predeclared protocol in docs/research/tempo-stability/PROTOCOL.md over
the preserved short (TRACK-006 / EVAL-003 derived-24) and long (TRACK-007)
matrices, for four backends (default BTrack, the old fixed variant, the new
candidate, aubio), with the unchanged pinned EVAL-007 scorer at block 128,
uncompensated, plus the pinned diagnostic for the canonical beat series.

Hard-fails on any pin, corpus, framing, method-log, timeline or beat-equality
mismatch. Writes raw and derived evidence (no WAVs) under --out. Run from the
repository root after the candidate freeze is committed and before/independent of
any result-driven change.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import math
import os
import re
import statistics
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


robustness = load('tools/rhythm-eval/tools/run_robustness.py', 'robustness')

CLI = Path('/home/mojo/projects/build-EVAL-007-integration/cli/rhythm-eval')
DIAG = Path('/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics')
PLUGINS = {
    'btrack': Path('/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so'),
    'btrack-tempo-variant': Path('/home/mojo/projects/build-TRACK-005-integration/libtempo-variant-btrack.so'),
    'btrack-tempo-stable': Path('/home/mojo/projects/build-TRACK-008-worker/libtempo-stable-btrack.so'),
    'aubio': Path('/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so'),
}
BACKENDS = ('btrack', 'btrack-tempo-variant', 'btrack-tempo-stable', 'aubio')
BTACK_FAMILY = ('btrack', 'btrack-tempo-variant', 'btrack-tempo-stable')
LOG_ENV = {
    'btrack-tempo-variant': 'JAM_TEMPO_VARIANT_LOG_DIR',
    'btrack-tempo-stable': 'JAM_TEMPO_STABILITY_LOG_DIR',
}

SHORT_DIR = ROOT / 'testdata/rhythm/derived'
SHORT_MANIFEST = SHORT_DIR / 'manifest.json'
LONG_DIR = Path('/home/mojo/projects/build-TRACK-007-integration/corpus')
LONG_MANIFEST = ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json'
FREEZE = ROOT / 'docs/research/tempo-stability/FREEZE.json'
CANDIDATE_DIR = ROOT / 'tools/tempo-stability'

PINS = {
    'cli': (CLI, 'caba565f6c6d8482443edf8d2a0a7ec40b6327ba99ccb2898f7213f8c28de552'),
    'diagnostic': (DIAG, '75b0d73bdfedb5028b65a0e171d371c7ea3bc97ed6cb0b5ed60c84b30f3edee2'),
    'btrack': (PLUGINS['btrack'], '41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d'),
    'aubio': (PLUGINS['aubio'], '61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41'),
    'btrack-tempo-variant': (PLUGINS['btrack-tempo-variant'],
                             '920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717'),
    'short-manifest': (SHORT_MANIFEST, '3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd'),
    'long-manifest': (LONG_MANIFEST, 'dd3d196fde5d1b7967ef25494be9c3350398c87995d21f476ebf4871561b9236'),
}
EMBEDDED_ARCHIVES = {
    'libjam-btrack.a': ('/home/mojo/projects/build-EVAL-005/main-core/btrack/libjam-btrack.a',
                        '92659b889ec7beb9e0e6d0ab0df59acd8c0b14eeff6de9b4ff5ca23561833255'),
    'libbtrack.a': ('/home/mojo/projects/build-EVAL-005/main-core/btrack/libbtrack.a',
                    '6993f8224ea626802a28475cce3c1bcf209336919746ac41d7e99b6cae39f1f8'),
    'libsamplerate.a': ('/home/mojo/projects/build-EVAL-005/main-core/btrack/libsamplerate.a',
                        '55c591cff00889a7207a14eac442a36c1d7348f3cf003ef14fbf1718f97be072'),
    'libkiss_fft.a': ('/home/mojo/projects/build-EVAL-005/main-core/btrack/libkiss_fft.a',
                      'fa209a29eed3ebbcb6bfb72471a48bd0b2bfa4c7b9fd33da6537ed161a932250'),
}

TWO_BARS = 2.0
REGRESSION_BAND = 0.005
PERSIST_BAND = 0.02
INTERVAL_BAND = 0.10
BLOCK = 128
GAP_START, GAP_END = 16.0, 17.0
RETENTION_RANGE = "scorer range only; no absolute acquisition/silence claim"

CORPORA = {
    'short': {'dir': SHORT_DIR, 'manifest': SHORT_MANIFEST, 'committed': SHORT_MANIFEST},
    'long': {'dir': LONG_DIR, 'manifest': LONG_MANIFEST, 'committed': LONG_MANIFEST},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def sha(path):
    return robustness.sha256_file(Path(path))


def execute(cmd, log, env=None):
    result = subprocess.run(list(map(str, cmd)), cwd=ROOT, env=env,
                            text=True, capture_output=True)
    log.write_text(result.stdout + result.stderr)
    require(result.returncode == 0,
            'command failed (%d): %s; see %s' % (result.returncode, cmd, log))


def ceil_blocks(frames):
    return math.ceil(frames / BLOCK)


def longest_run(flags):
    best = run = 0
    for flag in flags:
        run = run + 1 if flag else 0
        best = max(best, run)
    return best


# ---------------------------------------------------------------------------
# Pins, freeze and corpus verification
# ---------------------------------------------------------------------------

def verify_pins():
    out = []
    for key, (path, expected) in PINS.items():
        actual = sha(path)
        require(actual == expected, 'pin mismatch %s: %s' % (key, actual))
        out.append({'role': key, 'path': str(path), 'sha256': actual})
    for key, (path, expected) in EMBEDDED_ARCHIVES.items():
        actual = sha(path)
        require(actual == expected, 'embedded dependency mismatch %s: %s' % (key, actual))
        out.append({'role': 'embedded:' + key, 'path': path, 'sha256': actual})
    # Candidate freeze: source texts + built plugin binary.
    require(FREEZE.exists(), 'candidate freeze missing: %s' % FREEZE)
    freeze = robustness.load_json(FREEZE)
    for entry in freeze['sources']:
        actual = sha(ROOT / entry['path'])
        require(actual == entry['sha256'], 'candidate source changed: %s' % entry['path'])
    actual_so = sha(PLUGINS['btrack-tempo-stable'])
    require(actual_so == freeze['binary']['sha256'],
            'candidate binary mismatch: %s' % actual_so)
    out.append({'role': 'btrack-tempo-stable', 'path': str(PLUGINS['btrack-tempo-stable']),
                'sha256': actual_so})
    return freeze, out


def verify_corpus(label, corpus):
    committed = robustness.load_json(corpus['committed'])
    live_manifest = corpus['dir'] / 'manifest.json'
    require(live_manifest.read_bytes() == Path(corpus['manifest']).read_bytes(),
            '%s corpus manifest differs from the committed manifest' % label)
    fixtures = committed['fixtures']
    for fixture in fixtures:
        path = corpus['dir'] / fixture['file']
        require(path.exists(), 'missing WAV: %s' % path)
        require(sha(path) == fixture['sha256'], 'WAV hash mismatch: %s' % fixture['name'])
        require(path.stat().st_size == fixture['bytes'], 'WAV byte size mismatch: %s' % fixture['name'])
        with wave.open(str(path)) as wav:
            require((wav.getnframes(), wav.getframerate(), wav.getnchannels(),
                     wav.getsampwidth()) ==
                    (fixture['signal']['frames'], fixture['sampleRate'], fixture['channels'],
                     fixture['bitDepth'] // 8),
                    'WAV framing mismatch: %s' % fixture['name'])
        require('core' not in fixture['scenarioTags'], 'core tag present: %s' % fixture['name'])
    if label == 'long':
        by = {f['name']: f for f in fixtures}
        require(len(fixtures) == 16, 'expected 16 long fixtures')
        for bpm in (96, 126):
            for rate in (48000, 44100):
                reg = 'regular_%dbpm_%dhz' % (bpm, rate)
                sp = 'sparse_%dbpm_%dhz' % (bpm, rate)
                g = 'gap_%dbpm_%dhz' % (bpm, rate)
                no = 'noise_%dbpm_%dhz' % (bpm, rate)
                require(by[reg]['silentBeats'] == [], 'regular has silent beats')
                require(by[no]['onsets'] == by[reg]['onsets'], 'noise onsets differ')
                require(by[no]['beats'] == by[reg]['beats'], 'noise beats differ')
                require(all(b % 2 == 1 for b in by[sp]['silentBeats']),
                        'sparse silent beats not odd indices')
                require(any(GAP_START <= b < GAP_END for b in by[g]['beats']),
                        'gap window has no beat')
        for fixture in fixtures:
            require(fixture['trueSilenceSpans'] == [],
                    'unexpected trueSilenceSpans: %s' % fixture['name'])
    else:
        require(len(fixtures) == 24, 'expected 24 short fixtures')
        baselines = [f for f in fixtures if f['transformation']['kind'] == 'baseline']
        require(len(baselines) == 2, 'expected 2 short baselines')
    return committed


def validate_results(path, backend, fixtures, label):
    data = robustness.load_json(path)
    require(data.get('backend') == backend, 'backend identity mismatch')
    require(data.get('blockFrames') == BLOCK and not data.get('legacyBlockStampedBeats'),
            'missing/wrong framing or legacy stamps')
    variants = data.get('variants', [])
    require(len(variants) == 1 and variants[0].get('label') == 'uncompensated',
            'expected only uncompensated scoring')
    require(data.get('fixtures') == variants[0].get('fixtures'),
            'top-level and uncompensated fixtures disagree')
    result = robustness.read_backend_results(path)
    require(not result['errors'], '; '.join(result['errors']))
    expected = {f['name']: f for f in fixtures}
    require(set(result['fixturesByName']) == set(expected), 'fixture set mismatch')
    for name, metric in result['fixturesByName'].items():
        require(metric.get('sourceSha256') == expected[name]['sha256'],
                'audio identity mismatch: %s' % name)
        require(not metric.get('core'), 'core membership in a non-core matrix')
        if label == 'short' and expected[name]['transformation']['kind'] == 'noise':
            require(metric.get('falseBeatCoverage') == 'NotAssessedStructuralNoise'
                    and not metric.get('falseBeatMetricInformative'),
                    'stale noise coverage scorer: %s' % name)
    return result


# ---------------------------------------------------------------------------
# Method log parsing
# ---------------------------------------------------------------------------

def read_csv(path):
    import csv
    with Path(path).open(newline='') as stream:
        return list(csv.DictReader(stream))


def split_method_log(rows, fixtures):
    groups = []
    offset = 0
    for fixture in fixtures:
        count = ceil_blocks(fixture['signal']['frames'])
        group = rows[offset:offset + count]
        require(len(group) == count and all(int(r['blockIndex']) == i for i, r in enumerate(group)),
                'incomplete/reordered method fixture window')
        groups.append(group)
        offset += count
    require(offset == len(rows), 'extra method blocks')
    return groups


def stable_summary(rows, fixture, default_beats, nominal):
    require(len(rows) == ceil_blocks(fixture['signal']['frames']), 'incomplete candidate log')
    emitted = [r for r in rows if r['beatEvent'] == '1']
    require(len(emitted) == len(default_beats), 'method/diagnostic beat count mismatch')
    for index, row in enumerate(rows):
        require(int(row['blockIndex']) == index, 'missing/reordered method blocks')
        require(row['ringFull'] in ('0', '1') and row['confirmed'] in ('0', '1'), 'bad flags')
        require(row['derivedValid'] in ('0', '1'), 'bad derivedValid')
        require(int(row['ringCount']) <= 4, 'ring over capacity')
        require((row['intervalMeasured'] == '0') == (row['intervalSeconds'] == ''),
                'missing interval must have an empty cell')
        if row['ringFull'] == '1':
            require(row['derivedBpm'] != '' and row['derivedValid'] == '1',
                    'full ring must carry a derived value')
        else:
            require(row['derivedBpm'] == '', 'derived value without a full ring')
        if row['confirmed'] == '0':
            require(row['emittedBpm'] == row['baseBpm'],
                    'fallback differs from base: %s' % fixture['name'])
    for row, beat in zip(emitted, default_beats):
        require(row['eventSeconds'] == beat['eventSeconds']
                and row['blockEndSeconds'] == beat['availabilitySeconds']
                and row['blockIndex'] == beat['blockIndex'],
                'method/diagnostic beat timeline mismatch')
    confirmed = [r for r in emitted if r['confirmed'] == '1']
    bpms = [float(r['emittedBpm']) for r in confirmed]
    flags = [abs(v / nominal - 1.0) > PERSIST_BAND for v in bpms] if nominal else []
    return {
        'fixture': fixture['name'], 'blocks': len(rows), 'beats': len(emitted),
        'fallbackBeats': sum(r['confirmed'] == '0' for r in emitted),
        'confirmedBeats': len(confirmed),
        'fallbackBlocks': sum(r['confirmed'] == '0' for r in rows),
        'confirmedBlocks': sum(r['confirmed'] == '1' for r in rows),
        'firstConfirmedEventSeconds': float(confirmed[0]['eventSeconds']) if confirmed else None,
        'firstConfirmedAvailabilitySeconds': float(confirmed[0]['blockEndSeconds']) if confirmed else None,
        'confirmedBpmMin': min(bpms) if bpms else None,
        'confirmedBpmMedian': statistics.median(bpms) if bpms else None,
        'confirmedBpmMax': max(bpms) if bpms else None,
        'confirmedBeatsOutOfBand': sum(flags),
        'longestOutOfBandConfirmedRun': longest_run(flags),
        'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                  for s in sorted({r['intervalState'] for r in emitted})},
    }


def variant_summary(rows, fixture, default_beats, nominal):
    require(len(rows) == ceil_blocks(fixture['signal']['frames']), 'incomplete variant log')
    emitted = [r for r in rows if r['beatEvent'] == '1']
    require(len(emitted) == len(default_beats), 'variant/diagnostic beat count mismatch')
    for index, row in enumerate(rows):
        require(int(row['blockIndex']) == index, 'missing/reordered variant blocks')
        require(row['ready'] in ('0', '1'), 'bad readiness')
        require((row['intervalMeasured'] == '0') == (row['intervalSeconds'] == ''),
                'missing interval must have an empty cell')
        if row['ready'] == '0':
            require(row['variantBpm'] == row['baseBpm'], 'fallback differs from base')
        if row['ready'] == '1':
            require(row['ringCount'] == '4', 'ready without four intervals')
    for row, beat in zip(emitted, default_beats):
        require(row['eventSeconds'] == beat['eventSeconds']
                and row['blockEndSeconds'] == beat['availabilitySeconds']
                and row['blockIndex'] == beat['blockIndex'],
                'variant/diagnostic beat timeline mismatch')
    ready = [r for r in emitted if r['ready'] == '1']
    bpms = [float(r['variantBpm']) for r in ready]
    flags = [abs(v / nominal - 1.0) > PERSIST_BAND for v in bpms] if nominal else []
    return {
        'fixture': fixture['name'], 'blocks': len(rows), 'beats': len(emitted),
        'fallbackBeats': sum(r['ready'] == '0' for r in emitted),
        'readyBeats': len(ready),
        'firstReadyEventSeconds': float(ready[0]['eventSeconds']) if ready else None,
        'firstReadyAvailabilitySeconds': float(ready[0]['blockEndSeconds']) if ready else None,
        'readyBpmMin': min(bpms) if bpms else None,
        'readyBpmMedian': statistics.median(bpms) if bpms else None,
        'readyBpmMax': max(bpms) if bpms else None,
        'readyBeatsOutOfBand': sum(flags),
        'longestOutOfBandReadyRun': longest_run(flags),
        'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                  for s in sorted({r['intervalState'] for r in emitted})},
    }


def compress_csv(src, dst):
    with Path(src).open('rb') as inp, Path(dst).open('wb') as out:
        with gzip.GzipFile(filename='', mode='wb', fileobj=out, mtime=0) as compressed:
            compressed.write(inp.read())
    return sha(dst), Path(src).stat().st_size


# ---------------------------------------------------------------------------
# Outcomes, steady windows, comparisons
# ---------------------------------------------------------------------------

def steady_window(truth, acquisition_seconds, acquired):
    beats = truth['beats']
    if not beats:
        return 0.0, truth['durationSeconds']
    end = truth.get('durationSeconds') or beats[-1]
    if acquired and acquisition_seconds is not None and acquisition_seconds >= 0.0:
        start = beats[0] + acquisition_seconds
    else:
        start = beats[0] + 0.5 * (beats[-1] - beats[0])
    return (min(start, end), end)


def outcome(metric, truth, backend, corpus_label, nominal):
    acquired = bool(metric['acquired'])
    raw_bars = metric['acquisitionBars']
    start, end = steady_window(truth, metric.get('acquisitionSeconds'), acquired)
    if not metric['hasBpmLock']:
        missing = 'no-bpm-lock'
    elif not metric['hasNominalBpm']:
        missing = 'no-nominal-bpm'
    else:
        missing = None
    return {
        'corpus': corpus_label, 'fixture': metric['name'], 'backend': backend,
        'acquired': acquired,
        'acquisitionBars': raw_bars if acquired else None,
        'acquisitionBarsRaw': raw_bars,
        'acquisitionSeconds': metric['acquisitionSeconds'] if acquired else None,
        'acquiredWithinTwoBars': acquired and raw_bars is not None and raw_bars <= TWO_BARS,
        'hasBpmLock': bool(metric['hasBpmLock']),
        'hasNominalBpm': bool(metric['hasNominalBpm']),
        'detectionMeasured': bool(metric['detectionMeasured']),
        'bpmMissingReason': missing,
        'lockedBpm': metric['lockedBpm'] if metric['hasBpmLock'] else None,
        'bpmRelativeError': metric['bpmRelativeError'] if missing is None else None,
        'steadyWindowStartSeconds': start, 'steadyWindowEndSeconds': end,
        'fMeasure': metric['fMeasure'] if metric['detectionMeasured'] else None,
        'phaseMeasured': bool(metric.get('phaseMeasured')),
        'halfTimeLock': metric.get('halfTimeLock'),
        'doubleTimeLock': metric.get('doubleTimeLock'),
        'nominalBpm': nominal,
    }


def pair(base, other):
    def state():
        if base['bpmRelativeError'] is None or other['bpmRelativeError'] is None:
            return 'not-evaluable'
        delta = other['bpmRelativeError'] - base['bpmRelativeError']
        if delta > REGRESSION_BAND:
            return 'regression'
        if -delta > REGRESSION_BAND:
            return 'gain'
        return 'no-change'
    return {
        'acquisitionGained': (not base['acquired']) and other['acquired'],
        'acquisitionLost': base['acquired'] and not other['acquired'],
        'withinTwoBarsGained': (not base['acquiredWithinTwoBars']) and other['acquiredWithinTwoBars'],
        'withinTwoBarsLost': base['acquiredWithinTwoBars'] and not other['acquiredWithinTwoBars'],
        'bpmErrorState': state(),
        'baseAcquisitionBars': base['acquisitionBars'],
        'otherAcquisitionBars': other['acquisitionBars'],
        'baseBpmRelativeError': base['bpmRelativeError'],
        'otherBpmRelativeError': other['bpmRelativeError'],
    }


def interval_summary(events, control, bpm, long_gap=False):
    period = 60.0 / bpm
    expected = (2 if control == 'sparse' else 1) * period
    rows = []
    for a, b in zip(events, events[1:]):
        spanning = long_gap and control == 'gap' and a < GAP_START and b >= GAP_END
        outlier = (not spanning) and abs((b - a) / expected - 1.0) > INTERVAL_BAND
        rows.append({'start': a, 'end': b, 'gapSpanning': spanning, 'outlier': outlier})
    outliers = [r for r in rows if r['outlier']]
    return {
        'expectedSeconds': expected, 'intervals': len(rows),
        'outliers': len(outliers), 'longestOutlierRun': longest_run([r['outlier'] for r in rows]),
        'gapSpanningCount': sum(r['gapSpanning'] for r in rows),
        'maxIntervalSeconds': max((r['end'] - r['start'] for r in rows), default=None),
        'firstOutlierStart': outliers[0]['start'] if outliers else None,
    }


def control_of_long(name):
    return name.split('_')[0]


def tempo_of_long(name):
    return int(re.search(r'_(\d+)bpm_', name).group(1))


def rate_of_long(name):
    return int(re.search(r'_(\d+)hz$', name).group(1))


def coverage_of(metric):
    return {
        'falseBeatCoverage': metric.get('falseBeatCoverage'),
        'falseBeatMetricInformative': metric.get('falseBeatMetricInformative'),
        'trueSilenceMeasured': metric.get('trueSilenceMeasured'),
        'trueSilenceSeconds': metric.get('trueSilenceSeconds'),
        'falseBeatsInTrueSilence': metric.get('falseBeatsInTrueSilence'),
        'falseBeatsInTrueSilencePerSecond': metric.get('falseBeatsInTrueSilencePerSecond'),
        'silenceAccelerationMeasured': metric.get('silenceAccelerationMeasured'),
        'silenceAccelerationInsufficientEvidence': metric.get('silenceAccelerationInsufficientEvidence'),
        'silenceSpansEvaluated': metric.get('silenceSpansEvaluated'),
        'maxSilenceTempoIncreaseBpm': metric.get('maxSilenceTempoIncreaseBpm'),
        'phaseMeasured': metric.get('phaseMeasured'),
        'halfTimeLock': metric.get('halfTimeLock'),
        'doubleTimeLock': metric.get('doubleTimeLock'),
        'retentionRange': RETENTION_RANGE,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path,
                        default=ROOT / 'docs/research/tempo-stability/evidence')
    parser.add_argument('--scratch', type=Path,
                        default=Path('/home/mojo/projects/build-TRACK-008-worker/run'))
    args = parser.parse_args()
    require(Path.cwd() == ROOT, 'run in the explicit repository workdir')
    out = args.out
    require(not out.exists(), 'output exists; choose a new directory: %s' % out)

    freeze, pins = verify_pins()
    corpora = {}
    for label, corpus in CORPORA.items():
        corpora[label] = verify_corpus(label, corpus)
    out.mkdir(parents=True)
    scratch = args.scratch
    require(not scratch.exists(), 'scratch exists; choose a new directory: %s' % scratch)
    scratch.mkdir(parents=True)

    commands = []
    metrics = {}       # (label, backend) -> fixturesByName
    all_fixtures = {}  # label -> manifest dict
    for label, corpus in CORPORA.items():
        fixtures = corpora[label]['fixtures']
        all_fixtures[label] = {f['name']: f for f in fixtures}
        for backend in BACKENDS:
            run_dir = scratch / 'raw' / label / backend
            run_dir.mkdir(parents=True)
            method_dir = scratch / 'method' / label / backend
            method_dir.mkdir(parents=True)
            cmd = [CLI, '--corpus', corpus['dir'], '--out', run_dir, '--backend', backend,
                   '--backend-lib', PLUGINS[backend], '--block', str(BLOCK)]
            env = dict(os.environ)
            for var in LOG_ENV.values():
                env.pop(var, None)
            if backend in LOG_ENV:
                env[LOG_ENV[backend]] = str(method_dir.resolve())
            execute(cmd, run_dir / 'command-output.txt', env)
            commands.append({'corpus': label, 'backend': backend, 'argv': list(map(str, cmd)),
                             'methodLogDir': env.get(LOG_ENV.get(backend, ''))})
            result = validate_results(run_dir / 'results.json', backend, fixtures, label)
            metrics[(label, backend)] = result['fixturesByName']
            # Retain the raw scorer JSON and its log.
            (out / 'raw').mkdir(exist_ok=True)
            dest = out / 'raw' / ('%s-%s' % (label, backend))
            dest.mkdir(parents=True, exist_ok=True)
            (dest / 'results.json').write_bytes((run_dir / 'results.json').read_bytes())
            (dest / 'command-output.txt').write_bytes((run_dir / 'command-output.txt').read_bytes())

        for backend in BACKENDS:
            trace_dir = scratch / 'diagnostic' / label / backend
            trace_dir.mkdir(parents=True)
            cmd = [DIAG, '--corpus', corpus['dir'], '--out', trace_dir, '--backend', backend,
                   '--backend-lib', PLUGINS[backend], '--block', str(BLOCK),
                   '--trace-files', 'beats', '--label', backend]
            if backend not in ('btrack', 'aubio'):
                cmd.append('--variant-not-default')
            env = dict(os.environ)
            for var in LOG_ENV.values():
                env.pop(var, None)
            execute(cmd, trace_dir / 'command-output.txt', env)
            commands.append({'corpus': label, 'backend': backend, 'mode': 'diagnostic',
                             'argv': list(map(str, cmd))})
            dest = out / 'diagnostic' / label / backend / 'beats'
            dest.mkdir(parents=True, exist_ok=True)
            for fixture in corpora[label]['fixtures']:
                name = fixture['name']
                src = trace_dir / 'beats' / (name + '.csv')
                beats = read_csv(src)
                require(len(beats) == metrics[(label, backend)][name]['predictedBeats'],
                        'CLI/diagnostic predicted count mismatch: %s/%s/%s'
                        % (label, backend, name))
                (dest / (name + '.csv')).write_bytes(src.read_bytes())

    # --- method logs (candidate + old variant), split and validated ---------
    method_raw = {}
    method_summaries = {'btrack-tempo-stable': [], 'btrack-tempo-variant': []}
    for label in CORPORA:
        fixtures = corpora[label]['fixtures']
        for backend, parser, store in (
                ('btrack-tempo-stable', stable_summary, method_summaries['btrack-tempo-stable']),
                ('btrack-tempo-variant', variant_summary, method_summaries['btrack-tempo-variant'])):
            method_dir = scratch / 'method' / label / backend
            names = sorted(p.name for p in method_dir.iterdir())
            require(names == ['instance_0.csv'],
                    'method log must be one reused instance for %s/%s: %s' % (label, backend, names))
            raw = method_dir / 'instance_0.csv'
            rows = read_csv(raw)
            groups = split_method_log(rows, fixtures)
            (out / 'method-raw').mkdir(exist_ok=True)
            gz = out / 'method-raw' / ('%s-%s.csv.gz' % (label, backend))
            digest, raw_bytes = compress_csv(raw, gz)
            method_raw[(label, backend)] = {'sha256': digest, 'rawBytes': raw_bytes,
                                            'retainedAs': str(gz.relative_to(ROOT))}
            for fixture, group in zip(fixtures, groups):
                name = fixture['name']
                default_beats = read_csv(out / 'diagnostic' / label / 'btrack' / 'beats' / (name + '.csv'))
                nominal = fixture.get('nominalBpm') or 0.0
                store.append(parser(group, fixture, default_beats, nominal))

    # --- beat equality (BTrack family) --------------------------------------
    equality = []
    for label in CORPORA:
        for fixture in corpora[label]['fixtures']:
            name = fixture['name']
            digests = {}
            for backend in BTACK_FAMILY:
                path = out / 'diagnostic' / label / backend / 'beats' / (name + '.csv')
                digests[backend] = sha(path)
            require(len(set(digests.values())) == 1,
                    'BTrack-family beat series not byte-identical: %s/%s' % (label, name))
            equality.append({'corpus': label, 'fixture': name,
                             'exactBTrackFamilyEquality': True, 'sha256': digests})
            for backend in ('aubio',):
                path = out / 'diagnostic' / label / backend / 'beats' / (name + '.csv')
                digests[backend] = sha(path)
            equality[-1]['aubioSha256'] = digests['aubio']

    # --- outcomes, coverage, comparisons -----------------------------------
    outcomes = []
    coverage = []
    for label in CORPORA:
        fixtures = corpora[label]['fixtures']
        long_gap = (label == 'long')
        for fixture in fixtures:
            name = fixture['name']
            nominal = fixture.get('nominalBpm') or 0.0
            for backend in BACKENDS:
                metric = metrics[(label, backend)][name]
                o = outcome(metric, fixture, backend, label, nominal)
                outcomes.append(o)
                coverage.append(dict(coverage_of(metric), corpus=label, fixture=name,
                                     backend=backend))
    index = {(o['corpus'], o['fixture'], o['backend']): o for o in outcomes}

    other_backends = ('btrack-tempo-variant', 'btrack-tempo-stable', 'aubio')
    comparisons = []
    for label in CORPORA:
        for fixture in corpora[label]['fixtures']:
            name = fixture['name']
            for other in other_backends:
                rec = {'kind': 'default-vs-%s' % other, 'corpus': label, 'fixture': name,
                       'otherBackend': other}
                rec.update(pair(index[(label, name, 'btrack')], index[(label, name, other)]))
                comparisons.append(rec)

    control_pairs = []
    for label in CORPORA:
        for fixture in corpora[label]['fixtures']:
            name = fixture['name']
            if label == 'short':
                base_name = fixture.get('pairedBaseline')
                if not base_name or base_name == name:
                    continue
                control = fixture['transformation']['kind']
                pair_kind = 'perturbation-vs-baseline'
            else:
                control = control_of_long(name)
                if control == 'regular':
                    continue
                base_name = 'regular_%dbpm_%dhz' % (tempo_of_long(name), rate_of_long(name))
                pair_kind = 'control-vs-regular'
            for backend in BACKENDS:
                rec = {'kind': pair_kind, 'corpus': label, 'fixture': name,
                       'pairedBaseline': base_name, 'control': control, 'backend': backend}
                rec.update(pair(index[(label, base_name, backend)], index[(label, name, backend)]))
                control_pairs.append(rec)

    # --- intervals ----------------------------------------------------------
    intervals = {}
    for label in CORPORA:
        intervals[label] = {}
        for backend in BACKENDS:
            intervals[label][backend] = {}
            for fixture in corpora[label]['fixtures']:
                name = fixture['name']
                beats = read_csv(out / 'diagnostic' / label / backend / 'beats' / (name + '.csv'))
                events = [float(b['eventSeconds']) for b in beats]
                if label == 'long':
                    control = control_of_long(name)
                    bpm = tempo_of_long(name)
                else:
                    control = fixture['transformation']['kind']
                    bpm = fixture.get('nominalBpm') or 0.0
                intervals[label][backend][name] = interval_summary(
                    events, control, bpm, long_gap=(label == 'long'))

    # --- validation summary -------------------------------------------------
    validation = {
        'pins': len(pins), 'candidateSources': len(freeze['sources']),
        'shortFixtures': len(corpora['short']['fixtures']),
        'longFixtures': len(corpora['long']['fixtures']),
        'backends': len(BACKENDS),
        'scorerResultsValidated': len(BACKENDS) * len(CORPORA),
        'exactBTrackFamilyBeatSeries': len(equality),
        'outcomes': len(outcomes), 'comparisons': len(comparisons),
        'controlPairs': len(control_pairs),
        'beatEqualityAll': all(e['exactBTrackFamilyEquality'] for e in equality),
        'hardErrors': 0,
        'missingSemantics': 'null = not measured; raw sentinels preserved in *Raw fields',
    }

    # --- write derived evidence --------------------------------------------
    dump(out / 'outcomes.json', outcomes)
    dump(out / 'comparison.json', comparisons)
    dump(out / 'control-pairs.json', control_pairs)
    dump(out / 'coverage.json', coverage)
    dump(out / 'intervals.json', intervals)
    dump(out / 'beat-equality.json', equality)
    dump(out / 'method-summary.json', method_summaries)
    dump(out / 'validation.json', validation)
    dump(out / 'provenance.json', {
        'task': 'TRACK-008',
        'protocol': 'docs/research/tempo-stability/PROTOCOL.md',
        'protocolCommit': 'docs(TRACK-008): predeclare post-readiness tempo-stability protocol',
        'diagnosticOnly': True, 'g3': 'OPEN', 'selection': None,
        'blockFrames': BLOCK, 'scoring': 'current EVAL-007, uncompensated',
        'matrix': {'short': 'testdata/rhythm/derived (TRACK-006 derived-24)',
                   'long': 'docs/research/tempo-long-windows (TRACK-007 16)'},
        'freeze': freeze, 'pins': pins, 'commands': commands,
        'methodRawLogs': {('%s/%s' % key): value for key, value in method_raw.items()},
        'retentionRange': RETENTION_RANGE,
    })
    files = sorted(p for p in out.rglob('*') if p.is_file())
    (out / 'artifact-hashes.txt').write_text(''.join(
        '%s  %s\n' % (sha(p), p.relative_to(out)) for p in files))
    total = sum(p.stat().st_size for p in files)
    print('PASS: %d fixtures x %d backends = %d outcomes; %d comparisons; %d control pairs; '
          '%d exact BTrack-family beat series; evidence %d bytes (%.2f MiB)'
          % (len(corpora['short']['fixtures']) + len(corpora['long']['fixtures']),
             len(BACKENDS), len(outcomes), len(comparisons), len(control_pairs),
             len(equality), total, total / (1024.0 * 1024.0)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print('run_stability: %s' % error, file=sys.stderr)
        sys.exit(2)
