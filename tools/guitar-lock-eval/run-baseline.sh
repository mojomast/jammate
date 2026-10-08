#!/usr/bin/env bash
# EVAL-GUITAR-009 — one-command diagnostic baseline + evidence regeneration.
#
# It (1) builds the pre-existing tracker-diagnostics binary into the designated
# EVAL-GUITAR-009 build root using that tool's own unmodified build script,
# (2) imports the committed synthetic corpus as a labelled synthetic import
# manifest, and (3) runs the one-command useful-lock comparison of the real
# BTrack/aubio backends and the derived beat-interval diagnostic candidate,
# writing bounded evidence under docs/research/guitar-lock-eval.
#
# This is a DIAGNOSTIC. The >=95% gate is evaluated only over representative
# real recordings and fails closed because none are available; synthetic
# recordings never count toward it.
#
#   tools/guitar-lock-eval/run-baseline.sh [artifact-dir] [build-root]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

artifact="${1:-docs/research/guitar-lock-eval}"
build_root="${2:-${JAM_GLE_BUILD_ROOT:-/home/mojo/projects/build-EVAL-GUITAR-009}}"
core="${JAM_GLE_CORE:-/home/mojo/projects/build-EVAL-005/main-core}"

diag="$build_root/diag/tracker-diagnostics"
mkdir -p "$build_root/plugins"

if [ ! -x "$diag" ]; then
    echo "[1/4] building tracker-diagnostics into $build_root/diag"
    JAM_DIAG_BUILD_CONFIG_PLUGINS=0 \
        tools/tracker-diagnostics/build.sh "$core" "$build_root/diag"
else
    echo "[1/4] reusing $diag"
fi

for backend in btrack aubio; do
    dst="$build_root/plugins/librhythm-eval-$backend.so"
    if [ ! -f "$dst" ]; then
        src="$core/librhythm-eval-$backend.so"
        if [ ! -f "$src" ]; then
            echo "run-baseline.sh: missing plugin $src" >&2
            exit 2
        fi
        cp "$src" "$dst"
    fi
done

work="${JAM_GLE_WORKDIR:-$build_root/guitar-lock-eval}"
mkdir -p "$work"

echo "[2/4] import manifest (synthetic baseline)"
python3 "$here/guitar-lock-eval" import-corpus \
    --rhythm-manifest testdata/rhythm/manifest.json \
    --audio-root testdata/rhythm \
    --base-root . \
    --classification synthetic \
    --out "$artifact/baseline-import.json"

echo "[3/4] compare backends from actual generated traces"
python3 "$here/guitar-lock-eval" evaluate \
    --manifest "$artifact/baseline-import.json" \
    --audio-root . \
    --backends btrack,aubio \
    --block 128 \
    --tracker-diagnostics "$diag" \
    --btrack-lib "$build_root/plugins/librhythm-eval-btrack.so" \
    --aubio-lib "$build_root/plugins/librhythm-eval-aubio.so" \
    --workdir "$work" \
    --traces-out "$artifact/traces" \
    --json-out "$artifact/results.json" \
    --summary-md "$artifact/summary.md" \
    --per-fixture-csv "$artifact/per-fixture.csv" \
    --provenance-out "$artifact/provenance.json" \
    --diagnostic-ok

echo "[4/4] replay the committed traces without any backend (reproducibility)"
python3 "$here/guitar-lock-eval" evaluate \
    --manifest "$artifact/baseline-import.json" \
    --audio-root . \
    --backends btrack,aubio \
    --traces-dir "$artifact/traces" \
    --workdir "$work-replay" \
    --json-out "$work-replay-results.json" \
    --diagnostic-ok

echo
echo "done: $artifact"
du -sh "$artifact"
