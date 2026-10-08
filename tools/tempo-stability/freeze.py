#!/usr/bin/env python3
"""TRACK-008 candidate source freeze.

Computes the sha256 of every candidate behaviour source, the combined source
freeze hash, the built plugin binary and the embedded EVAL-005 archives, and
writes docs/research/tempo-stability/FREEZE.json. Committed BEFORE any scorer or
diagnostic run (PROTOCOL.md sections 5 and 10). Any change to the candidate
sources or binary requires regenerating this file (a new freeze) before any new
scoring.
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/tempo-stability'
OUT = ROOT / 'docs/research/tempo-stability/FREEZE.json'
BINARY = Path('/home/mojo/projects/build-TRACK-008-worker/libtempo-stable-btrack.so')

# Candidate behaviour sources. The order is fixed; the combined hash is taken over
# sorted "path\0sha\n" records so it is independent of listing order.
SOURCES = [
    'tools/tempo-stability/TempoStable.h',
    'tools/tempo-stability/TempoStable.cpp',
    'tools/tempo-stability/MethodLog.h',
    'tools/tempo-stability/MethodLog.cpp',
    'tools/tempo-stability/TempoStablePlugin.cpp',
    'tools/tempo-stability/build.sh',
]

# Evaluation tooling: recorded for audit but not part of the behaviour freeze.
EVALUATION = [
    'tools/tempo-stability/run_stability.py',
    'tools/tempo-stability/tests/TempoStableTests.cpp',
    'tools/tempo-stability/tests/test_stability_method.py',
    'tools/tempo-stability/tests/test_stability_evidence.py',
]

EMBEDDED = [
    '/home/mojo/projects/build-EVAL-005/main-core/btrack/libjam-btrack.a',
    '/home/mojo/projects/build-EVAL-005/main-core/btrack/libbtrack.a',
    '/home/mojo/projects/build-EVAL-005/main-core/btrack/libsamplerate.a',
    '/home/mojo/projects/build-EVAL-005/main-core/btrack/libkiss_fft.a',
]


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def records(paths, base=ROOT):
    out = []
    for rel in paths:
        path = Path(rel) if str(rel).startswith('/') else base / rel
        out.append({'path': str(rel), 'sha256': sha(path)})
    return out


def combined(entries):
    h = hashlib.sha256()
    for entry in sorted(entries, key=lambda e: e['path']):
        h.update(entry['path'].encode())
        h.update(b'\0')
        h.update(entry['sha256'].encode())
        h.update(b'\n')
    return h.hexdigest()


def main():
    if not BINARY.exists():
        print('freeze: candidate binary not built: %s' % BINARY, file=sys.stderr)
        return 2
    sources = records(SOURCES)
    evaluation = records(EVALUATION)
    embedded = records(EMBEDDED)
    payload = {
        'task': 'TRACK-008',
        'candidateId': 'btrack-tempo-stable',
        'method': 'confirmation-gated median (median-of-4, 3 agreeing updates within 2%)',
        'protocol': 'docs/research/tempo-stability/PROTOCOL.md',
        'sources': sources,
        'combinedSha256': combined(sources),
        'evaluation': evaluation,
        'evaluationCombinedSha256': combined(evaluation),
        'binary': {
            'path': str(BINARY),
            'sha256': sha(BINARY),
            'builtFrom': SOURCES,
            'buildCommand': ('tools/tempo-stability/build.sh '
                             '/home/mojo/projects/build-EVAL-005/main-core '
                             '/home/mojo/projects/build-TRACK-008-worker'),
            'embeddedDependencies': embedded,
        },
        'note': ('The behaviour freeze is sources + binary + embeddedDependencies. '
                 'Evaluation tooling is recorded separately and is not part of the '
                 'candidate behaviour. Any source/binary change needs a new freeze '
                 'committed before any new scoring.'),
    }
    OUT.write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    print('wrote %s' % OUT)
    print('combined source freeze: %s' % payload['combinedSha256'])
    print('binary sha256:          %s' % payload['binary']['sha256'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
