# TRACK-008 — post-readiness tempo-stability candidate (diagnostic)

Base `main` `677727c`; branch `wp/TRACK-008-tempo-stability`; worktree
`/home/mojo/projects/worktrees/TRACK-008-tempo-stability`. Method, matrix, bands,
missingness and failure criteria are frozen in
`docs/research/tempo-stability/PROTOCOL.md`; the candidate source/binary freeze is
`docs/research/tempo-stability/FREEZE.json`. **G3 stays OPEN. No backend is
selected, no gate or default is changed, and nothing here promotes a candidate to
production.** The long/short audio is synthetic guitar-like plucks and derived
windows of the EVAL-001 corpus — not recorded guitar.

## 1. Question

The corrected TRACK-007 evidence showed the old fixed variant's losses were
**early post-readiness derived-BPM excursions**, not a readiness startup cost: the
variant forwards base BPM strictly before readiness, then switches to
`60/median(last 4 intervals)`, which at the first ready beats can sit outside the
scorer's 2 % agreement band while the base was closer (e.g. sparse 96 BPM first
ready beats 103.36 / 99.39 / 99.39 vs base 95.7031; noise 126 BPM @44.1 kHz first
ready beats 88.34 / 84.03 / 86.13). TRACK-008 asks whether a **new named,
bounded candidate** that gates the derived estimate behind a self-consistency
confirmation removes those excursions, and what else changes.

## 2. Candidate (frozen method)

`btrack-tempo-stable` is a JUCE-free `IRhythmTracker` decorator that composes the
unmodified `jam::BTrackBackend` and changes only `bpmCandidate`:

- estimator core **identical** to the old variant: `60 / median4(last 4 valid
  consecutive emitted intervals)`, interval window `[0.25, 1.50] s` from the
  adapter's own minBpm 40 / maxBpm 240;
