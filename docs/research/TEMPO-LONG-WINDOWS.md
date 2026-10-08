# Longer-window synthetic characterization (TRACK-007)

Diagnostic follow-up to TRACK-005/TRACK-006. **The fixed variant, adapters, scorer,
metrics, old corpus and artifacts are unchanged. No backend is selected, no
parameter is tuned, no guard is implemented, no gate changes. G3 stays OPEN.**
This work characterizes the variant on **new, longer (~33.8 s) paired synthetic
guitar-like windows** and reports measured wins, regressions, and limits. It is
not a production selection.

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

## Measured results (whole-clip scorer measurements; not release-gate rates)

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
V **9/16**, A 13/16. Whole-clip BPM error ≤ 2 %: D **10/16**, V **16/16**, A 12/16.

**Paired comparison (predeclared rules):** BPM **6 gains, 0 regressions, 10
no-change**. Acquisition **1 gain, 0 losses**. Within-2-bar **1 gain, 3 losses**.
The variant converts every 126 BPM whole-clip error from the default's 2.34 % to
0.038 %; at 96 BPM both sit at 0.309 % so nothing moves.

### Regressions and what did *not* reproduce

1. **Within-2-bar acquisition losses (3):** `sparse` 96 BPM at both rates and `noise`
   96 BPM 48k. In each the default confirms at ~0.8–1.3 bars and the variant at
   ~2.0 bars. Cause: the variant's first in-band *derived* value cannot exist until
   readiness (5 emitted events). At 96 BPM two bars are only 5.0 s, so this startup
   cost plus one bar of confirmation lands at the boundary. These are startup-cost
   losses, not steady-state drift; whole-clip BPM is unaffected (0.309 %).
2. **Aubio half-time lock on sparse 126 BPM** (2 cells): aubio reports **63.5 BPM,
   49.6 % error, `halfTimeLock=true`**. The variant and default do not. This is an
   aubio failure on the half-density pattern, not a variant regression, and is
   recorded so it is not mistaken for one.
3. **Not reproduced from TRACK-006 on longer windows:** the carved-gap **82.687 BPM**
   and 0 dB-noise **111.14 BPM** regressions do **not** appear here. The 1 s gap
   clips sit on a 4 s beat grid where no emitted interval bridges the gap
   (`intervals.json`: `gapSpanning` empty for BTrack/variant on all gap cells), and
   the 0 dB noise clips still acquire. This is **evidence about these longer,
   regular-grid windows only** — it does not refute the 5 s derived-window results
   and does not excuse the short-window behaviour. The longer windows carry one
   strum per beat (regular grid), so they are easier to phase-lock than the 5 s
   slices; that is the honest reason, and it bounds the claim.

### Readiness, causality and intervals

Every clip has exactly **4 fallback beats** (base BPM forwarded) before the first
ready beat; **first-ready availability 2.25–3.21 s** and the event clock is
strictly earlier than availability on every clip (causal). Between beats the last
candidate persists stale. **No variant interval exceeds the frozen 1.50 s bound on
any of the 16 clips** (`intervals.json`; max observed 0.743 s), so the frozen
interval policy is never stress-tested to its edge on this matrix. Sparse clips show
2-beat expected intervals and 52/68 single-beat outliers, as designed. Whole-clip
BPM is stable even though individual ready beats are often > 2 % off nominal
(`readyBeatsOutOfBand`), the same median-robustness pattern seen in TRACK-005/006.

### Missingness and silence (unchanged semantics, nothing hidden)

`trueSilenceSpans` is empty on all 16 fixtures: the capture chain keeps a −66 dBFS
floor and a 0.42 s room tail, so **no exact-zero acoustic silence exists**. Every
scorer record is `NoTrueSilence`, `falseBeatMetricInformative=false`,
`silenceAccelerationMeasured=false`. The gap and noise cells are *structural*
event-free/noisy regions, so false-beat-in-silence and silence-acceleration are
**unassessed**, not zero. `noise` carries `noisy` (never `derived`+`noise`), so the
structural-noise exception is correctly not invoked. Missing measurements stay
`null`, never 0 (`test_long_windows_evidence.py` enforces this).

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

## Next action (evidence-driven proposal, **not** implemented here)

The measured cost is the variant's **readiness startup latency** (4 fallback beats;
2.25–3.21 s to first derived value), which alone explains all three within-2-bar
losses at the slower tempo, while steady-state whole-clip BPM is uniformly better.
The next candidate contract, if pursued as a **new named variant with a new freeze**,
should target that startup/availability cost (e.g. an earlier readiness rule or a
warm-start from existing accepted intervals), evaluated on this same preserved
longer-window matrix plus the regular-material gains — **without** relaxing the 2 %
band, changing the frozen interval window, or selecting a backend. This report does
not implement or authorise it; it is the evidence-driven next contract.

## Reproduction

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
python3 tools/tempo-characterization/gen_long_windows.py --out "$TMPDIR/TRACK-007/corpus"
python3 tools/tempo-characterization/run_long_windows.py --corpus "$TMPDIR/TRACK-007/corpus" --out "$TMPDIR/TRACK-007/evidence"
python3 -m unittest discover -s tools/tempo-characterization/tests -p 'test_*.py'
```

The generator and runner **refuse to overwrite** an existing output directory and
fail closed on pin, framing, byte-identity, pairing, block-count, reset, readiness and
beat-equality violations. Executed here: 14 unit + 15 evidence tests **OK**, plus the
real 16×3 pinned run (exit 0). Audio stays in scratch; only manifests, hashes, logs
and results are committed.