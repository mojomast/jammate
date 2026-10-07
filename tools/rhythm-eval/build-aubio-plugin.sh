#!/usr/bin/env bash
# Build the GPLv3-or-later aubio backend as a dlopen()-able plugin for rhythm-eval.
#
# Mirrors tools/rhythm-eval/build-btrack-plugin.sh. Why a script and not CMake:
# the tool's CMakeLists.txt is deliberately left untouched (the evaluation brief
# freezes every CMake file), and jam-core's CMake already builds
# `libjam-aubio.a` (and its vendored `libaubio.a`) under
# -DJAM_ENABLE_AUBIO=ON. This shim only has to wrap that static library in a
# shared object exporting the plugin convention the CLI documents:
#
#   extern "C" jam::IRhythmTracker* jam_rhythm_create();
#   extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
#
# The CLI itself stays free of GPL code (SPEC.md section 25.6). Nothing here is
# committed except the source of the shim (AubioPlugin.cpp) and this script.
#
#   tools/rhythm-eval/build-aubio-plugin.sh [jam-core-build-dir] [output.so]
#
# Defaults: build dir /tmp/opencode/eval004-core, output in that build dir.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${1:-/tmp/opencode/eval004-core}"
out="${2:-$build/librhythm-eval-aubio.so}"

cmake -S "$root/jam-core" -B "$build" -G Ninja -DJAM_ENABLE_AUBIO=ON
cmake --build "$build" --target jam-aubio

# Static link order mirrors jam-aubio's own dependency graph
# (jam-aubio -> aubio); a group removes any left-to-right ordering risk.
# aubio's bundled Ooura FFT means no FFT/IO backend is needed.
c++ -std=c++17 -O2 -fPIC -shared -o "$out" \
    "$here/AubioPlugin.cpp" \
    -I"$root/src" \
    -Wl,--start-group \
    "$build/aubio/libjam-aubio.a" \
    "$build/aubio/libaubio.a" \
    -Wl,--end-group \
    -lm -lpthread

echo "wrote $out"
