#!/usr/bin/env python3
"""TRACK-006: fixed, pinned paired evidence; run from the repository root.

Current rhythm-eval supplies scores; the pinned diagnostic supplies canonical
beat series. Raw variant logs come from the CURRENT scorer process, not a replay.
No builds, downloads, parameter search, or historical artifact writes.
"""
import argparse
import csv
import gzip
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "robustness", ROOT / "tools/rhythm-eval/tools/run_robustness.py")
robustness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(robustness)
CLI = Path('/home/mojo/projects/build-EVAL-007-integration/cli/rhythm-eval')
DIAG = Path('/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics')
PLUGINS = {
    'btrack': Path('/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so'),
    'btrack-tempo-variant': Path('/home/mojo/projects/build-TRACK-005-integration/libtempo-variant-btrack.so'),
    'aubio': Path('/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def verify_pin(path, expected):
    actual = robustness.sha256_file(path)
    require(actual == expected, f'pin mismatch: {path}: {actual} != {expected}')
    return {'path': str(path), 'sha256': actual}


def verify_inputs(manifest, corpus):
    _, meta, errors = robustness.build_metadata(manifest)
    require(not errors, '; '.join(errors))
    require(len(meta) == 24 and sum(m['kind'] == 'baseline' for m in meta.values()) == 2,
            'expected unchanged derived-24 (22 perturbations + 2 baselines)')
    records = []
    for fixture in manifest['fixtures']:
        path = corpus / fixture['file']
        pin = verify_pin(path, fixture['sha256'])
        require(path.stat().st_size == fixture['bytes'], f'byte size mismatch: {path}')
        with wave.open(str(path)) as wav:
            require((wav.getnframes(), wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) ==
                    (fixture['signal']['frames'], fixture['sampleRate'], fixture['channels'],
                     fixture['bitDepth'] // 8), f'WAV framing mismatch: {path}')
            if fixture['transformation']['kind'] == 'baseline':
                parent = ROOT / 'testdata/rhythm' / fixture['parentFile']
                verify_pin(parent, fixture['parentSha256'])
                with wave.open(str(parent)) as source:
                    window = fixture['truncation']
                    source.setpos(window['sourceStartFrame'])
                    pcm = source.readframes(window['sourceEndFrame'] - window['sourceStartFrame'])
                    require(wav.readframes(wav.getnframes()) == pcm,
                            f'baseline PCM is not the exact parent window: {path}')
        require('core' not in fixture['scenarioTags'], 'derived clip in core denominator')
        records.append(dict(pin, bytes=path.stat().st_size, fixture=fixture['name']))
    return meta, records


def validate_results(path, backend, manifest):
    data = robustness.load_json(path)
    require(data.get('backend') == backend, 'backend identity mismatch')
    require(data.get('blockFrames') == 128 and not data.get('legacyBlockStampedBeats'),
            'missing/wrong framing or legacy stamps')
    variants = data.get('variants', [])
    require(len(variants) == 1 and variants[0].get('label') == 'uncompensated',
            'expected only uncompensated scoring')
    require(data.get('fixtures') == variants[0].get('fixtures'),
            'top-level and uncompensated fixtures disagree')
    result = robustness.read_backend_results(path)
    require(not result['errors'], '; '.join(result['errors']))
    expected = {f['name']: f for f in manifest['fixtures']}
    require(set(result['fixturesByName']) == set(expected), 'fixture set mismatch')
    for name, metric in result['fixturesByName'].items():
        require(metric.get('sourceSha256') == expected[name]['sha256'], 'audio identity mismatch')
        require(not metric.get('core'), 'derived result in core denominator')
        if expected[name]['transformation']['kind'] == 'noise':
            require(metric.get('falseBeatCoverage') == 'NotAssessedStructuralNoise' and
                    not metric.get('falseBeatMetricInformative'), 'stale noise coverage scorer')
    return result


def method_summary(rows, fixture, beats):
    require(len(rows) == math.ceil(fixture['signal']['frames'] / 128), 'incomplete method log')
    emitted = [r for r in rows if r['beatEvent'] == '1']
    require(len(emitted) == len(beats), 'method/diagnostic beat count mismatch')
    for index, row in enumerate(rows):
        require(int(row['blockIndex']) == index, 'missing/reordered method blocks')
        require(row['ready'] in ('0', '1'), 'invalid readiness')
        require((row['intervalMeasured'] == '0') == (row['intervalSeconds'] == ''),
                'missing interval must have empty value')
        if row['ready'] == '0':
            require(row['variantBpm'] == row['baseBpm'], 'fallback differs from base')
        else:
            require(row['ringCount'] == '4', 'ready without four intervals')
    for row, beat in zip(emitted, beats):
        require(row['eventSeconds'] == beat['eventSeconds'] and
                row['blockEndSeconds'] == beat['availabilitySeconds'] and
                row['blockIndex'] == beat['blockIndex'], 'method/diagnostic beat timeline mismatch')
    ready = [r for r in emitted if r['ready'] == '1']
    bpms = [float(r['variantBpm']) for r in ready]
    return {
        'fixture': fixture['name'], 'blocks': len(rows), 'beats': len(emitted),
        'readyBlocks': sum(r['ready'] == '1' for r in rows),
        'fallbackBlocks': sum(r['ready'] == '0' for r in rows),
        'readyBeats': len(ready), 'fallbackBeats': len(emitted) - len(ready),
        'firstReadyEventSeconds': float(ready[0]['eventSeconds']) if ready else None,
        'firstReadyAvailabilitySeconds': float(ready[0]['blockEndSeconds']) if ready else None,
        'readyBpmMin': min(bpms) if bpms else None,
        'readyBpmMedian': statistics.median(bpms) if bpms else None,
        'readyBpmMax': max(bpms) if bpms else None,
        'finalReady': rows[-1]['ready'] == '1',
        'staleReadyNonBeatBlocks': sum(r['ready'] == '1' and r['beatEvent'] == '0' for r in rows),
        'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                 for s in sorted({r['intervalState'] for r in emitted})},
    }


def read_csv(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))


def split_method_log(rows, fixtures):
    """Current CLI creates once and resets per fixture, in manifest order."""
    groups = []
    offset = 0
    for fixture in fixtures:
        count = math.ceil(fixture['signal']['frames'] / 128)
        group = rows[offset:offset + count]
        require(len(group) == count and
                all(int(row['blockIndex']) == i for i, row in enumerate(group)),
                'incomplete/reordered method fixture window')
        groups.append(group)
        offset += count
    require(offset == len(rows), 'extra method blocks')
    return groups


def execute(cmd, log, env=None):
    result = subprocess.run(list(map(str, cmd)), cwd=ROOT, env=env,
                            text=True, capture_output=True)
    log.write_text(result.stdout + result.stderr)
    require(result.returncode == 0, f'command failed ({result.returncode}): {cmd}; see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('docs/research/tempo-variant-robustness'))
    args = parser.parse_args()
    require(Path.cwd() == ROOT, 'run in explicit repository workdir')
    out = args.out
    require(not out.exists(), f'output exists; choose a new directory: {out}')
    manifest_path = Path('testdata/rhythm/derived/manifest.json')
    manifest = robustness.load_json(manifest_path)
    meta, inputs = verify_inputs(manifest, manifest_path.parent)
    historical = robustness.load_json(ROOT / 'docs/research/tempo-variant/provenance.json')
    integration = robustness.load_json(ROOT / 'docs/research/silence-coverage/integration-verification.json')
    variant_integration = robustness.load_json(ROOT / 'docs/research/tempo-variant/integration-verification.json')
    pins = [verify_pin(CLI, integration['cliSha256']),
            verify_pin(DIAG, historical['binaries']['diagnosticCli']['sha256'])]
    for backend, key in [('btrack', 'baselinePlugin'), ('aubio', 'aubioPlugin'),
                         ('btrack-tempo-variant', 'variantPlugin')]:
        pins.append(verify_pin(PLUGINS[backend], historical['binaries'][key]['sha256']))
    for path, sha in integration['sourceHashes'].items():
        pins.append(verify_pin(Path(path), sha))
    for path, sha in historical['freezeHashes']['files'].items():
        pins.append(verify_pin(Path(path), sha))
        require(variant_integration['hashes'][path] == sha, 'variant integration source pin mismatch')
    pins.append({'path': str(manifest_path), 'sha256': robustness.sha256_file(manifest_path)})
    prior_derived = robustness.load_json(ROOT / 'docs/research/robustness/degradation.json')
    verify_pin(manifest_path, prior_derived['generatedFrom']['derivedManifestSha256'])
    for fixture in manifest['fixtures']:
        if fixture['transformation']['kind'] == 'baseline':
            pins.append(verify_pin(ROOT / 'testdata/rhythm' / fixture['parentFile'],
                                   fixture['parentSha256']))
    for path in ('tools/rhythm-eval/tools/run_robustness.py', 'tools/tempo-variant/run_paired.py'):
        pins.append({'path': path, 'sha256': robustness.sha256_file(Path(path))})
    for backend, source in [('btrack', 'BTrackBackend'), ('aubio', 'AubioBackend')]:
        for suffix in ('.h', '.cpp'):
            relative = f'src/{backend}/{source}{suffix}'
            exported = Path('/home/mojo/projects/build-EVAL-005/main-src') / relative
            sha = robustness.sha256_file(exported)
            pins.append(verify_pin(Path(relative), sha))
            pins.append({'path': str(exported), 'sha256': sha,
                         'note': 'read-only EVAL-005 build-source; matches current adapter'})
    for archive in sorted(Path('/home/mojo/projects/build-EVAL-005/main-core').rglob('*.a')):
        pins.append({'path': str(archive), 'sha256': robustness.sha256_file(archive)})
    out.mkdir(parents=True)
    method_dir = out / 'raw-method'
    method_dir.mkdir()
    commands = []
    backends = []
    for backend, plugin in PLUGINS.items():
        run_dir = out / 'raw' / backend / 'block128'
        run_dir.mkdir(parents=True)
        cmd = [CLI, '--corpus', manifest_path.parent, '--out', run_dir,
               '--backend', backend, '--backend-lib', plugin, '--block', '128']
        env = dict(os.environ)
        env.pop('JAM_TEMPO_VARIANT_LOG_DIR', None)
        if backend == 'btrack-tempo-variant':
            env['JAM_TEMPO_VARIANT_LOG_DIR'] = str(method_dir.resolve())
        execute(cmd, run_dir / 'command-output.txt', env)
        commands.append({'argv': list(map(str, cmd)), 'methodLogDirectory': env.get('JAM_TEMPO_VARIANT_LOG_DIR')})
        backends.append(validate_results(run_dir / 'results.json', backend, manifest))
        trace_dir = out / 'diagnostic' / backend
        trace_dir.mkdir(parents=True)
        cmd = [DIAG, '--corpus', manifest_path.parent, '--out', trace_dir,
               '--backend', backend, '--backend-lib', plugin, '--block', '128',
               '--trace-files', 'beats', '--label', backend]
        if backend == 'btrack-tempo-variant':
            cmd.append('--variant-not-default')
        env.pop('JAM_TEMPO_VARIANT_LOG_DIR', None)
        execute(cmd, trace_dir / 'command-output.txt', env)
        commands.append({'argv': list(map(str, cmd))})
    require({p.name for p in method_dir.iterdir()} == {'instance_0.csv'},
            'current CLI must reuse exactly one method instance')
    raw_method_path = method_dir / 'instance_0.csv'
    groups = split_method_log(read_csv(raw_method_path), manifest['fixtures'])
    equality = []
    summaries = []
    method_beats = []
    for index, fixture in enumerate(manifest['fixtures']):
        name = fixture['name']
        beat_pins = {}
        for backend in PLUGINS:
            path = out / 'diagnostic' / backend / 'beats' / (name + '.csv')
            beats = read_csv(path)
            metric = next(b for b in backends if b['backend'] == backend)['fixturesByName'][name]
            require(len(beats) == metric['predictedBeats'], 'CLI/diagnostic predicted count mismatch')
            beat_pins[backend] = robustness.sha256_file(path)
        require(beat_pins['btrack'] == beat_pins['btrack-tempo-variant'], 'default/variant beat-series mismatch')
        equality.append({'fixture': name, 'exactDefaultVariantEquality': True, 'sha256': beat_pins})
        rows = groups[index]
        summaries.append(method_summary(rows, fixture, read_csv(
            out / 'diagnostic/btrack/beats' / (name + '.csv'))))
        method_beats.extend(dict(row, fixture=name) for row in rows if row['beatEvent'] == '1')
    # Lossless raw log, deterministic gzip header. Keep original byte hash.
    with raw_method_path.open('rb') as src, raw_method_path.with_suffix('.csv.gz').open('wb') as dst:
        with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0) as compressed:
            compressed.write(src.read())
    pins.append({'path': str(raw_method_path), 'sha256': robustness.sha256_file(raw_method_path),
                 'retainedAs': str(raw_method_path.with_suffix('.csv.gz'))})
    raw_method_path.unlink()
    dump(out / 'method-summary.json', summaries)
    with (out / 'method-beats.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(method_beats[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(method_beats)
    dump(out / 'beat-equality.json', equality)
    # Reuse existing missing-value, proxy, and noise caveat semantics unchanged.
    rows = robustness.build_rows(meta, backends)
    require(not robustness.deduplicate_check(rows), 'duplicate paired rows')
    robustness.write_csv(rows, out / 'degradation.csv')
    dump(out / 'degradation.json', {'rows': rows, 'caveats': robustness.GLOBAL_CAVEATS,
         'coverage': robustness.compute_coverage(list(meta), backends, rows)})
    metrics = {b['backend']: b['fixturesByName'] for b in backends}
    comparison = []
    for name in meta:
        base, variant = metrics['btrack'][name], metrics['btrack-tempo-variant'][name]
        comparison.append({
            'fixture': name, 'pairedBaseline': meta[name]['pairedBaseline'],
            'perturbation': meta[name]['kind'],
            'defaultVariantDiffExceptCpu': {k: {'default': base[k], 'variant': variant[k]}
                                          for k in base if k != 'cpuSeconds' and base[k] != variant[k]},
            'acquisitionGained': not base['acquired'] and variant['acquired'],
            'acquisitionLost': base['acquired'] and not variant['acquired'],
            'bpmErrorWorsened': (base['hasBpmLock'] and base['hasNominalBpm'] and
                                variant['hasBpmLock'] and variant['hasNominalBpm'] and
                                variant['bpmRelativeError'] > base['bpmRelativeError']),
            'backends': {backend: {
                'acquired': m[name]['acquired'],
                'acquisitionBars': m[name]['acquisitionBars'] if m[name]['acquired'] else None,
                'fMeasure': m[name]['fMeasure'] if m[name]['detectionMeasured'] else None,
                'lockedBpm': m[name]['lockedBpm'] if m[name]['hasBpmLock'] else None,
                'bpmRelativeError': (m[name]['bpmRelativeError'] if m[name]['hasBpmLock'] and
                                     m[name]['hasNominalBpm'] else None),
            } for backend, m in metrics.items()},
        })
    dump(out / 'comparison.json', comparison)
    historical_checks = []
    for backend in ('btrack', 'aubio'):
        previous = robustness.load_json(ROOT / f'docs/research/silence-coverage/raw/derived/{backend}/block128/results.json')
        current = next(b for b in backends if b['backend'] == backend)['fixturesByName']
        for metric in previous['fixtures']:
            prior = {k: v for k, v in metric.items() if k != 'cpuSeconds'}
            now = {k: v for k, v in current[metric['name']].items() if k != 'cpuSeconds'}
            require(prior == now, f'historical EVAL-007 scoring differs: {backend}/{metric["name"]}')
        historical_checks.append({'backend': backend, 'all24FixturesExactExceptCpu': True})
    dump(out / 'provenance.json', {
        'task': 'TRACK-006', 'base': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'diagnosticOnly': True, 'g3': 'OPEN', 'selection': None, 'blockFrames': 128,
        'scoring': 'current EVAL-007, uncompensated', 'pins': pins, 'inputs': inputs,
        'commands': commands, 'historicalScoringChecks': historical_checks,
        'validation': {'fixturesPerBackend': 24, 'perturbations': 22, 'matchedBaselines': 2,
                       'exactBeatSeriesPairs': 24, 'hardErrors': 0},
        'freeze': historical['freezeHashes'], 'caveats': robustness.GLOBAL_CAVEATS,
    })
    files = sorted(p for p in out.rglob('*') if p.is_file())
    (out / 'artifact-hashes.txt').write_text(''.join(
        robustness.sha256_file(p) + '  ' + str(p.relative_to(out)) + '\n' for p in files))
    print(f'PASS: 3 x 24 current scores; 24 exact beat pairs; {len(rows)} paired metric rows')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(f'run_paired: {error}', file=sys.stderr)
        sys.exit(2)
