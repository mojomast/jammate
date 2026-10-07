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
- The **timing-corrected CLI built from main sources** (`ad7872f`, which
  contains `0027baa`) and the two unmodified `dlopen` shims. Primary
  configuration: 128-frame block, **uncompensated**, no legacy stamping. The
  branch base `0a15eef` predates `0027baa`; main was exported read-only and
  built in its own root rather than merged (see build pins below).

## Contract implemented

`run_robustness.py` (stdlib only) consumes a derived manifest plus one
`results.json` per backend (or runs the CLI itself with `--cli`) and emits
`degradation.{json,csv,md}` and `coverage.csv`:

- **Tidy curves** — one row per `(backend, parent, perturbation, param, metric)`
  (2160 rows = 2 × 24 × 45), each with `fixture`, `baselineFixture`, raw
  `value`, `baselineValue`, `difference`, `status`, `baselineStatus`, `reason`
  and `caveat`. The paired baseline is the declared same-parent, same-source
  truncation baseline, validated to be a baseline of the same parent and cut
  from the identical source window.
- **Validation** — `parentFixture` + `pairedBaseline` present and consistent;
  exactly one baseline per parent; **same source truncation**
  (`truncation.source{Start,End}Frame` / `…Seconds`) between a clip and its
  pair, so a different original source window is rejected even when the parent
  matches; no duplicate fixture names in the manifest; no duplicate fixture
  names in a backend's results; a backend's declared `backend` label must match
  the requested name (never silently overwritten); mismatched / non-baseline
  pairs and duplicate metric rows are hard errors (non-zero exit). Missing
  fixtures are errors and appear in coverage. Output-length differences are
  legitimate (warp/pad) and only warned about.
- **Missing stays missing** — `bpm.*` requires `hasBpmLock` (+ `hasNominalBpm`
  for the error), `phase.*` requires `phaseMeasured`, acquisition times require
  `acquired`, silence requires `trueSilenceMeasured`. Missing is `null` (JSON) /
  empty (CSV), never `0.0` and never a pass.
