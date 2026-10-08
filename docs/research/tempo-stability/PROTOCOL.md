# TRACK-008 predeclared protocol: post-readiness tempo-stability candidate

Committed **before** the candidate is implemented and **before** any scorer or
diagnostic run on the fixed matrix. Anything not stated here is not claimed.
Changing a declared value after this commit is a protocol deviation and must be
reported as one; a changed candidate requires a new freeze.

## 1. Question and non-goals

TRACK-007 (corrected evidence) showed that the unchanged fixed variant
(`60 / median(last 4 emitted intervals)`, ready after five events, base BPM
forwarded before readiness) is not biased by readiness *latency*: it forwards the
base BPM strictly before readiness and diverges only **after** readiness. The
measured within-2-bar losses are **early post-readiness derived-BPM excursions**:
at the first ready beat the derived value can sit well outside the scorer's 2 %
agreement band while the wrapped backend's own value was closer to the truth
(e.g. `sparse_96bpm_48000hz` first ready beats 103.36 / 99.39 / 99.39 with base
95.7031; `noise_126bpm_44100hz` first ready beats 88.34 / 84.03 / 86.13 with
later convergence to 126).

This task asks whether a **new named, bounded candidate** that gates the derived
estimate behind a **self-consistency confirmation** removes those early
excursions on the preserved short and long matrices, and what other acquisition,
BPM, coverage, phase and interval outcomes change. It is **diagnostic**. It
selects no backend, tunes no gate, changes no default, defines no production
guard, does not touch the old variant, adapters, scorer, metrics, corpus or
shared build, and does not pool results into a SPEC 19 pass/fail rate.

Non-goals: no truth/fixture-identity lookup, no latency fabrication, no
octave/half/double correction, no parameter sweep, no iterative benchmark
tuning, no production promotion from synthetic results.

## 2. Base, branch, ownership, scratch

- Base `main` `677727c`; branch `wp/TRACK-008-tempo-stability`; explicit workdir
  `/home/mojo/projects/worktrees/TRACK-008-tempo-stability` (session moved here).
- Owned and touched: new `tools/tempo-stability/**`, new
  `docs/research/TEMPO-STABILITY.md`, new `docs/research/tempo-stability/**`,
  new `task-notes/TRACK-008.md`.
- **Not edited:** the old candidate (`tools/tempo-variant/**`), the tracker
  adapters (`src/btrack/**`, `src/aubio/**`), the scorer and metrics
  (`tools/rhythm-eval/**`), default plugins, corpus and raw historical evidence,
  shared CMake/CI, ledgers (`EXECUTION-LEDGER.md`), `HANDOFF.md`, `DEVPLAN.md`,
  processor/editor/DrumEngine.
- Scratch: `/home/mojo/projects/build-TRACK-008-worker`;
  `TMPDIR=/home/mojo/projects/guitars-build-resume/tmp/TRACK-008`;
  `PATH=/tmp/opencode/venv/bin:$PATH`; compile at most 2 jobs; project scratch
  budget ~1 GiB. Render/compile outside git.

## 3. Candidate: frozen method ("confirmation-gated median", CGM)

The candidate is a JUCE-free `jam::IRhythmTracker` **decorator** that composes the
real, unmodified `jam::BTrackBackend` and changes **exactly one** observation
field: `bpmCandidate`. Every other field (`inputSampleTime`, `beatEvent`,
`onsetStrength01`, `energyRmsDbfs`, `beatConfidence01`, `silence`, `phaseValid`,
`beatPhase01`, `transientDensity01`, `sourceSampleRate`) is forwarded
byte-for-byte. No truth, no future audio, no fixture metadata and no scorer state
are consulted. The clock still owns tempo; the decorator only emits evidence.

The estimator core is deliberately the **same median-of-4** as the old fixed
variant, so the measured difference is attributable to the confirmation gate, not
to a different core. Unique plugin id: **`btrack-tempo-stable`**. Unique method
log env var: **`JAM_TEMPO_STABILITY_LOG_DIR`**.

### 3.1 Interval handling (reused, unchanged window)

An interval is the difference between two consecutive emitted beat sample times
divided by the rate passed to `reset()`. The accepted window is the wrapped
adapter's own declared tempo range (`BTrackBackendConfig` minBpm 40 / maxBpm 240),
i.e. `[0.25, 1.50] s`; it is not fitted to any corpus result.

- non-causal beat (reported sample time after the end of the block that produced
  it) -> no interval, **reset**;
- first beat after reset -> no interval;
- duplicate / non-monotonic sample time (checked **before** any unsigned
  subtraction) -> no interval, **reset**;
- non-finite or `< 0.25 s` -> **malformed reset**;
- `> 1.50 s` -> **gap reset**;
- otherwise -> accepted into a fixed **4-element** ring.
- invalid frame metadata (`numSamples > kMaxAnalysisBlock`, or a sample-time
  overflow) -> **frame-invalid reset**.

### 3.2 Estimation and confirmation gate

