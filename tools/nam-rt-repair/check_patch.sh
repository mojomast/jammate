#!/usr/bin/env bash
# NAM overlay patch-provenance checks (GNU patch / Linux scope; see README).
#
# RT-003 checks (retained) plus RT-005 activation-patch checks:
#   1. The tracked unified patches, applied in order to a fresh copy of the
#      pinned NAM sources, reproduce the configure-time generated overlay
#      byte-for-byte (lstm.cpp, lstm.h, activations.h, gating_activations.h).
#   2. Overlay generation is idempotent on the SAME build directory (repeated
#      configure re-seeds the existing patched copy) and equals the built
#      overlay.
#   3. Every copied NAM source/header is a tracked dependency: editing a
#      harmless other NAM header in a scratch clone refreshes the generated
#      copy, and deleting it removes the generated file.
#   4. A mutated pinned source fails closed (input SHA guard) for both the LSTM
#      and the activation inputs.
#   4a. A CRLF pinned source fails closed: the overlay SHA pins are over LF bytes,
#      so only LF inputs are accepted (the scope of the byte-identity claim).
#   5. A mutated tracked patch fails closed while the pinned sources are clean
#      (patch SHA guard, independent of the input hashes) for both patches.
#
# Exits non-zero if any check fails.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAM_CORE_DIR=""
OVERLAY_DIR=""
PATCH_FILE=""
ACTIVATION_PATCH_FILE=""
CMAKE_BIN="cmake"
SOURCE_DIR=""
WORKDIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --nam-core-dir)     NAM_CORE_DIR="$2"; shift 2 ;;
        --overlay-dir)      OVERLAY_DIR="$2";  shift 2 ;;
        --patch)            PATCH_FILE="$2";   shift 2 ;;
        --activation-patch) ACTIVATION_PATCH_FILE="$2"; shift 2 ;;
        --cmake)            CMAKE_BIN="$2";    shift 2 ;;
        --source-dir)       SOURCE_DIR="$2";   shift 2 ;;
        --workdir)          WORKDIR="$2";      shift 2 ;;
        *) echo "unknown arg $1" >&2; exit 64 ;;
    esac
done

for v in NAM_CORE_DIR OVERLAY_DIR PATCH_FILE ACTIVATION_PATCH_FILE SOURCE_DIR WORKDIR; do
    if [ -z "${!v}" ]; then echo "missing --${v}" >&2; exit 64; fi
done

MODULE="${SOURCE_DIR}/../../cmake/nam-rt/NamRtPatch.cmake"
if [ ! -f "$MODULE" ]; then echo "missing module $MODULE" >&2; exit 1; fi

fail=0
ok()  { echo "PASS  $*"; }
bad() { echo "FAIL  $*"; fail=$((fail + 1)); }

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

echo "NAM overlay patch checks (RT-003 + RT-005)"
echo "  nam core : $NAM_CORE_DIR"
echo "  overlay  : $OVERLAY_DIR"
echo "  patch    : $PATCH_FILE"
echo "  act patch: $ACTIVATION_PATCH_FILE"

make_proj() {  # <dir> <module-abspath> <nam-core-dir>
    mkdir -p "$1"
    cat > "$1/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.22)
project(namrt_probe NONE)
include("$2")
nam_rt_generate_overlay("$3")
EOF
}

OVERLAY_FILES="lstm.cpp lstm.h activations.h gating_activations.h"

# --- 1. tracked patches reproduce the overlay --------------------------------
PT="$WORKDIR/patchtree"
mkdir -p "$PT/NAM"
cp "$NAM_CORE_DIR/NAM/lstm.cpp" "$NAM_CORE_DIR/NAM/lstm.h" \
   "$NAM_CORE_DIR/NAM/activations.h" "$NAM_CORE_DIR/NAM/gating_activations.h" "$PT/NAM/"
if (cd "$PT" && patch -p1 --silent < "$PATCH_FILE" && patch -p1 --silent < "$ACTIVATION_PATCH_FILE"); then
    same=1
    for f in $OVERLAY_FILES; do
        cmp -s "$PT/NAM/$f" "$OVERLAY_DIR/$f" || same=0
    done
    if [ "$same" -eq 1 ]; then
        ok "tracked patches reproduce generated overlay byte-for-byte"
    else
        bad "tracked patch output differs from generated overlay"
    fi
