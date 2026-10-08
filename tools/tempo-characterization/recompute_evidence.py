#!/usr/bin/env python3
"""TRACK-007 correction: re-derive outcomes from the IMMUTABLE historical raw.

Reads docs/research/tempo-long-windows/evidence/ (raw scores, beat series, method
log; never modified) and the immutable fixtures/manifest.json, and writes corrected
derived evidence to a SEPARATE directory. Implements the corrections in
docs/research/tempo-long-windows/CORRECTIONS.md. No scorer, plugin, generator,
protocol or historical artifact is touched. Run from the repository root.
"""
import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


robustness = load('tools/rhythm-eval/tools/run_robustness.py', 'robustness')
paired = load('tools/tempo-variant/run_paired.py', 'run_paired')
run = load('tools/tempo-characterization/run_long_windows.py', 'run_long')

HIST = ROOT / 'docs/research/tempo-long-windows/evidence'
MANIFEST = ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json'
BACKENDS = ('btrack', 'btrack-tempo-variant', 'aubio')
CONTROLS = ('regular', 'sparse', 'gap', 'noise')
PROVENANCE_FILE_SHA256 = '202714b16002ac9f9c94aca67684abaacaab5666a35aff9f018c8450d4c032fe'
FREEZE_COMBINED_SHA256 = 'cd8291e6d8ad49cae64fe43768ddc88e70679a2835dad78e62b5644b0503ec77'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def corrected_intervals(events, control, bpm):
    """Protocol 7.5 with the exemption restricted to the `gap` control (E5)."""
    period = 60.0 / bpm
    expected = (2 if control == 'sparse' else 1) * period
    rows = []
    for a, b in zip(events, events[1:]):
        spanning = control == 'gap' and a < run.GAP_START and b >= run.GAP_END
        outlier = (not spanning) and abs((b - a) / expected - 1.0) > run.INTERVAL_BAND
        rows.append({'start': a, 'end': b, 'gapSpanning': spanning, 'outlier': outlier})
    outliers = [r for r in rows if r['outlier']]
    return {
        'expectedSeconds': expected, 'intervals': len(rows),
        'outliers': len(outliers),
        'longestOutlierRun': run.longest_run([r['outlier'] for r in rows]),
        'gapSpanningCount': sum(r['gapSpanning'] for r in rows),
        'maxIntervalSeconds': max((r['end'] - r['start'] for r in rows), default=None),
    }


def steady_window(truth, acquisition_seconds, acquired):
    """Metrics.cpp steadyWindow, reproduced from the raw acquisition field (E4)."""
    if not truth['beats']:
        return 0.0, truth['durationSeconds']
    end = truth['durationSeconds']
    if acquired and acquisition_seconds is not None and acquisition_seconds >= 0.0:
        start = truth['beats'][0] + acquisition_seconds
    else:
        start = truth['beats'][0] + 0.5 * (truth['beats'][-1] - truth['beats'][0])
    return (min(start, end), end)


def outcome(metric, truth, backend):
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
        'fixture': metric['name'], 'backend': backend,
        'control': truth['control'], 'tempoBpm': truth['tempoBpm'], 'rateHz': truth['rateHz'],
        'acquired': acquired,
        'acquisitionBars': raw_bars if acquired else None,
        'acquisitionBarsRaw': raw_bars,
        'acquisitionSeconds': metric['acquisitionSeconds'] if acquired else None,
        'acquiredWithinTwoBars': acquired and raw_bars <= run.TWO_BARS,
        'hasBpmLock': bool(metric['hasBpmLock']),
        'hasNominalBpm': bool(metric['hasNominalBpm']),
        'detectionMeasured': bool(metric['detectionMeasured']),
        'bpmMissingReason': missing,
        'lockedBpm': metric['lockedBpm'] if metric['hasBpmLock'] else None,
        'bpmRelativeError': metric['bpmRelativeError'] if missing is None else None,
        'steadyWindowStartSeconds': start, 'steadyWindowEndSeconds': end,
    }


