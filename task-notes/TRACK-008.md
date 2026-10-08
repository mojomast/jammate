# TRACK-008 — post-readiness tempo-stability candidate (diagnostic)

## Base, ownership, executed scope

Base `main` `677727c`; branch `wp/TRACK-008-tempo-stability`; explicit workdir
`/home/mojo/projects/worktrees/TRACK-008-tempo-stability` (session moved there).

Owned and touched: new `tools/tempo-stability/**`, new
`docs/research/TEMPO-STABILITY.md`, new `docs/research/tempo-stability/**`, this
note. **Not touched:** the old candidate (`tools/tempo-variant/**`), the tracker
adapters (`src/btrack/**`, `src/aubio/**`), the scorer/metrics
(`tools/rhythm-eval/**`), default plugins, corpus and raw historical evidence,
shared CMake/CI, `EXECUTION-LEDGER.md`, `HANDOFF.md`, `DEVPLAN.md`, processor,
editor, DrumEngine. No subagents, no production wiring.

Scratch: `/home/mojo/projects/build-TRACK-008-worker`;
`TMPDIR=/home/mojo/projects/guitars-build-resume/tmp/TRACK-008`;
`PATH=/tmp/opencode/venv/bin:$PATH`; compile sequential (≤2 jobs); project
scratch ~1 GiB.

## Order of work (protocol before inference)

1. **`0c1f972`** — preregistered comparison protocol
   `docs/research/tempo-stability/PROTOCOL.md`: candidate method and constants,
   fixed 40-fixture matrix (preserved short TRACK-006 derived-24 + long TRACK-007
   16), pins, truth/missingness, bands, measured outcomes, failure criteria and
   freeze discipline. Committed **before** the candidate existed and **before**
   any scorer run.
2. **`8a0c637`** — candidate + deterministic method tests + `FREEZE.json`
   (behaviour source + plugin binary + embedded EVAL-005 archive hashes),
   committed **before** any scorer run.
3. **Only then** the scorer/diagnostic run, then evidence/report/note.

The final handoff SHA is recorded in the closing commit (`git log -1` on the
branch); a commit cannot embed its own SHA. Protocol `0c1f972` and the behaviour
freeze `8a0c637` precede all inference and are preserved.

## Candidate (frozen method)

`btrack-tempo-stable` — a JUCE-free `IRhythmTracker` decorator over the
unmodified `jam::BTrackBackend` that changes only `bpmCandidate`. Estimator core
identical to the old fixed variant (`60 / median4(last 4 valid emitted
intervals)`, window `[0.25, 1.50] s`), plus a **confirmation gate**: emit the
derived value only after three consecutive full-ring updates agree within **2 %**
(the scorer's frozen `kBpmAgreementFraction`); otherwise forward base BPM; any
ring reset drops confirmation. No truth/identity lookup, no octave correction, no
sweep, bounded 4-element ring. Behaviour freeze combined
`8034e2d1fd8b6dc268084e24a75878fdfeda7888809e4386fbd18174660cbed3`; plugin
`791d36ecb4f2e9a7c8a4f045d13763d177d71c32257c174fb8f06fddffcff183`; embedded
archives `92659b88… / 6993f822… / 55c591cf… / fa209a29…`. The plugin's honest
source label is a BTrack backend plus the decorator; id `btrack-tempo-stable`;
log env `JAM_TEMPO_STABILITY_LOG_DIR`.

## Method validation

- `TempoStableTests`: **62 checks, 0 failures** (startup fallback; exact
  confirmation timing; post-ready jitter reset; outlier robustness;
  missing-vs-measured; duplicate/non-monotonic; gap; sub-minimum; non-causal;
  invalid frame; rate/sample-clock fallback; 0.25 s boundary; no octave
  correction; confirmation persistence until reset; bit-exact forwarding).
- `tests/test_stability_method.py`: **10 tests OK** (independent Python
  implementation + committed-header constant parse).
- Evaluation derivation cross-check against the preserved TRACK-007 corrected
  evidence: **624/624 fields identical**.
- `tests/test_stability_evidence.py`: **8 tests OK** (independent re-derivation
  of every outcome, comparison and control pair from raw scorer JSON + manifests;
  artifact and method-log re-hash; freeze integrity).

## Results (raw/derived)

Full tables: `docs/research/tempo-stability/tables.md`. Evidence ~**5.22 MiB**
(WAVs never in git; method logs losslessly gzipped, original byte hashes kept).

- **Matrix:** 40 fixtures (24 short + 16 long) × 4 backends = **160 scores**,
  block 128, uncompensated; canonical diagnostic beat series for all four.
- **Beat series:** **40/40** default = old variant = candidate **byte-identical**;
  aubio reported separately (no equality claim).
- **Long:** default 15 acquired / 11 within 2 bars; candidate **16 / 11**; old
  variant 16 / 9. Candidate vs default: **1 acquisition gain, 0 losses, 0
  within-2 losses, 6 BPM gains, 0 BPM regressions**. Candidate vs old variant:
  recovers the old variant's three within-2 losses (`sparse_96bpm_48000hz`,
  `noise_96bpm_48000hz`, `sparse_96bpm_44100hz`) and the
  `noise_126bpm_44100hz` **+2.249-bar** regression (candidate 2.492 bars =
  default); keeps the 6 BPM gains.
