#!/usr/bin/env bash
# Build the TRACK-008 post-readiness stability candidate: the diagnostic plugin
# over the pinned BTrack adapter, and the deterministic method tests.
#
# No CMake file is created or edited. The candidate plugin links the SAME pinned
# EVAL-005 main-core static archives the pinned default/variant plugins used, so
# the only behavioural difference from the default btrack plugin is the
# TempoStableTracker decorator. The plugin's id() is "btrack-tempo-stable".
#
#   tools/tempo-stability/build.sh [core-dir] [output-dir]
#
# Defaults: core = /home/mojo/projects/build-EVAL-005/main-core
#           out  = /home/mojo/projects/build-TRACK-008-worker

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

core="${1:-${JAM_TS_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"
out="${2:-${JAM_TS_OUT:-/home/mojo/projects/build-TRACK-008-worker}}"

mkdir -p "$out/obj"

cxx="${CXX:-c++}"
std="-std=c++17 -O2 -fPIC -Wall -Wextra -Wpedantic"
incs="-I$here -I$root/src -I$root/tools/rhythm-eval -I$root/tools/tracker-diagnostics"

echo "[build] candidate objects (<=2 jobs)"
for src in TempoStable MethodLog; do
    $cxx $std $incs -c "$here/$src.cpp" -o "$out/obj/$src.o"
done

echo "[build] libtempo-stable-btrack.so"
$cxx -std=c++17 -O2 -fPIC -shared -o "$out/libtempo-stable-btrack.so" \
    "$here/TempoStablePlugin.cpp" \
    "$out/obj/TempoStable.o" "$out/obj/MethodLog.o" \
    -I"$root/src" -I"$root/third_party/BTrack/src" -I"$core/btrack" \
    -Wl,--start-group \
    "$core/btrack/libjam-btrack.a" "$core/btrack/libbtrack.a" \
    "$core/btrack/libsamplerate.a" "$core/btrack/libkiss_fft.a" \
    -Wl,--end-group -lm -lpthread

echo "[build] TempoStableTests"
$cxx $std $incs "$here/tests/TempoStableTests.cpp" \
    "$out/obj/TempoStable.o" "$out/obj/MethodLog.o" \
    -o "$out/TempoStableTests"

echo "[build] ok -> $out"
echo "  $out/libtempo-stable-btrack.so"
echo "  $out/TempoStableTests"
