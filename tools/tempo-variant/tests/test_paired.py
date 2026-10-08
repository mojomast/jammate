"""Fail-closed validation and end-to-end audit of TRACK-006 evidence."""
import copy
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'paired', Path(__file__).resolve().parents[1] / 'run_paired.py')
paired = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paired)


class PairedEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.corpus = paired.ROOT / 'testdata/rhythm/derived'
        self.manifest = json.loads((self.corpus / 'manifest.json').read_text())
        self.out = paired.ROOT / 'docs/research/tempo-variant-robustness'

    def test_same_parent_different_window_rejected(self):
        self.manifest['fixtures'][1]['truncation']['sourceStartFrame'] += 1
        with self.assertRaisesRegex(ValueError, 'SOURCE TRUNCATION MISMATCH'):
            paired.verify_inputs(self.manifest, self.corpus)

    def test_actual_input_bytes_authenticated(self):
        _, pins = paired.verify_inputs(self.manifest, self.corpus)
        self.assertEqual(len(pins), 24)
        self.manifest['fixtures'][0]['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'pin mismatch'):
            paired.verify_inputs(self.manifest, self.corpus)

    def test_results_missing_framing_identity_and_stale_noise_fail(self):
        data = json.loads((self.out / 'raw/btrack/block128/results.json').read_text())
        mutations = [lambda d: d.pop('blockFrames'),
                     lambda d: d.update(backend='aubio'),
                     lambda d: d['fixtures'].pop(),
                     lambda d: d['fixtures'][0].update(sourceSha256='wrong'),
                     lambda d: d['fixtures'][1].update(falseBeatCoverage='Measured')]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.json'
            for mutation in mutations:
                modified = copy.deepcopy(data)
                mutation(modified)
                path.write_text(json.dumps(modified))
                with self.assertRaises(ValueError):
                    paired.validate_results(path, 'btrack', self.manifest)

    def load_groups(self):
        import csv
        with gzip.open(self.out / 'raw-method/instance_0.csv.gz', 'rt', newline='') as stream:
            rows = list(csv.DictReader(stream))
        return rows, paired.split_method_log(rows, self.manifest['fixtures'])

    def test_missing_extra_reordered_log_blocks_rejected(self):
        rows, _ = self.load_groups()
        for invalid in (rows[:-1], rows + rows[:1], rows[1:] + rows[:1]):
            with self.assertRaises(ValueError):
                paired.split_method_log(invalid, self.manifest['fixtures'])

    def test_fallback_and_missing_interval_cannot_be_fabricated(self):
        _, groups = self.load_groups()
        fixture = self.manifest['fixtures'][0]
        beats = paired.read_csv(self.out / 'diagnostic/btrack/beats' / (fixture['name'] + '.csv'))
        for key, value in [('variantBpm', '999'), ('intervalSeconds', '0'), ('ready', '1')]:
            rows = copy.deepcopy(groups[0])
            rows[0][key] = value
            with self.assertRaises(ValueError):
                paired.method_summary(rows, fixture, beats)

    def test_current_log_timeline_matches_all_24_exact_beat_pairs(self):
        _, groups = self.load_groups()
        for fixture, rows in zip(self.manifest['fixtures'], groups):
            paths = [self.out / 'diagnostic' / b / 'beats' / (fixture['name'] + '.csv')
                     for b in ('btrack', 'btrack-tempo-variant')]
            self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())
            paired.method_summary(rows, fixture, paired.read_csv(paths[0]))
        # A causal availability mismatch must be rejected, even with equal event time.
        fixture = self.manifest['fixtures'][0]
        beats = paired.read_csv(self.out / 'diagnostic/btrack/beats' / (fixture['name'] + '.csv'))
        beats[0]['availabilitySeconds'] = '999'
        with self.assertRaisesRegex(ValueError, 'timeline mismatch'):
            paired.method_summary(groups[0], fixture, beats)

    def test_no_beats_missing_lock_stays_missing(self):
        data = json.loads((self.out / 'degradation.json').read_text())
        rows = [r for r in data['rows'] if r['fixture'] == 'clean_eighths__level_-60db'
                and r['metric'] in ('bpm.lockedBpm', 'bpm.bpmRelativeError',
                                   'acquisition.acquisitionSeconds', 'phase.phaseMeanAbsMs')]
        self.assertEqual(len(rows), 12)
        self.assertTrue(all(r['value'] is None and r['difference'] is None for r in rows))

    def test_noise_counts_retained_but_unassessed(self):
        rows = json.loads((self.out / 'degradation.json').read_text())['rows']
        selected = [r for r in rows if r['perturbation'] == 'noise' and
                    r['metric'] == 'silence.falseBeatsInTrueSilencePerSecond']
        self.assertEqual(len(selected), 15)
        self.assertTrue(all(r['status'] == 'not_assessed_noise' and
                            r['value'] is not None and r['difference'] is None for r in selected))

    def test_all_retained_artifact_and_raw_log_hashes(self):
        for line in (self.out / 'artifact-hashes.txt').read_text().splitlines():
            sha, path = line.split('  ', 1)
            self.assertEqual(paired.robustness.sha256_file(self.out / path), sha, path)
        provenance = json.loads((self.out / 'provenance.json').read_text())
        pin = next(p for p in provenance['pins'] if 'retainedAs' in p)
        with gzip.open(paired.ROOT / pin['retainedAs'], 'rb') as stream:
            self.assertEqual(hashlib.sha256(stream.read()).hexdigest(), pin['sha256'])


if __name__ == '__main__':
    unittest.main()
