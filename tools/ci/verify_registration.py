#!/usr/bin/env python3
"""Fail closed on omitted suites and broken optional tracker linkage in CI."""

import argparse
import json
from pathlib import Path
import re
import subprocess


CORE = {
    'jam.AnalysisAudioRing', 'jam.BackendRunner', 'jam.DrumTransportAdapter',
    'jam.MusicalClock', 'jam.RhythmAnalyzer', 'jam.RhythmCorpus',
    'jam.RhythmDerived', 'jam.RhythmEvalMetrics', 'jam.RhythmSilenceCoverage',
    'jam.RtSignal', 'jam.AnalysisWorkerBenchmark', 'jam.TempoVariant',
    'jam.TempoVariantResearch', 'jam.TrackerDiagnostics',
    'jam.RhythmDerivedGenerator', 'jam.RhythmRobustness',
    'jam.RhythmSustainRepair', 'jam.BeatNetResearch',
}
NAM = {
    'nam_rt_diff', 'nam_rt_compare_unit', 'nam_rt_patch_checks',
    'nam_rt_probe_repair', 'nam_rt_probe_verifier_unit',
}
DRUMS = {
    'drums.parseSpec', 'drums.library', 'drums.generator', 'drums.barCodec',
    'drums.midiCapacity',
    'drums.foundation',
}
TRACKERS = {
    'btrack': ('jam.BTrackBackend', 'jamBTrackTests', r'BTrack::'),
    'aubio': ('jam.AubioBackend', 'jamAubioTests', r'\baubio_tempo_'),
}
TRACKER_SYMBOLS = re.compile(r'BTrack|btrack|kiss_fft|aubio', re.IGNORECASE)
SYMBOL_LINE = re.compile(r'^(?:[0-9a-fA-F]+\s+[A-Za-z]\s|\s+U\s)')


def symbols(path):
    result = subprocess.run(['nm', '-C', str(path)], check=True,
                            capture_output=True, text=True)
    lines = [line for line in result.stdout.splitlines() if SYMBOL_LINE.match(line)]
    if not lines:
        raise ValueError(f'nm returned no symbol lines: {path}')
    return '\n'.join(lines)


def verify(names, lane, enabled):
    if len(names) != len(set(names)):
        raise ValueError('duplicate CTest suite names')
    registered = set(names)
    expected = {'core': CORE, 'nam': NAM, 'drums': DRUMS}[lane].copy()
    if lane == 'core':
        for tracker, (suite, _, _) in TRACKERS.items():
            if enabled[tracker]:
                expected.add(suite)
            elif suite in registered:
                raise ValueError(f'disabled tracker registered: {suite}')
    missing = expected - registered
    if missing:
        raise ValueError(f'missing required suites: {sorted(missing)}')
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lane', choices=('core', 'nam', 'drums'), required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--config')
    parser.add_argument('--btrack', choices=('ON', 'OFF'), default='OFF')
    parser.add_argument('--aubio', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    command = ['ctest', '--test-dir', str(args.build), '-N', '--show-only=json-v1']
    if args.config:
        command += ['-C', args.config]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    names = [test['name'] for test in json.loads(result.stdout)['tests']]
    enabled = {'btrack': args.btrack == 'ON', 'aubio': args.aubio == 'ON'}
    expected = verify(names, args.lane, enabled)
    if args.lane == 'core':
        for name in ('libjam-core.a', 'jamTests'):
            if TRACKER_SYMBOLS.search(symbols(args.build / name)):
                raise ValueError(f'core links tracker symbols: {name}')
        for tracker, (_, binary, pattern) in TRACKERS.items():
            if enabled[tracker] and not re.search(pattern, symbols(args.build / binary)):
                raise ValueError(f'enabled tracker has no real symbols: {tracker}')
        if not any(enabled.values()):
            graph = subprocess.run(['ninja', '-C', str(args.build), '-t', 'commands'],
                                   check=True, capture_output=True, text=True).stdout
            if re.search(r'third_party/(?:BTrack|aubio)', graph):
                raise ValueError('dependency-free graph contains a tracker source')
    print(f'{args.lane}: {len(expected)} required suites present; linkage checks passed')


if __name__ == '__main__':
    main()
