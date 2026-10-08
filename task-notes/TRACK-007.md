# TRACK-007 — longer-window acquisition/interval characterization

> **Historical banner.** The summary below (through "Limits and next action") records
> the first `5098df9` handoff. Its causal claim — that the within-2-bar losses are a
> "readiness startup cost" — is **superseded**: see the correction section at the end
> and `docs/research/tempo-long-windows/CORRECTIONS.md`. The losses are **post-ready
> derived-BPM excursions**; the variant forwards base BPM strictly before ready. The
> retained raw/evidence is unchanged.

## Base, ownership, executed scope

Base `main` `bcf540a`; branch `wp/TRACK-007-long-windows`; explicit workdir
`/home/mojo/projects/worktrees/TRACK-007-long-windows`; session moved here.

Owned and touched: new `tools/tempo-characterization/**`, new
`docs/research/TEMPO-LONG-WINDOWS.md`, new `docs/research/tempo-long-windows/**`,
this note. Fixed variant, adapters, metrics/scorer, old corpus and artifacts,
shared CMake, ledger, HANDOFF, DEVPLAN, processor, editor and DrumEngine are
unchanged. No build, no product build, no subagents. Only the pinned read-only
integration binaries were reused.

Read SPEC, TRACK-005/TRACK-006 notes and reports, `tools/tempo-variant/run_paired.py`,
`tools/tempo-variant/ClickTrain.cpp`, `tools/rhythm-eval/tools/run_robustness.py`,
`gen_fixtures.py` and the scorer metrics/coverage code. Synthesis reuses the
EVAL-001 `gen_fixtures.py` v1 code (pinned by hash); the strum pattern, sparse/gap
filters and additive SNR noise are new. These are synthetic guitar-like plucks
from the existing model — **not recorded guitar**, not a corpus member.

## Order of work (protocol before inference)

1. `492c5a8` — **predeclared protocol** `docs/research/tempo-long-windows/PROTOCOL.md`
   plus the deterministic generator, committed **before** any fixture render or any
   tracker run. Pins the seeds, 16-fixture matrix, lengths, ground-truth and silence
   construction, pairing, scoring pins, measured outcomes and exact comparison rules.
2. `d21e2a6` — generated fixture identities (`fixtures/manifest.json`), committed
   before inference.
3. Then the real run, then evidence/report/note.

## Results

