#!/usr/bin/env bash
# EVAL-GUITAR-009 — regenerate the CORRECTED diagnostic baseline additively.
#
# The original frozen protocol and the original evidence under
# docs/research/guitar-lock-eval (results.json, traces/**, ...) are PRESERVED
# byte-for-byte. Corrected measurements go to
# docs/research/guitar-lock-eval/correction-01, governed by the preregistered
# additive correction protocol tools/guitar-lock-eval/protocol/correction-01.json.
#
#   tools/guitar-lock-eval/run-correction.sh [artifact-dir] [build-root]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

artifact="${1:-docs/research/guitar-lock-eval}"
build_root="${2:-${JAM_GLE_BUILD_ROOT:-/home/mojo/projects/build-EVAL-GUITAR-009}}"
core="${JAM_GLE_CORE:-/home/mojo/projects/build-EVAL-005/main-core}"
diag="$build_root/diag/tracker-diagnostics"
corr="$artifact/correction-01"

# Preserve the originals: record their hashes, assert unchanged at the end.
orig_results_sha="$(sha256sum "$artifact/results.json" | cut -d' ' -f1)"
orig_traces_sha="$(find "$artifact/traces" -type f -name '*.json' -print0 \
    | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
orig_protocol_sha="$(sha256sum "$here/protocol/useful-lock-protocol.json" | cut -d' ' -f1)"

if [ ! -x "$diag" ]; then
    echo "run-correction.sh: $diag missing; run run-baseline.sh first" >&2
    exit 2
fi

work="${JAM_GLE_WORKDIR:-$build_root/guitar-lock-eval}/correction-01"
mkdir -p "$corr" "$work"

echo "[1/3] corrected compare (additive, synthetic diagnostic)"
python3 "$here/guitar-lock-eval" evaluate \
    --manifest "$artifact/baseline-import.json" \
    --audio-root . \
    --backends btrack,aubio \
    --block 128 \
    --tracker-diagnostics "$diag" \
    --btrack-lib "$build_root/plugins/librhythm-eval-btrack.so" \
    --aubio-lib "$build_root/plugins/librhythm-eval-aubio.so" \
    --workdir "$work" \
    --traces-out "$corr/traces" \
    --json-out "$corr/results.json" \
    --summary-md "$corr/summary.md" \
    --per-fixture-csv "$corr/per-fixture.csv" \
    --provenance-out "$corr/provenance.json" \
    --diagnostic-ok

echo "[2/3] replay the corrected committed traces without any backend"
python3 "$here/guitar-lock-eval" evaluate \
    --manifest "$artifact/baseline-import.json" \
    --audio-root . \
    --backends btrack,aubio \
    --traces-dir "$corr/traces" \
    --workdir "$work-replay" \
    --json-out "$work-replay-results.json" \
    --diagnostic-ok

echo "[3/3] assert original evidence and protocol unchanged"
new_results_sha="$(sha256sum "$artifact/results.json" | cut -d' ' -f1)"
new_traces_sha="$(find "$artifact/traces" -type f -name '*.json' -print0 \
    | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
new_protocol_sha="$(sha256sum "$here/protocol/useful-lock-protocol.json" | cut -d' ' -f1)"
[ "$orig_results_sha" = "$new_results_sha" ] || { echo "original results.json changed" >&2; exit 2; }
[ "$orig_traces_sha" = "$new_traces_sha" ] || { echo "original traces changed" >&2; exit 2; }
[ "$orig_protocol_sha" = "$new_protocol_sha" ] || { echo "original protocol changed" >&2; exit 2; }

echo "done: $corr (originals preserved)"
du -sh "$corr"