- **No invented thresholds** — detection, CPU, silence and ramp are raw. The
  only normalisation is the documented SPEC 19 BPM `<= 2 %` number, emitted as
  `bpm.spec2pctWithin` / `bpm.spec2pctNormalized` only for a measured,
  comparable, **steady** lock, and `null` otherwise. `spec2pctWithin` compares
  the **numeric** relative error to `0.02` (`0`, `0.0004`, `0.0133`, `0.02`
  pass; `0.0234` fails), never a bool cast of a nonzero value. There is no
  overall grade.
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
Ran 29 tests ... OK
```

Coverage: parameter-token mapping; manifest pair mapping and mismatched /
non-baseline / duplicate-name / **source-truncation-mismatch** / missing-source
detection; that a legitimate output-length difference is accepted; missing
BPM/phase/acquisition semantics and that a real `0.0` is not treated as unset;
the BPM 2 % regression (`0`, `0.0004`, `0.0133`, `0.02`, `0.0234`, plus
missing-lock and non-steady) proving the numeric comparison, not a bool cast;
noise silence not-assessed with no difference; duplicate-row detection; backend
results duplicate-name / legacy-stamp / wrong-block rejection; **every derived
WAV re-hashed against the manifest sha256 and byte size**; an **actual
subprocess run of the script** over a synthetic corpus verifying CSV/JSON
value-for-value consistency, coverage and validation, plus hard-error runs for a
missing fixture, a mismatched baseline and a backend-label mismatch; and an
**actual run of the script driving the main-source CLI over the real 24-clip
derived corpus** (29 tests total, all green).

Real run (primary): `run_robustness.py --cli … --backend btrack --backend aubio`
produced 2160 rows, 24 pairs, **0 hard errors**, 0 duplicate rows, both backends
24/24 fixtures present. Raw CLI outputs committed under
`docs/research/robustness/raw/`.

Build pins: GCC 14.2.0, CMake 4.4.4, Ninja, Release, `-j2`; jam-core both
backends ON; CLI `-rdynamic`; plugin shims `build-{btrack,aubio}-plugin.sh`.
Main `ad7872f` (contains `0027baa`) was exported with `git archive` to
`/home/mojo/projects/build-EVAL-005/main-src` and built in
`…/build-EVAL-005/main-{core,cli}`; main was neither merged nor edited. `/tmp`
is a full tmpfs, so all build roots / `TMPDIR` / scratch live under
`/home/mojo/projects/build-EVAL-005/`.

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
- Tempo step: F collapses for both (BTrack 0.30/0.22; aubio 0.125/0.00). Both
  backends' whole-clip median `lockedBpm` is unchanged from their baseline value
  (~123.05 BTrack, ~127.8 aubio) and **no within-clip tempo trajectory was
  measured**, so this is not evidence of step-following failure; `nominalBpm` is
  null so BPM error is `NA`; duration differs (caveated).
- 1 s carved silence: no false beats inside the gap for either backend; BTrack
  silence tempo-increase proxy 0.000 BPM, aubio insufficient evidence.

These are deliberately reported as raw, including the non-monotone and
counter-intuitive cases. No normalisation or ranking is drawn from them.

## Integration review fixes (round 2, on `acc6e7f`)

1. **BPM 2 % boolean bug** — `bpm.spec2pctWithin` had output kind `bool` but a
   numeric source, so `extract_metric`'s bool cast turned any nonzero relative
   error into `1` before the comparison; `.0004` and `.0133` therefore failed.
   The metric now carries `raw_kind="float"` and preserves the numeric error
   until the `<= 0.02` comparison (regression test with `0`, `0.0004`, `0.0133`,
   `0.02`, `0.0234`, plus missing / non-steady, on both the clip and baseline
   sides). On the committed data this flips 27 rows from `0` to `1` (all sub-2 %
   locks: every aubio derived clip, the BTrack `syncopated_funk` clips and the
   BTrack `0.000381` clips); the BTrack `0.0234` clips and the clean baseline
   correctly stay `0`.
2. **Reproduction command** — the module docstring and `README.md` omitted
   `--backend aubio` while listing both plugins; both now list both backends and
   the corrected build roots.
3. **Same source-truncation pairing** — pairing is now enforced on the original
   source window (`truncation.source{Start,End}Frame` / `…Seconds`), not the
   output length: a warp/pad may change `signal.frames`, but a clip from a
   different source window is a hard error, and output differences are warnings.
   Added duplicate-manifest-name, duplicate-results-name and backend-label-
   mismatch hard errors (`main` previously overwrote the backend identity), with
   targeted malformed-input tests.
4. **Rebuild from main sources** — `ad7872f` (contains `0027baa`) was exported
   with `git archive` and jam-core + CLI + plugins rebuilt in
   `…/build-EVAL-005/main-{src,core,cli}`; main was neither merged nor edited.
   Measured effect: **none on any scored metric**. No beat is stamped at a block
   boundary (`beatsStampAtBlockStart == 0` for all 48 fixture runs), so
   `beatsReportedByBackend == reportedLatencyCount` and the boundary-latency fix
   is a no-op on this corpus; only the non-scored wall-clock `cpuSeconds`
   changed, which is why the raw `results.json` hashes moved. All 24 derived
   WAVs were re-hashed against the manifest (sha256 + byte size all match).
5. **Wording** — the tempo-step discussion no longer asserts that the whole-clip
   median `lockedBpm` proves a step-following failure; no within-clip trajectory
   was measured.

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

- Round 1 (implementation + evidence): `a1fc75f`.
- Round 2 (integration-review fixes + regenerated evidence): `_filled in after
  commit_`.
- The note-SHA update is the subsequent commit on `wp/EVAL-005-robustness`; the
  branch head is the handoff SHA reported to the orchestrator.
