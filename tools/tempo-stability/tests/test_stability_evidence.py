#!/usr/bin/env python3
"""TRACK-008 evidence authentication and independent re-derivation.

Re-derives the committed outcomes, default-vs-backend comparisons, perturbation
control pairs and BTrack-family beat equality DIRECTLY from the immutable raw
scorer results and the committed corpus manifests, and compares them to the
committed derived evidence. Also verifies the candidate freeze, every declared
artifact hash, and the validation counts. Run after the frozen candidate has been
scored:
  python3 tools/tempo-stability/tests/test_stability_evidence.py
"""
import gzip
import hashlib
import json
import math
import statistics
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EVID = ROOT / 'docs/research/tempo-stability/evidence'
FREEZE = ROOT / 'docs/research/tempo-stability/FREEZE.json'

import importlib.util as _ilu
_spec = _ilu.spec_from_file_location('robustness', ROOT / 'tools/rhythm-eval/tools/run_robustness.py')
robustness = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(robustness)

BACKENDS = ('btrack', 'btrack-tempo-variant', 'btrack-tempo-stable', 'aubio')
BTACK_FAMILY = ('btrack', 'btrack-tempo-variant', 'btrack-tempo-stable')
TWO_BARS = 2.0
REGRESSION_BAND = 0.005
PERSIST_BAND = 0.02
INTERVAL_BAND = 0.10
BLOCK = 128
GAP_START, GAP_END = 16.0, 17.0

CORPORA = {
    'short': ROOT / 'testdata/rhythm/derived/manifest.json',
    'long': ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json',
}
RAW = {'short': 'testdata/rhythm/derived/manifest.json',
       'long': 'docs/research/tempo-long-windows/fixtures/manifest.json'}


def sha(path):
    return robustness.sha256_file(Path(path))


def ceil_blocks(frames):
    return math.ceil(frames / BLOCK)


def load_json(path):
    return robustness.load_json(Path(path))


def read_csv(path):
    import csv
    with Path(path).open(newline='') as s:
        return list(csv.DictReader(s))


def longest_run(flags):
    best = run = 0
    for f in flags:
        run = run + 1 if f else 0
        best = max(best, run)
    return best


def steady_window(truth, acq_seconds, acquired):
    beats = truth['beats']
    end = truth.get('durationSeconds') or beats[-1]
    if acquired and acq_seconds is not None and acq_seconds >= 0.0:
        start = beats[0] + acq_seconds
    else:
        start = beats[0] + 0.5 * (beats[-1] - beats[0])
    return (min(start, end), end)


