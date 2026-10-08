#!/usr/bin/env python3
"""TRACK-007: run the predeclared longer-window matrix (docs/research/tempo-long-windows/PROTOCOL.md).

Scores the 16 generated fixtures with the pinned current CLI (block 128,
uncompensated) and the pinned diagnostic CLI for default BTrack, the fixed
variant and aubio. Applies the protocol's outcome and comparison rules exactly.
Diagnostic only: no backend selection, no tuning, no gate. Run from the worktree root.
"""
import argparse
import csv
import gzip
import importlib.util
import json
import math
import os
import re
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
paired = load('tools/tempo-variant/run_paired.py', 'run_paired')
gen = load('tools/tempo-characterization/gen_long_windows.py', 'gen_long_windows')

COMMITTED_MANIFEST = ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json'
BACKENDS = ('btrack', 'btrack-tempo-variant', 'aubio')
PINS = {
    'cli': (paired.CLI, 'caba565f6c6d8482443edf8d2a0a7ec40b6327ba99ccb2898f7213f8c28de552'),
    'diagnostic': (paired.DIAG, '75b0d73bdfedb5028b65a0e171d371c7ea3bc97ed6cb0b5ed60c84b30f3edee2'),
    'btrack': (paired.PLUGINS['btrack'], '41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d'),
    'aubio': (paired.PLUGINS['aubio'], '61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41'),
    'btrack-tempo-variant': (paired.PLUGINS['btrack-tempo-variant'],
                             '920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717'),
}
GEN_PINS = {
    'gen_fixtures': (gen.GEN_PATH, gen.GEN_SHA256),
    'gen_long_windows': (ROOT / 'tools/tempo-characterization/gen_long_windows.py',
                         '9e76b10822f5ea732d39a3314fb87cb13869782f477924ccfe16e161c470e766'),
}
EXPECTED_FIXTURES = 16
TWO_BARS = 2.0
REGRESSION_BAND = 0.005       # 0.5 percentage points, absolute relative-error difference
PERSIST_BAND = 0.02           # 2 % from nominal for a ready beat's reported BPM
INTERVAL_BAND = 0.10          # +/-10 % of the expected period
GAP_START, GAP_END = gen.GAP_START, gen.GAP_END


def require(condition, message):
    if not condition:
        raise ValueError(message)


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def control_of(name):
    return name.split('_')[0]


def tempo_of(name):
    return int(re.search(r'_(\d+)bpm_', name).group(1))


def rate_of(name):
    return int(re.search(r'_(\d+)hz$', name).group(1))


def longest_run(flags):
    best = run = 0
    for flag in flags:
        run = run + 1 if flag else 0
        best = max(best, run)
    return best


def interval_summary(events, control, bpm):
    """Consecutive emitted-event intervals against the expected period (protocol 7.5)."""
    period = 60.0 / bpm
    expected = (2 if control == 'sparse' else 1) * period
    rows = []
    for a, b in zip(events, events[1:]):
        dt = b - a
        spanning = a < GAP_START and b >= GAP_END
        outlier = (not spanning) and abs(dt / expected - 1.0) > INTERVAL_BAND
        rows.append({'start': a, 'end': b, 'seconds': dt, 'gapSpanning': spanning, 'outlier': outlier})
    outliers = [r for r in rows if r['outlier']]
    return {
        'expectedSeconds': expected,
        'intervals': len(rows),
        'outliers': len(outliers),
        'longestOutlierRun': longest_run([r['outlier'] for r in rows]),
        'gapSpanning': [r for r in rows if r['gapSpanning']],
        'maxIntervalSeconds': max((r['seconds'] for r in rows), default=None),
        'firstOutlierStart': outliers[0]['start'] if outliers else None,
    }


