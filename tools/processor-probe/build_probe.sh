#!/usr/bin/env bash
# RT-002 processor runtime probe - independent local build.
#
# Reuses the already-built, read-only plugin artefacts under
#   /home/mojo/projects/guitars-build-resume/plugin
# specifically the self-contained shared-code static archive that already
# contains the pinned JUCE modules plus the compiled GuitarCompanionProcessor.
# It does not rebuild JUCE, does not modify any shared source and does not write
# into the plugin build directory.
#
# The exact compiler flags/defines/includes of the production
# PluginProcessor.cpp translation unit are read from the existing build.ninja so
# the probe sees the same JUCE configuration as the archive it links against.
#
# Env overrides:
#   PLUGIN_BUILD_DIR  default /home/mojo/projects/guitars-build-resume/plugin
#   RT002_OUT         default <parent of PLUGIN_BUILD_DIR>/probe/build
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR:-/home/mojo/projects/guitars-build-resume/plugin}"
RT002_OUT="${RT002_OUT:-$(dirname "$PLUGIN_BUILD_DIR")/probe/build}"

SHARED_ARCHIVE="$PLUGIN_BUILD_DIR/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a"
NAM_ARCHIVE="$PLUGIN_BUILD_DIR/libnam_core.a"
ASSETS_ARCHIVE="$PLUGIN_BUILD_DIR/libGuitarCompanionAssets.a"
BUILD_NINJA="$PLUGIN_BUILD_DIR/build.ninja"
SYSROOT_LIB="$(dirname "$PLUGIN_BUILD_DIR")/sysroot/usr/lib/x86_64-linux-gnu"

for f in "$SHARED_ARCHIVE" "$NAM_ARCHIVE" "$ASSETS_ARCHIVE" "$BUILD_NINJA"; do
    if [ ! -f "$f" ]; then
        echo "error: expected prebuilt artefact not found: $f" >&2
        echo "build the plugin once (see docs/research/LOCAL-LINUX-BUILD.md), or set PLUGIN_BUILD_DIR" >&2
        exit 1
    fi
done

mkdir -p "$RT002_OUT"

# --- extract the exact production compile flags -------------------------------
python3 - "$BUILD_NINJA" "$RT002_OUT/plugin-cxx-flags.env" <<'PY'
import sys, re, shlex
ninja, out = sys.argv[1], sys.argv[2]
lines = open(ninja).read().split('\n')
target = 'build CMakeFiles/GuitarCompanion.dir/src/PluginProcessor.cpp.o:'
i = next((k for k, l in enumerate(lines) if l.startswith(target)), None)
if i is None:
    sys.exit('could not find PluginProcessor.cpp.o compile rule in build.ninja')
vals = {'DEFINES': '', 'FLAGS': '', 'INCLUDES': ''}
j = i + 1
while j < len(lines):
    ln = lines[j]
    if ln.startswith('build ') and not ln.startswith('  '):
        break
    m = re.match(r'\s*(DEFINES|FLAGS|INCLUDES) = (.*)', ln)
    if m:
        vals[m.group(1)] = m.group(2)
    j += 1
with open(out, 'w') as f:
    for k in ('FLAGS', 'INCLUDES', 'DEFINES'):
        f.write(f'{k}={shlex.quote(vals[k])}\n')
PY

# shellcheck disable=SC1090
source "$RT002_OUT/plugin-cxx-flags.env"

echo "RT-002 probe build"
echo "  repo root    : $REPO_ROOT"
echo "  plugin build : $PLUGIN_BUILD_DIR"
echo "  output dir   : $RT002_OUT"
echo "  archive sha  : $(sha256sum "$SHARED_ARCHIVE" | cut -d' ' -f1)"
echo

CXX="${CXX:-g++}"
SRC="$SCRIPT_DIR/src"
OBJ_DIR="$RT002_OUT/obj"
mkdir -p "$OBJ_DIR"

# Probe TUs must keep default visibility so the global operator new/delete
# replacements win over the weak ones in libstdc++.
PROBE_FLAGS="$FLAGS -fvisibility=default -Wno-frame-address"
INCLUDES_ALL="$INCLUDES -I$REPO_ROOT/src -I$SRC"

echo "compiling instrumentation ..."
eval "$CXX $PROBE_FLAGS $INCLUDES_ALL $DEFINES -c \"$SRC/RtProbeInstrumentation.cpp\" -o \"$OBJ_DIR/RtProbeInstrumentation.o\""

echo "compiling probe ..."
eval "$CXX $PROBE_FLAGS $INCLUDES_ALL $DEFINES -c \"$SRC/ProcessorProbe.cpp\" -o \"$OBJ_DIR/ProcessorProbe.o\""

echo "linking ..."
WRAPS="-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free"
WRAPS="$WRAPS -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_trylock"
WRAPS="$WRAPS -Wl,--wrap=pthread_mutex_unlock -Wl,--wrap=pthread_cond_clockwait"

# shellcheck disable=SC2086
"$CXX" $WRAPS \
    "$OBJ_DIR/RtProbeInstrumentation.o" "$OBJ_DIR/ProcessorProbe.o" \
    "$SHARED_ARCHIVE" \
    -Wl,--push-state,--whole-archive "$NAM_ARCHIVE" -Wl,--pop-state \
    "$ASSETS_ARCHIVE" \
    "$SYSROOT_LIB/libasound.so" "$SYSROOT_LIB/libfontconfig.so" "$SYSROOT_LIB/libfreetype.so" \
    -lrt -ldl -lpthread \
    -o "$RT002_OUT/processor_probe"

echo
echo "built: $RT002_OUT/processor_probe"
sha256sum "$RT002_OUT/processor_probe"
