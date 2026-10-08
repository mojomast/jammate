#!/usr/bin/env bash
# RT-003 patch-provenance checks (GNU patch / Linux scope; see README).
#
#   1. The tracked unified patch, applied to a fresh copy of the pinned NAM
#      sources, reproduces the configure-time generated overlay byte-for-byte.
#   2. Overlay generation is idempotent on the SAME build directory (repeated
#      configure re-seeds the existing patched copy) and equals the built
#      overlay.
#   3. Every copied NAM source/header is a tracked dependency: editing a
#      harmless other NAM header in a scratch clone refreshes the generated
#      copy, and deleting it removes the generated file.
#   4. A mutated pinned source fails closed (input SHA guard).
#   5. A mutated tracked patch fails closed while the pinned sources are clean
#      (patch SHA guard, independent of the input hashes).
#
# Exits non-zero if any check fails.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAM_CORE_DIR=""
OVERLAY_DIR=""
PATCH_FILE=""
CMAKE_BIN="cmake"
SOURCE_DIR=""
WORKDIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --nam-core-dir) NAM_CORE_DIR="$2"; shift 2 ;;
        --overlay-dir)  OVERLAY_DIR="$2";  shift 2 ;;
        --patch)        PATCH_FILE="$2";   shift 2 ;;
        --cmake)        CMAKE_BIN="$2";    shift 2 ;;
        --source-dir)   SOURCE_DIR="$2";   shift 2 ;;
        --workdir)      WORKDIR="$2";      shift 2 ;;
        *) echo "unknown arg $1" >&2; exit 64 ;;
    esac
done

for v in NAM_CORE_DIR OVERLAY_DIR PATCH_FILE SOURCE_DIR WORKDIR; do
    if [ -z "${!v}" ]; then echo "missing --${v}" >&2; exit 64; fi
done

MODULE="${SOURCE_DIR}/../../cmake/nam-rt/NamRtPatch.cmake"
if [ ! -f "$MODULE" ]; then echo "missing module $MODULE" >&2; exit 1; fi

fail=0
ok()  { echo "PASS  $*"; }
bad() { echo "FAIL  $*"; fail=$((fail + 1)); }

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

echo "RT-003 patch checks"
echo "  nam core : $NAM_CORE_DIR"
echo "  overlay  : $OVERLAY_DIR"
echo "  patch    : $PATCH_FILE"

make_proj() {  # <dir> <module-abspath> <nam-core-dir>
    mkdir -p "$1"
    cat > "$1/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.22)
project(namrt_probe NONE)
include("$2")
nam_rt_generate_overlay("$3")
EOF
}

# --- 1. tracked patch reproduces the overlay ---------------------------------
PT="$WORKDIR/patchtree"
mkdir -p "$PT/NAM"
cp "$NAM_CORE_DIR/NAM/lstm.cpp" "$NAM_CORE_DIR/NAM/lstm.h" "$PT/NAM/"
if (cd "$PT" && patch -p1 --silent < "$PATCH_FILE"); then
    if cmp -s "$PT/NAM/lstm.cpp" "$OVERLAY_DIR/lstm.cpp" \
       && cmp -s "$PT/NAM/lstm.h" "$OVERLAY_DIR/lstm.h"; then
        ok "tracked patch reproduces generated overlay byte-for-byte"
    else
        bad "tracked patch output differs from generated overlay"
    fi
else
    bad "tracked patch did not apply cleanly to pinned sources"
fi

# --- 2. same-build idempotence ------------------------------------------------
make_proj "$WORKDIR/idem-src" "$MODULE" "$NAM_CORE_DIR"
"$CMAKE_BIN" -S "$WORKDIR/idem-src" -B "$WORKDIR/idem-b" > "$WORKDIR/idem-1.log" 2>&1 || bad "first configure failed"
GEN="$WORKDIR/idem-b/nam-rt/generated/NAM"
h1=$(sha256sum "$GEN/lstm.cpp" "$GEN/lstm.h" | sha256sum)
# second configure on the SAME build dir must succeed and not double-apply
"$CMAKE_BIN" -S "$WORKDIR/idem-src" -B "$WORKDIR/idem-b" > "$WORKDIR/idem-2.log" 2>&1 || bad "second configure on same build dir failed"
h2=$(sha256sum "$GEN/lstm.cpp" "$GEN/lstm.h" | sha256sum)
if [ "$h1" = "$h2" ] && cmp -s "$GEN/lstm.cpp" "$OVERLAY_DIR/lstm.cpp" \
   && cmp -s "$GEN/lstm.h" "$OVERLAY_DIR/lstm.h"; then
    ok "same-build reconfigure idempotent and equals the built overlay"
