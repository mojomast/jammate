#!/usr/bin/env bash
# Run the TRACK-005 tempo-report variant experiment and assemble the bounded
# evidence tree.
#
#   tools/tempo-variant/run-corpus.sh [artifact-dir] [build-dir] [core-dir] [diag-binary]
#
# Defaults:
#   artifact docs/research/tempo-variant
#   build    /home/mojo/projects/build-TRACK-005
#   core     /home/mojo/projects/build-EVAL-005/main-core
#   diag     /home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics
#
# The variant plugin is loaded by the UNCHANGED TRACK-004 diagnostic binary via
# --backend-lib; the baseline is the default pinned btrack plugin. The per-beat
# method history comes from the plugin's own raw log, projected by
# extract_method_history.py.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root"

artifact="${1:-docs/research/tempo-variant}"
build="${2:-${JAM_TV_OUT:-/home/mojo/projects/build-TRACK-005}}"
core="${3:-${JAM_TV_CORE:-/home/mojo/projects/build-EVAL-005/main-core}}"
diag="${4:-${JAM_TV_DIAG:-/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics}}"

for f in "$diag" "$core/librhythm-eval-btrack.so" \
         "$build/libtempo-variant-btrack.so" "$build/tempo-variant-click" \
         "$build/tempo-variant-metrics" "$build/TempoVariantTests"; do
    if [ ! -e "$f" ]; then
        echo "run-corpus.sh: missing $f; run tools/tempo-variant/build.sh first" >&2
        exit 2
    fi
done

BT="$core/librhythm-eval-btrack.so"
VAR="$build/libtempo-variant-btrack.so"
CLICK="$build/tempo-variant-click"
MET="$build/tempo-variant-metrics"
DIAG="$diag"
WORK="$build/corpus"
rm -rf "$artifact" "$WORK"
mkdir -p "$artifact" "$WORK/raw-method/original" "$WORK/raw-method/repaired"

echo "[1/9] verify corpus hashes (original 19 + repaired 19 entries)"
python3 "$here/verify_hashes.py" "$artifact/wav-hashes.txt"

echo "[2/9] unit + arithmetic tests"
"$build/TempoVariantTests" | tee "$artifact/tests-TempoVariantTests.txt"
python3 "$here/tests/test_click_lag.py" > "$artifact/tests-click-lag.txt" 2>&1 \
    && echo "click-lag unit tests: OK" || { echo "click-lag unit tests FAILED" >&2; cat "$artifact/tests-click-lag.txt"; exit 1; }
"$here/check_failclosed.sh" "$build" "$core" > "$artifact/tests-failclosed.txt" 2>&1 \
    && echo "fail-closed checks: OK" || { echo "fail-closed checks FAILED" >&2; cat "$artifact/tests-failclosed.txt"; exit 1; }

run_backend () { # corpus out backend label lib extra...
    local corpus="$1" out="$2" backend="$3" label="$4" lib="$5"; shift 5
    "$DIAG" --corpus "$corpus" --out "$out" --backend "$backend" \
        --backend-lib "$lib" --label "$label" "$@"
}

echo "[3/9] original corpus, block 128 (baseline + variant)"
mkdir -p "$artifact/original/block128"
run_backend testdata/rhythm "$WORK/baseline/original/block128" btrack default_btrack "$BT" \
    --block 128 --trace-files all --state-max-rows 500
JAM_TEMPO_VARIANT_LOG_DIR="$WORK/raw-method/original" "$DIAG" \
    --corpus testdata/rhythm --out "$WORK/variant/original/block128" \
    --backend btrack-tempo-variant --backend-lib "$VAR" --block 128 \
    --variant-not-default --label btrack_tempo_variant --trace-files all --state-max-rows 500

echo "[4/9] repaired corpus, block 128 (baseline + variant)"
run_backend testdata/rhythm/repaired-sustain "$WORK/baseline/repaired/block128" btrack default_btrack "$BT" \
    --block 128 --trace-files all --state-max-rows 500
JAM_TEMPO_VARIANT_LOG_DIR="$WORK/raw-method/repaired" "$DIAG" \
    --corpus testdata/rhythm/repaired-sustain --out "$WORK/variant/repaired/block128" \
    --backend btrack-tempo-variant --backend-lib "$VAR" --block 128 \
    --variant-not-default --label btrack_tempo_variant --trace-files all --state-max-rows 500

echo "[5/9] original corpus, block 512 (baseline + variant; beats only)"
run_backend testdata/rhythm "$WORK/baseline/original/block512" btrack default_btrack "$BT" \
    --block 512 --trace-files beats
run_backend testdata/rhythm "$WORK/variant/original/block512" btrack-tempo-variant btrack_tempo_variant "$VAR" \
    --block 512 --variant-not-default --trace-files beats

echo "[6/9] full per-fixture metrics (unmodified rhythmeval source maths)"
mkdir -p "$WORK/metrics"
for set in original repaired; do
    corpus=testdata/rhythm; [ "$set" = repaired ] && corpus=testdata/rhythm/repaired-sustain
    "$MET" --corpus "$corpus" --backend-lib "$BT" --backend btrack --block 128 \
        --out "$WORK/metrics/base-$set.json"
    "$MET" --corpus "$corpus" --backend-lib "$VAR" --backend btrack-tempo-variant --block 128 \
        --out "$WORK/metrics/var-$set.json"
