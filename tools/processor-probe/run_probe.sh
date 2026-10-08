#!/usr/bin/env bash
# RT-002 processor runtime probe - isolated bounded run.
#
# Runs the independently built probe with an isolated HOME/XDG and a
# disk-backed TMPDIR (the default /tmp is a full tmpfs on this host). Captures
# the log and the per-case CSV into the repository's dedicated artefacts dir.
#
# Env overrides:
#   PLUGIN_BUILD_DIR  default /home/mojo/projects/guitars-build-resume/plugin
#   RT002_OUT         default <parent of PLUGIN_BUILD_DIR>/probe/build
#   RT002_SCRATCH     default <parent of PLUGIN_BUILD_DIR>/probe/run
#   RT002_ARTIFACTS   default <repo>/docs/research/processor-probe
#   RT002_NAM_MODEL   default <main checkout>/third_party/.../example_models/lstm.nam
#   RT002_WARM        warm blocks per case (default 256)
#   RT002_TIMEOUT     seconds (default 900)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR:-/home/mojo/projects/guitars-build-resume/plugin}"
RT002_OUT="${RT002_OUT:-$(dirname "$PLUGIN_BUILD_DIR")/probe/build}"
RT002_SCRATCH="${RT002_SCRATCH:-$(dirname "$PLUGIN_BUILD_DIR")/probe/run}"
RT002_ARTIFACTS="${RT002_ARTIFACTS:-$REPO_ROOT/docs/research/processor-probe}"
RT002_NAM_MODEL="${RT002_NAM_MODEL:-/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models/lstm.nam}"
RT002_WARM="${RT002_WARM:-256}"
RT002_TIMEOUT="${RT002_TIMEOUT:-900}"

BIN="$RT002_OUT/processor_probe"
if [ ! -x "$BIN" ]; then
    echo "error: probe binary not built: $BIN" >&2
    echo "run tools/processor-probe/build_probe.sh first" >&2
    exit 1
fi

mkdir -p "$RT002_SCRATCH/home" "$RT002_SCRATCH/xdg/config" \
         "$RT002_SCRATCH/xdg/data" "$RT002_SCRATCH/xdg/cache" \
         "$RT002_SCRATCH/tmp" "$RT002_ARTIFACTS"

LOG="$RT002_ARTIFACTS/processor-probe-run.log"
CSV="$RT002_ARTIFACTS/processor-probe-combos.csv"

NAM_ARGS=()
if [ -f "$RT002_NAM_MODEL" ]; then
    NAM_ARGS=(--nam-model "$RT002_NAM_MODEL")
else
    echo "notice: NAM model not found at $RT002_NAM_MODEL; NAM case will be unmeasured" >&2
fi

echo "RT-002 probe run"
echo "  binary   : $BIN"
echo "  log      : $LOG"
echo "  csv      : $CSV"
echo "  scratch  : $RT002_SCRATCH"
echo "  timeout  : ${RT002_TIMEOUT}s"
echo

set +e
timeout "$RT002_TIMEOUT" env -i \
    PATH=/usr/bin:/bin \
    HOME="$RT002_SCRATCH/home" \
    XDG_CONFIG_HOME="$RT002_SCRATCH/xdg/config" \
    XDG_DATA_HOME="$RT002_SCRATCH/xdg/data" \
    XDG_CACHE_HOME="$RT002_SCRATCH/xdg/cache" \
    TMPDIR="$RT002_SCRATCH/tmp" \
    "$BIN" --csv "$CSV" --warm-blocks "$RT002_WARM" "${NAM_ARGS[@]}" \
    2>&1 | tee "$LOG"
status=${PIPESTATUS[0]}
set -e

echo
echo "probe exit status: $status"
exit "$status"
