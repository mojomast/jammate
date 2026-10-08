# TRACK-007 correction contract and errata

Committed **before** any corrected derived evidence is recomputed. This file and
the resulting `evidence-corrected/` are additive. The following are **immutable**
and are not edited, re-rendered or overwritten by this correction:

- `492c5a8` — the predeclared protocol (`PROTOCOL.md`).
- `d21e2a6` — fixture identities (`fixtures/manifest.json`).
- `5098df9` — the original generator, runner, tests, report, task note and the
  entire historical `evidence/` tree (raw scores, beat series, method log).

The WAVs, the generator and the ground truth are unchanged. The orchestrator
independently replayed all 48 scores, 48 beat files and the method log at
`/home/mojo/projects/build-TRACK-007-integration`; those differ from the committed
raw only in the resource field `cpuSeconds` (and its aggregate), every scored field
byte-identical.

## Errata (what the original report/evidence got wrong)

- **E1 — cause of the within-2-bar losses.** The original text said the losses were
  a "readiness startup cost" / "one bar of confirmation". That is wrong. The fixed
  variant **forwards the base BPM unchanged before readiness** (verified: on all 16
  clips `variantBpm == baseBpm` on every pre-ready block), so the two backends'
  reported tempos are identical until the first ready block. Acquisition divergence
  therefore begins only **after** readiness. Acquisition is granted by
  `findFirstLockFrom`, which requires a run of **`kLockRunLength = 4` consecutive
  predicted beats**, each matching a truth beat within tolerance, each advancing,
  and each carrying a tempo sample within **`kBpmAgreementFraction = 2 %`** of the
  local truth BPM (`tools/rhythm-eval/Metrics.cpp`, `Metrics.h`). It is a 4-agreeing-
  beat run, not "one bar". The 96 BPM losses come from the variant's **post-ready
  derived values being out of band for the first ready beats**: e.g. sparse 96 BPM
  48 kHz reports 103.36 (+7.67 %), 99.39 (+3.53 %), 99.39 (+3.53 %) before the first
  4-in-band run. On `regular_126bpm_48000hz` and `gap_126bpm_48000hz` the lock is
  found at **1.2887 s**, *before* first-ready availability **2.2533 s**, on the
  forwarded base BPM, so default and variant share `acquisitionBars =
  0.492821875` exactly.
- **E2 — missed regression: `noise_126bpm_44100hz`.** The original report did not
  call this out. Variant acquisition **4.7412 bars** versus default **2.4919 bars**
  (**+2.2491 bars** = 4.74120238 − 2.49205952 = 2.24914286, about +4.29 s). The first
  five ready beats report
  −29.89 / −33.31 / −31.64 / −31.64 / −18.78 % (minimum **84.03 BPM**, 126 nominal);
  a clear post-ready estimator excursion, and the strongest such case in the matrix.
- **E3 — `acquisitionBars` sentinel not normalised.** The raw scorer emits
  `acquisitionBars = 0` when not acquired. The original `outcomes.json` copied that
  0, and an evidence test asserted it. Corrected outcomes normalise the measured
  value to **`null`** when `acquired` is false, while **explicitly preserving the raw
  sentinel 0** in a separate field. The corrected test asserts the null, not the 0.
- **E4 — `lockedBpm` / `bpmRelativeError` are not whole-clip.** They are the median
  of phase-valid tempo samples inside a **backend-specific** window
  (`Metrics.cpp` steadyWindow): `start = first truth beat + acquisitionSeconds` when
  that backend acquired, else the grid midpoint; `end = duration`. Default and
  variant windows therefore **differ on 10 of 16 cells** (they acquired at different
  times). The original "whole-clip" label is dropped and the window bounds are
  reported per backend. The frozen metric is not redefined; only the label is fixed.
- **E5 — gap-spanning exemption was too wide.** The original `interval_summary`
  exempted **any** interval crossing 16–17 s, on any control. It must apply only to
  `control == 'gap'`. Correcting it, the aubio `sparse_126bpm_48000hz` interval
  15.9766 → 17.4922 s is a genuine outlier: aubio outliers become **29** (was 28),
  longest run **29** (was 28).
- **E6 — combined-freeze pin was mis-attributed.** `PROTOCOL.md` listed
  `cd8291e6…` as the sha256 of the file
  `docs/research/tempo-variant/provenance.json`. It is not: it is
  `freezeHashes.correctedCombinedSha256`, the combined hash of the five variant
  **source** files, recorded **inside** that file. The file's own sha256 is
  `202714b16002ac9f9c94aca67684abaacaab5666a35aff9f018c8450d4c032fe` (verified at
  `492c5a8` and at `HEAD`). The binary/source pins are unchanged and remain valid.
- **E7 — process deviations, disclosed honestly.** Two things were not predeclared
  even though the *scientific* contract was: (a) the runner's additional fail-closed
  apparatus (pin verification, manifest byte-equality) was written after the run
  rather than frozen in the protocol; (b) the run wrote its raw output into the
  committed tree `docs/research/tempo-long-windows/evidence/` whereas the protocol
  §6 showed the commands writing to `$TMPDIR/…/evidence`. Neither affects the
  measured numbers; both are recorded here rather than silently.

## What the corrected derived evidence adds

Written to `docs/research/tempo-long-windows/evidence-corrected/` by
`tools/tempo-characterization/recompute_evidence.py`, which reads the **immutable**
historical raw and manifest and re-derives, without touching them:

1. `outcomes.json` — corrected per fixture × backend: `acquisitionBars` (null when
   not acquired), `acquisitionBarsRaw` (raw sentinel preserved),
   `acquisitionSeconds`, `acquiredWithinTwoBars` (strict, unrounded comparison),
   `hasBpmLock`, `hasNominalBpm`, `detectionMeasured`, `bpmMissingReason`,
   `lockedBpm`, `bpmRelativeError`, and the backend-specific `steadyWindow` bounds.
2. `comparison.json` — the original 16 default-vs-variant records, kept, plus **36
   paired control-vs-regular records** (12 perturbations × 3 backends) using the
   same bands and null semantics.
3. `intervals.json` — corrected: the gap-spanning exemption applies only to the
   `gap` control.
4. `validation.json` — hard checks: `paired.validate_results` on every raw
   `results.json` (block 128, no legacy stamps, uncompensated only, fixture sets),
   plus manifest-level truth checks (noise onsets/beats identical to regular,
   sparse silent beats are the odd indices, gap silent beats include the removed
   window, regular has none, the full 16-cell matrix is present).
5. `errata.json` — E1–E7 and the verified pins, for machine reading.

No new backend, candidate, default, scorer, adapter or shared build/ledger/
HANDOFF/DEVPLAN change is made. G3 stays OPEN. The next-action contract is
**post-ready estimator stability** as a new named candidate with a new freeze, not
a blind earlier-readiness change.