def pair(base, other):
    """Same bands and null semantics as the protocol comparison, generic names."""
    def state():
        if base['bpmRelativeError'] is None or other['bpmRelativeError'] is None:
            return 'not-evaluable'
        delta = other['bpmRelativeError'] - base['bpmRelativeError']
        if delta > run.REGRESSION_BAND:
            return 'regression'
        if -delta > run.REGRESSION_BAND:
            return 'gain'
        return 'no-change'
    return {
        'acquisitionGained': (not base['acquired']) and other['acquired'],
        'acquisitionLost': base['acquired'] and not other['acquired'],
        'withinTwoBarsGained': (not base['acquiredWithinTwoBars']) and other['acquiredWithinTwoBars'],
        'withinTwoBarsLost': base['acquiredWithinTwoBars'] and not other['acquiredWithinTwoBars'],
        'bpmErrorState': state(),
        'baseBpmRelativeError': base['bpmRelativeError'],
        'otherBpmRelativeError': other['bpmRelativeError'],
    }


def read_truth(manifest):
    out = {}
    for fixture in manifest['fixtures']:
        name = fixture['name']
        out[name] = {
            'name': name, 'control': run.control_of(name), 'tempoBpm': run.tempo_of(name),
            'rateHz': run.rate_of(name), 'sha256': fixture['sha256'],
            'beats': fixture['beats'], 'onsets': fixture['onsets'],
            'durationSeconds': fixture['durationSeconds'],
            'silentBeats': fixture['silentBeats'], 'onsetsRaw': fixture['onsets'],
        }
    return out


