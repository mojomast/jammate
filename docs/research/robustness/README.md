# Robustness degradation curves — EVAL-005

Paired, per-perturbation degradation curves for **both real tracker
integrations** (BTrack 1.0.7 and aubio 0.4.9) driven over the unchanged
**EVAL-003 deterministic 24-clip derived corpus**. This directory is new;
nothing under `testdata/`, `src/`, `vendor/`, any CMake file, the harness sources
or the execution ledger was modified.

This is a **diagnostic** artifact. It selects no tracker, tunes no gate, invents
no threshold, and produces no rolled-up overall grade. G3 stays open.

## Files

| file | what it is |
|---|---|
| `degradation.csv` | authoritative tidy long record: one row per `(backend, parent, perturbation, parameter, metric)` with raw value, paired-baseline value, difference and status |
| `degradation.json` | the same rows plus provenance, global caveats, metadata warnings, validation and coverage |
| `degradation.md` | human-readable curves + caveats (generated) |
| `coverage.csv` | measured / missing / not-assessed row counts per backend |
| `raw/<backend>/block128/` | the untouched CLI output (`results.json`, `summary.md`, `fixtures.csv`, `per_fixture/`) that the curves consume |

2160 rows = 2 backends × 24 clips × 45 metrics.

## Generation (one command)

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-EVAL-005/tmp
python3 tools/rhythm-eval/tools/run_robustness.py \
  --derived-manifest testdata/rhythm/derived/manifest.json \
  --cli /home/mojo/projects/build-EVAL-005/main-cli/rhythm-eval \
  --backend btrack --backend aubio \
  --backend-lib btrack=/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so \
  --backend-lib aubio=/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so \
  --out docs/research/robustness
```

Omitting `--cli` and passing `--backend btrack=<results.json>` instead re-derives
the curves from already-produced CLI runs without touching a tracker.

The tool itself invokes each backend as (primary, uncompensated, 128-frame):

```bash
rhythm-eval --corpus testdata/rhythm/derived --out .../raw/<backend>/block128 \
            --backend <name> --backend-lib <plugin.so> --block 128
