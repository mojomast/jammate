# TRACK-005 — BPM-report variant from emitted beat intervals (diagnostic)

## Goal

Measure, without changing the default adapter or any gate, whether a `bpmCandidate`
derived **only** from the beat intervals BTrack has already emitted is stable and
unbiased on the pinned click sweep and the corpus. This is the first narrow
next-repair contract proposed in
`docs/research/TRACKER-ACQUISITION.md` §9.1 (adapter-evidence question, SPEC 9.2;
evidence, not a command).

This is a diagnostic. **G3 stays OPEN. No tracker is selected, no ADR, no gate
tuned, no default changed, no production wiring.**

## Base, branch, scope

- Base `main` `bf61598` (`merge(TRACK-004): integrate causal acquisition diagnosis
  and fail-closed replay checks`), worktree
  `/home/mojo/projects/worktrees/TRACK-005-tempo-variant`, branch
  `wp/TRACK-005-tempo-variant`.
- Owned and changed: **new** `tools/tempo-variant/**`, **new**
  `docs/research/TEMPO-REPORT-VARIANT.md`, **new** `docs/research/tempo-variant/**`,
  **new** `task-notes/TRACK-005.md`.
- **Not touched:** `src/`, `vendor/`/`third_party/`, any CMake file, `testdata/`
  (corpus), `tools/rhythm-eval/**` (shared harness/scoring), `tools/tracker-diagnostics/**`
  (existing diagnostic), `docs/research/tracker-acquisition/**`, the ledger and
  shared docs. No new dependency.

## Method (predeclared, frozen before the corpus run)

```
candidate = 60 / median( last 4 positive finite consecutive emitted beat-event
                         intervals );  5 events required
before readiness -> wrapped backend bpmCandidate forwarded unchanged
```

Only `bpmCandidate` changes; every other `RhythmObservation` field is forwarded
unchanged (no invented confidence). No truth, no future audio, no octave
correction, no grid snapping. Interval window `[0.25, 1.50] s` comes from the
adapter's declared `minBpm=40 / maxBpm=240`; non-finite/`<=0`/non-monotonic/
sub-minimum/gap-above-max/non-causal intervals **reset** the ring; between beats
the candidate is retained stale. Fixed 4-element ring, no heap in `process()`.

Freeze `sha256` (algorithm + plugin source set):
`7fccdd7f2dc32d9bbaebb8a5ac0db6f7cc989c0b386403b77adc97a8dac4ba0b`.
Variant plugin `libtempo-variant-btrack.so`:
`19c4bc67484b7add47124ae87ed89b2bc107e234c2f5054a724eb497428865fe`.

## Files added

```
tools/tempo-variant/TempoVariant.{h,cpp}        # the derived-BPM decorator
tools/tempo-variant/MethodLog.{h,cpp}           # per-block method CSV
tools/tempo-variant/TempoVariantPlugin.cpp      # dlopen plugin over pinned BTrack
tools/tempo-variant/ClickTrain.{h,cpp}          # deterministic constant + step click
tools/tempo-variant/main.cpp                    # click sweep / tempo-step CLI
tools/tempo-variant/FixtureMetricsDump.cpp      # full per-fixture metric JSON
tools/tempo-variant/tests/TempoVariantTests.cpp
tools/tempo-variant/build.sh
tools/tempo-variant/run-corpus.sh
tools/tempo-variant/verify_hashes.py
tools/tempo-variant/extract_method_history.py
tools/tempo-variant/compare_runs.py
tools/tempo-variant/click_lag.py
docs/research/TEMPO-REPORT-VARIANT.md
docs/research/tempo-variant/**                  # evidence tree (514 KiB, budget 5 MiB)
```

## Method / source separation

