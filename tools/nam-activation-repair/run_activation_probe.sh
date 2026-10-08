#!/usr/bin/env bash
# RT-005 activation-repair processor probe runner.
#
# Runs the EXISTING processor probe binaries (read-only, not rebuilt) once per
# predeclared NAM model and archive variant, each in an independent clean
# process, with a fixed warm-block budget and a bounded timeout. Model files are
# read in place (read-only), never copied.
#
# Variants:
#   original   pinned upstream NAM (no overlay patch); historical control
#   lstm_only  RT-003 integration product probe (LSTM patch only)
#   new        RT-005 production nam_core (LSTM + activation repair)
#
# The `lstm_only` and `new` probes share the same read-only shared/assets
# archives, so they differ only in libnam_core.a.
#
# Env overrides:
#   NAM_ACT_MODELS_DIR  default <main checkout>/third_party/NeuralAmpModelerCore/example_models
#   NAM_ACT_OUT         default <repo>/docs/research/nam-activation-repair/runs
#   NAM_ACT_SCRATCH     default /home/mojo/projects/guitars-build-resume/tmp/nam-activation-run
#   NAM_ACT_ONLY        optional variant name to restrict the run
#   NAM_ACT_BIN_<V>     override binary for variant V (V upper-cased)
#   NAM_ACT_ARCHIVE_<V> override NAM archive for variant V
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PREDECLARED="$SCRIPT_DIR/predeclared.json"

MODELS_DIR="${NAM_ACT_MODELS_DIR:-/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models}"
OUT="${NAM_ACT_OUT:-$REPO_ROOT/docs/research/nam-activation-repair/runs}"
SCRATCH="${NAM_ACT_SCRATCH:-/home/mojo/projects/guitars-build-resume/tmp/nam-activation-run}"
ONLY="${NAM_ACT_ONLY:-}"

WARM=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["matrix"]["warm_blocks"])' "$PREDECLARED")
TIMEOUT_S=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["matrix"]["timeout_s"])' "$PREDECLARED")

mkdir -p "$OUT" "$SCRATCH/home" "$SCRATCH/xdg/config" "$SCRATCH/xdg/data" \
         "$SCRATCH/xdg/cache" "$SCRATCH/tmp"

# --- variant table (binary, archive, shared) --------------------------------
VTABLE="$SCRATCH/variants.tsv"
python3 - "$PREDECLARED" > "$VTABLE" <<'PY'
import json, os, sys
pre = json.load(open(sys.argv[1]))
order = pre["matrix"]["variants"]
for v in order:
    d = pre["variants"][v]
    key = v.upper()
    binary = os.environ.get(f"NAM_ACT_BIN_{key}", d["binary"])
    archive = os.environ.get(f"NAM_ACT_ARCHIVE_{key}", d["nam_archive"])
    print("\t".join([v, binary, archive, d["shared_archive"],
                     d["binary_sha256"], d["nam_archive_sha256"], d["shared_archive_sha256"]]))
PY

VARIANTS=()
while IFS=$'\t' read -r v _rest; do VARIANTS+=("$v"); done < "$VTABLE"

