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
# The worktree src/ include is placed FIRST, and every source file the probe
# compiles against is script-verified byte-identical to the main checkout from
# which the archive was built; the check fails closed on any difference.
#
# Env overrides:
#   PLUGIN_BUILD_DIR  default /home/mojo/projects/guitars-build-resume/plugin
#   RT002_OUT         default <parent of PLUGIN_BUILD_DIR>/probe/build
#   RT002_MAIN_REPO   default /home/mojo/projects/guitars (archive source pin)
#   RT002_SYSROOT_LIB default <parent of PLUGIN_BUILD_DIR>/sysroot/usr/lib/x86_64-linux-gnu
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR:-/home/mojo/projects/guitars-build-resume/plugin}"
RT002_OUT="${RT002_OUT:-$(dirname "$PLUGIN_BUILD_DIR")/probe/build}"
RT002_MAIN_REPO="${RT002_MAIN_REPO:-/home/mojo/projects/guitars}"
RT002_SYSROOT_LIB="${RT002_SYSROOT_LIB:-$(dirname "$PLUGIN_BUILD_DIR")/sysroot/usr/lib/x86_64-linux-gnu}"

SHARED_ARCHIVE="$PLUGIN_BUILD_DIR/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a"
NAM_ARCHIVE="$PLUGIN_BUILD_DIR/libnam_core.a"
ASSETS_ARCHIVE="$PLUGIN_BUILD_DIR/libGuitarCompanionAssets.a"
BUILD_NINJA="$PLUGIN_BUILD_DIR/build.ninja"

for f in "$SHARED_ARCHIVE" "$NAM_ARCHIVE" "$ASSETS_ARCHIVE" "$BUILD_NINJA"; do
    if [ ! -f "$f" ]; then
        echo "error: expected prebuilt artefact not found: $f" >&2
        echo "build the plugin once (see docs/research/LOCAL-LINUX-BUILD.md), or set PLUGIN_BUILD_DIR" >&2
        exit 1
    fi
done

mkdir -p "$RT002_OUT"

# --- scripted source pin: the worktree headers/sources must be byte-identical
# to the main checkout the archive was compiled from. Fails closed otherwise.
PINNED_SOURCES=(
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/DrumEngine.cpp
    src/DrumEngine.h
    src/rt/RtSignal.h
)
PIN_FILE="$RT002_OUT/source-pin.txt"
{
    echo "base_commit=cd9f97f"
    echo "rt001_f2_implementation=677ce9f"
    echo "worktree_head=$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "main_repo=$RT002_MAIN_REPO"
    echo "shared_archive=$SHARED_ARCHIVE"
    echo "shared_archive_sha256=$(sha256sum "$SHARED_ARCHIVE" | cut -d' ' -f1)"
    echo "nam_archive_sha256=$(sha256sum "$NAM_ARCHIVE" | cut -d' ' -f1)"
    echo "assets_archive_sha256=$(sha256sum "$ASSETS_ARCHIVE" | cut -d' ' -f1)"
    for rel in "${PINNED_SOURCES[@]}"; do
        if [ ! -f "$REPO_ROOT/$rel" ]; then
            echo "error: missing pinned source $rel" >&2
            exit 1
        fi
        if [ -f "$RT002_MAIN_REPO/$rel" ]; then
            if ! cmp -s "$REPO_ROOT/$rel" "$RT002_MAIN_REPO/$rel"; then
                echo "error: $rel differs from the archive source pin at $RT002_MAIN_REPO/$rel" >&2
                echo "       the prebuilt archive was compiled from that checkout; refusing to link a stale header" >&2
                exit 1
            fi
            echo "$rel sha256=$(sha256sum "$REPO_ROOT/$rel" | cut -d' ' -f1) main_identical=yes"
        else
            echo "$rel sha256=$(sha256sum "$REPO_ROOT/$rel" | cut -d' ' -f1) main_identical=unknown(missing at pin)"
        fi
    done
} > "$PIN_FILE"
echo "source pin written: $PIN_FILE"

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
echo "  main pin     : $RT002_MAIN_REPO"
echo "  sysroot lib  : $RT002_SYSROOT_LIB"
echo "  archive sha  : $(sha256sum "$SHARED_ARCHIVE" | cut -d' ' -f1)"
echo

CXX="${CXX:-g++}"
SRC="$SCRIPT_DIR/src"
OBJ_DIR="$RT002_OUT/obj"
mkdir -p "$OBJ_DIR"

# Probe TUs must keep default visibility so the global operator new/delete
# replacements win over the weak ones in libstdc++. The worktree src/ include
# is prepended so it always wins over any production include path.
PROBE_FLAGS="$FLAGS -fvisibility=default -Wno-frame-address"
INCLUDES_ALL="-I$REPO_ROOT/src $INCLUDES -I$SRC"

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
    "$RT002_SYSROOT_LIB/libasound.so" "$RT002_SYSROOT_LIB/libfontconfig.so" "$RT002_SYSROOT_LIB/libfreetype.so" \
    -lrt -ldl -lpthread \
    -o "$RT002_OUT/processor_probe"

echo
echo "built: $RT002_OUT/processor_probe"
sha256sum "$RT002_OUT/processor_probe" | tee "$RT002_OUT/probe-binary.sha256"