- 16 fixtures (2 tempos 96/126 × 44.1/48 kHz × regular/sparse/gap/noise),
  33.850 s / 33.731 s, generated to scratch; all 16 WAVs re-verified against the
  committed manifest; pairing verified (sparse/gap strict onset subsets, noise
  shares regular's onsets and beats). No exact-zero audio anywhere.
- Executed **3 × 16** current EVAL-007 scores plus three diagnostic beat-series
  runs at **128 frames, uncompensated** on the pinned binaries. **16/16 exact
  byte-identical default/variant beat series**.
- **BPM: 6 gains, 0 regressions, 10 no-change.** Variant steady-window error ≤ 2 % on
  **16/16** (default 10/16, aubio 12/16); 126 BPM improves 2.344 % → 0.038 %.
  (Superseded wording: "whole-clip" — see banner; it is each backend's own
  steady window, which differs on 14/16 cells.)
- **Acquisition 1 gain / 0 losses; within-2-bar 1 gain / 3 losses** (sparse 96 both
  rates, noise 96 @48k). The losses are **post-ready derived-BPM excursions**, not a
  readiness startup cost (see banner and correction section).
- **Readiness/fallback chronology:** exactly 4 fallback beats on every clip,
  first-ready availability 2.25–3.21 s, event clock strictly earlier than
  availability everywhere. No variant interval exceeds the frozen 1.50 s bound
  (max 0.743 s); gap-spanning intervals reported separately from outliers.
- **TRACK-006's 82.687 BPM gap and 111.14 BPM noise regressions did not reproduce**
  on these longer regular-grid windows; honestly bounded (one strum per beat is
  easier to phase-lock than the 5 s derived slices).
- **aubio half-time lock on sparse 126 BPM** (63.5 BPM, 49.6 % error) reported as
  an aubio finding, not a variant regression.
- Missingness preserved: `trueSilenceSpans` empty everywhere, coverage
  `NoTrueSilence`, silence-acceleration/false-beat unassessed, nulls never zeroed.
- Evidence tree ~2.36 MiB (WAVs in scratch only).

## Validation and commands

Environment: `PATH=/tmp/opencode/venv/bin:$PATH`,
`TMPDIR=/home/mojo/projects/guitars-build-resume/tmp`.

```bash
python3 tools/tempo-characterization/gen_long_windows.py --out "$TMPDIR/TRACK-007/corpus"
python3 tools/tempo-characterization/run_long_windows.py --corpus "$TMPDIR/TRACK-007/corpus" --out docs/research/tempo-long-windows/evidence
python3 tools/tempo-characterization/tests/test_long_windows.py
python3 tools/tempo-characterization/tests/test_long_windows_evidence.py
python3 -m unittest discover -s tools/tempo-characterization/tests -p 'test_*.py'
```

Results: real run **PASS** (exit 0); 14 unit tests **OK**; 15 evidence tests
**OK**; **29** total **OK**. Tests cover pattern selection/pairing/determinism,
finite no-clip output, the interval and gap-span rules, readiness persistence,
missingness semantics, provenance pins, and authentication of every retained
artifact; the evidence tests re-derive the comparison claims independently.

## Limits and next action

Synthetic regular-grid windows, two tempos/rates, overlapping clips (not
independent trials), no recorded guitar, no true-silence measurement, no
Musical Clock/audition/live/Windows, same framing does not equalise backend
internals. G3 **OPEN**. No backend selected, no guard implemented, no gate or
default changed.

Next (superseded wording — see banner): the measured cost is **post-ready estimator
stability**, not readiness startup latency. The variant forwards base BPM strictly
before ready; its regressions come from derived values out of band immediately after
readiness (and the +2.2491-bar `noise_126bpm_44100hz` delay). The evidence-driven
candidate next contract is a **new named variant with a new freeze** targeting that
post-ready stability, evaluated on this same
preserved longer-window matrix plus the regular-material gains, without relaxing
the 2 % band or the frozen interval window and without selecting a backend. That
work is **not** started here and does not follow from this measurement as a
selection.

Handoff: implementation/evidence/report/note commit recorded as the final worker
commit (`git log -1`); a commit cannot embed its own SHA. Protocol `492c5a8` and
fixture identities `d21e2a6` are preserved separately and precede all inference.

## Correction after independent review (2026-10-08)

Independent review of `5098df9` required corrections before merge. The original raw
run and evidence tree (`5098df9`), the protocol (`492c5a8`), the fixture identities
(`d21e2a6`) and the generator are **preserved unchanged as historical**; the fix is
additive.

- Correction contract committed **before** recomputation: `4a89dc1`
  (`docs/research/tempo-long-windows/CORRECTIONS.md`).
- Corrected derived evidence (derived-only, 84 KiB, reuses the immutable raw):
  `docs/research/tempo-long-windows/evidence-corrected/`, produced by the new
  `tools/tempo-characterization/recompute_evidence.py`.
- Corrections applied: **E1** the within-2-bar losses are post-ready estimator
  excursions, not a readiness cost (verified: `variantBpm == baseBpm` on all pre-ready
  blocks; lock needs a 4-consecutive-beat run within 2 %); **E2** the missed
  `noise_126bpm_44100hz` regression (+2.2491 bars) added; **E3** not-acquired
  `acquisitionBars` normalised to `null` with the raw sentinel preserved; **E4**
  `lockedBpm`/`bpmRelativeError` relabelled as backend-specific steady-window medians
  (D/V windows differ on 14/16 cells); **E5** the gap exemption applies only to the
  `gap` control (aubio sparse 126 48k outliers 28 → 29); **E6** provenance.json's
  own file sha256 `202714b1…` vs the mis-attributed combined freeze `cd8291e6…`;
  **E7** disclosed process deviations.
- 36 control-vs-regular paired records (12 perturbations × 3 backends) added
  alongside the retained 16 default-vs-variant records.
- Hard validation now uses the reviewed `paired.validate_results` (block 128, no
  legacy stamps, uncompensated only) plus manifest-level truth checks; failure tests
  cover wrong framing, legacy stamps, wrong backend and non-uncompensated input.
- New tests: 19 corrected-evidence tests, independently re-deriving every corrected
  field from the immutable raw (not mirroring the tool). Full suite **48 tests OK**.
- The orchestrator independently replayed all 48 scores, 48 beat files and the
  method log at `/home/mojo/projects/build-TRACK-007-integration`; every scored field
  is byte-identical to the committed raw (only `cpuSeconds` and its aggregate differ).
- No backend/candidate/default/scorer/adapter/shared-build/ledger/HANDOFF/DEVPLAN
  change. G3 remains OPEN; the next contract is post-ready estimator stability as a
  new named candidate, not a blind earlier-readiness change.