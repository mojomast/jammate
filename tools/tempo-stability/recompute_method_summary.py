#!/usr/bin/env python3
"""TRACK-008 correction: recompute the method-summary from the preserved raw
method logs, fixing the nominal-missing out-of-band semantics (ERRATA E2).

Reads ONLY the four gzipped raw method logs preserved under
docs/research/tempo-stability/evidence/method-raw/ and the two committed corpus
manifests. Writes docs/research/tempo-stability/evidence-corrected/. It never
runs a scorer/evaluator binary, never touches the historical evidence, and never
changes any scored metric. Run from the repository root.
"""
import csv
import gzip
import hashlib
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVID = ROOT / 'docs/research/tempo-stability/evidence'
OUT = ROOT / 'docs/research/tempo-stability/evidence-corrected'

MANIFESTS = {
    'short': ROOT / 'testdata/rhythm/derived/manifest.json',
    'long': ROOT / 'docs/research/tempo-long-windows/fixtures/manifest.json',
}
LOG_FILES = {
    ('short', 'btrack-tempo-stable'): EVID / 'method-raw/short-btrack-tempo-stable.csv.gz',
    ('long', 'btrack-tempo-stable'): EVID / 'method-raw/long-btrack-tempo-stable.csv.gz',
    ('short', 'btrack-tempo-variant'): EVID / 'method-raw/short-btrack-tempo-variant.csv.gz',
    ('long', 'btrack-tempo-variant'): EVID / 'method-raw/long-btrack-tempo-variant.csv.gz',
}
BLOCK = 128


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read_gz_csv(path):
    with gzip.open(path, 'rt') as stream:
        return list(csv.DictReader(stream))


def ceil_blocks(frames):
    return math.ceil(frames / BLOCK)


def longest_run(flags):
    best = run = 0
    for flag in flags:
        run = run + 1 if flag else 0
        best = max(best, run)
    return best


def out_of_band(values, nominal):
    """Return (count, longestRun, measured). Unmeasured when nominal is absent."""
    if not nominal or nominal <= 0.0:
        return None, None, False
    flags = [abs(v / nominal - 1.0) > 0.02 for v in values]
    return sum(flags), longest_run(flags), True


def stable_fixture(rows, fixture):
    emitted = [r for r in rows if r['beatEvent'] == '1']
    confirmed = [r for r in emitted if r['confirmed'] == '1']
    bpms = [float(r['emittedBpm']) for r in confirmed]
    nominal = fixture.get('nominalBpm')
    count, run, measured = out_of_band(bpms, nominal)
    return {
        'fixture': fixture['name'], 'blocks': len(rows), 'beats': len(emitted),
        'fallbackBeats': sum(r['confirmed'] == '0' for r in emitted),
        'confirmedBeats': len(confirmed),
        'fallbackBlocks': sum(r['confirmed'] == '0' for r in rows),
        'confirmedBlocks': sum(r['confirmed'] == '1' for r in rows),
        'firstConfirmedEventSeconds': float(confirmed[0]['eventSeconds']) if confirmed else None,
        'firstConfirmedAvailabilitySeconds': float(confirmed[0]['blockEndSeconds']) if confirmed else None,
        'confirmedBpmMin': min(bpms) if bpms else None,
        'confirmedBpmMedian': statistics.median(bpms) if bpms else None,
        'confirmedBpmMax': max(bpms) if bpms else None,
        'confirmedBeatsOutOfBand': count,
        'confirmedBeatsOutOfBandMeasured': measured,
        'longestOutOfBandConfirmedRun': run,
        'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                  for s in sorted({r['intervalState'] for r in emitted})},
    }


def variant_fixture(rows, fixture):
    emitted = [r for r in rows if r['beatEvent'] == '1']
    ready = [r for r in emitted if r['ready'] == '1']
    bpms = [float(r['variantBpm']) for r in ready]
    nominal = fixture.get('nominalBpm')
    count, run, measured = out_of_band(bpms, nominal)
    return {
        'fixture': fixture['name'], 'blocks': len(rows), 'beats': len(emitted),
        'fallbackBeats': sum(r['ready'] == '0' for r in emitted),
        'readyBeats': len(ready),
        'firstReadyEventSeconds': float(ready[0]['eventSeconds']) if ready else None,
        'firstReadyAvailabilitySeconds': float(ready[0]['blockEndSeconds']) if ready else None,
        'readyBpmMin': min(bpms) if bpms else None,
        'readyBpmMedian': statistics.median(bpms) if bpms else None,
        'readyBpmMax': max(bpms) if bpms else None,
        'readyBeatsOutOfBand': count,
        'readyBeatsOutOfBandMeasured': measured,
        'longestOutOfBandReadyRun': run,
        'intervalStatesAtBeats': {s: sum(r['intervalState'] == s for r in emitted)
                                  for s in sorted({r['intervalState'] for r in emitted})},
    }


def main():
    if OUT.exists():
        print('recompute_method_summary: output exists; choose a new directory', file=sys.stderr)
        return 2
    out = {}
    inputs = {}
    for label, manifest_path in MANIFESTS.items():
        manifest = json.loads(manifest_path.read_text())
        for backend, parser in (('btrack-tempo-stable', stable_fixture),
                                ('btrack-tempo-variant', variant_fixture)):
            log_path = LOG_FILES[(label, backend)]
            rows = read_gz_csv(log_path)
            inputs[str(log_path.relative_to(ROOT))] = sha(log_path)
            groups = []
            offset = 0
            for fixture in manifest['fixtures']:
                count = ceil_blocks(fixture['signal']['frames'])
                group = rows[offset:offset + count]
                if len(group) != count:
                    print('recompute_method_summary: short log window for %s' % fixture['name'],
                          file=sys.stderr)
                    return 2
                groups.append(parser(group, fixture))
                offset += count
            if offset != len(rows):
                print('recompute_method_summary: extra method rows', file=sys.stderr)
                return 2
            out.setdefault(backend, []).extend(groups)
    OUT.mkdir(parents=True)
    summary_path = OUT / 'method-summary.json'
    summary_path.write_text(json.dumps(out, indent=2, sort_keys=True) + '\n')
    derived = {
        'corrected': {'method-summary.json': sha(summary_path)},
        'inputs': inputs,
        'semantics': 'null + ...Measured=false when the fixture declares no nominalBpm',
    }
    (OUT / 'derived-hashes.txt').write_text(json.dumps(derived, indent=2, sort_keys=True) + '\n')
    print('wrote %s and derived-hashes.txt' % summary_path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
