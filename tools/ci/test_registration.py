"""Acceptance checks for fail-closed CI guards, including silent suite loss."""

from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import verify_registration as guard


class RegistrationTests(unittest.TestCase):
    def test_every_required_core_suite_is_load_bearing(self):
        for missing in guard.CORE:
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                guard.verify(list(guard.CORE - {missing}), 'core',
                             {'btrack': False, 'aubio': False})

    def test_enabled_and_disabled_tracker_contracts(self):
        for tracker, (suite, _, _) in guard.TRACKERS.items():
            enabled = {'btrack': False, 'aubio': False}
            with self.assertRaises(ValueError):
                guard.verify(list(guard.CORE) + [suite], 'core', enabled)
            enabled[tracker] = True
            with self.assertRaises(ValueError):
                guard.verify(list(guard.CORE), 'core', enabled)
            guard.verify(list(guard.CORE) + [suite], 'core', enabled)

    def test_duplicate_registration_fails(self):
        with self.assertRaises(ValueError):
            guard.verify(list(guard.NAM) + ['nam_rt_diff'], 'nam', {})

    def test_required_nam_and_drum_suites_cannot_disappear(self):
        for lane, expected in [('nam', guard.NAM), ('drums', guard.DRUMS)]:
            for missing in expected:
                with self.subTest(lane=lane, missing=missing), self.assertRaises(ValueError):
                    guard.verify(list(expected - {missing}), lane, {})

    def test_nm_failure_propagates(self):
        with patch.object(guard.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'nm')):
            with self.assertRaises(subprocess.CalledProcessError):
                guard.symbols(Path('bad.a'))

    def test_empty_and_header_only_nm_output_fails(self):
        for output in ('', '/scratch/aubio/libjam-core.a:\n', 'nm: no symbols\n'):
            with patch.object(guard.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, output)), self.assertRaises(ValueError):
                guard.symbols(Path('empty.a'))

    def test_directory_names_are_not_tracker_symbols(self):
        output = '/scratch/btrack/libjam-core.a:\n00000000 T jam::MusicalClock::advance()\n'
        with patch.object(guard.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, output)):
            self.assertIsNone(guard.TRACKER_SYMBOLS.search(guard.symbols(Path('core.a'))))

    def test_stub_tracker_names_are_not_backend_linkage(self):
        for symbol in ('(anonymous namespace)::StubTracker::reset(double)',
                       'typeinfo for (anonymous namespace)::StubTracker',
                       'jam::IRhythmTracker::~IRhythmTracker()'):
            with self.subTest(symbol=symbol):
                self.assertIsNone(guard.TRACKER_SYMBOLS.search(symbol))

    def test_real_backend_symbols_remain_forbidden_in_core(self):
        for symbol in ('BTrack::processAudioFrame(double*)',
                       'jam::BTrackBackend::BTrackBackend()',
                       'jam::AubioBackend::reset(double)',
                       'kiss_fft', 'kiss_fftr_alloc', 'aubio_tempo_do'):
            with self.subTest(symbol=symbol):
                self.assertIsNotNone(guard.TRACKER_SYMBOLS.search(symbol))


if __name__ == '__main__':
    unittest.main()