- `derived = 60 / median4(ring)` when the ring holds 4 valid intervals and the
  result is finite and positive.
- **Confirmation.** At the first full-ring beat update set `prevDerived = derived`,
  `stableCount = 0`. At each subsequent full-ring beat update: if
  `prevDerived` is finite positive and
  `|derived - prevDerived| <= 0.02 * prevDerived`, increment `stableCount`,
  else set `stableCount = 0`; then set `prevDerived = derived`. The candidate
  becomes **confirmed** once `stableCount >= 3` and stays confirmed until a ring
  reset. The agreement tolerance `0.02` is exactly the scorer's frozen BPM
  agreement band (`kBpmAgreementFraction`), reused rather than invented.
- **Emission.** If confirmed and `derived` is valid, `obs.bpmCandidate = derived`.
  Otherwise the wrapped backend's `bpmCandidate` is forwarded unchanged
  (explicit startup/fallback). Between beats (no beat event) the last emitted
  value is retained; it is stale, not a new measurement.
- **Reset.** Any ring reset (malformed, gap, out-of-order, frame-invalid) clears
  the ring, `prevDerived`, `stableCount` and `confirmed`, so the candidate
  returns to fallback and must re-confirm.
- The wrapper's own arithmetic is bounded: a 4-element `std::array` ring, no
  allocation, no locking, no I/O; O(1) except a 4-element insertion sort. The
  wrapped backend runs on the analysis worker and may allocate; any optional
  method-log callback is a diagnostic/offline path only.
- `reset(sampleRate)` forwards the rate to the backend unchanged and uses a
  finite positive value (fallback 48000) for the wrapper's own arithmetic.

The four constants — window `[0.25, 1.50] s`, ring 4, agreement `2 %`,
`stableCount >= 3` — are **frozen**. There is no sweep.

## 4. Fixed matrix (no new fixtures, no sweep)

Two preserved corpora, reused byte-for-byte without regeneration or identity
change:

1. **Short (TRACK-006 / EVAL-003 derived-24):** `testdata/rhythm/derived/`,
   24 fixtures = 22 controlled perturbations + 2 matched baselines, 5 s, tracked
   in git. Manifest identity below.
2. **Long (TRACK-007):** the 16-fixture manifest
   `docs/research/tempo-long-windows/fixtures/manifest.json` (2 tempos 96/126 ×
   rates 48/44.1 kHz × regular/sparse/gap/noise), ~33.8 s. WAVs are **reused
   read-only** from `/home/mojo/projects/build-TRACK-007-integration/corpus`
   (verified against the committed manifest); if absent they are rendered outside
   git from the frozen generator without changing identity.

Total matrix: **40 fixtures**. No other fixture is generated or scored.

## 5. Backends, framing, pins

Four backends, same unchanged scorer, block **128**, **uncompensated**:

| role | name | source |
|---|---|---|
| default | `btrack` | pin below |
| old fixed variant | `btrack-tempo-variant` | pin below |
| **new candidate** | `btrack-tempo-stable` | new plugin from `tools/tempo-stability` |
| non-BTrack reference | `aubio` | pin below |

Existing pins (verified by sha256 at run time):

| role | path | sha256 |
|---|---|---|
| EVAL-007 scorer CLI | `/home/mojo/projects/build-EVAL-007-integration/cli/rhythm-eval` | `caba565f6c6d8482443edf8d2a0a7ec40b6327ba99ccb2898f7213f8c28de552` |
| default BTrack plugin | `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so` | `41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d` |
| aubio plugin | `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so` | `61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41` |
| old fixed variant | `/home/mojo/projects/build-TRACK-005-integration/libtempo-variant-btrack.so` | `920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717` |
| diagnostic CLI | `/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics` | `75b0d73bdfedb5028b65a0e171d371c7ea3bc97ed6cb0b5ed60c84b30f3edee2` |
| long manifest | `docs/research/tempo-long-windows/fixtures/manifest.json` | `dd3d196fde5d1b7967ef25494be9c3350398c87995d21f476ebf4871561b9236` |
| short manifest | `testdata/rhythm/derived/manifest.json` | `3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd` |
| embedded core archives | `/home/mojo/projects/build-EVAL-005/main-core/btrack/{libjam-btrack,libbtrack,libsamplerate,libkiss_fft}.a` | `92659b88…`, `6993f822…`, `55c591cf…`, `fa209a29…` (full values in provenance) |

The new candidate's **source texts** and the **built plugin binary** are pinned in
`docs/research/tempo-stability/FREEZE.json`, committed **before** the first scorer
run. The candidate plugin statically embeds the pinned EVAL-005 archives above;
its provenance records that honest composition and its distinct id.

## 6. Truth and missingness

- Truth is the corpus manifest beat grid and `nominalBpm`; no truth is read by the
  candidate.
- `acquired`, `acquisitionSeconds`, `acquisitionBars`, `lockedBpm`,
  `bpmRelativeError` are the scorer's frozen fields, unchanged.
