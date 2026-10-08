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
import csv
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
CORRECTED = ROOT / 'docs/research/tempo-stability/evidence-corrected'
TOOLING = ROOT / 'docs/research/tempo-stability/tooling-hashes.json'
ERRATA = ROOT / 'docs/research/tempo-stability/ERRATA.md'
LOG_DIR = EVID / 'method-raw'

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


# --- independent references for the correction (ERRATA E2/E4) --------------

def out_of_band(values, nominal):
    """(count, longestRun, measured). Unmeasured when there is no nominal BPM."""
    if nominal is None or nominal <= 0.0:
        return None, None, False
    flags = [abs(v / nominal - 1.0) > PERSIST_BAND for v in values]
    return sum(flags), longest_run(flags), True


def read_gz_csv(path):
    with gzip.open(path, 'rt') as stream:
        return list(csv.DictReader(stream))


def recompute_summary(raw_logs, manifests):
    """Independent re-derivation of the corrected method-summary from gz logs."""
    out = {}
    for (label, backend), path in raw_logs.items():
        rows = read_gz_csv(path)
        records = []
        offset = 0
        for fixture in manifests[label]['fixtures']:
            count = ceil_blocks(fixture['signal']['frames'])
            window = rows[offset:offset + count]
            assert len(window) == count
            offset += count
            emitted = [r for r in window if r['beatEvent'] == '1']
            nominal = fixture.get('nominalBpm')
            if backend == 'btrack-tempo-stable':
                active = [r for r in emitted if r['confirmed'] == '1']
                values = [float(r['emittedBpm']) for r in active]
                count_ob, run_ob, measured = out_of_band(values, nominal)
                records.append({
                    'fixture': fixture['name'], 'blocks': len(window), 'beats': len(emitted),
                    'fallbackBeats': sum(r['confirmed'] == '0' for r in emitted),
                    'confirmedBeats': len(active),
                    'fallbackBlocks': sum(r['confirmed'] == '0' for r in window),
                    'confirmedBlocks': sum(r['confirmed'] == '1' for r in window),
                    'firstConfirmedEventSeconds':
                        float(active[0]['eventSeconds']) if active else None,
                    'firstConfirmedAvailabilitySeconds':
                        float(active[0]['blockEndSeconds']) if active else None,
                    'confirmedBpmMin': min(values) if values else None,
                    'confirmedBpmMedian': statistics.median(values) if values else None,
                    'confirmedBpmMax': max(values) if values else None,
                    'confirmedBeatsOutOfBand': count_ob,
                    'confirmedBeatsOutOfBandMeasured': measured,
                    'longestOutOfBandConfirmedRun': run_ob,
                    'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                              for s in sorted({r['intervalState'] for r in emitted})},
                })
            else:
                active = [r for r in emitted if r['ready'] == '1']
                values = [float(r['variantBpm']) for r in active]
                count_ob, run_ob, measured = out_of_band(values, nominal)
                records.append({
                    'fixture': fixture['name'], 'blocks': len(window), 'beats': len(emitted),
                    'fallbackBeats': sum(r['ready'] == '0' for r in emitted),
                    'readyBeats': len(active),
                    'firstReadyEventSeconds': float(active[0]['eventSeconds']) if active else None,
                    'firstReadyAvailabilitySeconds':
                        float(active[0]['blockEndSeconds']) if active else None,
                    'readyBpmMin': min(values) if values else None,
                    'readyBpmMedian': statistics.median(values) if values else None,
                    'readyBpmMax': max(values) if values else None,
                    'readyBeatsOutOfBand': count_ob,
                    'readyBeatsOutOfBandMeasured': measured,
                    'longestOutOfBandReadyRun': run_ob,
                    'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                              for s in sorted({r['intervalState'] for r in emitted})},
                })
        out.setdefault(backend, []).extend(records)
        assert offset == len(rows), 'extra method rows for %s/%s' % (label, backend)
    return out


