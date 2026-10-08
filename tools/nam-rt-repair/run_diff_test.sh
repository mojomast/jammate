#!/usr/bin/env bash
# RT-003 deterministic differential test driver.
#
# Runs the same deterministic LSTM workload in two independent processes
# (pinned upstream vs generated patched overlay) at several block sizes and
# compares every output sample within a predeclared tolerance.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIGINAL=""
PATCHED=""
OUT=""
WARM=64
BLOCKS="64 128 512"
SEED=0x5eed1234

while [ $# -gt 0 ]; do
    case "$1" in
        --original) ORIGINAL="$2"; shift 2 ;;
        --patched)  PATCHED="$2";  shift 2 ;;
        --out)      OUT="$2";      shift 2 ;;
        --warm)     WARM="$2";     shift 2 ;;
        --blocks)   BLOCKS="$2";   shift 2 ;;
        --seed)     SEED="$2";     shift 2 ;;
        *) echo "unknown arg $1" >&2; exit 64 ;;
    esac
done

if [ -z "$ORIGINAL" ] || [ -z "$PATCHED" ] || [ -z "$OUT" ]; then
    echo "usage: $0 --original EXE --patched EXE --out DIR [--warm N --blocks '64 128 512' --seed N]" >&2
    exit 64
fi

mkdir -p "$OUT"
overall=0
summaries=()

echo "RT-003 differential test"
echo "  original: $ORIGINAL"
echo "  patched : $PATCHED"
echo "  out     : $OUT (warm=$WARM blocks='$BLOCKS')"

for BLOCK in $BLOCKS; do
    d="$OUT/block-$BLOCK"
    mkdir -p "$d"
    "$ORIGINAL" --out "$d/original.bin" --json "$d/original.json" \
        --warm-blocks "$WARM" --block "$BLOCK" --seed "$SEED"
    "$PATCHED"  --out "$d/patched.bin"  --json "$d/patched.json" \
        --warm-blocks "$WARM" --block "$BLOCK" --seed "$SEED"
    if python3 "$SCRIPT_DIR/compare_diff.py" \
        --original-bin "$d/original.bin" --patched-bin "$d/patched.bin" \
        --original-json "$d/original.json" --patched-json "$d/patched.json" \
        --out-json "$d/diff.json"; then
        echo "  block=$BLOCK PASS"
    else
        echo "  block=$BLOCK FAIL"
        overall=1
    fi
    summaries+=("$d/diff.json")
done

python3 - "$OUT/combined-diff.json" "${summaries[@]}" <<'PY'
import json, sys
out = sys.argv[1]
parts = [json.load(open(p)) for p in sys.argv[2:]]
combined = {
    "blocks": [p.get("block") for p in parts],
    "all_pass": all(p["all_pass"] for p in parts),
    "max_abs_diff": max(p["max_abs_diff"] for p in parts),
    "max_rel_diff": max(p["max_rel_diff"] for p in parts),
    "per_block": parts,
}
json.dump(combined, open(out, "w"), indent=2)
print("combined:", out, "all_pass =", combined["all_pass"],
      "max_abs =", combined["max_abs_diff"], "max_rel =", combined["max_rel_diff"])
PY

if [ "$overall" -eq 0 ]; then
    echo "RT-003 differential test PASS (all block sizes)"
else
    echo "RT-003 differential test FAIL"
fi
exit "$overall"
