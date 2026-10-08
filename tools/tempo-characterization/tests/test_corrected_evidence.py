#!/usr/bin/env python3
"""TRACK-007 corrections: independently re-derive corrected claims from immutable raw.

The re-derivation tests read the historical raw (`evidence/`) and recompute the
corrected fields with their own code, then compare against the committed corrected
tree — so a bug shared with the correction tool cannot hide a wrong result. The
adversarial interval test additionally exercises the corrected rule as direct
function cases (it does call `recompute_evidence.corrected_intervals`), and the
failure tests call the reviewed `paired.validate_results`. Immutability, comparison
and validation tests read the committed artifacts directly. Run from the repository
root.
"""
import hashlib
import importlib.util
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HIST = ROOT / 'docs/research/tempo-long-windows/evidence'
CORR = ROOT / 'docs/research/tempo-long-windows/evidence-corrected'
FIX = ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json'
BACKENDS = ('btrack', 'btrack-tempo-variant', 'aubio')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


paired = load('tools/tempo-variant/run_paired.py', 'run_paired')
robustness = load('tools/rhythm-eval/tools/run_robustness.py', 'robustness')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def loadc(name):
    return json.loads((CORR / name).read_text())


def raw_metric(backend, name):
    result = robustness.read_backend_results(HIST / 'raw' / backend / 'block128/results.json')
    return result['fixturesByName'][name]


def steady_window(truth, acquired, acquisition_seconds):
    beats = truth['beats']
    end = truth['durationSeconds']
    if acquired and acquisition_seconds is not None and acquisition_seconds >= 0.0:
        start = beats[0] + acquisition_seconds
    else:
        start = beats[0] + 0.5 * (beats[-1] - beats[0])
    return min(start, end), end


class HistoricalImmutabilityTests(unittest.TestCase):
    def test_historical_evidence_still_matches_its_recorded_hashes(self):
        recorded = {}
        for line in (HIST / 'artifact-hashes.txt').read_text().splitlines():
            digest, name = line.split('  ', 1)
            recorded[name] = digest
        for name, digest in recorded.items():
            self.assertEqual(sha(HIST / name), digest, name)

    def test_immutable_commits_are_still_ancestors(self):
        import subprocess
        for commit in ('492c5a8', 'd21e2a6', '5098df9'):
            out = subprocess.run(['git', 'merge-base', '--is-ancestor', commit, 'HEAD'],
                                 cwd=ROOT).returncode
            self.assertEqual(out, 0, commit)

    def test_corrected_tree_is_separate_and_derived_only(self):
        names = {p.name for p in CORR.iterdir() if p.is_file()}
        self.assertEqual(names, {'outcomes.json', 'comparison.json', 'control-pairs.json',
                                 'intervals.json', 'validation.json', 'errata.json',
                                 'derived-hashes.txt'})
        self.assertNotIn('manifest.json', names, 'must not duplicate fixture identity')


class IndependentReDerivationTests(unittest.TestCase):
    def setUp(self):
        self.truth = {f['name']: f for f in json.loads(FIX.read_text())['fixtures']}
        self.corrected = {(o['fixture'], o['backend']): o for o in loadc('outcomes.json')}

    def test_every_outcome_field_re_derives_from_raw(self):
        for (name, backend), corrected in self.corrected.items():
            metric = raw_metric(backend, name)
            truth = self.truth[name]
            acquired = bool(metric['acquired'])
            raw_bars = metric['acquisitionBars']
            self.assertEqual(corrected['acquired'], acquired, name)
            self.assertEqual(corrected['acquisitionBars'], raw_bars if acquired else None, name)
            self.assertEqual(corrected['acquisitionBarsRaw'], raw_bars, name)
            self.assertEqual(corrected['acquiredWithinTwoBars'], acquired and raw_bars <= 2.0, name)
            self.assertEqual(corrected['hasBpmLock'], bool(metric['hasBpmLock']), name)
            self.assertEqual(corrected['hasNominalBpm'], bool(metric['hasNominalBpm']), name)
            start, end = steady_window(truth, acquired, metric.get('acquisitionSeconds'))
            self.assertAlmostEqual(corrected['steadyWindowStartSeconds'], start, places=9, msg=name)
            self.assertAlmostEqual(corrected['steadyWindowEndSeconds'], end, places=9, msg=name)

    def test_e3_not_acquired_normalised_null_with_raw_sentinel(self):
        rows = [o for o in self.corrected.values() if not o['acquired']]
        self.assertTrue(rows, 'expected at least one not-acquired row')
        for row in rows:
            self.assertIsNone(row['acquisitionBars'], row['fixture'])
            self.assertEqual(row['acquisitionBarsRaw'], 0, row['fixture'])
            self.assertFalse(row['acquiredWithinTwoBars'])

    def test_missing_bpm_reason_consistent_with_flags(self):
        for row in self.corrected.values():
            if row['bpmMissingReason'] is None:
                self.assertTrue(row['hasBpmLock'] and row['hasNominalBpm'])
                self.assertIsNotNone(row['bpmRelativeError'])
            else:
                self.assertIn(row['bpmMissingReason'], ('no-bpm-lock', 'no-nominal-bpm'))
                self.assertIsNone(row['bpmRelativeError'])

    def test_e4_steady_windows_are_backend_specific(self):
        differing = 0
        for name in self.truth:
            d = self.corrected[(name, 'btrack')]
            v = self.corrected[(name, 'btrack-tempo-variant')]
            if (d['steadyWindowStartSeconds'], d['steadyWindowEndSeconds']) != \
                    (v['steadyWindowStartSeconds'], v['steadyWindowEndSeconds']):
                differing += 1
        self.assertEqual(differing, 14)

    def test_e2_noise_126_44100_regression(self):
        d = self.corrected[('noise_126bpm_44100hz', 'btrack')]
        v = self.corrected[('noise_126bpm_44100hz', 'btrack-tempo-variant')]
        self.assertAlmostEqual(v['acquisitionBars'] - d['acquisitionBars'], 2.2491, places=3)

    def test_e5_gap_exemption_only_for_gap_control(self):
        self.assertEqual(loadc('intervals.json')['aubio']['sparse_126bpm_48000hz']['outliers'], 29)
        self.assertEqual(loadc('intervals.json')['aubio']['sparse_126bpm_48000hz']['longestOutlierRun'], 29)

    def test_adversarial_non_gap_interval_crossing_window_is_not_exempt(self):
        # A sparse-control interval spanning 16->17 s must be counted, not exempted.
        sys.path.insert(0, str(ROOT / 'tools/tempo-characterization'))
        import recompute_evidence as rec
        import run_long_windows as run
        events = [15.9, 17.5]
        self.assertEqual(rec.corrected_intervals(events, 'gap', 96)['outliers'], 0)
        self.assertGreater(rec.corrected_intervals(events, 'sparse', 96)['outliers'], 0)
        # The historical rule exempted any crossing interval regardless of control (E5).
        self.assertEqual(run.interval_summary(events, 'sparse', 96)['outliers'], 0)


