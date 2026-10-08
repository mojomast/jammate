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
declare -A NAM_ARCHIVE=(
    [repaired]=/home/mojo/projects/build-RT-003-integration/product/libnam_core.a
    [original_control]=/home/mojo/projects/build-RT-003-integration/probe-baseline/plugin/libnam_core.a
)
declare -A PINFILE=(
    [repaired]=/home/mojo/projects/build-RT-003-integration/product-probe/build/source-pin.txt
    [original_control]=/home/mojo/projects/build-RT-003-integration/probe-baseline/build/source-pin.txt
)
MODELS=(lstm.nam wavenet_a1_standard.nam wavenet_a2_max.nam slimmable_wavenet.nam A2.nam)
IDS=(lstm_control a1_wavenet_standard a2_wavenet_max slimmable_wavenet a2_slimmable_container)
VARIANTS=(repaired original_control)

mkdir -p "$OUT" "$SCRATCH/home" "$SCRATCH/xdg/config" "$SCRATCH/xdg/data" \
         "$SCRATCH/xdg/cache" "$SCRATCH/tmp"

# --- preflight: identity and protocol checks BEFORE any model is measured ----
# Every check below fails closed (exit 2) without running a single probe process.
preflight() {
    local ok=0

    # 1. Protocol budget/timeout are frozen in predeclared.json. An override that
    #    changes the budget is a protocol change and is refused, not measured.
    if [ "$WARM" != "$P_WARM" ]; then
        echo "error: NAM_ARCH_WARM=$WARM != protocol warm_blocks=$P_WARM" >&2
        ok=1
    fi
    if [ "$TIMEOUT_S" != "$P_TIMEOUT" ]; then
        echo "error: NAM_ARCH_TIMEOUT=$TIMEOUT_S != protocol timeout_s=$P_TIMEOUT" >&2
        ok=1
    fi

    # 2. Variant selection must name a known variant.
    if [ -n "$ONLY" ]; then
        local found=0
        for v in "${VARIANTS[@]}"; do
            [ "$ONLY" = "$v" ] && found=1
        done
        if [ "$found" -ne 1 ]; then
            echo "error: NAM_ARCH_ONLY='$ONLY' is not a predeclared variant (${VARIANTS[*]})" >&2
            ok=1
        fi
    fi

    # 3. Hash the probe binaries and the actual NAM archives on disk, and require
    #    each RT-003 pin file, comparing against the predeclared identity.
    for v in "${VARIANTS[@]}"; do
        if [ -n "$ONLY" ] && [ "$ONLY" != "$v" ]; then continue; fi
        local bin="${BIN[$v]}" pin="${PINFILE[$v]}" arc="${NAM_ARCHIVE[$v]}"
        if [ ! -x "$bin" ]; then
            echo "error: probe binary missing for $v: $bin" >&2; ok=1; continue
        fi
        if [ ! -f "$pin" ]; then
            echo "error: required RT-003 pin file missing for $v: $pin" >&2; ok=1; continue
        fi
        if [ ! -f "$arc" ]; then
            echo "error: NAM archive missing for $v: $arc" >&2; ok=1; continue
        fi
        if ! python3 - "$bin" "$pin" "$arc" "$v" "$SCRIPT_DIR/predeclared.json" <<'PY'
import hashlib, json, os, sys
binary, pin_path, archive, variant, pre_path = sys.argv[1:6]
def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()
pre = json.load(open(pre_path))["binaries"][variant]
pin = dict(l.strip().split("=", 1) for l in open(pin_path) if "=" in l and not l.startswith("src/"))
bad = []
if sha(binary) != pre["sha256"]:
    bad.append(f"binary sha {sha(binary)} != {pre['sha256']}")
if sha(archive) != pre["nam_archive_sha256"]:
    bad.append(f"actual NAM archive sha != {pre['nam_archive_sha256']}")
if pin.get("nam_archive_sha256") != pre["nam_archive_sha256"]:
    bad.append("pin file NAM archive sha != predeclared")
if pin.get("shared_archive_sha256") != pre["shared_archive_sha256"]:
    bad.append("pin file shared archive sha != predeclared")
shared = pre.get("shared_archive_path")
if not shared or not os.path.isfile(shared):
    bad.append(f"shared archive missing: {shared}")
elif sha(shared) != pre["shared_archive_sha256"]:
    bad.append("actual shared archive sha != predeclared")
if bad:
    for b in bad:
        print(f"error: {variant}: {b}", file=sys.stderr)
    sys.exit(1)
PY
        then ok=1
        fi
    done

    # 4. Hash every predeclared model before measuring anything.
    for i in "${!MODELS[@]}"; do
        local model="$MODELS_DIR/${MODELS[$i]}"
        if [ ! -f "$model" ]; then
            echo "error: model missing: $model" >&2; ok=1; continue
        fi
        if ! python3 - "$model" "${MODELS[$i]}" "$SCRIPT_DIR/predeclared.json" <<'PY'
import hashlib, json, sys
model, name, pre_path = sys.argv[1:4]
def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()
want = {a["file"]: a["sha256"] for a in json.load(open(pre_path))["architectures"]}.get(name)
got = sha(model)
if want is None or got != want:
    print(f"error: {name} sha {got} != predeclared {want}", file=sys.stderr)
    sys.exit(1)
PY
        then ok=1
        fi
    done

    # 5. Processor sources must match the pinned revision the archive was built from.
    if ! python3 - "$REPO_ROOT" "$SCRIPT_DIR/predeclared.json" <<'PY'
import hashlib, json, os, subprocess, sys
repo, pre_path = sys.argv[1:3]
def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()
pre = json.load(open(pre_path))["source_pin"]
for rel, want in pre["files"].items():
    p = os.path.join(repo, rel)
    if not os.path.isfile(p):
        print(f"error: missing pinned source {rel}", file=sys.stderr); sys.exit(1)
    got = sha_bytes(open(p, "rb").read())
    if got != want:
        print(f"error: {rel} sha {got} != predeclared {want}", file=sys.stderr); sys.exit(1)
    try:
        pinned = subprocess.run(["git", "-C", repo, "show", f"{pre['revision']}:{rel}"],
                                check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        print(f"error: cannot read {rel} at {pre['revision']}", file=sys.stderr); sys.exit(1)
    if sha_bytes(pinned) != want:
        print(f"error: {rel} at {pre['revision']} differs from predeclared pin", file=sys.stderr)
        sys.exit(1)
PY
    then ok=1
    fi

    return $ok
}

P_WARM=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["matrix"]["warm_blocks"])' \
         "$SCRIPT_DIR/predeclared.json")
P_TIMEOUT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["matrix"]["timeout_s"])' \
         "$SCRIPT_DIR/predeclared.json")

if ! preflight; then
    echo "error: preflight failed; refusing to measure (no probe process was run)" >&2
    exit 2
fi
echo "preflight PASS (binaries, NAM/shared archives, pin files, models, sources, protocol)"
echo

for v in "${VARIANTS[@]}"; do
    if [ -n "$ONLY" ] && [ "$ONLY" != "$v" ]; then continue; fi
    bin="${BIN[$v]}"
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
            echo "protocol_warm_blocks=$P_WARM"
            echo "protocol_timeout_s=$P_TIMEOUT"
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
