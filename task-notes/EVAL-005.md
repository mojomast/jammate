# EVAL-005 — robustness degradation curves

## Goal

Add the robustness axis to the tracker comparison as **measured degradation
curves**, not pass/fail: run both real tracker integrations over the unchanged
EVAL-003 deterministic derived corpus, pair every perturbed clip with its
declared same-parent baseline, and report the raw delta and the coverage. This
is the consumer side of EVAL-003's contract and the successor of EVAL-004's
timing-corrected comparison.

## Base and scope

- Base commit: `0a15eef` (`merge(EVAL-003): deterministic paired robustness
  corpus with acoustic integrity tests`). Worktree
  `/home/mojo/projects/worktrees/EVAL-005-robustness`, branch
  `wp/EVAL-005-robustness`.
- Only these new paths are owned and touched:

  ```
  tools/rhythm-eval/tools/run_robustness.py
  tools/rhythm-eval/tools/test_run_robustness.py
  docs/research/robustness/**
  task-notes/EVAL-005.md
  ```

- No shared harness / base corpus / derived corpus / `src/` / `vendor/` / CMake
  file / execution ledger was modified. No new fixtures, no downloads. No tracker
  or gate was tuned and no production choice was made.

## What it consumes

- The **EVAL-003 derived manifest** `testdata/rhythm/derived/manifest.json`
  (sha256 `3bb6d350…`, base manifest `06fe2c43…`): 24 clips, 2 parents, 11
  perturbations, each with `parentFixture` / `pairedBaseline` /
  `groundTruth.policy` / `transformation` / `truncation`.
- The **EVAL-004 timing-corrected CLI** and the two unmodified `dlopen` shims.
  Primary configuration: 128-frame block, **uncompensated**, no legacy stamping.

## Contract implemented

`run_robustness.py` (stdlib only) consumes a derived manifest plus one
`results.json` per backend (or runs the CLI itself with `--cli`) and emits
`degradation.{json,csv,md}` and `coverage.csv`:

- **Tidy curves** — one row per `(backend, parent, perturbation, param, metric)`
  (2160 rows = 2 × 24 × 45), each with `fixture`, `baselineFixture`, raw
  `value`, `baselineValue`, `difference`, `status`, `baselineStatus`, `reason`
  and `caveat`. The paired baseline is the declared same-parent, same-5.0 s
  truncation baseline, validated to be a baseline of the same parent.
- **Validation** — `parentFixture` + `pairedBaseline` present and consistent;
  exactly one baseline per parent; mismatched / non-baseline pairs and duplicate
  metric rows are hard errors (non-zero exit). Missing fixtures are errors and
  appear in coverage.
- **Missing stays missing** — `bpm.*` requires `hasBpmLock` (+ `hasNominalBpm`
  for the error), `phase.*` requires `phaseMeasured`, acquisition times require
  `acquired`, silence requires `trueSilenceMeasured`. Missing is `null` (JSON) /
  empty (CSV), never `0.0` and never a pass.
- **No invented thresholds** — detection, CPU, silence and ramp are raw. The
  only normalisation is the documented SPEC 19 BPM `<= 2 %` number, emitted as
  `bpm.spec2pctWithin` / `bpm.spec2pctNormalized` only for a measured,
  comparable, **steady** lock, and `null` otherwise. There is no overall grade.
- **Commensurability caveats** — `tempo_step` (duration changes), `onset_offset`
  / `leading_silence` (content window shifts), `trailing_silence` (duration
  changes) carry explicit caveats; `silence_gap`, `syncopation_burst`,
  `drop_onset` carry the "SPEC gate NOT-MEASURED offline" caveat.
- **Noise** — its structural `trueSilenceSpans` are not a silence test: the two
  true-silence metrics are marked `not_assessed_noise` and a metadata warning is
  written.
- **5 s scope** — derived clips carry `core_parent`, not `core`; their
  acquisition is per-clip only and the absolute SPEC 19 acquisition gate is
  explicitly out of reach.

## Tests executed

Python (stdlib `unittest`), `tools/rhythm-eval/tools/test_run_robustness.py`:

```
python3 tools/rhythm-eval/tools/test_run_robustness.py
Ran 16 tests ... OK
```

