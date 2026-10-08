#!/usr/bin/env bash
# RT-002 processor runtime probe - isolated bounded run.
#
# Runs three bounded probe invocations with an isolated HOME/XDG and a
# disk-backed TMPDIR (the default /tmp is a full tmpfs on this host):
#   1. full            : self-check, scene(audio), dry matrix, NAM matrix
#   2. scene-noaudio-short : scene armed, no audio, 150 ms loop  -> must NOT restore
#   3. scene-noaudio-long  : scene armed, no audio, 400 ms loop  -> fallback restores
# The three results are combined into a machine-verified findings.json. The
# short/long pair discriminates the processor-owned Timer (audio path, restored
# before the 262 ms fallback) from the no-device fallback timer.
#
# Env overrides:
#   PLUGIN_BUILD_DIR  default /home/mojo/projects/guitars-build-resume/plugin
#   RT002_OUT         default <parent of PLUGIN_BUILD_DIR>/probe/build
#   RT002_SCRATCH     default <parent of PLUGIN_BUILD_DIR>/probe/run
#   RT002_ARTIFACTS   default <repo>/docs/research/processor-probe
#   RT002_NAM_MODEL   default <main checkout>/third_party/.../example_models/lstm.nam
#   RT002_WARM        warm blocks per case (default 256)
#   RT002_TIMEOUT     seconds per invocation (default 900)
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

LOG_FULL="$RT002_ARTIFACTS/processor-probe-run.log"
CSV="$RT002_ARTIFACTS/processor-probe-combos.csv"
JSON_FULL="$RT002_ARTIFACTS/findings-full.json"
LOG_NS="$RT002_ARTIFACTS/scene-noaudio-short.log"
JSON_NS="$RT002_ARTIFACTS/scene-noaudio-short.json"
LOG_NL="$RT002_ARTIFACTS/scene-noaudio-long.log"
JSON_NL="$RT002_ARTIFACTS/scene-noaudio-long.json"
FINDINGS="$RT002_ARTIFACTS/findings.json"
MANIFEST="$RT002_ARTIFACTS/manifest.sha256"
STATUS="$RT002_ARTIFACTS/exit-status.txt"

NAM_ARGS=()
if [ -f "$RT002_NAM_MODEL" ]; then
    NAM_ARGS=(--nam-model "$RT002_NAM_MODEL")
else
    echo "notice: NAM model not found at $RT002_NAM_MODEL; NAM case will be unmeasured" >&2
fi

run_mode() {
    local log="$1"; shift
    local -a statuses
    timeout "$RT002_TIMEOUT" env -i \
        PATH=/usr/bin:/bin \
        HOME="$RT002_SCRATCH/home" \
        XDG_CONFIG_HOME="$RT002_SCRATCH/xdg/config" \
        XDG_DATA_HOME="$RT002_SCRATCH/xdg/data" \
        XDG_CACHE_HOME="$RT002_SCRATCH/xdg/cache" \
        TMPDIR="$RT002_SCRATCH/tmp" \
        "$BIN" "$@" 2>&1 | tee "$log"
    statuses=("${PIPESTATUS[@]}")
    if [ "${statuses[0]}" -ne 0 ]; then
        return "${statuses[0]}"
    fi
    return "${statuses[1]}"
}

echo "RT-002 probe run"
echo "  binary   : $BIN"
echo "  artifacts: $RT002_ARTIFACTS"
echo "  scratch  : $RT002_SCRATCH"
echo

echo "=== 1/3 full run ==="
run_mode "$LOG_FULL" --csv "$CSV" --json "$JSON_FULL" \
    --warm-blocks "$RT002_WARM" "${NAM_ARGS[@]}" && st_full=0 || st_full=$?
echo
echo "=== 2/3 scene, no audio, short window ==="
run_mode "$LOG_NS" --json "$JSON_NS" --scene-mode scene-noaudio-short && st_ns=0 || st_ns=$?
echo
echo "=== 3/3 scene, no audio, long window ==="
run_mode "$LOG_NL" --json "$JSON_NL" --scene-mode scene-noaudio-long && st_nl=0 || st_nl=$?
echo

