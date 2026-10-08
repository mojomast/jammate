#!/usr/bin/env bash
# TRACK-005 fail-closed checks: every malformed/invalid CLI argument must exit
# non-zero before any work, and a write failure (e.g. /dev/full) must be
# detected, not silently ignored. Exits non-zero if any check does not fail
# closed. No shared file is touched.
#
#   tools/tempo-variant/check_failclosed.sh [build-dir] [core-dir]

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

build="${1:-${JAM_TV_OUT:-/home/mojo/projects/build-TRACK-005}}"
core="${2:-${JAM_TV_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"

MET="$build/tempo-variant-metrics"
CLICK="$build/tempo-variant-click"
BT="$core/librhythm-eval-btrack.so"

fails=0
checks=0

expect_fail () { # label command...
    local label="$1"; shift
    checks=$((checks + 1))
    "$@" >/dev/null 2>&1
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "FAIL (expected non-zero): $label"
        fails=$((fails + 1))
    else
        echo "ok (exit $rc): $label"
    fi
}

expect_fail "metrics block 0"        "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /tmp/tv-x.json --block 0
expect_fail "metrics block 2049"     "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /tmp/tv-x.json --block 2049
expect_fail "metrics block 128x"     "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /tmp/tv-x.json --block 128x
expect_fail "metrics block -1"       "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /tmp/tv-x.json --block -1
expect_fail "metrics block overflow" "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /tmp/tv-x.json --block 18446744073709551744
expect_fail "metrics /dev/full"      "$MET" --corpus testdata/rhythm --backend-lib "$BT" --out /dev/full

expect_fail "click bad mode"         "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode bogus
expect_fail "click block 0"          "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --block 0
expect_fail "click block 128x"       "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --block 128x
expect_fail "click rate nan"         "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --rate nan
expect_fail "click rate 0"           "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --rate 0
expect_fail "click rate inf"         "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --rate inf
expect_fail "click rate out of range" "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --rate 300000
expect_fail "click seconds negative" "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --seconds -1
expect_fail "click seconds too big"  "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --seconds 100000
expect_fail "click sweep nan"        "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --sweep 126,nan
expect_fail "click sweep negative"   "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode sweep --sweep 126,-5
expect_fail "click step equal"       "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode step --step-from 126 --step-to 126
expect_fail "click step outside clip" "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode step --step-seconds 30
expect_fail "click step reversed window" "$CLICK" --backend-lib "$BT" --out /tmp/tv-out --mode step --step-seconds -3
expect_fail "click missing backend"  "$CLICK" --out /tmp/tv-out --mode sweep
expect_fail "click /dev/full out"    "$CLICK" --backend-lib "$BT" --out /dev/full/nope --mode sweep

echo "check_failclosed: $checks checks, $fails failures"
[ "$fails" -eq 0 ]
