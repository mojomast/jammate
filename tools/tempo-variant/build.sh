#!/usr/bin/env bash
# Build the TRACK-005 tempo-report variant: the diagnostic plugin, the method
# tests and the click/tempo-step CLI.
#
# No CMake file is created or edited. The variant plugin links the SAME pinned
# EVAL-005 main-core static archives the TRACK-004 diagnostic used, so the only
# behavioural difference from the default btrack plugin is the decorator.
#
#   tools/tempo-variant/build.sh [core-dir] [output-dir]
#
# Defaults: core = /home/mojo/projects/build-EVAL-005/main-core
#           out  = /home/mojo/projects/build-TRACK-005

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

core="${1:-${JAM_TV_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"
out="${2:-${JAM_TV_OUT:-/home/mojo/projects/build-TRACK-005}}"

mkdir -p "$out/obj"

cxx="${CXX:-c++}"
std="-std=c++17 -O2 -fPIC -Wall -Wextra -Wpedantic"
incs="-I$here -I$root/src -I$root/tools/rhythm-eval"

echo "[build] variant objects"
for src in TempoVariant MethodLog; do
    $cxx $std $incs -c "$here/$src.cpp" -o "$out/obj/$src.o"
done

echo "[build] libtempo-variant-btrack.so"
$cxx -std=c++17 -O2 -fPIC -shared -o "$out/libtempo-variant-btrack.so" \
    "$here/TempoVariantPlugin.cpp" \
    "$out/obj/TempoVariant.o" "$out/obj/MethodLog.o" \
    -I"$root/src" -I"$root/third_party/BTrack/src" -I"$core/btrack" \
    -Wl,--start-group \
    "$core/btrack/libjam-btrack.a" "$core/btrack/libbtrack.a" \
    "$core/btrack/libsamplerate.a" "$core/btrack/libkiss_fft.a" \
    -Wl,--end-group -lm -lpthread

echo "[build] TempoVariantTests"
$cxx $std $incs "$here/tests/TempoVariantTests.cpp" \
    "$out/obj/TempoVariant.o" "$out/obj/MethodLog.o" \
    -o "$out/TempoVariantTests"

echo "[build] tempo-variant-click"
$cxx $std $incs "$here/main.cpp" "$here/ClickTrain.cpp" \
    "$out/obj/TempoVariant.o" "$out/obj/MethodLog.o" \
    -o "$out/tempo-variant-click" -ldl

echo "[build] tempo-variant-metrics (unmodified rhythmeval source maths)"
for src in Metrics Manifest BackendRunner; do
    $cxx $std $incs -c "$root/tools/rhythm-eval/$src.cpp" -o "$out/obj/eval-$src.o"
done
$cxx $std $incs "$here/FixtureMetricsDump.cpp" \
    "$out/obj/eval-Metrics.o" "$out/obj/eval-Manifest.o" "$out/obj/eval-BackendRunner.o" \
    -o "$out/tempo-variant-metrics" -ldl

echo "[build] ok -> $out"
echo "  $out/libtempo-variant-btrack.so"
echo "  $out/TempoVariantTests"
echo "  $out/tempo-variant-click"
echo "  $out/tempo-variant-metrics"