def derive_outcome(metric, truth, backend, label, nominal):
    acquired = bool(metric['acquired'])
    raw_bars = metric['acquisitionBars']
    start, end = steady_window(truth, metric.get('acquisitionSeconds'), acquired)
    missing = (None if (metric['hasBpmLock'] and metric['hasNominalBpm'])
               else ('no-bpm-lock' if not metric['hasBpmLock'] else 'no-nominal-bpm'))
    return {
        'corpus': label, 'fixture': metric['name'], 'backend': backend,
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


def derive_pair(base, other):
    if base['bpmRelativeError'] is None or other['bpmRelativeError'] is None:
        state = 'not-evaluable'
    else:
        delta = other['bpmRelativeError'] - base['bpmRelativeError']
        state = ('regression' if delta > REGRESSION_BAND
                 else ('gain' if -delta > REGRESSION_BAND else 'no-change'))
    return {
        'acquisitionGained': (not base['acquired']) and other['acquired'],
        'acquisitionLost': base['acquired'] and not other['acquired'],
        'withinTwoBarsGained': (not base['acquiredWithinTwoBars']) and other['acquiredWithinTwoBars'],
        'withinTwoBarsLost': base['acquiredWithinTwoBars'] and not other['acquiredWithinTwoBars'],
        'bpmErrorState': state,
        'baseAcquisitionBars': base['acquisitionBars'],
        'otherAcquisitionBars': other['acquisitionBars'],
        'baseBpmRelativeError': base['bpmRelativeError'],
        'otherBpmRelativeError': other['bpmRelativeError'],
    }


class TestEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not EVID.exists():
            raise unittest.SkipTest('evidence not generated yet')
        cls.manifests = {label: load_json(path) for label, path in CORPORA.items()}
        cls.truth = {label: {f['name']: f for f in m['fixtures']}
                     for label, m in cls.manifests.items()}
        cls.raw = {}
        for label in CORPORA:
            for backend in BACKENDS:
                p = EVID / 'raw' / ('%s-%s' % (label, backend)) / 'results.json'
                result = robustness.read_backend_results(p)
                assert not result['errors'], result['errors']
                cls.raw[(label, backend)] = result['fixturesByName']

    def test_freeze_matches_sources_and_binary(self):
        freeze = load_json(FREEZE)
        for entry in freeze['sources']:
            self.assertEqual(sha(ROOT / entry['path']), entry['sha256'],
                             'source changed: ' + entry['path'])
        self.assertEqual(sha(freeze['binary']['path']), freeze['binary']['sha256'])
        for dep in freeze['binary']['embeddedDependencies']:
            self.assertEqual(sha(dep['path']), dep['sha256'], 'embedded dep changed')

    def test_artifact_hashes_are_exact(self):
        listing = EVID / 'artifact-hashes.txt'
        self.assertTrue(listing.exists())
        for line in listing.read_text().splitlines():
            digest, rel = line.split('  ', 1)
            self.assertEqual(sha(EVID / rel), digest, 'artifact changed: ' + rel)

    def test_outcomes_match_independent_rederivation(self):
        committed = load_json(EVID / 'outcomes.json')
        rederived = []
        for label in CORPORA:
            for f in self.manifests[label]['fixtures']:
                nominal = f.get('nominalBpm') or 0.0
                for backend in BACKENDS:
                    metric = self.raw[(label, backend)][f['name']]
                    rederived.append(derive_outcome(metric, f, backend, label, nominal))
        self.assertEqual(committed, rederived, 'outcomes differ from re-derivation')

    def test_comparisons_match_independent_rederivation(self):
        index = {}
        for label in CORPORA:
            for f in self.manifests[label]['fixtures']:
                nominal = f.get('nominalBpm') or 0.0
                for backend in BACKENDS:
                    metric = self.raw[(label, backend)][f['name']]
                    index[(label, f['name'], backend)] = derive_outcome(
                        metric, f, backend, label, nominal)
        comparisons = []
        for label in CORPORA:
            for f in self.manifests[label]['fixtures']:
                for other in ('btrack-tempo-variant', 'btrack-tempo-stable', 'aubio'):
                    rec = {'kind': 'default-vs-%s' % other, 'corpus': label,
                           'fixture': f['name'], 'otherBackend': other}
                    rec.update(derive_pair(index[(label, f['name'], 'btrack')],
                                           index[(label, f['name'], other)]))
                    comparisons.append(rec)
        self.assertEqual(load_json(EVID / 'comparison.json'), comparisons)

    def test_control_pairs_match_independent_rederivation(self):
        index = {}
        for label in CORPORA:
            for f in self.manifests[label]['fixtures']:
                nominal = f.get('nominalBpm') or 0.0
                for backend in BACKENDS:
                    metric = self.raw[(label, backend)][f['name']]
                    index[(label, f['name'], backend)] = derive_outcome(
                        metric, f, backend, label, nominal)
        pairs = []
        for label in CORPORA:
            for f in self.manifests[label]['fixtures']:
                name = f['name']
                if label == 'short':
                    base_name = f.get('pairedBaseline')
                    if not base_name or base_name == name:
                        continue
                    control = f['transformation']['kind']
                    kind = 'perturbation-vs-baseline'
                else:
                    control = name.split('_')[0]
                    if control == 'regular':
                        continue
                    parts = name.split('_')
                    base_name = 'regular_%s_%s' % (parts[1], parts[2])
                    kind = 'control-vs-regular'
                for backend in BACKENDS:
                    rec = {'kind': kind, 'corpus': label, 'fixture': name,
                           'pairedBaseline': base_name, 'control': control,
                           'backend': backend}
                    rec.update(derive_pair(index[(label, base_name, backend)],
                                           index[(label, name, backend)]))
                    pairs.append(rec)
        self.assertEqual(load_json(EVID / 'control-pairs.json'), pairs)

    def test_beat_equality_is_exact(self):
        committed = load_json(EVID / 'beat-equality.json')
        self.assertEqual(len(committed),
                         sum(len(m['fixtures']) for m in self.manifests.values()))
        for entry in committed:
            label, name = entry['corpus'], entry['fixture']
            digests = {}
            for backend in BTACK_FAMILY:
                p = EVID / 'diagnostic' / label / backend / 'beats' / (name + '.csv')
                digests[backend] = sha(p)
            self.assertEqual(len(set(digests.values())), 1, 'beat series differ: ' + name)
            self.assertEqual(digests, entry['sha256'])
            self.assertEqual(sha(EVID / 'diagnostic' / label / 'aubio' / 'beats' / (name + '.csv')),
                             entry['aubioSha256'])

    def test_validation_counts(self):
        v = load_json(EVID / 'validation.json')
        self.assertEqual(v['shortFixtures'], 24)
        self.assertEqual(v['longFixtures'], 16)
        self.assertEqual(v['backends'], 4)
        self.assertEqual(v['outcomes'], 40 * 4)
        self.assertTrue(v['beatEqualityAll'])
        self.assertEqual(v['hardErrors'], 0)

    def test_method_raw_logs_authenticated_and_consistent(self):
        prov = load_json(EVID / 'provenance.json')
        for key, entry in prov['methodRawLogs'].items():
            gz = ROOT / entry['retainedAs']
            self.assertEqual(sha(gz), entry['sha256'], 'method log changed: ' + key)
            with gzip.open(gz, 'rt') as s:
                rows = list(__import__('csv').DictReader(s))
            label, backend = key.split('/')
            fixtures = self.manifests[label]['fixtures']
            expected = sum(ceil_blocks(f['signal']['frames']) for f in fixtures)
            self.assertEqual(len(rows), expected, 'method log block count: ' + key)
            offset = 0
            for fixture in fixtures:
                count = ceil_blocks(fixture['signal']['frames'])
                window = rows[offset:offset + count]
                for i, row in enumerate(window):
                    self.assertEqual(int(row['blockIndex']), i)
                    if backend == 'btrack-tempo-stable':
                        self.assertEqual((row['intervalMeasured'] == '0'),
                                         (row['intervalSeconds'] == ''))
                        if row['confirmed'] == '0':
                            self.assertEqual(row['emittedBpm'], row['baseBpm'])
                offset += count
            self.assertEqual(offset, len(rows))


if __name__ == '__main__':
    unittest.main(verbosity=2)
