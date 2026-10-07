#!/usr/bin/env bash
# Build the GPLv3 BTrack backend as a dlopen()-able plugin for rhythm-eval.
#
# Why a script and not CMake: the tool's CMakeLists.txt is deliberately left
# untouched (the EVAL-002R brief freezes every CMake file), and jam-core's CMake
# already builds `libjam-btrack.a` under -DJAM_ENABLE_BTRACK=ON. This shim only
# has to wrap that static library in a shared object exporting the plugin
# convention the CLI already documents:
#
#   extern "C" jam::IRhythmTracker* jam_rhythm_create();
#   extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
#
# The CLI itself stays free of GPL code (SPEC.md section 25.6). Nothing here is
# committed except the source of the shim (BtrackPlugin.cpp) and this script.
#
#   tools/rhythm-eval/build-btrack-plugin.sh [jam-core-build-dir] [output.so]
#
# Defaults: build dir /tmp/opencode/build-e2r, output in that build dir.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${1:-/tmp/opencode/build-e2r}"
out="${2:-$build/librhythm-eval-btrack.so}"

cmake -S "$root/jam-core" -B "$build" -G Ninja -DJAM_ENABLE_BTRACK=ON
cmake --build "$build" --target jam-btrack

# Static link order mirrors jam-btrack's own dependency graph
# (jam-btrack -> btrack -> kiss_fft + samplerate); a group removes any
# left-to-right ordering risk.
c++ -std=c++17 -O2 -fPIC -shared -o "$out" \
    "$here/BtrackPlugin.cpp" \
    -I"$root/src" -I"$root/third_party/BTrack/src" -I"$build/btrack" \
    -Wl,--start-group \
    "$build/btrack/libjam-btrack.a" \
    "$build/btrack/libbtrack.a" \
    "$build/btrack/libsamplerate.a" \
    "$build/btrack/libkiss_fft.a" \
    -Wl,--end-group \
    -lm -lpthread

echo "wrote $out"