- **confirmation gate**: the derived value is emitted only after three
  consecutive full-ring updates agree with the previous derived value within
  **2 %** (the scorer's frozen `kBpmAgreementFraction`, reused, not tuned);
- before confirmation the wrapped backend's own `bpmCandidate` is forwarded
  unchanged; between beats the last value is retained; **any ring reset**
  (malformed / gap / out-of-order / non-causal / invalid frame) drops confirmation
  and returns to fallback;
- no truth, no fixture identity, no future audio, no octave correction, no
  parameter sweep. Wrapper arithmetic is a fixed 4-element ring, bounded and
  allocation-free.

The plugin `id()` is `btrack-tempo-stable` and its log env is
`JAM_TEMPO_STABILITY_LOG_DIR`; the plugin statically embeds the pinned EVAL-005
main-core BTrack archives. Full pins and hashes are in `FREEZE.json` and the
evidence `provenance.json`.

## 3. Order of work (protocol before inference)

1. `0c1f972` — preregistered protocol committed **before** the candidate was
   implemented and before any scorer run.
2. `8a0c637` — candidate + deterministic method tests + `FREEZE.json`
   (source/binary/embedded-dependency freeze), committed **before** any scorer
   run.
3. Only then the scorer/diagnostic run. No result-driven change to the candidate,
   bands or matrix.

A tooling crash (a guard for the two `tempo_step` short fixtures that declare no
`nominalBpm`) and a beat-equality bookkeeping bug (the aubio digest leaked into
the BTrack-family dict) were fixed **in the evaluation tooling only** before the
retained run; neither touches the candidate, bands or matrix. The retained
evidence was generated fresh after those fixes. This is disclosed rather than
silent.

## 4. Method validation

- C++ method tests: **62 checks, 0 failures** (`TempoStableTests`) covering
  startup fallback, the exact confirmation timing (ring full at beat 5, confirmed
  at beat 8 on a steady train), post-ready jitter resetting the counter, single
  outlier robustness, missing vs measured intervals, duplicate/non-monotonic,
  gap, sub-minimum malformed, non-causal, invalid frame, rate/sample-clock,
  boundary 0.25 s, no octave correction, confirmation persistence until reset, and
  bit-exact forwarding of every other field.
- Independent Python spec tests: **10 tests OK** (second implementation of the
  method plus a parse of the committed header constants).
- The evaluation's outcome/steady-window derivation was cross-checked against the
  preserved TRACK-007 corrected evidence: **624/624 fields identical**.
- Evidence authentication/re-derivation tests: **8 tests OK**, re-deriving every
  outcome, comparison and control pair directly from the raw scorer JSON and the
  manifests, and re-hashing every artifact and method log.

## 5. Matrix, framing and pins

Preserved matrices only: the **24** short fixtures (TRACK-006 / EVAL-003
derived-24, 5 s) and the **16** long fixtures (TRACK-007, ~33.8 s), 40 fixtures ×
4 backends = **160 scores** at block **128, uncompensated**, plus canonical beat
series from the pinned diagnostic. Pinned EVAL-007 scorer `caba565f…`, default
BTrack `41e6476e…`, aubio `61336277…`, old variant `920a30f9…`, diagnostic
`75b0d73b…`; candidate `791d36ec…`; embedded archives `92659b88… / 6993f822… /
55c591cf… / fa209a29…`. `40/40` BTrack-family beat series (default = old variant =
candidate) are **byte-identical**; aubio is reported separately with no equality
claim.

## 6. Results

Full tables: `docs/research/tempo-stability/tables.md`. Missing values are empty
(`null`), never zero; raw sentinels (e.g. `acquisitionBars = 0` when not
acquired) are preserved in `*Raw` fields.

### 6.1 Acquisition and within-2-bar denominators

| corpus | backend | acquired | within 2 bars | has lock | has nominal |
|---|---|---|---|---|---|
| short (24) | btrack | 6 | 6 | 23 | 22 |
| short | old variant | 1 | 1 | 23 | 22 |
| short | **candidate** | **6** | **6** | 23 | 22 |
| short | aubio | 15 | 15 | 23 | 22 |
| long (16) | btrack | 15 | 11 | 16 | 16 |
| long | old variant | 16 | 9 | 16 | 16 |
| long | **candidate** | **16** | **11** | 16 | 16 |
| long | aubio | 16 | 13 | 16 | 16 |

Default-vs-backend flag counts (no pooled rate, no pass threshold):

| corpus | other | n | acq gain | acq loss | w2 gain | w2 loss | BPM gain | BPM regression |
|---|---|---|---|---|---|---|---|---|
| short | old variant | 24 | 1 | 6 | 1 | 6 | 9 | 4 |
| short | **candidate** | 24 | **0** | **0** | **0** | **0** | 1 | **0** |
| short | aubio | 24 | 13 | 4 | 13 | 4 | 17 | 4 |
| long | old variant | 16 | 1 | 0 | 1 | 3 | 6 | 0 |
| long | **candidate** | 16 | **1** | **0** | **0** | **0** | 6 | **0** |
| long | aubio | 16 | 1 | 0 | 4 | 2 | 4 | 10 |

### 6.2 The candidate removes the old variant's regressions

- **Short (TRACK-006 regressions removed).** The old variant loses acquisition on
  six short clips (`noise_snr0db`, `clip_0.25`, `clip_0.125`,
  `syncopated_funk__baseline`, `syncopated_funk__noise_snr10db`,
  `syncopated_funk__noise_snr0db`) and has four BPM regressions
  (`noise_snr0db` 0.04 %→11.79 %, `silence_gap_1s` 2.34 %→34.38 %,
  `syncopated_funk__noise_snr0db/10db` 1.82 %→2.86 %). The candidate has **no
  acquisition loss and no BPM regression** on any short clip: it matches the
  default acquisition/BPM on 20/24 and *gains* one BPM (`level_-40db`
  2.34 %→0.036 %). On these 5 s windows the gate **usually** forwards base, but it
  is **not** pure fallback: it confirms on **seven** short fixtures (six with a
  nominal BPM; `tempo_step_0.85` also confirms but declares no nominal — see
  `ERRATA.md`). That confirmation is why the `level_-40db` gain is real and why
  nothing regresses.
- **Long within-2-bar losses recovered.** The old variant loses within-2 on
  `sparse_96bpm_48000hz` (2.001 bars), `noise_96bpm_48000hz` (2.006) and
  `sparse_96bpm_44100hz` (2.001). The candidate acquires at 1.002 / 0.775 / 1.253
  bars — identical to the default — recovering all three.
- **E2 regression removed.** `noise_126bpm_44100hz`: old variant 4.741 bars,
  default 2.492, candidate **2.492** (the +2.249-bar regression is gone).
- **BPM gains retained.** The candidate has the same 6 long BPM gains as the old
  variant (all 126 BPM clips: 2.34 %→0.038 %), with **0 regressions**.
- **Long acquisition gain.** `noise_126bpm_48000hz`: default never acquires;
  candidate acquires at 3.004 bars (old variant 2.504). This is a gain over
  default but still outside 2 bars.

### 6.3 Post-ready stability (candidate vs old variant, long)

Candidate confirmed beats out of band (±2 % of nominal) and longest run, versus
the old variant's ready beats: 96 BPM candidate **0/0 on all eight** 96 BPM cells
(old variant up to 3 out of band, longest run 3); 126 BPM candidate 1–2 out of
band with longest run 1–2 (old variant 1–9, longest run up to 7, e.g.
`sparse_126bpm_44100hz` 9, `noise_126bpm_44100hz` 7). The early excursions are
removed; a residual late-confirmation jitter of 1–2 beats remains on five 126 BPM
clips.

### 6.4 Steady windows (backend-specific, not whole-clip)

`lockedBpm` / `bpmRelativeError` are the median of phase-valid tempo samples in
each backend's own window (`start = first truth beat + acquisitionSeconds` when
acquired, else the grid midpoint; `end = duration`). The per-backend bounds are in
`tables.md` §2; the windows differ between default and the derived backends
because they acquire at different times, so these are **not** a common-window
comparison. Representative: `noise_126bpm_48000hz` default never acquires, so its
window starts at 16.302 s, while the candidate's starts at 6.072 s.

