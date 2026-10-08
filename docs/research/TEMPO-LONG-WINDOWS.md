# Longer-window synthetic characterization (TRACK-007)

Diagnostic follow-up to TRACK-005/TRACK-006. **The fixed variant, adapters, scorer,
metrics, old corpus and artifacts are unchanged. No backend is selected, no
parameter is tuned, no guard is implemented, no gate changes. G3 stays OPEN.**
This work characterizes the variant on **new, longer (~33.8 s) paired synthetic
guitar-like windows** and reports measured wins, regressions, and limits. It is
not a production selection.

> **Correction applied (independent review of `5098df9`).** An independent review
> found errors in the first version of this report and its derived outcomes. The
> corrections are declared in [`CORRECTIONS.md`](tempo-long-windows/CORRECTIONS.md)
> (commit `4a89dc1`) and recomputed into a **separate**
> `docs/research/tempo-long-windows/evidence-corrected/` tree (layout in
> [`CORRECTIONS.md`](tempo-long-windows/CORRECTIONS.md)); the
> original raw/evidence (`5098df9`) is preserved unchanged as historical. Read this
> section before the original text below, which is retained with inline corrections.
> The substantive fixes: the within-2-bar losses are **post-ready estimator
> excursions**, not a "readiness cost" (§Regressions); a **missed regression** on
> `noise_126bpm_44100hz` is added; `lockedBpm`/`bpmRelativeError` are
> **backend-specific steady-window** medians, not whole-clip; not-acquired
> `acquisitionBars` is `null` (raw sentinel preserved); the gap-spanning interval
> exemption applies only to the `gap` control; and the combined-freeze pin was
> mis-attributed (`cd8291e6…` is the five-source combined hash recorded inside
> `docs/research/tempo-variant/provenance.json`, whose own file hash is
> `202714b1…`).

## What was run

- **Protocol** committed **before any render or tracker run**: `PROTOCOL.md` at commit
  `492c5a8` (generator `gen_long_windows.py` sha `9e76b108…`). Fixture identities
  committed before inference at `d21e2a6` (`fixtures/manifest.json`, sha
  `dd3d196f…`). Protocol declared matrix, seeds, pins, outcomes and comparison rules
  in advance; this report only reads them.