else
    bad "same-build reconfigure not idempotent"
fi

# --- 3. dependency tracking of other NAM headers (scratch clone) --------------
CLONE="$WORKDIR/clone"
mkdir -p "$CLONE"
cp -r "$NAM_CORE_DIR/NAM" "$CLONE/NAM"
make_proj "$WORKDIR/dep-src" "$MODULE" "$CLONE"
"$CMAKE_BIN" -S "$WORKDIR/dep-src" -B "$WORKDIR/dep-b" > "$WORKDIR/dep-1.log" 2>&1 || bad "clone configure failed"
DGN="$WORKDIR/dep-b/nam-rt/generated/NAM"
printf '\n// rt003 dependency-refresh marker\n' >> "$CLONE/NAM/util.h"
"$CMAKE_BIN" -S "$WORKDIR/dep-src" -B "$WORKDIR/dep-b" > "$WORKDIR/dep-2.log" 2>&1 || bad "clone reconfigure failed"
if grep -q "rt003 dependency-refresh marker" "$DGN/util.h"; then
    ok "editing a copied NAM header refreshes the generated overlay"
else
    bad "generated overlay did not refresh after other NAM header edit"
fi
rm -f "$CLONE/NAM/util.h"
"$CMAKE_BIN" -S "$WORKDIR/dep-src" -B "$WORKDIR/dep-b" > "$WORKDIR/dep-3.log" 2>&1 || bad "clone reconfigure (delete) failed"
if [ ! -e "$DGN/util.h" ]; then
    ok "deleting an upstream NAM file removes the generated stale copy"
else
    bad "generated overlay kept a stale copy of a deleted NAM file"
fi

# --- 4. mutated pinned source fails closed -----------------------------------
MUT="$WORKDIR/mutated"
mkdir -p "$MUT/NAM"
cp "$NAM_CORE_DIR/NAM/lstm.cpp" "$MUT/NAM/lstm.cpp"
cp "$NAM_CORE_DIR/NAM/lstm.h" "$MUT/NAM/lstm.h"
printf '// rt003 staleness probe\n' >> "$MUT/NAM/lstm.cpp"
make_proj "$WORKDIR/mut-src" "$MODULE" "$MUT"
set +e
"$CMAKE_BIN" -S "$WORKDIR/mut-src" -B "$WORKDIR/mut-b" > "$WORKDIR/mut.log" 2>&1
mut_rc=$?
set -e
if [ "$mut_rc" -ne 0 ] && grep -q "pinned upstream lstm.cpp SHA256 mismatch" "$WORKDIR/mut.log"; then
    ok "mutated pinned source fails closed with input SHA256 mismatch"
else
    bad "mutated pinned source did not fail closed (rc=$mut_rc)"
    tail -8 "$WORKDIR/mut.log" || true
fi

# --- 5. mutated tracked patch fails closed with clean pinned sources ----------
FAKEMOD="$WORKDIR/fakemod"
mkdir -p "$FAKEMOD/cmake/nam-rt" "$FAKEMOD/patches/nam"
cp "$MODULE" "$FAKEMOD/cmake/nam-rt/NamRtPatch.cmake"
cp "$PATCH_FILE" "$FAKEMOD/patches/nam/lstm-rt-alloc.patch"
printf '# rt003 mutated-patch probe\n' >> "$FAKEMOD/patches/nam/lstm-rt-alloc.patch"
make_proj "$WORKDIR/pmut-src" "$FAKEMOD/cmake/nam-rt/NamRtPatch.cmake" "$NAM_CORE_DIR"
set +e
"$CMAKE_BIN" -S "$WORKDIR/pmut-src" -B "$WORKDIR/pmut-b" > "$WORKDIR/pmut.log" 2>&1
pmut_rc=$?
set -e
if [ "$pmut_rc" -ne 0 ] && grep -q "tracked overlay patch SHA256 mismatch" "$WORKDIR/pmut.log"; then
    ok "mutated tracked patch fails closed (pinned sources clean)"
else
    bad "mutated tracked patch did not fail closed (rc=$pmut_rc)"
    tail -8 "$WORKDIR/pmut.log" || true
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "ALL RT-003 PATCH CHECKS PASSED"
    exit 0
fi
echo "$fail RT-003 PATCH CHECK(S) FAILED"
exit 1
