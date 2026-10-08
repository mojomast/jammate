#!/usr/bin/env bash
# Run the TRACK-004 acquisition diagnostic over the untouched original corpus
# and assemble the bounded evidence artifact tree.
#
#   tools/tracker-diagnostics/run-corpus.sh [artifact-dir] [build-dir] [core-dir]
#
# Defaults:
#   artifact-dir docs/research/tracker-acquisition
#   build-dir    /home/mojo/projects/build-TRACK-004/diag
#   core-dir     /home/mojo/projects/build-EVAL-005/main-core
#
# Everything is deterministic except wall-clock-free file ordering. Artifacts
# are capped well under 5 MiB (see the size check at the end).

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

artifact="${1:-docs/research/tracker-acquisition}"
build="${2:-${JAM_DIAG_OUT:-/home/mojo/projects/build-TRACK-004/diag}}"
core="${3:-${JAM_DIAG_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"

DIAG="$build/tracker-diagnostics"
if [ ! -x "$DIAG" ]; then
    echo "run-corpus.sh: $DIAG missing; run tools/tracker-diagnostics/build.sh first" >&2
    exit 2
fi

plug_bt="$core/librhythm-eval-btrack.so"
plug_au="$core/librhythm-eval-aubio.so"
cfg_bt="$build/libtracker-diag-config-btrack.so"
cfg_au="$build/libtracker-diag-config-aubio.so"

SWEEP="118,120,122,123,124,125,126,127,128,130,132,134"

echo "[1/6] verify original corpus WAV hashes"
mkdir -p "$artifact"
python3 "$here/verify_corpus_hashes.py" testdata/rhythm "$artifact/wav-hashes.txt"

echo "[2/6] BTrack tempo->lag grid arithmetic"
mkdir -p "$artifact/experiments"
"$DIAG" --out "$artifact" --backend-lib "$plug_bt" --grid-out "$artifact/experiments/btrack_grid.csv" --grid-only

echo "[3/6] synthetic click BPM sweep (48k and 44.1k)"
for b in btrack aubio; do
    lib="$plug_bt"; [ "$b" = aubio ] && lib="$plug_au"
    for rate in 48000 44100; do
        "$DIAG" --out "$artifact/experiments/$b/click-rate$rate" \
            --backend "$b" --backend-lib "$lib" --block 128 \
            --click-sweep "$SWEEP" --synthetic-rate "$rate" --synthetic-seconds 24
    done
done

echo "[4/6] corpus: block 128 (full trace) and block 512 (beats only)"
for b in btrack aubio; do
    lib="$plug_bt"; [ "$b" = aubio ] && lib="$plug_au"
    "$DIAG" --corpus testdata/rhythm --out "$artifact/$b/block128" \
        --backend "$b" --backend-lib "$lib" --block 128 \
        --label default_adapter_config --trace-files all --state-max-rows 500
    "$DIAG" --corpus testdata/rhythm --out "$artifact/$b/block512" \
        --backend "$b" --backend-lib "$lib" --block 512 \
        --label default_adapter_config --trace-files beats
done

echo "[5/6] bounded config experiment: silence gate disabled (NOT default evidence)"
for b in btrack aubio; do
    cfg="$cfg_bt"; [ "$b" = aubio ] && cfg="$cfg_au"
    JAM_DIAG_SILENCE_DBFS=-120 "$DIAG" --corpus testdata/rhythm \
        --out "$artifact/experiments/silence-gate-off/$b-block128" \
        --backend "$b" --backend-lib "$cfg" --block 128 \
        --variant-not-default --label "silence_gate_disabled_m120dbfs" \
        --trace-files none
done

echo "[5b] per-core-fixture reason tables"
python3 "$here/make_reason_table.py" "$artifact"

echo "[6/6] provenance"
python3 - "$artifact" "$core" "$plug_bt" "$plug_au" "$build" <<'PY'
import hashlib, json, os, subprocess, sys

artifact, core, plug_bt, plug_au, build = sys.argv[1:6]

def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def git(*args):
    try:
        return subprocess.check_output(["git", *args], text=True).strip()
    except Exception:
        return ""

manifest = os.path.join("testdata", "rhythm", "manifest.json")
prov = {
    "schemaVersion": 1,
    "task": "TRACK-004",
    "kind": "diagnostic",
    "selection": None,
    "adr": None,
    "g3": "OPEN",
    "baseSha": (git("merge-base", "HEAD", "6287288") or git("rev-parse", "HEAD")),
    "headSha": git("rev-parse", "HEAD"),
    "baseSubject": git("log", "-1", "--format=%s", "6287288") or git("log", "-1", "--format=%s"),
    "corpusManifest": manifest,
    "corpusManifestSha256": sha(manifest),
    "wavHashes": "wav-hashes.txt",
    "backendPlugins": {
        "btrack": {"path": plug_bt, "sha256": sha(plug_bt)},
        "aubio": {"path": plug_au, "sha256": sha(plug_au)},
    },
    "pluginProvenance": (
        "librhythm-eval-{btrack,aubio}.so from the EVAL-005 main-core build root; "
        "src/btrack, src/aubio, tools/rhythm-eval/*.cpp and third_party tracker "
        "sources are byte-identical to this base (git diff empty), so the build is "
        "behaviorally the current main adapter build."),
    "configExperiment": {
        "tool": "tools/tracker-diagnostics/libtracker-diag-config-*.so",
        "buildDir": build,
        "btrackShimSha256": sha(os.path.join(build, "libtracker-diag-config-btrack.so"))
            if os.path.exists(os.path.join(build, "libtracker-diag-config-btrack.so")) else None,
        "aubioShimSha256": sha(os.path.join(build, "libtracker-diag-config-aubio.so"))
            if os.path.exists(os.path.join(build, "libtracker-diag-config-aubio.so")) else None,
        "knob": "silenceRmsDbfs = -120 (gate effectively disabled)",
        "evidenceClass": "NOT default evidence; adapter-config variant only",
    },
    "artifactBudgetBytes": 5 * 1024 * 1024,
}
with open(os.path.join(artifact, "provenance.json"), "w", encoding="utf-8") as fh:
    json.dump(prov, fh, indent=2, sort_keys=True)
    fh.write("\n")
print("wrote", os.path.join(artifact, "provenance.json"))
PY

size=$(du -sb "$artifact" | cut -f1)
echo "artifact size: $size bytes ($((size / 1024)) KiB)"
if [ "$size" -gt $((5 * 1024 * 1024)) ]; then
    echo "run-corpus.sh: artifact exceeds 5 MiB budget" >&2
    exit 1
fi

echo "done: $artifact"
