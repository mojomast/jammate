#!/usr/bin/env bash
# RT-004 bounded NAM architecture coverage - runner.
#
# Runs the EXISTING processor probe binaries (read-only, not rebuilt) once per
# predeclared NAM model and variant, each in an independent clean process, with
# a fixed warm-block budget and a bounded timeout. Does not repair or edit any
# processor, NAM or probe source. Model files are read in place (read-only),
# never copied. Summarisation and fail-closed validation live in
# summarize_nam_arch.py.
#
# Variants:
#   repaired          current processor, libnam_core.a patched (RT-003 product probe)
#   original_control  same probe linked against pinned upstream libnam_core_original.a
#                     (control only; NOT the current processor)
#
# Env overrides:
#   NAM_MODELS_DIR    default /home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models
#   NAM_ARCH_OUT      default <repo>/docs/research/nam-architecture-probe/runs
#   NAM_ARCH_SCRATCH  default /home/mojo/projects/guitars-build-resume/tmp/nam-arch-run
#   NAM_ARCH_WARM     warm blocks per case (default 128)
#   NAM_ARCH_TIMEOUT  seconds per process (default 600)
#   NAM_ARCH_ONLY     optional variant name to restrict the run
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

MODELS_DIR="${NAM_MODELS_DIR:-/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models}"
OUT="${NAM_ARCH_OUT:-$REPO_ROOT/docs/research/nam-architecture-probe/runs}"
SCRATCH="${NAM_ARCH_SCRATCH:-/home/mojo/projects/guitars-build-resume/tmp/nam-arch-run}"
WARM="${NAM_ARCH_WARM:-128}"
TIMEOUT_S="${NAM_ARCH_TIMEOUT:-600}"
ONLY="${NAM_ARCH_ONLY:-}"

declare -A BIN=(
    [repaired]=/home/mojo/projects/build-RT-003-integration/product-probe/build/processor_probe
    [original_control]=/home/mojo/projects/build-RT-003-integration/probe-baseline/build/processor_probe
)
MODELS=(lstm.nam wavenet_a1_standard.nam wavenet_a2_max.nam slimmable_wavenet.nam A2.nam)
IDS=(lstm_control a1_wavenet_standard a2_wavenet_max slimmable_wavenet a2_slimmable_container)
VARIANTS=(repaired original_control)

mkdir -p "$OUT" "$SCRATCH/home" "$SCRATCH/xdg/config" "$SCRATCH/xdg/data" \
         "$SCRATCH/xdg/cache" "$SCRATCH/tmp"

for v in "${VARIANTS[@]}"; do
    if [ -n "$ONLY" ] && [ "$ONLY" != "$v" ]; then continue; fi
    bin="${BIN[$v]}"
    if [ ! -x "$bin" ]; then
        echo "error: probe binary missing for $v: $bin" >&2
        exit 1
    fi
    for i in "${!MODELS[@]}"; do
        file="${MODELS[$i]}"
        id="${IDS[$i]}"
        model="$MODELS_DIR/$file"
        dir="$OUT/$v/$id"
        mkdir -p "$dir"
        if [ ! -f "$model" ]; then
            echo "error: model missing: $model" >&2
            printf 'model_missing=1\nexit=66\n' > "$dir/exit-status.txt"
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
            echo "model_sha256=$(sha256sum "$model" | cut -d' ' -f1)"
            echo "exit=$st"
        } > "$dir/exit-status.txt"
        echo "exit=$st"
    done
done

echo
echo "runs written under $OUT"