### 6.5 Perturbation control pairs

| corpus | backend | pairs | acq loss | within-2 loss | BPM regression | BPM gain |
|---|---|---|---|---|---|---|
| short | btrack | 22 | 0 | 0 | 0 | 4 |
| short | old variant | 22 | 0 | 0 | 4 | 11 |
| short | **candidate** | 22 | **0** | **0** | **0** | 5 |
| short | aubio | 22 | 6 | 6 | 1 | 0 |
| long | btrack | 12 | 1 | 1 | 0 | 2 |
| long | old variant | 12 | 0 | 4 | 0 | 0 |
| long | **candidate** | 12 | **0** | **1** | **0** | 0 |
| long | aubio | 12 | 0 | 3 | 6 | 0 |

The candidate's single long control-pair within-2 loss is `noise_126bpm_48000hz`
(regular within 2 bars, noise not) — the same loss the default has, not a
regression against default.

### 6.6 Coverage, silence and missingness (scorer range retained)

The decorator never touches silence/coverage fields, and the counts are unchanged
across all four backends: short **19 `Measured` + 5
`NotAssessedStructuralNoise`** (derived noise), long **16 `NoTrueSilence`**.
Silence-acceleration is `Measured` on exactly one short clip (the BTrack-family
clips with genuine true silence); it is otherwise absent/insufficient and reported
as missing, never zero. There is no true-silence measurement and no
false-beat-in-silence rate on the long matrix.

## 7. Regressions, losses and downstream useful-lock limits

- **Candidate vs default:** by the frozen protocol flags, **0 acquisition losses,
  0 within-2-bar losses, 0 BPM regressions** on both matrices; 1 long acquisition
  gain and 6 long + 1 short BPM gains.
