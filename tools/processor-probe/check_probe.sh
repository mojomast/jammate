#!/usr/bin/env bash
# RT-002 probe - manually executed failure checks (no CMake/ctest wiring).
#
# Verifies that the probe fails closed on malformed CLI input and on output
# failures, and succeeds on the bounded contract cases. Exits non-zero if any
# expectation is violated. Intended to be run and its output archived by hand.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR:-/home/mojo/projects/guitars-build-resume/plugin}"
RT002_OUT="${RT002_OUT:-$(dirname "$PLUGIN_BUILD_DIR")/probe/build}"
RT002_SCRATCH="${RT002_SCRATCH:-$(dirname "$PLUGIN_BUILD_DIR")/probe/run}"
BIN="$RT002_OUT/processor_probe"
BOUND=30

if [ ! -x "$BIN" ]; then
    echo "error: probe binary not built: $BIN" >&2
    exit 1
fi

mkdir -p "$RT002_SCRATCH/home" "$RT002_SCRATCH/xdg/config" \
         "$RT002_SCRATCH/xdg/data" "$RT002_SCRATCH/xdg/cache" "$RT002_SCRATCH/tmp"

run() {
    timeout "$BOUND" env -i \
        PATH=/usr/bin:/bin \
        HOME="$RT002_SCRATCH/home" \
        XDG_CONFIG_HOME="$RT002_SCRATCH/xdg/config" \
        XDG_DATA_HOME="$RT002_SCRATCH/xdg/data" \
        XDG_CACHE_HOME="$RT002_SCRATCH/xdg/cache" \
        TMPDIR="$RT002_SCRATCH/tmp" \
        "$BIN" "$@"
}

fails=0
check_exit() {
    local want="$1"; shift
    local desc="$1"; shift
    local out
    out="$(run "$@" 2>&1)"
    local got=$?
    if [ "$got" -eq "$want" ]; then
        echo "PASS  exit=$got  $desc"
    else
        echo "FAIL  exit=$got (want $want)  $desc"
        fails=$((fails + 1))
    fi
}

check_contains() {
    local needle="$1"; shift
    local desc="$1"; shift
    local out
    out="$(run "$@" 2>&1)"
    local got=$?
    if [ "$got" -eq 0 ] && printf '%s' "$out" | grep -qF "$needle"; then
        echo "PASS  contains '$needle'  $desc"
    else
        echo "FAIL  exit=$got missing '$needle'  $desc"
        fails=$((fails + 1))
    fi
}

echo "RT-002 probe failure checks"
echo "binary: $BIN"
echo

# malformed CLI -> exit 64
check_exit 64 "--warm-blocks 0"          --warm-blocks 0
check_exit 64 "--warm-blocks -5"         --warm-blocks -5
check_exit 64 "--warm-blocks abc"        --warm-blocks abc
check_exit 64 "--warm-blocks 100001"     --warm-blocks 100001
check_exit 64 "--warm-blocks (missing)"  --warm-blocks
check_exit 64 "unknown option"           --not-an-option
check_exit 64 "unknown scene mode"       --scene-mode bogus

# output failure -> non-zero
check_exit 5  "unwriteable CSV path (full run)" --warm-blocks 1 --csv /nonexistent_dir_rt002/x.csv

# contract cases -> exit 0 and expected evidence
TMPD="$RT002_SCRATCH/check"
mkdir -p "$TMPD"
check_contains "self-check PASS" "full warm=1 self-check" --warm-blocks 1 --csv "$TMPD/ok.csv" --json "$TMPD/ok.json"
check_contains "scene[audio]" "full warm=1 scene" --warm-blocks 1 --csv "$TMPD/ok2.csv" --json "$TMPD/ok2.json"
check_contains "expectation=OK" "noaudio short" --scene-mode scene-noaudio-short --json "$TMPD/ns.json"
check_contains "expectation=OK" "noaudio long"  --scene-mode scene-noaudio-long  --json "$TMPD/nl.json"

echo
if [ "$fails" -eq 0 ]; then
    echo "ALL FAILURE CHECKS PASSED"
    exit 0
fi
echo "$fails FAILURE CHECK(S) FAILED"
exit 1