```

## Build pins

- Branch `wp/EVAL-005-robustness` base `0a15eef` (`merge(EVAL-003) ...`).
- **CLI built from main sources, not the branch base.** The base predates main
  `0027baa` ("include boundary events in latency mean"). Main sources were
  exported to `git archive ad7872f` into
  `/home/mojo/projects/build-EVAL-005/main-src` (read-only; main was never
  merged and its sources were never edited) and built there.
- GCC 14.2.0, CMake 4.4.4, Ninja, Release, `-j2`.
- jam-core build root `/home/mojo/projects/build-EVAL-005/main-core` with
  `-DJAM_ENABLE_BTRACK=ON -DJAM_ENABLE_AUBIO=ON`.
- CLI build root `/home/mojo/projects/build-EVAL-005/main-cli` with
  `-DCMAKE_EXE_LINKER_FLAGS=-rdynamic` (the global `operator new/delete`
  counter must see the `dlopen`ed plugin).
- Plugins built from the same main sources with the unchanged shims
  `tools/rhythm-eval/build-{btrack,aubio}-plugin.sh`.
- `/tmp` is a full tmpfs here, so every build root, `TMPDIR` and scratch output
  lives under `/home/mojo/projects/build-EVAL-005/`.

The `0027baa` boundary-latency fix is a **no-op on this corpus**: no beat is
stamped exactly at a block boundary (`beatsStampAtBlockStart == 0` for every
fixture of both backends), so `beatsReportedByBackend == reportedLatencyCount`
and every scored metric is byte-identical to the branch-base build. Only the
non-scored wall-clock `cpuSeconds` differs, which is why the raw hashes changed
on the rebuild.

## Provenance hashes

- Derived manifest sha256 `3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd`
  (base manifest sha256 `06fe2c4356dd411a90b5e4948ebeb00eeb68d82f42d26b5ad25f4797c530fab1`).
- Every one of the 24 derived WAVs was re-hashed against the manifest
  (`DerivedCorpusHashTest`); all sha256 and byte sizes match.
- `raw/btrack/block128/results.json` sha256 `07a82506e2fa748b05a6fffffad58f1690e1906113b95d94f1cb5d13d372979c`.
- `raw/aubio/block128/results.json` sha256 `c772690539f3ec019fcd92c0f86f772f784a85e7246138de8df2624d86a18bd3`.

## Semantics (enforced, in code and in tests)

### Pairing

Every derived clip is paired against its declared `pairedBaseline` — the
**same-parent, same-source-truncation** baseline. Pairing is enforced on the
*original source window* (`truncation.sourceStartFrame` / `sourceEndFrame` /
`sourceStartSeconds` / `sourceEndSeconds`), not on the output length: a warp or
pad legitimately changes `signal.frames` / `durationSeconds`, but a clip cut
from a different source window is a hard error. The script validates the
manifest before producing a row: `parentFixture` and `pairedBaseline` must
exist, the pair must be the baseline of the same parent, there must be exactly
one baseline per parent, and no fixture name may repeat. A mismatched,
non-baseline, non-same-window or duplicate-name entry is a hard error
(non-zero exit). In the backend results, duplicate fixture names and a declared
`backend` label that disagrees with the requested name are also hard errors
(never silently collapsed or overwritten). Row keys
`(backend, parent, perturbation, param, metric)` must be unique; duplicates are
a hard error.

### Missing stays missing

A missing lock is never a zero. `bpm.*` numbers are emitted only when the CLI
reports `hasBpmLock`; `phase.*` only when `phaseMeasured`; acquisition time only
when `acquired`; silence metrics only when `trueSilenceMeasured`. Otherwise the
value is empty in CSV and `null` in JSON. A `0.0` reported by the harness is kept
as a measured `0.0`.

### Commensurability caveats

Perturbations that change the audio timeline relative to the baseline are
emitted with an explicit caveat on every row, because the raw counts then span a
different window: `tempo_step` (duration changes), `onset_offset` /
`leading_silence` (content window shifts), `trailing_silence` (duration +0.5 s).
`silence_gap`, `syncopation_burst` and `drop_onset` carry the corresponding
"edit-onsets / SPEC gate NOT-MEASURED" caveat.

### Noise is not a silence test

The five `noise` clips inherit **structural** `trueSilenceSpans` from their
parent while the added floor fills what was near-silent. Their raw silence
counts are shown, but the two true-silence metrics are marked
`not_assessed_noise` (no difference computed) and a metadata warning is
recorded. The raw CLI summary's "fixtures measured for true silence" line
counts those noise clips; it must not be read as a silence result.

### BPM normalisation, and only that

`bpm.spec2pctNormalized = bpmRelativeError / 0.02` and `bpm.spec2pctWithin`
use the one documented SPEC 19 number (`<= 2 %`). `spec2pctWithin` compares the
**numeric** relative error to `0.02` (so `0.0004` and `0.0133` pass, exactly
`0.02` passes, `0.0234` fails) — it is not a cast of a nonzero value to true.
Both are emitted only for a measured, comparable lock on a **steady** clip, and
are `null` otherwise. No other normalisation exists; detection, CPU, silence and
ramp are raw only. There is deliberately no single overall score.

### 5 s clips are not a release gate

The derived clips are 5.0 s windows and carry `core_parent`, not `core`. They
are outside any SPEC 19 core denominator and cannot establish the absolute
"acquire within 2 bars for >= 95 % of core fixtures" gate. Acquisition is
reported per clip only.

## Headline measured results (including the inconvenient ones)

Absolute numbers on a 5 s window are **not** comparable to the whole-fixture
EVAL-004 numbers; only the paired deltas are meaningful.

- **The baseline itself is weak and not locked.** On `clean_eighths__baseline`
  BTrack F = 0.737 (9 predicted / 10 truth, 7 matched) and aubio F = 0.667
  (5 predicted, 5 matched); neither reports a sustained acquisition lock
  (`acquired=false`) despite the matches. The 5 s window is too short for the
  harness acquisition definition.
- **Attenuation is not a monotone degradation for BTrack.** F goes
  0.737 (baseline) -> 0.800 (-20 dB) -> 0.842 (-40 dB) -> 0.000 (-60 dB).
  aubio goes 0.667 -> 0.667 -> 0.182 -> 0.000: a different absolute level floor.
- **Clipping at 0.5 / 0.25 / 0.125 FS changes neither backend's F at all.** The
  distorted transients are still detected on this clip.
- **A real 40 ms delay or 0.5 s leading silence *raises* BTrack F to 0.900.**
  The window moved (caveated), so this is not a claim of improvement — it is a
  demonstration that absolute counts are not commensurable under a shift.
- **BTrack's locked-BPM error is bimodal**: the documented 44.1 kHz
  -2.34 % bias (0.0234) on most clips, but 0.0004 on noise `snrDb=0`,
  clipping 0.125/0.25 and `drop_every2`. The metric *improves* on perturbations
  that *worsen* F, because the median lock flips. It is not a degradation curve.
- **The tempo step collapses F for both** (BTrack 0.30 at 0.85, 0.22 at 1.25;
  aubio 0.125 at 0.85, 0.00 at 1.25). Both backends' **whole-clip median
  `lockedBpm`** is unchanged from their baseline value (~123.05 BTrack,
  ~127.8 aubio) on clips warped by ±15/25 %, and the warped clips declare no
  `nominalBpm`, so BPM error is `NA`. **No within-clip tempo trajectory was
  measured**, so this does not by itself show the backend failed to follow the
  step: it only shows that the whole-clip median lock is not an instrument for a
  within-clip step. Duration also differs (caveated); the F/phase collapse is
  the measured effect.
- **The 1 s carved silence produces no false beats inside the gap** for either
  backend (both stop emitting through it), and BTrack's silence tempo-increase
  proxy is 0.000 BPM; aubio has insufficient evidence to evaluate it. This is a
  raw proxy, not the SPEC silence gate.
- **The isolated/multi syncopation bursts do not move either backend** on this
  clean clip (F unchanged), but the CLI's syncopation proxy is keyed to the
  `syncopated_funk` fixture name, so the derived burst clips produce no
  syncopation proxy at all (`hasSyncopation=false`). The available evidence is
  the F/phase/BPM deltas.

## Limitations

1. Synthetic corpus (see `testdata/rhythm/README.md`); 5 s windows, one parent
   with the full grid and one with a 2-point noise curve (EVAL-003 budget).
2. `trueSilenceSpans` on noise clips is structural only (above).
3. Both adapters are not equally configured (BTrack resamples to 44.1 kHz;
   aubio runs at the device rate; internal silence gates differ). This compares
   integrations, not library cores — see
   `../tracker-comparison/comparison.md`.
4. The CLI's syncopation-stability metric never fires for the derived clips
   (name/tag keyed), and acquisition on 5 s windows is frequently not
   sustained; both are reported as missing rather than substituted.
5. CPU seconds and allocation counts are resource diagnostics, not gates.

**G3 remains OPEN. No tracker is selected, no production wiring, no ADR.**
