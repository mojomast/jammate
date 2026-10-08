#!/usr/bin/env bash
# RT-005 deterministic activation differential test driver.
#
# Runs the same deterministic activation + real-model workload in two
# independent processes (pinned upstream vs generated patched overlay) and
# compares every output sample within a predeclared tolerance. The comparator
# also requires the patched process to perform zero C++ allocations in every
# measured section and the pinned upstream process to allocate in the
# PReLU-bearing sections.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIGINAL=""
PATCHED=""
MODEL=""
OUT=""
WARM=64
BLOCK=128
SEED=0x5eed1234

while [ $# -gt 0 ]; do
    case "$1" in
        --original) ORIGINAL="$2"; shift 2 ;;
        --patched)  PATCHED="$2";  shift 2 ;;
        --model)    MODEL="$2";    shift 2 ;;
        --out)      OUT="$2";      shift 2 ;;
        --warm)     WARM="$2";     shift 2 ;;
        --block)    BLOCK="$2";    shift 2 ;;
        --seed)     SEED="$2";     shift 2 ;;
        *) echo "unknown arg $1" >&2; exit 64 ;;
    esac
done

if [ -z "$ORIGINAL" ] || [ -z "$PATCHED" ] || [ -z "$MODEL" ] || [ -z "$OUT" ]; then
    echo "usage: $0 --original EXE --patched EXE --model MODEL.nam --out DIR [--warm N --block N --seed N]" >&2
    exit 64
fi
if [ ! -f "$MODEL" ]; then
    echo "error: model not found: $MODEL" >&2
    exit 64
fi

mkdir -p "$OUT"

echo "RT-005 activation differential test"
echo "  original: $ORIGINAL"
echo "  patched : $PATCHED"
echo "  model   : $MODEL"
echo "  out     : $OUT (warm=$WARM block=$BLOCK)"

"$ORIGINAL" --out "$OUT/original.bin" --json "$OUT/original.json" \
    --model "$MODEL" --warm-blocks "$WARM" --block "$BLOCK" --seed "$SEED"
"$PATCHED"  --out "$OUT/patched.bin"  --json "$OUT/patched.json" \
    --model "$MODEL" --warm-blocks "$WARM" --block "$BLOCK" --seed "$SEED"

rc=0
if python3 "$SCRIPT_DIR/compare_activation_diff.py" \
    --original-bin "$OUT/original.bin" --patched-bin "$OUT/patched.bin" \
    --original-json "$OUT/original.json" --patched-json "$OUT/patched.json" \
    --out-json "$OUT/activation-diff.json"; then
    echo "RT-005 activation differential test PASS"
else
    echo "RT-005 activation differential test FAIL"
    rc=1
fi
exit "$rc"