- **Short:** default 6 acquired / 6 within 2; candidate **6 / 6**; old variant
  1 / 1. Candidate vs default: **0 acquisition losses, 0 within-2 losses, 1 BPM
  gain, 0 BPM regressions**; the old variant's 6 acquisition losses and 4 BPM
  regressions (`noise_snr0db`, `silence_gap_1s` 34.38 %, funk-noise) are **absent**
  under the candidate. On 5 s clips the gate **usually** falls back, but it
  confirms on seven short fixtures (six with a nominal BPM) and delivers one
  derived BPM gain (`level_-40db` 2.344 %→0.036 %).
- **Post-ready stability (long):** candidate confirmed-beat out-of-band counts
  are **0/0 on all eight 96 BPM cells** (old variant up to 3, run 3) and **1–2
  with longest run 1–2 on the 126 BPM cells** (old variant 1–9, run up to 7).
- **Control pairs:** short candidate 0 acq-loss / 0 within-2-loss / 0 BPM
  regression (old variant 0 / 0 / 4); long candidate 0 / 1 / 0 (old variant
  0 / 4 / 0), the single long within-2 loss being the same one the default has.
- **Coverage/missingness:** unchanged across backends — short 19 `Measured` + 5
  `NotAssessedStructuralNoise`, long 16 `NoTrueSilence`; silence-acceleration
  measured on exactly one short clip; missing values stay empty/null, raw
  sentinels preserved.

## Regressions and scoped limits

- **Candidate vs default: none** by the frozen protocol flags on either matrix.
- **Cost:** first confirmation 3.69–7.50 s (7–14 fallback beats) on the long
  clips; on 5 s clips it **usually** falls back but confirms on seven short
  fixtures and delivers one derived BPM gain (`level_-40db`). Relative to the old
  variant, `gap_126bpm_44100hz` (2.004 vs 0.493 bars) and
  `regular_126bpm_44100hz` (2.754 vs 2.004) are later; `noise_126bpm_48000hz`
  still acquires only at 3.004 bars. Residual late jitter of 1–2 confirmed beats
  remains on five 126 BPM clips (longest run 2).
- **Aubio** carries its own long regressions (10) and the sparse-126 half-time
  lock (~49.6 % error): aubio findings, not candidate behaviour.
- **Scope:** synthetic guitar-like plucks (long) and EVAL-001-derived windows
  (short), not recorded guitar; two long performances reused across
  tempos/rates/controls (overlapping, not independent trials); 5 s short windows;
  backend-specific steady windows (bounds reported, not a common window); no
  true-silence rate or ramp/gap claim on the long matrix; no Musical
  Clock/audition/live/Windows; same framing does not equalise backend internals.
  Acquisition flags are per-clip, not a SPEC 19 release denominator.

## Process disclosure

Three **evaluation-tooling-only** fixes were made after the freeze and before the
retained run, none touching the candidate, bands or matrix: (a) a guard in
`run_stability.py` for the two short `tempo_step` fixtures that declare no
`nominalBpm` (interval diagnostic only); (b) a beat-equality bookkeeping bug where
the aubio digest leaked into the BTrack-family dict; (c) a **test-only** fix in
`tests/test_stability_evidence.py` that validated the concatenated method log with
a global block index instead of per-fixture windows. The retained evidence was
generated fresh after the fixes. `FREEZE.json` is the exact pre-run committed
artifact; its `evaluation` hashes describe the pre-fix tooling and are a
historical **pre-run snapshot**, **not** current-retained authentication. The
current evaluation-tooling hashes and commands are in
`docs/research/tempo-stability/tooling-hashes.json`; the correction contract is
`docs/research/tempo-stability/ERRATA.md`.

## Corrections and corpus availability (post-review)

- `docs/research/tempo-stability/ERRATA.md` (committed before recomputation)
  records E1–E5. The nominal-missing method-summary out-of-band fields are
  corrected to `null` + `...Measured=false` for the two short `tempo_step`
  fixtures in a separate `docs/research/tempo-stability/evidence-corrected/method-summary.json`
  recomputed from the preserved gzipped raw method logs
  (`tools/tempo-stability/recompute_method_summary.py`); the historical
  `evidence/**` and its `artifact-hashes.txt` are byte-preserved, no scorer is
  re-run, and no scored metric changes.
- Report/note wording for short clips was corrected from "pure fallback / cannot
  gain" to "**usually** fallback, confirming on seven short fixtures (six with a
  nominal BPM) with the one `level_-40db` gain".
- Corpus availability: `run_stability.py` **fails closed** if any expected WAV is
  missing or its hash/size/framing differs from the committed manifest.
  Re-rendering the long WAVs is an **explicit operator step** using the frozen
  TRACK-007 generator outside git; it is not implemented automatically here and
  no generator is changed.

## Commands

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

Runner **PASS** (160 outcomes, 120 comparisons, 136 control pairs, 40 exact
BTrack-family beat series); 62 C++ checks / 0 failures; 10 + 8 Python tests OK.

## Non-promotion and next action

**G3 stays OPEN. No backend is selected, no ADR, no guard, no gate and no default
is changed; nothing here promotes a candidate to production.** Any change to the
frozen method, or any production guard derived from it, requires a new
predeclared protocol and a **new freeze** before any new scoring. Orchestrator
owns shared registration/integration.