else
    bad "tracked patches did not apply cleanly to pinned sources"
fi

# --- 2. same-build idempotence ------------------------------------------------
make_proj "$WORKDIR/idem-src" "$MODULE" "$NAM_CORE_DIR"
"$CMAKE_BIN" -S "$WORKDIR/idem-src" -B "$WORKDIR/idem-b" > "$WORKDIR/idem-1.log" 2>&1 || bad "first configure failed"
GEN="$WORKDIR/idem-b/nam-rt/generated/NAM"
h1=$(sha256sum $(for f in $OVERLAY_FILES; do echo "$GEN/$f"; done) | sha256sum)
# second configure on the SAME build dir must succeed and not double-apply
"$CMAKE_BIN" -S "$WORKDIR/idem-src" -B "$WORKDIR/idem-b" > "$WORKDIR/idem-2.log" 2>&1 || bad "second configure on same build dir failed"
h2=$(sha256sum $(for f in $OVERLAY_FILES; do echo "$GEN/$f"; done) | sha256sum)
same=1
for f in $OVERLAY_FILES; do
    cmp -s "$GEN/$f" "$OVERLAY_DIR/$f" || same=0
done
if [ "$h1" = "$h2" ] && [ "$same" -eq 1 ]; then
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
cp "$NAM_CORE_DIR/NAM/activations.h" "$MUT/NAM/activations.h"
cp "$NAM_CORE_DIR/NAM/gating_activations.h" "$MUT/NAM/gating_activations.h"
printf '// rt003 staleness probe\n' >> "$MUT/NAM/lstm.cpp"
make_proj "$WORKDIR/mut-src" "$MODULE" "$MUT"
set +e
"$CMAKE_BIN" -S "$WORKDIR/mut-src" -B "$WORKDIR/mut-b" > "$WORKDIR/mut.log" 2>&1
mut_rc=$?
set -e
if [ "$mut_rc" -ne 0 ] && grep -q "pinned upstream lstm.cpp SHA256 mismatch" "$WORKDIR/mut.log"; then
    ok "mutated pinned lstm.cpp fails closed with input SHA256 mismatch"
else
    bad "mutated pinned lstm.cpp did not fail closed (rc=$mut_rc)"
    tail -8 "$WORKDIR/mut.log" || true
fi

# --- 4a. CRLF upstream input fails closed (LF-only accepted) ------------------
# The overlay SHA pins are over LF bytes; a CRLF checkout must abort before
# generation. This is the binary property the "platform-independent overlay"
# claim is scoped to.
CRLF="$WORKDIR/crlf"
mkdir -p "$CRLF/NAM"
cp "$NAM_CORE_DIR/NAM/lstm.h" "$CRLF/NAM/lstm.h"
cp "$NAM_CORE_DIR/NAM/activations.h" "$CRLF/NAM/activations.h"
cp "$NAM_CORE_DIR/NAM/gating_activations.h" "$CRLF/NAM/gating_activations.h"
sed 's/$/\r/' "$NAM_CORE_DIR/NAM/lstm.cpp" > "$CRLF/NAM/lstm.cpp"
make_proj "$WORKDIR/crlf-src" "$MODULE" "$CRLF"
set +e
"$CMAKE_BIN" -S "$WORKDIR/crlf-src" -B "$WORKDIR/crlf-b" > "$WORKDIR/crlf.log" 2>&1
crlf_rc=$?
set -e
if [ "$crlf_rc" -ne 0 ] && grep -q "pinned upstream lstm.cpp SHA256 mismatch" "$WORKDIR/crlf.log"; then
    ok "CRLF pinned lstm.cpp fails closed (LF-only overlay inputs)"
else
    bad "CRLF pinned lstm.cpp did not fail closed (rc=$crlf_rc)"
    tail -8 "$WORKDIR/crlf.log" || true
fi