The variant plugin composes the **unmodified** `jam::BTrackBackend` linked from
the pinned EVAL-005 main-core archives (the same archives whose BTrack plugin
sha256 `41e6476e…` matches TRACK-004's provenance). Corpus scoring uses the
**unchanged** TRACK-004 integration diagnostic binary (`75b0d73b…`) with
`--backend-lib` pointed at either the default or the variant plugin, so the
scoring maths is identical. `FixtureMetricsDump` compiles the unmodified
`rhythmeval` sources and its aggregate reproduces the CLI `summary.json` on every
scored field (only resource `cpuSeconds` differs). The variant plugin `id()` is
`btrack-tempo-variant`, distinct from the default `btrack`.

Method availability is exported separately (`MethodRecord` → per-beat CSV with
`ringCount`, `ready`, `intervalState`, `baseBpm`, `variantBpm`, causal
availability = block end), because `bpmCandidate` carries no readiness flag.

## Results (raw)

- Click sweep (24 s, 118…134, 44.1 k / 48 k): requested 126 → default
  `123.046875`, variant `126.048019` (44.1 k) / `126.050423` (48 k). Variant max
  last-value relative error **≤ 0.94 %**; default up to 2.68 %. The variant
  reproduces the quantised value where the grid lands on it (no blanket offset).
- Tempo step 126 → 132 at t = 12 s: variant first within ±2 % at 13.8507 s
  (**lag 1.85 s ≈ 5 beats**) at both rates, causally (no future); default never
  reaches 132 within ±2 %.
- Original 19, block 128: beat-event series **exactly equal** (sha256 every
  `beats/*.csv`); core acquisition **4/11 → 6/11**; worst core BPM error
  **0.0234 → 0.0077**; median steady BPM error **0.0212 → 0.0015**. Gained
  `arpeggio`, `clean_eighths`, `clean_sixteenths`, `line_input_low_level`,
  `missing_downbeats`, `noisy_microphone`; **lost** `sustained_chords`.
- Repaired 19, block 128: exactly-equal beat series; core acquisition
  **5/11 → 7/11**; same BPM improvement; no silence-acceleration change.
- Original, block 512 (variant run once): exactly-equal beats; core acquisition
  **4/11 → 8/11**.
- F-measure, precision, recall, half/double rate, false-beats-in-silence and
  phase metrics are unchanged (only bpm-dependent fields differ).
- **Worse where reported raw:** on the defective original `sustained_chords` the
  variant loses acquisition and reports a sub-octave 51–66 BPM (irregular sparse
  intervals; no octave correction), and `maxSilenceTempoIncreaseBpm` rises
  **0 → 44.28** on that fixture; the repaired corpus has no such regression.
- aubio is referenced from the stored verified TRACK-004 run (not re-run):
  7/11 core, worst core BPM error 0.0133.

Full tables and per-fixture start/confirm event **and** causal availability:
`docs/research/tempo-variant/compare/*.csv`, `method-history/*.csv`,
`click/*.csv`, `metrics/*.json`.

## Tests executed

```
tools/tempo-variant/build.sh                          # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-005/TempoVariantTests # 41 checks, 0 failures
tools/tempo-variant/run-corpus.sh                     # all runs; artifact 514 KiB < 5 MiB
```

Unit replay fixtures pin: steady 126 on a 123.046875 base → 126; startup fallback
for beats 0–3; single missing beat is in-window and the 4-median robust, two
missing beats move it; `> 1.5 s` gap resets; silence retains stale candidate;
half-time reported as-is, double-time resets; reset clears the ring;
non-monotonic and non-causal reset; all non-bpm fields forwarded unchanged.

## Limitations

1. Synthetic corpus; SPEC 20 real-guitar tests still required.
2. One predeclared window (4 intervals), no parameter sweep (no cherry-picking).
3. Sub-octave locking on very sparse trains; no octave correction by design.
4. Silence-acceleration regression on the defective original `sustained_chords`.
5. `cpuSeconds` differs between the metrics dumper and the trace CLI (resource
   only, never scored).
6. The optional EVAL-003 derived-24 paired robustness run was not executed.
7. No selection, no ADR, no gate/default change; G3 OPEN.

## Proposed next contract (proposal only)

Separately scoped: characterise the sparse-train sub-octave lock and test a
predeclared half-time guard (new variant, new freeze) that removes the
`sustained_chords` regression without touching the regular-material gains or the
2 % gate. Must not be merged as default without an ADR.

## Handoff

Analysis: `docs/research/TEMPO-REPORT-VARIANT.md`. Evidence:
`docs/research/tempo-variant/`. Engine: `tools/tempo-variant/`.

## Final commit SHA

- Implementation + evidence: this commit. The SHA line is updated by the
  subsequent commit on `wp/TRACK-005-tempo-variant`.
