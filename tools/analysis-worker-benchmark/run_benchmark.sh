#!/usr/bin/env bash
# Build and run the ANALYSIS-001 worker throughput benchmark.
#
# It compiles the benchmark tool plus the worktree's RhythmAnalyzer, dlopen()s the
# pinned read-only BTrack/aubio plugin shared objects built by EVAL-005, and
# writes throughput.csv + benchmark.json under the results directory. No GPL
# backend is linked into the tool (SPEC.md 25.6) and no jam-core archive is used,
# so the measured analysis worker is the one in this worktree.
#
#   tools/analysis-worker-benchmark/run_benchmark.sh [plugin-dir] [out-dir]
#
# Defaults: plugin dir /home/mojo/projects/build-EVAL-005/main-core (falls back to
#           .../build-EVAL-005/core), out dir ./results next to this script.
#
# Environment:
#   ANALYSIS_BENCH_SCRATCH  build/scratch directory (default
#                           /home/mojo/projects/build-ANALYSIS-001/benchmark)

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

plugin_dir="${1:-${ANALYSIS_BENCH_PLUGIN_DIR:-/home/mojo/projects/build-EVAL-005/main-core}}"
if [ ! -d "$plugin_dir" ]; then
    plugin_dir="/home/mojo/projects/build-EVAL-005/core"
fi

out_dir="${2:-$here/results}"
scratch="${ANALYSIS_BENCH_SCRATCH:-/home/mojo/projects/build-ANALYSIS-001/benchmark}"

mkdir -p "$out_dir" "$scratch"

btrack_so="$plugin_dir/librhythm-eval-btrack.so"
aubio_so="$plugin_dir/librhythm-eval-aubio.so"

for so in "$btrack_so" "$aubio_so"; do
    if [ ! -f "$so" ]; then
        echo "error: pinned plugin not found: $so" >&2
        exit 1
    fi
done

echo "== compiling benchmark (scratch: $scratch) =="
c++ -std=c++17 -O2 -Wall -Wextra -pthread -I"$root/src" \
    -o "$scratch/analysis_worker_benchmark" \
    "$here/analysis_worker_benchmark.cpp" \
    "$root/src/jam/RhythmAnalyzer.cpp" \
    -ldl

source_sha="$(cat "$root/src/jam/RhythmAnalyzer.h" \
                  "$root/src/jam/RhythmAnalyzer.cpp" \
                  "$here/analysis_worker_benchmark.cpp" | sha256sum | awk '{print $1}')"
btrack_sha="$(sha256sum "$btrack_so" | awk '{print $1}')"
aubio_sha="$(sha256sum "$aubio_so" | awk '{print $1}')"

echo "== plugins =="
printf '  btrack %s  %s\n' "$btrack_sha" "$btrack_so"
printf '  aubio  %s  %s\n' "$aubio_sha" "$aubio_so"
echo "  source $source_sha"
echo "  compiler $(c++ --version | head -1)"
echo "  date $(date -u +%Y-%m-%dT%H:%M:%SZ)"

echo "== running (block-frames=128, 44100 + 48000 Hz) =="
"$scratch/analysis_worker_benchmark" \
    --plugin=btrack="$btrack_so" --plugin-sha=btrack="$btrack_sha" \
    --plugin=aubio="$aubio_so"  --plugin-sha=aubio="$aubio_sha" \
    --rate=44100 --rate=48000 \
    --bpm=120 --seconds=5 --block-frames=128 \
    --ring-capacity=64 --burst=32 --mode=both \
    --source-sha="$source_sha" \
    --out="$out_dir"

# Reproducible manifest of everything that went into the run.
{
    echo "plugin_dir $plugin_dir"
    echo "source_sha256 $source_sha"
    echo "btrack_sha256 $btrack_sha"
    echo "aubio_sha256 $aubio_sha"
    echo "compiler $(${CXX:-c++} --version | head -1)"
    echo "date_utc $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out_dir/manifest.txt"

# Own-artifact budget: the whole results directory must stay <= 1 MiB.
bytes="$(du -sb "$out_dir" | awk '{print $1}')"
echo "== results ($bytes bytes) =="
if [ "$bytes" -gt 1048576 ]; then
    echo "error: results directory exceeds the 1 MiB budget" >&2
    exit 1
fi

cat "$out_dir/manifest.txt"
echo
cat "$out_dir/throughput.csv"