- **Matrix:** 16 fixtures = 2 tempos (96, 126 BPM) × 2 rates (44.1, 48 kHz) × 4
  controls (`regular`, `sparse` = strums on beats 1&3 of each bar, `gap` = 1 s
  event-free window at 16–17 s, `noise` = regular audio + white noise at 0 dB SNR).
  Lengths **33.850 s (96 BPM) / 33.731 s (126 BPM)**, all ≥ 32 s. All 16 WAVs
  re-hashed and re-verified byte-for-byte against the committed manifest; pairing
  (sparse/gap are strict onsets subsets, noise shares regular's onsets and beats)
  verified from the declared truth. No exact-zero audio anywhere.
- **Scoring:** current pinned EVAL-007 scorer, **block 128, uncompensated**, three
  backends: default BTrack, the fixed variant, aubio. Pinned diagnostic CLI supplies
  the beat series and the causal method log. All pins in
  `evidence/provenance.json`. **16/16 default/variant beat series are byte-identical.**

Read the retained evidence in `evidence/` (2.36 MiB): `outcomes.json`,
`comparison.json`, `intervals.json`, `method-summary.json`, `beat-equality.json`,
`raw/*/block128/results.json`, `diagnostic/*/beats/*.csv`, `raw-method/instance_0.csv.gz`.

## Measured results (scorer BPM is a backend-specific steady-window median; not release-gate rates)

`lockedBpm` is the median of phase-valid tempo samples inside a **backend-specific**
window (`Metrics.cpp` `steadyWindow`): `start = first truth beat + acquisitionSeconds`
if that backend acquired, else the grid midpoint; `end = duration`. The default and
variant windows **differ on 14 of 16 cells** (they acquired at different times), so
their BPM columns are not a common-window comparison. Corrected window bounds per
backend are in `evidence-corrected/outcomes.json`.

### Acquisition and BPM, per cell (D = default BTrack, V = variant, A = aubio)

| tempo | rate | control | D acq / bars / ≤2bar | V acq / bars / ≤2bar | D err % | V err % | A err % |
|---|---|---|---|---|---|---|---|
| 96 | 48k | regular | 1 / 0.75 / 1 | 1 / 1.75 / 1 | 0.309 | 0.309 | 1.02 |
| 96 | 48k | sparse | 1 / 1.00 / 1 | 1 / **2.00** / **0** | 0.309 | 0.309 | 1.16 |
| 96 | 48k | gap | 1 / 0.75 / 1 | 1 / 1.50 / 1 | 0.309 | 0.309 | 1.02 |
| 96 | 48k | noise | 1 / 0.77 / 1 | 1 / **2.01** / **0** | 0.309 | 0.309 | 1.59 |
| 96 | 44.1k | regular | 1 / 1.00 / 1 | 1 / 1.50 / 1 | 0.309 | 0.309 | 1.20 |
| 96 | 44.1k | sparse | 1 / 1.25 / 1 | 1 / **2.00** / **0** | 0.309 | 0.309 | 1.31 |
| 96 | 44.1k | gap | 1 / 1.00 / 1 | 1 / 1.50 / 1 | 0.309 | 0.309 | 1.20 |
| 96 | 44.1k | noise | 1 / 1.01 / 1 | 1 / 1.75 / 1 | 0.309 | 0.309 | 1.87 |
| 126 | 48k | regular | 1 / 0.49 / 1 | 1 / 0.49 / 1 | 2.344 | **0.038** | 1.38 |
| 126 | 48k | sparse | 1 / 1.01 / 1 | 1 / 1.50 / 1 | 0.038 | 0.038 | **49.61** |
| 126 | 48k | gap | 1 / 0.49 / 1 | 1 / 0.49 / 1 | 2.344 | **0.038** | 1.38 |
| 126 | 48k | noise | **0** / — / 0 | 1 / **2.50** / 0 | 2.344 | 0.038 | 2.11 |
| 126 | 44.1k | regular | 1 / 4.49 / 0 | 1 / 2.00 / 0 | 2.344 | **0.038** | 1.60 |
| 126 | 44.1k | sparse | 1 / 2.24 / 0 | 1 / 2.99 / 0 | 0.038 | 0.038 | **49.59** |
| 126 | 44.1k | gap | 1 / **4.49** / 0 | 1 / **0.49** / **1** | 2.344 | **0.038** | 1.63 |
| 126 | 44.1k | noise | 1 / 2.49 / 0 | 1 / 4.74 / 0 | 2.344 | 0.038 | 2.61 |

Aggregate: acquired D **15/16**, V **16/16**, A 16/16. Within 2 bars D **11/16**,
V **9/16**, A 13/16. Steady-window BPM error ≤ 2 % (each backend's own
`steadyWindow`, §above): D **10/16**, V **16/16**, A 12/16.

**Paired comparison (predeclared rules):** BPM **6 gains, 0 regressions, 10
no-change**. Acquisition **1 gain, 0 losses**. Within-2-bar **1 gain, 3 losses**.
In six 126 BPM cells, the variant changes the steady-window error from the
default's 2.34 % to 0.038 %; the two sparse 126 BPM cells already have 0.038 %
error with both backends. At 96 BPM both sit at 0.309 % so nothing moves. These BPM values are
backend-specific steady-window medians, and the default/variant windows differ on
14/16 cells, so they are not a common-window comparison.

### Control-vs-regular pairs (protocol §8 extension)

`evidence-corrected/control-pairs.json` adds **36 paired records** (12 perturbations
× 3 backends), each pairing a `sparse`/`gap`/`noise` cell with its own `regular` cell
at the same tempo and rate, under the same bands and null semantics. Measured:
acquisition **0 gains / 1 loss** (default BTrack on `noise_126bpm_48000hz`),
within-2-bar **1 gain / 8 losses**, BPM **28 no-change / 6 regressions / 2 gains**.
By control: **noise** → aubio regresses in all 4 cells, default and variant
no-change; **sparse** → aubio regresses in the 2 half-time cells, default
gains 2, variant no-change; **gap** → all 12 no-change. All BPM comparisons carry
the backend-specific-steady-window caveat above (the two windows differ), so these
are diagnostic control effects, not a common-window ranking.

### Corrected evidence layout

`docs/research/tempo-long-windows/evidence-corrected/` (derived-only, no raw or WAV
duplication; authenticated by `derived-hashes.txt` and independently checked by
`tools/tempo-characterization/tests/test_corrected_evidence.py`):

| file | content |
|---|---|
| `outcomes.json` | corrected per fixture × backend: null `acquisitionBars` (+ raw sentinel), flags, missing reason, backend-specific steady-window bounds |
| `comparison.json` | the 16 retained default-vs-variant records |
| `control-pairs.json` | the 36 control-vs-regular records |
| `intervals.json` | corrected intervals (gap exemption only for the `gap` control) |
| `validation.json` | `paired.validate_results` result and manifest truth checks |
| `errata.json` | E1–E7, immutable commits, verified pins, counts |
| `derived-hashes.txt` | sha256 of every file above |

### Regressions and what did *not* reproduce

1. **Within-2-bar acquisition losses (3):** `sparse` 96 BPM at both rates and `noise`
   96 BPM 48k, with exact raw margins `sparse96@48 = 2.000883` (+2.2 ms over 2 bars),
   `sparse96@44.1 = 2.000880` (+2.2 ms), `noise96@48 = 2.005525` (+13.8 ms). These
   are **not** a readiness cost. The variant forwards the base BPM unchanged until
   the first ready block, so the two backends report identical tempos until
   readiness; divergence begins only after it. Acquisition is granted by
   `findFirstLockFrom`, which needs a run of **4 consecutive** predicted beats each
   matching a truth beat within tolerance, advancing, and carrying a tempo sample
   within **2 %** of local truth BPM. The losses occur because the variant's
   **post-ready derived values are out of band for the first ready beats**:
   `sparse96@48` reports 103.36 (+7.67 %), 99.39 (+3.53 %), 99.39 (+3.53 %) before
   the first 4-in-band run. (On `regular_126bpm_48000hz` and `gap_126bpm_48000hz`
   the lock is found at 1.2887 s, *before* first-ready availability 2.2533 s, on the
   forwarded base BPM, so default and variant share `acquisitionBars = 0.492821875`
   exactly.) The steady-state window is nonetheless fine (BPM error 0.309 % at 96).
2. **Missed regression, `noise_126bpm_44100hz`** (added by review): the variant
   acquires at **4.7412 bars** vs the default's **2.4921 bars** — **+2.2491 bars**
   (about +4.3 s). The first five ready beats report 88.34 / 84.03 / 86.13 / 86.13 /
   102.34 BPM (−29.89 / −33.31 / −31.64 / −31.64 / −18.78 %; minimum 84.03, 126
   nominal): the strongest post-ready estimator excursion in the matrix. The
   corrected evidence makes this regression explicit; the first report did not.

3. **Aubio half-time lock on sparse 126 BPM** (2 cells): aubio reports **63.5 BPM,
   49.6 % error, `halfTimeLock=true`**. The variant and default do not. This is an
   aubio failure on the half-density pattern, not a variant regression, and is
   recorded so it is not mistaken for one.
4. **Not reproduced from TRACK-006 on longer windows:** the carved-gap **82.687 BPM**
   and 0 dB-noise **111.14 BPM** regressions do **not** appear here. The gap clips
   hold the beat grid (0.625 s/beat, 2.5 s/bar at 96 BPM; 0.47619 s/beat,
   1.904762 s/bar at 126 BPM) and the trackers keep emitting through the event-free
   window, so no emitted interval bridges the gap
   (`intervals.json`: `gapSpanning` empty for BTrack/variant on all gap cells), and
   the 0 dB noise clips still acquire. This is **evidence about these longer,
   regular-grid windows only** — it does not refute the 5 s derived-window results
   and does not excuse the short-window behaviour. The longer windows carry one
   strum per beat (regular grid), so they are easier to phase-lock than the 5 s
   slices; that is the honest reason, and it bounds the claim.

### Readiness, causality and intervals

Every clip has exactly **4 fallback beats** (base BPM forwarded) before the first
ready beat — this is a **structural method fact** (the ring needs 4 intervals / 5
events and no reset occurred here), not by itself evidence of a readiness cost;
first-ready availability is 2.25–3.21 s and the event clock is
strictly earlier than availability on every clip (causal). Between beats the last
candidate persists stale. **No variant interval exceeds the frozen 1.50 s bound on
any of the 16 clips** (`intervals.json`; max observed 0.743 s), so the frozen
interval policy is never stress-tested to its edge on this matrix. Sparse clips show
2-beat expected intervals and 52/68 single-beat outliers, as designed. Steady-window
BPM is stable even though individual ready beats can be > 2 % off nominal: across
the matrix **44 of 924 ready beats (4.8 %)**, worst clip 9/65 — a minority, not
"often", and the same median-robustness pattern seen in TRACK-005/006. Corrected
intervals (gap exemption only for the `gap` control): aubio `sparse_126bpm_48000hz`
has **29** outliers (29-long run), not the historical 28.

### Missingness and silence (unchanged semantics, nothing hidden)

`trueSilenceSpans` is empty on all 16 fixtures: the capture chain keeps a −66 dBFS
floor and a 0.42 s room tail, so **no exact-zero acoustic silence exists**. Every
scorer record is `NoTrueSilence`, `falseBeatMetricInformative=false`,
`silenceAccelerationMeasured=false`. The gap and noise cells are *structural*
event-free/noisy regions, so false-beat-in-silence and silence-acceleration are
**unassessed**, not zero. `noise` carries `noisy` (never `derived`+`noise`), so the
structural-noise exception is correctly not invoked.

**Structural vs true silence — field explainer.** The `gap`/`sparse` cells have
**nonzero, measured structural unplayed-beat windows** (`silenceSpans`,
`unplayedBeatWindowsSeconds`) that are real and reported, not ignored zeros: for
`sparse` these are the odd-index truth beats (26 at 96 BPM, 34 at 126 BPM) and for
`gap` the beats inside the removed 16–17 s window (1–2). These are *event-free*
regions with a live noise floor and room tail — they do **not** establish acoustic
silence and are kept distinct from `trueSilenceSpans`, which is empty, so
false-beat-in-silence and silence-acceleration are unassessed. Missing
measurements stay `null`, never 0 (corrected
`test_corrected_evidence.py` enforces the null; the historical raw sentinel 0 for
not-acquired `acquisitionBars` is preserved separately in `acquisitionBarsRaw`).

## Limits

Synthetic guitar-like material from a generator, not recorded or real-player
guitar. Regular one-strum-per-beat grid — easier to phase-lock than the sparse 5 s
windows in TRACK-006. Two tempos, two rates, one pattern family; no parameter sweep.
The 16 clips are overlapping views of two performances, **not independent trials**,
so these counts are diagnostic descriptions, not rates, passes, or a core
denominator. Same block-128 framing does not equalise backend internals (BTrack
resamples to 44.1 kHz); CPU is a resource field only. No Musical Clock, audition,
live input, or Windows/hardware path. No half/double classification change is claimed
for the variant (aubio's is reported as-is).

**The frozen gap reset is not exercised here.** The `gap` control removes onsets,
not backend beat events: on every gap cell the trackers keep emitting beats through
the event-free window (1–2 beats inside 16–17 s) and **no emitted interval bridges
the gap** (all interval states are `first_beat`/`accepted`, no reset). The realized
onset gap is 1.2383 s at 96 BPM and 1.4299 s at 126 BPM; the beat grid is
0.625 s/beat (2.5 s/bar) at 96 and 0.47619 s/beat (1.904762 s/bar) at 126. The
TRACK-006-style >1.5 s reset-triggering interval is therefore **not** produced on
this matrix — a deliberate consequence of the longer, regular-grid construction and
an explicit limit on what this run can say about the frozen interval policy.

## Next action (evidence-driven proposal, **not** implemented here)

The measured cost is **post-ready estimator stability**: the variant forwards base
BPM unchanged before readiness (so it cannot diverge early), and its regressions all
come from **derived values that are out of band immediately after readiness** — the
first ready beats on `sparse96` (+7.67 %, +3.53 %, +3.53 %), `noise96`, and most
severely `noise_126bpm_44100hz` (88.34/84.03/86.13/86.13/102.34 BPM, min 84.03),
plus the +2.2491-bar acquisition delay there. The next candidate contract, if
pursued as a **new named variant with a new freeze**, should target that
**post-ready estimator stability** (e.g. a guard that does not report a derived
tempo until the 4-window is itself in band), evaluated on this same preserved
longer-window matrix plus the regular-material gains — **without** relaxing the 2 %
band, changing the frozen interval window, or selecting a backend. A blind
"earlier readiness" change is explicitly *not* the proposal. This report does not
implement or authorise it; it is the evidence-driven next contract.

## Reproduction

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
python3 tools/tempo-characterization/gen_long_windows.py --out "$TMPDIR/TRACK-007/corpus"
python3 tools/tempo-characterization/run_long_windows.py --corpus "$TMPDIR/TRACK-007/corpus" --out "$TMPDIR/TRACK-007/evidence"
python3 tools/tempo-characterization/recompute_evidence.py   # corrected derived tree from immutable raw
python3 -m unittest discover -s tools/tempo-characterization/tests -p 'test_*.py'
```

The generator and runner **refuse to overwrite** an existing output directory and
fail closed on the checks it actually validates: generator/source pins, input bytes
and framing, default/variant byte-identity, per-fixture block counts,
fallback/readiness and beat equality. Executed here: 14 unit + 15 evidence tests **OK**, plus the
real 16×3 pinned run (exit 0). Audio stays in scratch; only manifests, hashes, logs
and results are committed.