done

# The 512-frame comparison needs its own metrics, not the 128-frame records.
for kind in base var; do
    lib="$BT"; backend=btrack
    if [ "$kind" = var ]; then lib="$VAR"; backend=btrack-tempo-variant; fi
    "$MET" --corpus testdata/rhythm --backend-lib "$lib" --backend "$backend" \
        --block 512 --out "$WORK/metrics/$kind-original-block512.json"
done

echo "[7/9] deterministic click sweep and 126->132 step (44.1k / 48k)"
for rate in 44100 48000; do
    "$CLICK" --backend-lib "$BT" --out "$WORK/click" --mode sweep --rate "$rate" --seconds 24
    "$CLICK" --backend-lib "$BT" --out "$WORK/click" --mode step --rate "$rate" --seconds 24 \
        --step-from 126 --step-to 132 --step-seconds 12
done

echo "[8/9] project method history + compare runs + click lag"
mkdir -p "$WORK/method-history" "$WORK/compare"
python3 "$here/extract_method_history.py" "$WORK/raw-method/original" testdata/rhythm \
    "$WORK/method-history/original-block128.csv" "$WORK/method-history/original-block128-fixtures.csv"
python3 "$here/extract_method_history.py" "$WORK/raw-method/repaired" testdata/rhythm/repaired-sustain \
    "$WORK/method-history/repaired-block128.csv" "$WORK/method-history/repaired-block128-fixtures.csv"

compare_pair() {
    local setn="$1" blk="$2"
    local suffix="$setn"
    if [ "$blk" != 128 ]; then suffix="$setn-block$blk"; fi
    python3 "$here/compare_runs.py" "$setn-block$blk" \
        "$WORK/baseline/$setn/block$blk" "$WORK/variant/$setn/block$blk" \
        "$WORK/metrics/base-$suffix.json" "$WORK/metrics/var-$suffix.json" \
        "$WORK/compare/$setn-block$blk.csv" "$WORK/compare/$setn-block$blk.md"
}
compare_pair original 128
compare_pair repaired 128
compare_pair original 512
for rate in 44100 48000; do
    python3 "$here/click_lag.py" "$WORK/click/click_step_rate$rate.csv" \
        "$WORK/click/click_step_rate${rate}_info.csv" 126 132 \
        "$WORK/click/click_step_rate${rate}_beats.csv" "$WORK/click/click_step_rate${rate}_summary.csv"
done

echo "[9/9] assemble bounded artifact tree + provenance"
mkdir -p "$artifact/compare" "$artifact/method-history" "$artifact/click" \
         "$artifact/metrics" "$artifact/reference" "$artifact/beats-sha"
for setn in original repaired; do
    mkdir -p "$artifact/$setn/block128/baseline" "$artifact/$setn/block128/variant"
    for kind in fixtures.csv acquisition.json summary.json; do
        cp "$WORK/baseline/$setn/block128/$kind" "$artifact/$setn/block128/baseline/"
        cp "$WORK/variant/$setn/block128/$kind" "$artifact/$setn/block128/variant/"
    done
done
mkdir -p "$artifact/original/block512/baseline" "$artifact/original/block512/variant"
for kind in fixtures.csv acquisition.json summary.json; do
    cp "$WORK/baseline/original/block512/$kind" "$artifact/original/block512/baseline/"
    cp "$WORK/variant/original/block512/$kind" "$artifact/original/block512/variant/"
done
for f in "$WORK"/compare/*.csv "$WORK"/compare/*.md; do cp "$f" "$artifact/compare/"; done
for f in "$WORK"/method-history/*.csv; do cp "$f" "$artifact/method-history/"; done
for f in "$WORK"/click/click_sweep_*.csv "$WORK"/click/click_step_*_beats.csv \
         "$WORK"/click/click_step_*_summary.csv "$WORK"/click/click_step_*_info.csv \
         "$WORK"/click/click_step_*_truth.csv; do
    cp "$f" "$artifact/click/"
done
for f in "$WORK"/metrics/*.json; do cp "$f" "$artifact/metrics/"; done
# canonical beat-event series (original block128) so equality is auditable
mkdir -p "$artifact/beats/original-block128/baseline" "$artifact/beats/original-block128/variant"
cp "$WORK"/baseline/original/block128/beats/*.csv "$artifact/beats/original-block128/baseline/"
cp "$WORK"/variant/original/block128/beats/*.csv "$artifact/beats/original-block128/variant/"
# sha manifests for every beat set
: > "$artifact/beats-sha/all.txt"
for d in "$WORK"/baseline/*/block*/beats "$WORK"/variant/*/block*/beats; do
    [ -d "$d" ] || continue
    (cd "$(dirname "$d")" && find "$(basename "$d")" -type f -name '*.csv' -print0 \
        | sort -z | xargs -0 sha256sum) >> "$artifact/beats-sha/all.txt"
done