- **Coverage gap and latency cost.** The confirmation gate means the derived
  estimate is used only after 3 agreeing updates (≥ 8 beat events). First
  confirmation on the long clips is **3.69–7.50 s** (7–14 fallback beats). On the
  5 s short clips the gate **usually** falls back, but it does confirm on seven
  short fixtures and delivers one derived BPM gain (`level_-40db`
  2.344 %→0.036 %); elsewhere on short clips it deliberately behaves as the base
  backend.
- **Relative to the old variant**, two long 126 BPM @44.1 kHz cells are later:
  `gap_126bpm_44100hz` 2.004 bars (old variant 0.493, *within* 2 bars; default
  4.491) and `regular_126bpm_44100hz` 2.754 bars (old variant 2.004). Both remain
  outside 2 bars, and interim gates require more than 3 agreeing updates; this is
  the cost of stability. `noise_126bpm_48000hz` still acquires only at 3.004 bars.
- **Residual late jitter.** 1–2 confirmed beats remain out of the 2 % band on five
  126 BPM long clips (longest run 2); no 96 BPM cell has any.
- **Aubio** has its own long regressions (10) and the half-time sparse-126 lock
  (≈49.6 % error); these are aubio findings, not candidate behaviour.
- **Limits.** Synthetic matrices, two long performances reused across
  tempos/rates/controls (overlapping, not independent trials), 5 s short windows,
  backend-specific steady windows, no true-silence or ramp-gap claim, no live /
  clock / Windows evidence, and same framing does not equalise backend internals.
  Acquisition flags are per-clip, not a SPEC 19 release denominator.

## 8. Commands

```bash
PATH=/tmp/opencode/venv/bin:$PATH
TMPDIR=/home/mojo/projects/guitars-build-resume/tmp/TRACK-008
bash tools/tempo-stability/build.sh
/home/mojo/projects/build-TRACK-008-worker/TempoStableTests
python3 tools/tempo-stability/tests/test_stability_method.py
python3 tools/tempo-stability/freeze.py            # committed before scoring
python3 tools/tempo-stability/run_stability.py
python3 tools/tempo-stability/summarize.py
python3 tools/tempo-stability/recompute_method_summary.py   # correction only; no re-score
python3 tools/tempo-stability/tests/test_stability_evidence.py
```

Results: build clean; **62 C++ checks / 0 failures**; **10** Python spec tests
OK; runner **PASS** (160 outcomes, 120 comparisons, 136 control pairs, 40 exact
BTrack-family beat series); **8** evidence tests OK. Evidence tree **5.22 MiB**
(no WAVs in git; method logs losslessly gzipped with deterministic headers,
original byte hashes retained).

**Tooling authentication and errata.** `FREEZE.json`'s `evaluation` hashes are a
historical **pre-run snapshot** of the evaluation tooling, not an authentication
of the current retained tooling; the behaviour freeze (`sources` + `binary` +
embedded dependencies) is unchanged and enforced. The current evaluation-tooling
hashes and commands are in `docs/research/tempo-stability/tooling-hashes.json` and
are authenticated by the evidence tests. The correction contract and the
nominal-missing method-summary correction (with the historical `evidence/**`
preserved byte-for-byte) are in `docs/research/tempo-stability/ERRATA.md` and
`docs/research/tempo-stability/evidence-corrected/`.

## 9. Conclusion and non-promotion

On these synthetic matrices the confirmation-gated median removes the measured
early post-readiness derived-BPM instability of the old fixed variant: it matches
the default on every short clip (removing the old variant's 6 acquisition losses
and 4 BPM regressions), matches the default's long acquisition/within-2 counts
while recovering the old variant's 3 within-2-bar losses and the
`noise_126bpm_44100hz` regression, and keeps the 126 BPM BPM gains. It buys this
with a confirmation latency of 7–14 beats; on 5 s clips it usually falls back
(confirming on seven short fixtures, with the single `level_-40db` derived BPM
gain).

This is evidence on synthetic material only. **No backend is selected, no ADR, no
guard, no gate and no default is changed; G3 remains OPEN.** Any production use,
further parameter, or change to the frozen method requires a new protocol and a
new freeze.
