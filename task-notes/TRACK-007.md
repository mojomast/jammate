# TRACK-007 — longer-window acquisition/interval characterization

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
- **BPM: 6 gains, 0 regressions, 10 no-change.** Variant whole-clip error ≤ 2 % on
  **16/16** (default 10/16, aubio 12/16); 126 BPM improves 2.344 % → 0.038 %.
- **Acquisition 1 gain / 0 losses; within-2-bar 1 gain / 3 losses** (sparse 96 both
  rates, noise 96 @48k). Losses are the variant's readiness startup cost, not
  steady-state drift.
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

Next: the measured cost is readiness startup latency, which alone explains all
three within-2-bar losses while steady-state BPM is uniformly better. The
evidence-driven candidate next contract is a **new named variant with a new
freeze** targeting that startup/availability cost, evaluated on this same
preserved longer-window matrix plus the regular-material gains, without relaxing
the 2 % band or the frozen interval window and without selecting a backend. That
work is **not** started here and does not follow from this measurement as a
selection.

Handoff: implementation/evidence/report/note commit recorded as the final worker
commit (`git log -1`); a commit cannot embed its own SHA. Protocol `492c5a8` and
fixture identities `d21e2a6` are preserved separately and precede all inference.