def interval_ref(events, control, bpm, long_gap):
    if bpm is None or bpm <= 0.0:
        return {'expectedSeconds': None, 'intervals': max(0, len(events) - 1),
                'outliers': None, 'longestOutlierRun': None, 'gapSpanningCount': 0,
                'maxIntervalSeconds': max((b - a for a, b in zip(events, events[1:])), default=None),
                'firstOutlierStart': None}
    expected = (2 if control == 'sparse' else 1) * (60.0 / bpm)
    outliers = []
    spans = 0
    for a, b in zip(events, events[1:]):
        spanning = long_gap and control == 'gap' and a < GAP_START and b >= GAP_END
        spans += 1 if spanning else 0
        outliers.append((not spanning) and abs((b - a) / expected - 1.0) > INTERVAL_BAND)
    flagged = [i for i, o in enumerate(outliers) if o]
    starts = list(zip(events, events[1:]))
    return {'expectedSeconds': expected, 'intervals': len(outliers),
            'outliers': len(flagged), 'longestOutlierRun': longest_run(outliers),
            'gapSpanningCount': spans,
            'maxIntervalSeconds': max((b - a for a, b in starts), default=None),
            'firstOutlierStart': starts[flagged[0]][0] if flagged else None}


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

    def test_corrected_method_summary_matches_independent_rederivation(self):
        raw_logs = {
            ('short', 'btrack-tempo-stable'): LOG_DIR / 'short-btrack-tempo-stable.csv.gz',
            ('long', 'btrack-tempo-stable'): LOG_DIR / 'long-btrack-tempo-stable.csv.gz',
            ('short', 'btrack-tempo-variant'): LOG_DIR / 'short-btrack-tempo-variant.csv.gz',
            ('long', 'btrack-tempo-variant'): LOG_DIR / 'long-btrack-tempo-variant.csv.gz',
        }
        corrected = load_json(CORRECTED / 'method-summary.json')
        self.assertEqual(corrected, recompute_summary(raw_logs, self.manifests))

    def test_historical_method_summary_old_semantics_authenticated(self):
        # The historical artifact is preserved with the pre-correction semantics:
        # the two short tempo_step fixtures carry out-of-band 0, not null.
        hist = load_json(EVID / 'method-summary.json')
        for backend, ob, run in (('btrack-tempo-stable', 'confirmedBeatsOutOfBand',
                                  'longestOutOfBandConfirmedRun'),
                                 ('btrack-tempo-variant', 'readyBeatsOutOfBand',
                                  'longestOutOfBandReadyRun')):
            for rec in hist[backend]:
                if 'tempo_step' in rec['fixture']:
                    self.assertEqual(rec[ob], 0, 'historical old semantics expected 0')
                    self.assertEqual(rec[run], 0)
                    self.assertNotIn(ob + 'Measured', rec)
        # And the historical file is still the one hashed by artifact-hashes.txt.
        listing = (EVID / 'artifact-hashes.txt').read_text().splitlines()
        entries = {rel: digest for digest, rel in (line.split('  ', 1) for line in listing)}
        self.assertEqual(entries['method-summary.json'], sha(EVID / 'method-summary.json'))

    def test_corrected_nominal_missing_is_null_adversarial(self):
        corrected = load_json(CORRECTED / 'method-summary.json')
        for backend, ob, meas in (('btrack-tempo-stable', 'confirmedBeatsOutOfBand',
                                  'confirmedBeatsOutOfBandMeasured'),
                                  ('btrack-tempo-variant', 'readyBeatsOutOfBand',
                                   'readyBeatsOutOfBandMeasured')):
            for rec in corrected[backend]:
                if 'tempo_step' in rec['fixture']:
                    self.assertIsNone(rec[ob])
                    self.assertFalse(rec[meas])
                else:
                    self.assertIsInstance(rec[ob], int)
                    self.assertTrue(rec[meas])
        # Adversarial reference: no nominal is unmeasured, never a measured zero.
        self.assertEqual(out_of_band([], None), (None, None, False))
        self.assertEqual(out_of_band([], 0.0), (None, None, False))
        self.assertEqual(out_of_band([126.0, 120.0], 126.0), (1, 1, True))

    def test_intervals_match_independent_rederivation(self):
        committed = load_json(EVID / 'intervals.json')
        for label, manifest in self.manifests.items():
            by_name = {f['name']: f for f in manifest['fixtures']}
            long_gap = label == 'long'
            for backend in BACKENDS:
                for fixture in manifest['fixtures']:
                    name = fixture['name']
                    beats = read_csv(EVID / 'diagnostic' / label / backend / 'beats' / (name + '.csv'))
                    events = [float(b['eventSeconds']) for b in beats]
                    if long_gap:
                        control = name.split('_')[0]
                        bpm = float(name.split('_')[1].replace('bpm', ''))
                    else:
                        control = fixture['transformation']['kind']
                        bpm = fixture.get('nominalBpm')
                        if not bpm:
                            bpm = by_name[fixture['pairedBaseline']].get('nominalBpm')
                    ref = interval_ref(events, control, bpm, long_gap)
                    ref['referenceNominalSource'] = ('fixture-nominal' if fixture.get('nominalBpm')
                                                     or long_gap else 'paired-baseline-nominal')
                    self.assertEqual(committed[label][backend][name], ref,
                                     'interval mismatch %s/%s/%s' % (label, backend, name))

    def test_intervals_adversarial_missing_nominal(self):
        empty = interval_ref([1.0, 2.0, 3.0], 'regular', None, False)
        self.assertIsNone(empty['expectedSeconds'])
        self.assertIsNone(empty['outliers'])
        self.assertIsNone(empty['longestOutlierRun'])
        self.assertIsNone(empty['firstOutlierStart'])
        measured = interval_ref([1.0, 2.0, 3.0], 'regular', 60.0, False)
        self.assertIsNotNone(measured['expectedSeconds'])
        self.assertEqual(measured['outliers'], 0)

    def test_tooling_hashes_authenticated(self):
        payload = load_json(TOOLING)
        self.assertTrue(payload['evaluation'])
        for entry in payload['evaluation']:
            self.assertEqual(sha(ROOT / entry['path']), entry['sha256'],
                             'current tooling changed: ' + entry['path'])
        # The historical FREEZE evaluation snapshot is explicitly not this list.
        freeze = load_json(FREEZE)
        self.assertFalse({e['path'] for e in payload['evaluation']}
                         == {e['path'] for e in freeze['evaluation']})

    def test_corrected_derived_hashes_authenticated(self):
        derived = load_json(CORRECTED / 'derived-hashes.txt')
        self.assertEqual(derived['corrected']['method-summary.json'],
                         sha(CORRECTED / 'method-summary.json'))
        for rel, digest in derived['inputs'].items():
            self.assertEqual(sha(ROOT / rel), digest, 'raw input changed: ' + rel)
        self.assertEqual(derived['semantics'],
                         'null + ...Measured=false when the fixture declares no nominalBpm')

    def test_errata_contract_present(self):
        text = ERRATA.read_text()
        for marker in ('E1', 'E2', 'E3', 'E4', 'E5', 'immutable', 'not rewritten'):
            self.assertIn(marker, text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