class ComparisonSemanticsTests(unittest.TestCase):
    def test_control_pairs_are_twelve_perturbations_by_three_backends(self):
        pairs = loadc('control-pairs.json')
        self.assertEqual(len(pairs), 36)
        for pair in pairs:
            self.assertEqual(pair['kind'], 'control-vs-regular')
            self.assertIn(pair['control'], ('sparse', 'gap', 'noise'))
            self.assertEqual(pair['pairedControl'].split('_')[0], 'regular')

    def test_control_pair_states_re_derive(self):
        outcomes = {(o['fixture'], o['backend']): o for o in loadc('outcomes.json')}
        for pair in loadc('control-pairs.json'):
            base = outcomes[(pair['pairedControl'], pair['backend'])]
            other = outcomes[(pair['fixture'], pair['backend'])]
            delta = (None if base['bpmRelativeError'] is None or other['bpmRelativeError'] is None
                     else other['bpmRelativeError'] - base['bpmRelativeError'])
            expected = ('not-evaluable' if delta is None else
                        'regression' if delta > 0.005 else 'gain' if -delta > 0.005 else 'no-change')
            self.assertEqual(pair['bpmErrorState'], expected, pair['fixture'])

    def test_default_vs_variant_records_retained(self):
        self.assertEqual(len(loadc('comparison.json')), 16)


class ValidationArtifactTests(unittest.TestCase):
    def test_derived_hashes_authenticate_the_tree(self):
        recorded = {}
        for line in (CORR / 'derived-hashes.txt').read_text().splitlines():
            digest, name = line.split('  ', 1)
            recorded[name] = digest
        present = {str(p.relative_to(CORR)) for p in CORR.rglob('*') if p.is_file()}
        present.discard('derived-hashes.txt')
        self.assertEqual(set(recorded), present)
        for name, digest in recorded.items():
            self.assertEqual(sha(CORR / name), digest, name)

    def test_errata_records_actual_provenance_hash(self):
        errata = loadc('errata.json')
        self.assertEqual(errata['verifiedPins']['provenanceJsonFileSha256'],
                         '202714b16002ac9f9c94aca67684abaacaab5666a35aff9f018c8450d4c032fe')
        self.assertEqual(sha(ROOT / 'docs/research/tempo-variant/provenance.json'),
                         errata['verifiedPins']['provenanceJsonFileSha256'])


class HardValidationFailureTests(unittest.TestCase):
    """paired.validate_results must reject malformed raw (uses the reviewed function)."""

    def setUp(self):
        import tempfile
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def _mutated(self, mapping):
        data = json.loads((HIST / 'raw/btrack/block128/results.json').read_text())
        mapping(data)
        path = Path(self._tmp.name) / 'mutated.json'
        path.write_text(json.dumps(data))
        return path

    def _adapted_manifest(self):
        fixtures = json.loads(FIX.read_text())['fixtures']
        return {'fixtures': [{'name': f['name'], 'sha256': f['sha256'],
                              'transformation': {'kind': 'generated'}} for f in fixtures]}

    def test_wrong_framing_rejected(self):
        path = self._mutated(lambda d: d.__setitem__('blockFrames', 512))
        with self.assertRaises(ValueError):
            paired.validate_results(path, 'btrack', self._adapted_manifest())

    def test_legacy_stamps_rejected(self):
        path = self._mutated(lambda d: d.__setitem__('legacyBlockStampedBeats', True))
        with self.assertRaises(ValueError):
            paired.validate_results(path, 'btrack', self._adapted_manifest())

    def test_wrong_backend_identity_rejected(self):
        with self.assertRaises(ValueError):
            paired.validate_results(HIST / 'raw/btrack/block128/results.json', 'aubio',
                                    self._adapted_manifest())

    def test_uncompensated_label_required(self):
        def mutate(data):
            data['variants'][0]['label'] = 'compensated'
        path = self._mutated(mutate)
        with self.assertRaises(ValueError):
            paired.validate_results(path, 'btrack', self._adapted_manifest())


if __name__ == '__main__':
    unittest.main()