# --- combine into machine-verified findings -----------------------------------
python3 - "$JSON_FULL" "$JSON_NS" "$JSON_NL" "$FINDINGS" "$st_full" "$st_ns" "$st_nl" <<'PY'
import json, sys
full_p, ns_p, nl_p, out_p, st_full, st_ns, st_nl = sys.argv[1:8]
def load(p):
    try:
        return json.load(open(p))
    except Exception as e:
        return {"_error": str(e)}
full, ns, nl = load(full_p), load(ns_p), load(nl_p)

checks = {}
checks["full_exit_ok"] = int(st_full) == 0
checks["noaudio_short_exit_ok"] = int(st_ns) == 0
checks["noaudio_long_exit_ok"] = int(st_nl) == 0
checks["selfcheck_pass"] = bool(full.get("selfcheck_pass"))
checks["scene_audio_restored"] = bool(full.get("scene_restored"))
checks["scene_audio_restore_before_fallback"] = (isinstance(full.get("scene_restore_elapsed_ms"), (int, float))
    and 0.0 <= full.get("scene_restore_elapsed_ms", -1.0) < 262.0)
checks["scene_audio_dispatch_within_bound"] = bool(full.get("scene_dispatch_within_bound"))
checks["scene_audio_stop_not_preset"] = full.get("scene_stop_sent_before") == 0
checks["scene_audio_signal_zero"] = bool(full.get("scene_signal_zero"))
checks["scene_audio_editor_null"] = bool(full.get("scene_editor_null"))
checks["scene_noaudio_short_not_restored"] = not bool(ns.get("scene_restored"))
checks["scene_noaudio_long_restored"] = bool(nl.get("scene_restored"))
checks["scene_timer_attribution_discriminated"] = (
    checks["scene_audio_restored"]
    and checks["scene_noaudio_short_not_restored"]
    and checks["scene_noaudio_long_restored"])

combined = {
    "full": full,
    "scene_noaudio_short": ns,
    "scene_noaudio_long": nl,
    "checks": checks,
    "all_primary_checks_pass": all(checks.values()),
}
json.dump(combined, open(out_p, "w"), indent=2)
print("combined findings:", out_p)
for k, v in checks.items():
    print(f"  {'PASS' if v else 'FAIL'}  {k}")
PY

# --- manifest: hashes of everything needed to reproduce -----------------------
write_manifest() {
{
    echo "# RT-002 artifact manifest (sha256)"
    echo "# generated=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    for f in "$BIN" "$RT002_ARTIFACTS"/*; do
        [ -f "$f" ] || continue
        case "$f" in */manifest.sha256) continue;; esac
        sha256sum "$f"
    done
    for f in "$PLUGIN_BUILD_DIR/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a" \
             "$PLUGIN_BUILD_DIR/libnam_core.a" "$PLUGIN_BUILD_DIR/libGuitarCompanionAssets.a" \
             "$RT002_NAM_MODEL"; do
        if [ -f "$f" ]; then
            sha256sum "$f"
        fi
    done
} > "$MANIFEST"
}

{
    echo "full_exit=$st_full"
    echo "scene_noaudio_short_exit=$st_ns"
    echo "scene_noaudio_long_exit=$st_nl"
} > "$STATUS"

# overall status from the machine-verified checks
overall=0
python3 - "$FINDINGS" <<'PY' || overall=$?
import json, sys
c = json.load(open(sys.argv[1]))["checks"]
sys.exit(0 if all(c.values()) else 1)
PY

{
    echo "combined_findings_exit=$overall"
} >> "$STATUS"

write_manifest

echo
echo "exit statuses (full, short, long, combined): $st_full $st_ns $st_nl $overall"
echo "findings : $FINDINGS"
echo "manifest : $MANIFEST"
exit "$overall"