def variant_readiness(rows, bpm, summary):
    """Ready-beat persistence from the method log (protocol 7.3-7.4)."""
    beats = [r for r in rows if r['beatEvent'] == '1']
    ready = [r for r in beats if r['ready'] == '1']
    flags = [abs(float(r['variantBpm']) / bpm - 1.0) > PERSIST_BAND for r in ready]
    return {
        'firstReadyEventSeconds': summary['firstReadyEventSeconds'],
        'firstReadyAvailabilitySeconds': summary['firstReadyAvailabilitySeconds'],
        'fallbackBeatsBeforeReady': summary['fallbackBeats'],
        'readyBeats': summary['readyBeats'],
        'readyBpmMin': summary['readyBpmMin'],
        'readyBpmMedian': summary['readyBpmMedian'],
        'readyBpmMax': summary['readyBpmMax'],
        'readyBeatsOutOfBand': sum(flags),
        'longestOutOfBandReadyRun': longest_run(flags),
    }


def outcome_for(metric, control, bpm, rate, backend, intervals, readiness):
    within = bool(metric['acquired']) and metric['acquisitionBars'] is not None \
        and metric['acquisitionBars'] <= TWO_BARS
    has_error = bool(metric['hasBpmLock'] and metric['hasNominalBpm'])
    return {
        'fixture': metric['name'], 'control': control, 'tempoBpm': bpm, 'rateHz': rate,
        'backend': backend,
        'acquired': bool(metric['acquired']), 'acquisitionBars': metric['acquisitionBars'],
        'acquiredWithinTwoBars': within,
        'lockedBpm': metric['lockedBpm'] if metric['hasBpmLock'] else None,
        'bpmRelativeError': metric['bpmRelativeError'] if has_error else None,
        'fMeasure': metric['fMeasure'] if metric['detectionMeasured'] else None,
        'halfTimeLock': metric.get('halfTimeLock'), 'doubleTimeLock': metric.get('doubleTimeLock'),
        'intervals': intervals,
        'variantReadiness': readiness,
    }


def compare(base, variant):
    """Protocol section 8 exact rules. Returns the paired record."""
    def bpm_state(b, v):
        if b['bpmRelativeError'] is None or v['bpmRelativeError'] is None:
            return 'not-evaluable'
        delta = v['bpmRelativeError'] - b['bpmRelativeError']
        if delta > REGRESSION_BAND:
            return 'regression'
        if -delta > REGRESSION_BAND:
            return 'gain'
        return 'no-change'
    return {
        'fixture': base['fixture'],
        'acquisitionGained': (not base['acquired']) and variant['acquired'],
        'acquisitionLost': base['acquired'] and not variant['acquired'],
        'withinTwoBarsGained': (not base['acquiredWithinTwoBars']) and variant['acquiredWithinTwoBars'],
        'withinTwoBarsLost': base['acquiredWithinTwoBars'] and not variant['acquiredWithinTwoBars'],
        'bpmErrorState': bpm_state(base, variant),
        'defaultBpmRelativeError': base['bpmRelativeError'],
        'variantBpmRelativeError': variant['bpmRelativeError'],
    }