# --- preflight: identity checks before any model is measured -----------------
preflight() {
    local ok=0
    if [ -n "$ONLY" ]; then
        local found=0
        for v in "${VARIANTS[@]}"; do [ "$ONLY" = "$v" ] && found=1; done
        if [ "$found" -ne 1 ]; then
            echo "error: NAM_ACT_ONLY='$ONLY' is not a predeclared variant (${VARIANTS[*]})" >&2
            ok=1
        fi
    fi

    while IFS=$'\t' read -r v bin arc shared bin_sha arc_sha shared_sha; do
        if [ -n "$ONLY" ] && [ "$ONLY" != "$v" ]; then continue; fi
        for f in "$bin" "$arc"; do
            if [ ! -f "$f" ]; then echo "error: missing $v file $f" >&2; ok=1; fi
        done
        if [ -x "$bin" ] || [ -f "$bin" ]; then
            [ "$(sha256sum "$bin" | cut -d' ' -f1)" = "$bin_sha" ] \
                || { echo "error: $v binary sha mismatch" >&2; ok=1; }
        fi
        if [ -f "$arc" ]; then
            [ "$(sha256sum "$arc" | cut -d' ' -f1)" = "$arc_sha" ] \
                || { echo "error: $v NAM archive sha mismatch" >&2; ok=1; }
        fi
        if [ -f "$shared" ]; then
            [ "$(sha256sum "$shared" | cut -d' ' -f1)" = "$shared_sha" ] \
                || { echo "error: $v shared archive sha mismatch" >&2; ok=1; }
        else
            echo "error: $v shared archive missing: $shared" >&2; ok=1
        fi
    done < "$VTABLE"

    # Hash every predeclared model before measuring anything.
    while IFS=$'\t' read -r id file want; do
        model="$MODELS_DIR/$file"
        if [ ! -f "$model" ]; then echo "error: model missing: $model" >&2; ok=1; continue; fi
        got=$(sha256sum "$model" | cut -d' ' -f1)
        [ "$got" = "$want" ] || { echo "error: $id model sha $got != $want" >&2; ok=1; }
    done < <(python3 - "$PREDECLARED" <<'PY'
import json,sys
for a in json.load(open(sys.argv[1]))["architectures"]:
    print("\t".join([a["id"], a["file"], a["sha256"]]))
PY
)
    return $ok
}

if ! preflight; then
    echo "error: preflight failed; refusing to measure (no probe process was run)" >&2
    exit 2
fi
echo "preflight PASS (binaries, NAM/shared archives, models, protocol warm=$WARM timeout=$TIMEOUT_S)"
echo

while IFS=$'\t' read -r v bin arc shared _bin_sha _arc_sha _shared_sha; do
    if [ -n "$ONLY" ] && [ "$ONLY" != "$v" ]; then continue; fi
    while IFS=$'\t' read -r id file; do
        model="$MODELS_DIR/$file"
        dir="$OUT/$v/$id"
        mkdir -p "$dir"
        if [ ! -f "$model" ]; then
            echo "error: model missing: $model" >&2
            printf 'variant=%s\narchitecture_id=%s\nmodel_file=%s\nexit=66\n' "$v" "$id" "$file" > "$dir/exit-status.txt"
            continue
        fi
        echo "=== $v / $id ($file) ==="
        set +e
        timeout "$TIMEOUT_S" env -i \
            PATH=/usr/bin:/bin \
            HOME="$SCRATCH/home" \
            XDG_CONFIG_HOME="$SCRATCH/xdg/config" \
            XDG_DATA_HOME="$SCRATCH/xdg/data" \
            XDG_CACHE_HOME="$SCRATCH/xdg/cache" \
            TMPDIR="$SCRATCH/tmp" \
            "$bin" --warm-blocks "$WARM" \
                --csv "$dir/cases.csv" --json "$dir/findings.json" \
                --nam-model "$model" > "$dir/probe.log" 2>&1
        st=$?
        set -e
        {
            echo "variant=$v"
            echo "architecture_id=$id"
            echo "model_file=$file"
            echo "warm_blocks=$WARM"
            echo "timeout_s=$TIMEOUT_S"
            echo "binary=$bin"
            echo "binary_sha256=$(sha256sum "$bin" | cut -d' ' -f1)"
            echo "nam_archive=$arc"
            echo "nam_archive_sha256=$(sha256sum "$arc" | cut -d' ' -f1)"
            echo "model_sha256=$(sha256sum "$model" | cut -d' ' -f1)"
            echo "exit=$st"
        } > "$dir/exit-status.txt"
        echo "exit=$st"
    done < <(python3 - "$PREDECLARED" <<'PY'
import json,sys
for a in json.load(open(sys.argv[1]))["architectures"]:
    print("\t".join([a["id"], a["file"]]))
PY
)
done < "$VTABLE"

echo
echo "runs written under $OUT"
