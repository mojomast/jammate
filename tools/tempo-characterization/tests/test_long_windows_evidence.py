#!/usr/bin/env python3
"""TRACK-007 evidence checks: authenticate retained results and re-derive the claims.

These tests read the committed evidence tree only. They re-derive the protocol's
comparison rules from `outcomes.json` independently of the runner's own pass, verify
missingness semantics, and authenticate retained artifacts against the recorded hashes.
Run from the repository root.
"""
import gzip
import hashlib
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/tempo-characterization'))
import run_long_windows as run  # noqa: E402

EV = ROOT / 'docs/research/tempo-long-windows/evidence'
FIX = ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(name):
    return json.loads((EV / name).read_text())


class ArtifactAuthenticationTests(unittest.TestCase):
    def test_artifact_hashes_cover_every_retained_file(self):
        recorded = {}
        for line in (EV / 'artifact-hashes.txt').read_text().splitlines():
            digest, name = line.split('  ', 1)
            recorded[name] = digest
        present = {str(p.relative_to(EV)) for p in EV.rglob('*') if p.is_file()}
        present.discard('artifact-hashes.txt')
        self.assertEqual(set(recorded), present, 'artifact list does not match the tree')
        for name, digest in recorded.items():
            self.assertEqual(sha(EV / name), digest, name)

    def test_evidence_manifest_is_the_committed_fixture_identity(self):
        self.assertEqual((EV / 'manifest.json').read_bytes(), FIX.read_bytes())

    def test_raw_method_log_matches_its_recorded_uncompressed_hash(self):
        recorded = load('provenance.json')['rawMethodLogSha256']
        with gzip.open(EV / 'raw-method/instance_0.csv.gz', 'rb') as handle:
            self.assertEqual(hashlib.sha256(handle.read()).hexdigest(), recorded)

    def test_provenance_records_the_executed_pins(self):
        pins = {p['role']: p['sha256'] for p in load('provenance.json')['pins']}
        for role, (_, sha_expected) in run.PINS.items():
            self.assertEqual(pins[role], sha_expected, role)
        self.assertEqual(load('provenance.json')['blockFrames'], 128)
        self.assertIsNone(load('provenance.json')['selection'])


class CompletenessTests(unittest.TestCase):
    def test_sixteen_fixtures_by_three_backends(self):
        outcomes = load('outcomes.json')
        self.assertEqual(len(outcomes), 48)
        names = {f['name'] for f in load('manifest.json')['fixtures']}
        self.assertEqual({o['fixture'] for o in outcomes}, names)
        for backend in run.BACKENDS:
            self.assertEqual(sum(o['backend'] == backend for o in outcomes), 16)

    def test_default_and_variant_beat_series_are_byte_identical(self):
        for entry in load('beat-equality.json'):
            self.assertEqual(entry['sha256']['btrack'], entry['sha256']['btrack-tempo-variant'])
            for backend in run.BACKENDS:
                path = EV / 'diagnostic' / backend / 'beats' / (entry['fixture'] + '.csv')
                self.assertEqual(sha(path), entry['sha256'][backend], entry['fixture'])

    def test_every_retained_scorer_record_is_block_128_uncompensated(self):
        for backend in run.BACKENDS:
            data = load('raw/%s/block128/results.json' % backend)
            self.assertEqual(data['blockFrames'], 128)
            self.assertFalse(data['legacyBlockStampedBeats'])
            self.assertEqual(len(data['variants']), 1)
            self.assertEqual(data['variants'][0]['label'], 'uncompensated')
            self.assertEqual(len(data['variants'][0]['fixtures']), 16)


class MissingnessTests(unittest.TestCase):
    def test_no_true_silence_is_claimed_anywhere(self):
        for fixture in load('manifest.json')['fixtures']:
            self.assertEqual(fixture['trueSilenceSpans'], [], fixture['name'])
        for backend in run.BACKENDS:
            for fixture in load('raw/%s/block128/results.json' % backend)['variants'][0]['fixtures']:
                self.assertEqual(fixture['falseBeatCoverage'], 'NoTrueSilence')
                self.assertFalse(fixture['falseBeatMetricInformative'])
                self.assertFalse(fixture['silenceAccelerationMeasured'])
                self.assertFalse(fixture['trueSilenceMeasured'])

    def test_bpm_error_is_null_exactly_when_no_lock_or_no_nominal_bpm(self):
        for outcome in load('outcomes.json'):
            if outcome['lockedBpm'] is None:
                self.assertIsNone(outcome['bpmRelativeError'], outcome['fixture'])
            else:
                self.assertIsNotNone(outcome['bpmRelativeError'], outcome['fixture'])

    def test_missing_measurements_are_never_zero(self):
        for outcome in load('outcomes.json'):
            if outcome['bpmRelativeError'] is None:
                self.assertNotEqual(outcome['bpmRelativeError'], 0)
            if not outcome['acquired']:
                self.assertEqual(outcome['acquisitionBars'], 0)
                self.assertFalse(outcome['acquiredWithinTwoBars'])


class DerivedClaimTests(unittest.TestCase):
    def test_comparisons_reproduce_from_outcomes_with_the_declared_band(self):
        outcomes = load('outcomes.json')
        by = {}
        for outcome in outcomes:
            by[(outcome['fixture'], outcome['backend'])] = outcome
        recorded = {c['fixture']: c for c in load('comparison.json')}
        self.assertEqual(len(recorded), 16)
        for fixture in recorded:
            default, variant = by[(fixture, 'btrack')], by[(fixture, 'btrack-tempo-variant')]
            expected = run.compare(default, variant)
            self.assertEqual(recorded[fixture], expected, fixture)

    def test_variant_never_exceeds_the_frozen_interval_bound(self):
        intervals = load('intervals.json')['btrack-tempo-variant']
        for name, summary in intervals.items():
            self.assertIsNotNone(summary['maxIntervalSeconds'], name)
            self.assertLessEqual(summary['maxIntervalSeconds'], 1.50, name)

    def test_gap_spanning_intervals_are_excluded_from_outliers(self):
        intervals = load('intervals.json')
        for backend in run.BACKENDS:
            for name, summary in intervals[backend].items():
                for span in summary['gapSpanning']:
                    self.assertFalse(span['outlier'], name)
                    self.assertGreaterEqual(span['end'], run.GAP_END)

    def test_sparse_expectation_is_two_beats_and_others_one(self):
        for backend in run.BACKENDS:
            for name, summary in load('intervals.json')[backend].items():
                period = 60.0 / run.tempo_of(name)
                expected = 2 * period if run.control_of(name) == 'sparse' else period
                self.assertAlmostEqual(summary['expectedSeconds'], expected, places=9, msg=name)

    def test_readiness_reports_fallback_before_ready_and_event_before_availability(self):
        for row in load('method-summary.json'):
            self.assertEqual(row['fallbackBeats'], 4, row['fixture'])
            self.assertGreater(row['firstReadyAvailabilitySeconds'], row['firstReadyEventSeconds'])
            self.assertEqual(row['readyBeats'] + row['fallbackBeats'], row['beats'])


if __name__ == '__main__':
    unittest.main()