Coverage: parameter-token mapping; manifest pair mapping and mismatched /
non-baseline detection; missing BPM/phase/acquisition semantics and that a real
`0.0` is not treated as unset; noise silence not-assessed with no difference;
BPM normalisation requires a steady clip; duplicate-row detection; an **actual
subprocess run of the script** over a synthetic corpus verifying CSV/JSON
value-for-value consistency, coverage and validation, plus hard-error runs for a
missing fixture and a mismatched baseline; and an **actual run of the script
driving the real CLI over the real 24-clip derived corpus** (16 tests total, all
green).

Real run (primary): `run_robustness.py --cli … --backend btrack --backend aubio`
produced 2160 rows, 24 pairs, **0 hard errors**, 0 duplicate rows, both backends
24/24 fixtures present. Raw CLI outputs committed under
`docs/research/robustness/raw/`.

Build pins: GCC 14.2.0, CMake 4.4.4, Ninja, Release, `-j2`; jam-core both
backends ON; CLI `-rdynamic`; plugin shims `build-{btrack,aubio}-plugin.sh`.
`/tmp` is a full tmpfs, so all build roots / `TMPDIR` / scratch live under
`/home/mojo/projects/build-EVAL-005/{tmp,core,cli}`.

## Measured results (see `docs/research/robustness/README.md` and `degradation.md`)

- Coverage: BTrack 1080 rows, 868 measured, 202 missing, 10 not-assessed;
  aubio 1080 rows, 890 measured, 180 missing, 10 not-assessed.
- The 5 s baseline is weak and not locked: BTrack F 0.737 (9 predicted, 7
  matched) and aubio F 0.667 (5 predicted, 5 matched), both `acquired=false`.
- Attenuation is non-monotone: BTrack F 0.737 -> 0.800 (-20 dB) -> 0.842
  (-40 dB) -> 0.000 (-60 dB); aubio 0.667 -> 0.667 -> 0.182 -> 0.000.
- Clipping 0.5/0.25/0.125 changes neither backend's F.
- A 40 ms delay or 0.5 s leading silence raises BTrack F to 0.900 (window moved;
  caveated).
- BTrack locked-BPM error is bimodal (0.0234 documented bias vs 0.0004) and can
  improve while F worsens.
- Tempo step: F collapses for both (BTrack 0.30/0.22; aubio 0.125/0.00) and
  neither `lockedBpm` follows the warp; `nominalBpm` is null so BPM error is
  `NA`; duration differs.
- 1 s carved silence: no false beats inside the gap for either backend; BTrack
  silence tempo-increase proxy 0.000 BPM, aubio insufficient evidence.

These are deliberately reported as raw, including the non-monotone and
counter-intuitive cases. No normalisation or ranking is drawn from them.

## Harness issues detected (reported, not rewritten)

These are semantics/limits of unowned files, recorded rather than patched:

1. The CLI's syncopation-stability proxy is keyed to the fixture name/tag
   `syncopated_funk`, so the derived `syncopation_burst` clips never populate
   `hasSyncopation` (`syncopation.*` is `missing` for them). The available
   evidence for the SPEC isolated-event question is the F/phase/BPM delta.
2. The CLI's raw true-silence coverage line counts the noise clips as "measured"
   because `trueSilenceSpans` is inherited structurally. The aggregator blocks
   those rows and warns; the raw summary line must not be used as a silence
   result.
3. On 5.0 s windows the harness acquisition definition is frequently not
   satisfied (`acquired=false`, including the clean baseline), so
   `acquisitionBars` is `NA` for many clips. This is why the absolute SPEC 19
   acquisition gate is out of scope here.

No CLI bug blocked the run; the exact commands are in the artifacts.

## Limitations

Synthetic 5 s corpus; only the paired deltas are meaningful; adapters are not
equally configured (BTrack resamples to 44.1 kHz); noise silence is structural;
CPU/allocation are resource diagnostics. G3 stays **OPEN**; no tracker selected,
no production wiring, no ADR.

## Handoff

Authoritative evidence: `docs/research/robustness/` (`degradation.{json,csv,md}`,
`coverage.csv`, `README.md`, `raw/`). Re-derive with the one command in
`README.md`. Implementation commit SHA recorded below.

## Final commit SHA

- Implementation + evidence + note commit: _filled in after commit_.