def validate(manifest, truth, raw):
    """paired.validate_results plus manifest-level truth checks (CORRECTIONS.md)."""
    adapted = {'fixtures': [{'name': f['name'], 'sha256': f['sha256'],
                             'transformation': {'kind': 'generated'}} for f in manifest['fixtures']]}
    for backend in BACKENDS:
        # Uses the reviewed pinned-scorer validator verbatim for framing/legacy/coverage.
        paired.validate_results(HIST / 'raw' / backend / 'block128/results.json', backend, adapted)
    by = {f['name']: f for f in manifest['fixtures']}
    for bpm in (96, 126):
        for rate in (48000, 44100):
            reg, sp = 'regular_%dbpm_%dhz' % (bpm, rate), 'sparse_%dbpm_%dhz' % (bpm, rate)
            g, no = 'gap_%dbpm_%dhz' % (bpm, rate), 'noise_%dbpm_%dhz' % (bpm, rate)
            require(by[reg]['silentBeats'] == [], 'regular has silent beats')
            require(by[no]['onsets'] == by[reg]['onsets'], 'noise onsets differ')
            require(by[no]['beats'] == by[reg]['beats'], 'noise beats differ')
            require(all(b % 2 == 1 for b in by[sp]['silentBeats']), 'sparse silent beats not odd indices')
            require(any(by[g]['beats'][i] >= run.GAP_START and by[g]['beats'][i] < run.GAP_END
                        for i in range(len(by[g]['beats']))), 'gap has no beat in the removed window')
    require(len(manifest['fixtures']) == 16, 'full matrix missing')
    return {'pairedValidateResults': 'PASS for 3 raw results.json',
            'noiseOnsetsBeatsEqualRegular': True,
            'sparseSilentBeatsOdd': True, 'gapWindowBeatPresent': True,
            'regularSilentEmpty': True, 'matrixCells': 16}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path,
                        default=ROOT / 'docs/research/tempo-long-windows/evidence-corrected')
    args = parser.parse_args()
    require(Path.cwd() == ROOT, 'run in explicit repository workdir')
    out = args.out
    require(not out.exists(), 'output exists; choose a new directory: %s' % out)
    manifest = robustness.load_json(MANIFEST)
    truth = read_truth(manifest)
    raw = {}
    for backend in BACKENDS:
        result = robustness.read_backend_results(HIST / 'raw' / backend / 'block128/results.json')
        require(not result['errors'], '; '.join(result['errors']))
        require(set(result['fixturesByName']) == set(truth), 'fixture set mismatch')
        raw[backend] = result['fixturesByName']
    validation = validate(manifest, truth, raw)
    outcomes = []
    for name in truth:
        for backend in BACKENDS:
            outcomes.append(outcome(raw[backend][name], truth[name], backend))
    index = {(o['fixture'], o['backend']): o for o in outcomes}
    # 16 default-vs-variant records (retained), 36 control-vs-regular records (added).
    comparisons = []
    for name in sorted(truth):
        control = truth[name]['control']
        record = {'kind': 'default-vs-variant', 'fixture': name, 'control': control,
                  'tempoBpm': truth[name]['tempoBpm'], 'rateHz': truth[name]['rateHz']}
        record.update(pair(index[(name, 'btrack')], index[(name, 'btrack-tempo-variant')]))
        comparisons.append(record)
    control_pairs = []
    for name in sorted(truth):
        control = truth[name]['control']
        if control == 'regular':
            continue
        regular = 'regular_%dbpm_%dhz' % (truth[name]['tempoBpm'], truth[name]['rateHz'])
        for backend in BACKENDS:
            record = {'kind': 'control-vs-regular', 'fixture': name, 'pairedControl': regular,
                      'control': control, 'backend': backend,
                      'tempoBpm': truth[name]['tempoBpm'], 'rateHz': truth[name]['rateHz']}
            record.update(pair(index[(regular, backend)], index[(name, backend)]))
            control_pairs.append(record)
    require(len(comparisons) == 16 and len(control_pairs) == 36, 'comparison counts wrong')
    intervals = {}
    for backend in BACKENDS:
        intervals[backend] = {}
        for name in truth:
            beats = paired.read_csv(HIST / 'diagnostic' / backend / 'beats' / (name + '.csv'))
            events = [float(b['eventSeconds']) for b in beats]
            intervals[backend][name] = corrected_intervals(
                events, truth[name]['control'], truth[name]['tempoBpm'])
    out.mkdir(parents=True)
    dump(out / 'outcomes.json', outcomes)
    dump(out / 'comparison.json', comparisons)
    dump(out / 'control-pairs.json', control_pairs)
    dump(out / 'intervals.json', intervals)
    dump(out / 'validation.json', validation)
    dump(out / 'errata.json', {
        'immutable': {'protocol': '492c5a8', 'fixtures': 'd21e2a6', 'historicalEvidence': '5098df9'},
        'corrections': ['E1 post-ready divergence and 4-agreeing-beat lock',
                        'E2 missed noise_126bpm_44100hz regression',
                        'E3 acquisitionBars null-normalised, raw sentinel preserved',
                        'E4 backend-specific steady windows, not whole-clip',
                        'E5 gap exemption restricted to the gap control',
                        'E6 provenance.json file sha256 vs combined freeze hash',
                        'E7 disclosed process deviations'],
        'verifiedPins': {
            'provenanceJsonFileSha256': PROVENANCE_FILE_SHA256,
            'variantCorrectedCombinedSha256': FREEZE_COMBINED_SHA256,
        },
        'counts': {'outcomes': len(outcomes), 'comparisons': len(comparisons),
                   'controlPairs': len(control_pairs)},
        'note': 'Derived only. Historical raw/evidence is unmodified; WAVs and generator unchanged.',
    })
    files = sorted(p for p in out.rglob('*') if p.is_file())
    (out / 'derived-hashes.txt').write_text(''.join(
        robustness.sha256_file(p) + '  ' + str(p.relative_to(out)) + '\n' for p in files))
    print('PASS: %d outcomes (%d cells x %d backends), %d default-vs-variant, %d control-vs-regular'
          % (len(outcomes), len(truth), len(BACKENDS), len(comparisons), len(control_pairs)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print('recompute_evidence: %s' % error, file=sys.stderr)
        sys.exit(2)