def verify_corpus(corpus, manifest):
    require(manifest_bytes_equal(corpus), 'corpus manifest differs from committed manifest')
    fixtures = manifest['fixtures']
    require(len(fixtures) == EXPECTED_FIXTURES, 'expected 16 fixtures')
    require(len({f['name'] for f in fixtures}) == EXPECTED_FIXTURES, 'duplicate fixture names')
    for fixture in fixtures:
        path = corpus / fixture['file']
        require(robustness.sha256_file(path) == fixture['sha256'], 'WAV hash mismatch: ' + fixture['name'])
        require(path.stat().st_size == fixture['bytes'], 'WAV byte size mismatch: ' + fixture['name'])
        with wave.open(str(path)) as wav:
            require((wav.getnframes(), wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) ==
                    (fixture['signal']['frames'], fixture['sampleRate'], fixture['channels'],
                     fixture['bitDepth'] // 8), 'WAV framing mismatch: ' + fixture['name'])
        require(fixture['durationSeconds'] >= gen.MIN_SECONDS, 'short window: ' + fixture['name'])
        require('core' not in fixture['scenarioTags'], 'core tag present')
        require(fixture['trueSilenceSpans'] == [], 'unexpected trueSilenceSpans: ' + fixture['name'])
        if control_of(fixture['name']) == 'noise':
            require(abs(fixture['realisedSnrDb']) < 0.01, 'noise SNR off target')


def manifest_bytes_equal(corpus):
    return (corpus / 'manifest.json').read_bytes() == COMMITTED_MANIFEST.read_bytes()


def verify_pins():
    out = []
    for key, (path, sha) in list(PINS.items()) + list(GEN_PINS.items()):
        actual = robustness.sha256_file(Path(path))
        require(actual == sha, 'pin mismatch %s: %s' % (key, actual))
        out.append({'role': key, 'path': str(path), 'sha256': actual})
    return out


def analyse(results, method_summaries, method_rows, intervals_by_backend, manifest):
    """Pure outcome/comparison computation (used by the runner and by tests)."""
    by_name = {f['name']: f for f in manifest['fixtures']}
    outcomes = []
    comparisons = []
    for name in by_name:
        control, bpm, rate = control_of(name), tempo_of(name), rate_of(name)
        per_backend = {}
        for backend in BACKENDS:
            metric = results[backend][name]
            readiness = None
            if backend == 'btrack-tempo-variant':
                readiness = variant_readiness(method_rows[name], bpm, method_summaries[name])
            record = outcome_for(metric, control, bpm, rate, backend,
                                 intervals_by_backend[backend][name], readiness)
            per_backend[backend] = record
            outcomes.append(record)
        comparisons.append(compare(per_backend['btrack'], per_backend['btrack-tempo-variant']))
    return outcomes, comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    require(Path.cwd() == ROOT, 'run in explicit repository workdir')
    corpus = args.corpus.resolve()
    out = args.out
    require(not out.exists(), 'output exists; choose a new directory: %s' % out)
    manifest = robustness.load_json(corpus / 'manifest.json')
    verify_corpus(corpus, manifest)
    pins = verify_pins()
    out.mkdir(parents=True)
    method_dir = out / 'raw-method'
    method_dir.mkdir()
    commands = []
    results = {}
    diag_beats = {}
    for backend in BACKENDS:
        plugin = paired.PLUGINS[backend]
        run_dir = out / 'raw' / backend / 'block128'
        run_dir.mkdir(parents=True)
        cmd = [paired.CLI, '--corpus', corpus, '--out', run_dir, '--backend', backend,
               '--backend-lib', plugin, '--block', '128']
        env = dict(os.environ)
        env.pop('JAM_TEMPO_VARIANT_LOG_DIR', None)
        if backend == 'btrack-tempo-variant':
            env['JAM_TEMPO_VARIANT_LOG_DIR'] = str(method_dir.resolve())
        paired.execute(cmd, run_dir / 'command-output.txt', env)
        commands.append({'argv': list(map(str, cmd)), 'backend': backend})
        result = robustness.read_backend_results(run_dir / 'results.json')
        require(not result['errors'], '; '.join(result['errors']))
        results[backend] = result['fixturesByName']
        require(set(results[backend]) == {f['name'] for f in manifest['fixtures']}, 'fixture set mismatch')
        for fixture in manifest['fixtures']:
            metric = results[backend][fixture['name']]
            require(metric.get('sourceSha256') == fixture['sha256'], 'audio identity mismatch')
            require(not metric.get('core'), 'long-window clip in core denominator')
        trace_dir = out / 'diagnostic' / backend
        trace_dir.mkdir(parents=True)
        cmd = [paired.DIAG, '--corpus', corpus, '--out', trace_dir, '--backend', backend,
               '--backend-lib', plugin, '--block', '128', '--trace-files', 'beats', '--label', backend]
        if backend == 'btrack-tempo-variant':
            cmd.append('--variant-not-default')
        env.pop('JAM_TEMPO_VARIANT_LOG_DIR', None)
        paired.execute(cmd, trace_dir / 'command-output.txt', env)
        commands.append({'argv': list(map(str, cmd)), 'backend': backend})
        diag_beats[backend] = {}
        for fixture in manifest['fixtures']:
            name = fixture['name']
            path = trace_dir / 'beats' / (name + '.csv')
            beats = paired.read_csv(path)
            require(len(beats) == results[backend][name]['predictedBeats'], 'predicted beat count mismatch')
            diag_beats[backend][name] = (path, beats)
    require({p.name for p in method_dir.iterdir()} == {'instance_0.csv'},
            'current CLI must reuse exactly one method instance')
    raw_method = method_dir / 'instance_0.csv'
    groups = paired.split_method_log(paired.read_csv(raw_method), manifest['fixtures'])
    equality, method_summaries, method_rows, intervals = [], {}, {}, {b: {} for b in BACKENDS}
    for index, fixture in enumerate(manifest['fixtures']):
        name = fixture['name']
        pins_beats = {b: robustness.sha256_file(diag_beats[b][name][0]) for b in BACKENDS}
        require(pins_beats['btrack'] == pins_beats['btrack-tempo-variant'],
                'default/variant beat series not byte-identical: ' + name)
        equality.append({'fixture': name, 'exactDefaultVariantEquality': True, 'sha256': pins_beats})
        rows = groups[index]
        beats_default = diag_beats['btrack'][name][1]
        method_summaries[name] = paired.method_summary(rows, fixture, beats_default)
        method_rows[name] = rows
        for backend in BACKENDS:
            events = [float(b['eventSeconds']) for b in diag_beats[backend][name][1]]
            intervals[backend][name] = interval_summary(events, control_of(name), tempo_of(name))
    with raw_method.open('rb') as src, raw_method.with_suffix('.csv.gz').open('wb') as dst:
        with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0) as compressed:
            compressed.write(src.read())
    raw_sha = robustness.sha256_file(raw_method)
    raw_method.unlink()
    outcomes, comparisons = analyse(results, method_summaries, method_rows, intervals, manifest)
    dump(out / 'outcomes.json', outcomes)
    dump(out / 'comparison.json', comparisons)
    dump(out / 'method-summary.json', [method_summaries[f['name']] for f in manifest['fixtures']])
    dump(out / 'beat-equality.json', equality)
    dump(out / 'intervals.json', intervals)
    dump(out / 'provenance.json', {
        'task': 'TRACK-007', 'protocol': 'docs/research/tempo-long-windows/PROTOCOL.md',
        'protocolCommit': 'committed before fixture render and before any tracker run',
        'diagnosticOnly': True, 'selection': None, 'blockFrames': 128, 'scoring': 'current EVAL-007, uncompensated',
        'pins': pins, 'commands': commands, 'rawMethodLogSha256': raw_sha,
        'rawMethodLogRetainedAs': 'raw-method/instance_0.csv.gz',
        'validation': {'fixtures': EXPECTED_FIXTURES, 'backends': len(BACKENDS),
                       'exactBeatSeriesPairs': len(equality), 'hardErrors': 0},
    })
    (out / 'manifest.json').write_bytes(COMMITTED_MANIFEST.read_bytes())
    files = sorted(p for p in out.rglob('*') if p.is_file())
    (out / 'artifact-hashes.txt').write_text(''.join(
        robustness.sha256_file(p) + '  ' + str(p.relative_to(out)) + '\n' for p in files))
    print('PASS: 16 fixtures x 3 backends; %d exact default/variant beat pairs; %d outcome rows'
          % (len(equality), len(outcomes)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print('run_long_windows: %s' % error, file=sys.stderr)
        sys.exit(2)