# stored verified aubio benchmark (referenced, not re-run)
python3 - "$artifact" <<'PY'
import json, sys
art = sys.argv[1]
src = "docs/research/tracker-acquisition/aubio/block128/summary.json"
try:
    s = json.load(open(src))
    json.dump({"source": src, "backend": "aubio", "blockFrames": 128,
               "note": "stored verified TRACK-004 run, referenced not re-run",
               "aggregate": s.get("aggregate")},
              open(art + "/reference/aubio-block128-summary.json", "w"), indent=2, sort_keys=True)
except Exception as e:
    print("aubio reference unavailable:", e)
PY

python3 - "$artifact" "$build" "$core" "$diag" "$root" "$WORK" <<'PY'
import hashlib, json, os, subprocess, sys

artifact, build, core, diag, root, work = sys.argv[1:7]

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

def provenance_self(base, rel):
    return os.path.relpath(os.path.join(base, rel), root)

# Algorithm freeze: hash the variant algorithm + plugin source set.
algo_files = ["tools/tempo-variant/TempoVariant.h",
              "tools/tempo-variant/TempoVariant.cpp",
              "tools/tempo-variant/MethodLog.h",
              "tools/tempo-variant/MethodLog.cpp",
              "tools/tempo-variant/TempoVariantPlugin.cpp"]
algo_hashes = {f: sha(os.path.join(root, f)) for f in algo_files}
combined = hashlib.sha256()
for f in algo_files:
    combined.update(f.encode())
    combined.update(b"\0")
    combined.update(open(os.path.join(root, f), "rb").read())
    combined.update(b"\0")

prov = {
    "schemaVersion": 1,
    "task": "TRACK-005",
    "kind": "diagnostic_measurement",
    "selection": None,
    "adr": None,
    "g3": "OPEN",
    "defaultChanged": False,
    "gateChanged": False,
    "baseSha": git("rev-parse", "HEAD"),
    "baseSubject": git("log", "-1", "--format=%s"),
    "claim": "diagnostic-only BPM-report variant; NOT default evidence",
    "variantAlgorithm": {
        "method": "median of last 4 positive finite consecutive emitted beat intervals; 5 events required",
        "startupFallback": "wrapped backend bpmCandidate unchanged until ready",
        "intervalWindowSeconds": [0.25, 1.5],
        "resetPolicy": "non-finite/<=0/non-monotonic/sub-min/gap>max/non-causal resets the ring",
        "octaveCorrection": False,
        "truthInputs": False,
    },
    "freezeHashes": {
        "method": "median of last 4 positive finite consecutive emitted beat intervals; 5 events required (unchanged)",
        "originalPreCorrectionCombinedSha256": "7fccdd7f2dc32d9bbaebb8a5ac0db6f7cc989c0b386403b77adc97a8dac4ba0b",
        "correctedCombinedSha256": combined.hexdigest(),
        "files": algo_hashes,
        "correctionDisclosure": (
            "The review corrections changed interval BOOKKEEPING only: a "
            "missing-vs-measured interval flag (empty cell, never a fabricated "
            "number), order-before-unsigned-subtraction, and frame-overflow "
            "rejection. The 4-interval median method, its window and the "
            "startup fallback are unchanged from the predeclared/frozen version."),
    },
    "pins": {
        "testBinary": sha(os.path.join(build, "TempoVariantTests")),
        "clickCli": sha(os.path.join(build, "tempo-variant-click")),
        "metricsCli": sha(os.path.join(build, "tempo-variant-metrics")),
    },
    "evidenceContract": (
        "Base is bf61598. The later EVAL-007 silence-coverage correction is "
        "merged on main SEPARATELY and is not rebased here; the primary gates, "
        "acquisition and BPM metrics are unaffected by it. The orchestrator "
        "verifies main-source-current metric consistency apart from the coverage "
        "flags."),
    "binaries": {
        "variantPlugin": {"path": os.path.join(build, "libtempo-variant-btrack.so"),
                          "sha256": sha(os.path.join(build, "libtempo-variant-btrack.so"))},
        "diagnosticCli": {"path": diag, "sha256": sha(diag)},
        "baselinePlugin": {"path": os.path.join(core, "librhythm-eval-btrack.so"),
                           "sha256": sha(os.path.join(core, "librhythm-eval-btrack.so"))},
        "aubioPlugin": {"path": os.path.join(core, "librhythm-eval-aubio.so"),
                        "sha256": sha(os.path.join(core, "librhythm-eval-aubio.so"))},
    },
    "coreArchives": os.path.join(core, "btrack"),
    "corpusOriginal": {
        "manifest": "testdata/rhythm/manifest.json",
        "manifestSha256": sha(os.path.join(root, "testdata/rhythm/manifest.json")),
        "verified": "wav-hashes.txt",
    },
    "corpusRepaired": {
        "manifest": "testdata/rhythm/repaired-sustain/manifest.json",
        "manifestSha256": sha(os.path.join(root, "testdata/rhythm/repaired-sustain/manifest.json")),
        "note": "18 original refs + repaired sustained_chords; hashes asserted in wav-hashes.txt",
    },
    "aubioReference": "reference/aubio-block128-summary.json",
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