- Scorer-missing measurements (`hasBpmLock` false, `hasNominalBpm` false) are
  exported as `null` with a `bpmMissingReason`; they are **never** zeroed. Raw
  sentinels (e.g. `acquisitionBars = 0` when not acquired) are preserved in a
  separate `*Raw` field.
- `lockedBpm` / `bpmRelativeError` are the median of phase-valid tempo samples in
  the **backend-specific** `steadyWindow` (`start = first truth beat +
  acquisitionSeconds` when acquired, else the grid midpoint; `end = duration`);
  the per-backend window bounds are reported. They are **not** "whole clip".
- Interval `measured` flags and empty cells follow the same rule: a missing
  interval is an empty cell, never a fabricated number.
- False-beat / silence-acceleration fields are retained from the scorer with its
  existing coverage labels (`NoTrueSilence`, `NotAssessedStructuralNoise`,
  `CorpusDefect`, `Measured`); no silence claim is made where the scorer reports
  unmeasured.

## 7. Measured outcomes (declared before inference)

For each fixture × backend: `acquired`, `acquisitionSeconds`,
`acquisitionBars` (null when not acquired) and `within 2 bars` strict
(`acquired && acquisitionBars <= 2.0`), `lockedBpm`, `bpmRelativeError`, the
backend-specific `steadyWindow` bounds, missing flags, `fMeasure`,
`phaseMeasured`, `halfTimeLock` / `doubleTimeLock`, and the false-beat coverage
and silence-acceleration fields.

For the candidate additionally, from its method log: first-confirmed event and
availability times, fallback beats before confirmation, confirmed beats, the
confirmed-value min/median/max, the number of confirmed beats more than 2 % from
nominal and the longest such run, and the interval-state counts.

Emitted-interval outliers for every backend's canonical beat series: expected
period is one beat for short and for `regular`/`gap`/`noise`, two beats for
`sparse`; an interval outside ±10 % of the expected period is an outlier, and an
interval spanning the long gap window (only on the `gap` control) is reported
separately, not as an outlier.

## 8. Comparison rules (exact)

Primary pair: **default `btrack` vs new candidate `btrack-tempo-stable`**. Also
reported: default vs old variant, and default vs aubio, and every short
**perturbation vs its paired baseline** for each backend. Per pair:

- **acquisition gained/lost** and **within-2-bar gained/lost** as in TRACK-007;
- **BPM state**: not-evaluable if either side's `bpmRelativeError` is null; else
  `regression` if `other - base > 0.5` percentage points, `gain` if
  `base - other > 0.5` pp, else `no-change`. The 0.5 pp band is a stated
  diagnostic threshold, not a SPEC gate;
- a null on either side makes the comparison not evaluable, not zero.

Counts are per fixture and per corpus. **No pooled pass threshold, no
significance claim, no core denominator.** Short clips are 5 s; long clips at
different tempos/rates overlap the same two performances and are not independent
trials.

## 9. Hard validation (any failure stops the run)

- Every pin in section 5 and the candidate freeze in `FREEZE.json` matches.
- Every short/long WAV hash, byte size, sample rate, channels, bit depth and frame
  count matches its committed manifest; no WAV is written into git.
- Long matrix: 16 fixtures, no `core` tag, `trueSilenceSpans` empty; noise onsets
  and beats equal the paired regular clip; sparse silent beats are the odd
  indices; the gap window contains a removed beat; regular has no silent beats.
- Scorer: 40 fixtures per backend, `blockFrames` 128, uncompensated only, no
  legacy block-stamped beats, source sha256 match, no core membership.
- Candidate method log: exactly one instance per corpus; block counts equal
  `ceil(frames/128)`; block indices in order; `emittedBpm == baseBpm` exactly when
  not confirmed; `derivedBpm` empty exactly when the ring is not full;
  `ringCount <= 4`.
- Beat series: default, old variant and new candidate are **byte-identical** on
  every fixture (the decorator cannot move beats). Any mismatch is a hard
  failure. Aubio is reported separately with no equality claim.
- No NaN/Inf in any scored field.

## 10. Order of work and freeze discipline

1. **This protocol** committed before the candidate is implemented and before any
   scorer run.
2. Candidate + deterministic method tests implemented, then a **source
   freeze/provenance** commit (`FREEZE.json`) before any scorer run. No scorer
   output exists at that point.
3. **Only then** the scorer/diagnostic runs. No result-driven change to the
   candidate, bands or matrix; any change requires a new freeze and a new
   protocol.
4. Report, evidence and `task-notes/TRACK-008.md`.

## 11. Non-promotion and limits

G3 remains **OPEN**. No backend, guard, gate or default is selected or changed.
The result is synthetic (guitar-like plucks for the long matrix, derived windows
of the EVAL-001 corpus for the short matrix); it is not recorded guitar and cannot
promote any candidate to production. Backend-specific steady windows, scoped
silence coverage and overlapping clips are stated limits, not pass claims.
