#!/usr/bin/env bash
# Build the TRACK-004 tracker-diagnostics tool and its arithmetic tests.
#
# No CMake file is created or edited: this mirrors the existing
# tools/rhythm-eval/build-*-plugin.sh convention and compiles the reused,
# unmodified evaluation objects (Metrics.cpp, Manifest.cpp, BackendRunner.cpp)
# straight into the diagnostic. The tracker backends are dlopen()ed at run time
# through the existing plugin convention, so no tracker source is compiled into
# the tool binary (a structural separation, not a legal conclusion).
#
#   tools/tracker-diagnostics/build.sh [jam-core-build-dir] [output-dir]
#
# Defaults: core = /home/mojo/projects/build-EVAL-005/main-core
#           out  = /home/mojo/projects/build-TRACK-004/diag
#
# Set JAM_DIAG_BUILD_CONFIG_PLUGINS=0 to skip the two config-experiment shims.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

core="${1:-${JAM_DIAG_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"
out="${2:-${JAM_DIAG_OUT:-/home/mojo/projects/build-TRACK-004/diag}}"

evaldir="$root/tools/rhythm-eval"
mkdir -p "$out"
mkdir -p "$out/obj"

cxx="${CXX:-c++}"
std="-std=c++17 -O2 -Wall -Wextra -Wpedantic"
incs="-I$here -I$evaldir -I$root/src"

echo "[build] diagnostic objects"
common_srcs=(
    "$here/TraceRunner.cpp"
    "$here/AcquisitionReplay.cpp"
    "$here/BtrackGrid.cpp"
    "$here/SyntheticClick.cpp"
    "$evaldir/Metrics.cpp"
    "$evaldir/Manifest.cpp"
    "$evaldir/BackendRunner.cpp"
)
objs=()
for src in "${common_srcs[@]}"; do
    obj="$out/obj/$(basename "${src%.cpp}").o"
    $cxx $std $incs -c "$src" -o "$obj"
    objs+=("$obj")
done

echo "[build] tracker-diagnostics"
$cxx $std $incs "$here/main.cpp" "${objs[@]}" -o "$out/tracker-diagnostics" -ldl

echo "[build] TraceReplayTests"
test_objs=()
for src in TraceRunner AcquisitionReplay BtrackGrid; do
    test_objs+=("$out/obj/$src.o")
done
$cxx $std $incs "$here/tests/TraceReplayTests.cpp" \
    "$out/obj/Metrics.o" "$out/obj/BackendRunner.o" "${test_objs[@]}" \
    -o "$out/TraceReplayTests"

# --- optional config-experiment shims (NOT default evidence) ----------------
if [ "${JAM_DIAG_BUILD_CONFIG_PLUGINS:-1}" = "1" ]; then
    if [ -f "$core/btrack/libjam-btrack.a" ]; then
        echo "[build] config shim (btrack)"
        $cxx -std=c++17 -O2 -fPIC -shared -o "$out/libtracker-diag-config-btrack.so" \
            "$here/ConfigPlugin.cpp" -DJAM_DIAG_BTRACK=1 \
            -I"$root/src" -I"$root/third_party/BTrack/src" -I"$core/btrack" \
            -Wl,--start-group \
            "$core/btrack/libjam-btrack.a" "$core/btrack/libbtrack.a" \
            "$core/btrack/libsamplerate.a" "$core/btrack/libkiss_fft.a" \
            -Wl,--end-group -lm -lpthread
    fi
    if [ -f "$core/aubio/libjam-aubio.a" ]; then
        echo "[build] config shim (aubio)"
        $cxx -std=c++17 -O2 -fPIC -shared -o "$out/libtracker-diag-config-aubio.so" \
            "$here/ConfigPlugin.cpp" \
            -I"$root/src" \
            -Wl,--start-group \
            "$core/aubio/libjam-aubio.a" "$core/aubio/libaubio.a" \
            -Wl,--end-group -lm -lpthread
    fi
fi

echo "[build] ok -> $out"
echo "  $out/tracker-diagnostics"
echo "  $out/TraceReplayTests"