MUTA="$WORKDIR/mutated-act"
mkdir -p "$MUTA/NAM"
cp "$NAM_CORE_DIR/NAM/lstm.cpp" "$MUTA/NAM/lstm.cpp"
cp "$NAM_CORE_DIR/NAM/lstm.h" "$MUTA/NAM/lstm.h"
cp "$NAM_CORE_DIR/NAM/activations.h" "$MUTA/NAM/activations.h"
cp "$NAM_CORE_DIR/NAM/gating_activations.h" "$MUTA/NAM/gating_activations.h"
printf '// rt005 staleness probe\n' >> "$MUTA/NAM/activations.h"
make_proj "$WORKDIR/muta-src" "$MODULE" "$MUTA"
set +e
"$CMAKE_BIN" -S "$WORKDIR/muta-src" -B "$WORKDIR/muta-b" > "$WORKDIR/muta.log" 2>&1
muta_rc=$?
set -e
if [ "$muta_rc" -ne 0 ] && grep -q "pinned upstream activations.h SHA256 mismatch" "$WORKDIR/muta.log"; then
    ok "mutated pinned activations.h fails closed with input SHA256 mismatch"
else
    bad "mutated pinned activations.h did not fail closed (rc=$muta_rc)"
    tail -8 "$WORKDIR/muta.log" || true
fi

# --- 5. mutated tracked patch fails closed with clean pinned sources ----------
FAKEMOD="$WORKDIR/fakemod"
mkdir -p "$FAKEMOD/cmake/nam-rt" "$FAKEMOD/patches/nam"
cp "$MODULE" "$FAKEMOD/cmake/nam-rt/NamRtPatch.cmake"
cp "$PATCH_FILE" "$FAKEMOD/patches/nam/lstm-rt-alloc.patch"
cp "$ACTIVATION_PATCH_FILE" "$FAKEMOD/patches/nam/activation-rt-alloc.patch"
printf '# rt003 mutated-patch probe\n' >> "$FAKEMOD/patches/nam/lstm-rt-alloc.patch"
make_proj "$WORKDIR/pmut-src" "$FAKEMOD/cmake/nam-rt/NamRtPatch.cmake" "$NAM_CORE_DIR"
set +e
"$CMAKE_BIN" -S "$WORKDIR/pmut-src" -B "$WORKDIR/pmut-b" > "$WORKDIR/pmut.log" 2>&1
pmut_rc=$?
set -e
if [ "$pmut_rc" -ne 0 ] && grep -q "tracked overlay patch SHA256 mismatch" "$WORKDIR/pmut.log"; then
    ok "mutated tracked LSTM patch fails closed (pinned sources clean)"
else
    bad "mutated tracked LSTM patch did not fail closed (rc=$pmut_rc)"
    tail -8 "$WORKDIR/pmut.log" || true
fi

FAKEMOD2="$WORKDIR/fakemod2"
mkdir -p "$FAKEMOD2/cmake/nam-rt" "$FAKEMOD2/patches/nam"
cp "$MODULE" "$FAKEMOD2/cmake/nam-rt/NamRtPatch.cmake"
cp "$PATCH_FILE" "$FAKEMOD2/patches/nam/lstm-rt-alloc.patch"
cp "$ACTIVATION_PATCH_FILE" "$FAKEMOD2/patches/nam/activation-rt-alloc.patch"
printf '# rt005 mutated-activation-patch probe\n' >> "$FAKEMOD2/patches/nam/activation-rt-alloc.patch"
make_proj "$WORKDIR/apmut-src" "$FAKEMOD2/cmake/nam-rt/NamRtPatch.cmake" "$NAM_CORE_DIR"
set +e
"$CMAKE_BIN" -S "$WORKDIR/apmut-src" -B "$WORKDIR/apmut-b" > "$WORKDIR/apmut.log" 2>&1
apmut_rc=$?
set -e
if [ "$apmut_rc" -ne 0 ] && grep -q "RT-005 tracked activation overlay patch SHA256 mismatch" "$WORKDIR/apmut.log"; then
    ok "mutated tracked activation patch fails closed (pinned sources clean)"
else
    bad "mutated tracked activation patch did not fail closed (rc=$apmut_rc)"
    tail -8 "$WORKDIR/apmut.log" || true
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "ALL NAM OVERLAY PATCH CHECKS PASSED"
    exit 0
fi
echo "$fail NAM OVERLAY PATCH CHECK(S) FAILED"
exit 1
