# TRACK-005 — BPM-report variant from emitted beat intervals (diagnostic)

## Goal

Measure, without changing the default adapter or any gate, whether a `bpmCandidate`
derived **only** from the beat intervals BTrack has already emitted is stable and
unbiased on the pinned click sweep and the corpus. This is the first narrow
next-repair contract proposed in `docs/research/TRACKER-ACQUISITION.md` §9.1
(adapter-evidence question, SPEC 9.2; evidence, not a command).

This is a diagnostic. **G3 stays OPEN. No tracker is selected, no ADR, no gate
tuned, no default changed, no production wiring.**

## Base, branch, scope

- Base `main` `bf61598` (`merge(TRACK-004): integrate causal acquisition diagnosis
  and fail-closed replay checks`), worktree
  `/home/mojo/projects/worktrees/TRACK-005-tempo-variant`, branch
  `wp/TRACK-005-tempo-variant`. EVAL-007 (silence coverage) is merged on `main`
  separately and is **not** rebased here; the primary gates/acquisition/BPM
  metrics are unaffected by it.
- Owned and changed: **new** `tools/tempo-variant/**`, **new**
  `docs/research/TEMPO-REPORT-VARIANT.md`, **new** `docs/research/tempo-variant/**`,
  **new** `task-notes/TRACK-005.md`.
- **Not touched:** `src/`, `vendor/`/`third_party/`, any CMake file, `testdata/`
  (corpus), `tools/rhythm-eval/**` (shared harness/scoring), `tools/tracker-diagnostics/**`
  (existing diagnostic — only `CliValidate.h` is included read-only), the
  TRACK-004 artifacts, the ledger and shared docs. No new dependency.

## Method (predeclared, frozen before the corpus run)

```
candidate = 60 / median( last 4 positive finite consecutive emitted beat-event
                         intervals );  5 events required
before readiness -> wrapped backend bpmCandidate forwarded unchanged
```

Only `bpmCandidate` changes; every other `RhythmObservation` field is forwarded
bit-exactly (no invented confidence). No truth, no future audio, no octave
correction, no grid snapping. Interval window `[0.25, 1.50] s` from the adapter's
declared `minBpm=40 / maxBpm=240`. A measured-but-invalid interval (gap above
max, sub-minimum) **resets**; a **missing** interval (first beat, duplicate /
non-monotonic — checked before the unsigned subtraction, non-causal,
invalid/overflowing frame) resets and is exported as `intervalMeasured=0` with an
**empty** cell; between beats the candidate is retained stale. Fixed 4-element
ring; the wrapper's own arithmetic is bounded with no allocation/locking/IO. The
wrapped backend runs on the analysis worker (may allocate) and the logger is an
offline diagnostic path only.

### Freeze and correction disclosure

Integrated pre-correction freeze (commit `bb30496`, method+params):
`7fccdd7f2dc32d9bbaebb8a5ac0db6f7cc989c0b386403b77adc97a8dac4ba0b`.
After the review corrections (bookkeeping/validation only, method unchanged) the
combined `sha256` is
`cd8291e6d8ad49cae64fe43768ddc88e70679a2835dad78e62b5644b0503ec77`.
Corrected variant plugin `libtempo-variant-btrack.so`:
`920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717`.
Both hashes and all pins are in `docs/research/tempo-variant/provenance.json`.

## Files added

```
tools/tempo-variant/TempoVariant.{h,cpp}        # the derived-BPM decorator
tools/tempo-variant/MethodLog.{h,cpp}           # per-block method CSV
tools/tempo-variant/TempoVariantPlugin.cpp      # dlopen plugin over pinned BTrack
tools/tempo-variant/ClickTrain.{h,cpp}          # constant + tempo-step click generator
tools/tempo-variant/main.cpp                    # click sweep / tempo-step CLI (strict CLI)
tools/tempo-variant/FixtureMetricsDump.cpp      # full per-fixture metric JSON
tools/tempo-variant/tests/TempoVariantTests.cpp
tools/tempo-variant/tests/test_click_lag.py
tools/tempo-variant/build.sh
tools/tempo-variant/run-corpus.sh
tools/tempo-variant/check_failclosed.sh
tools/tempo-variant/verify_hashes.py
tools/tempo-variant/extract_method_history.py
tools/tempo-variant/compare_runs.py
tools/tempo-variant/click_lag.py
docs/research/TEMPO-REPORT-VARIANT.md
docs/research/tempo-variant/**                  # evidence tree (522 KiB, budget 5 MiB)
```

## Method / source separation

The variant plugin composes the **unmodified** `jam::BTrackBackend` from the
pinned EVAL-005 main-core archives (BTrack plugin sha256 `41e6476e…`, identical to
TRACK-004's provenance). Corpus scoring uses the **unchanged** TRACK-004
integration diagnostic binary (`75b0d73b…`) with `--backend-lib` pointed at
either the default or the variant plugin. `FixtureMetricsDump` compiles the
unmodified `rhythmeval` sources; its aggregate reproduces the CLI `summary.json`
on every scored field (only resource `cpuSeconds` differs). The plugin `id()` is
`btrack-tempo-variant`, distinct from `btrack`. Method availability is exported
separately (per-beat `ready` / `intervalMeasured` / `intervalState` / causal
availability), because `bpmCandidate` carries no readiness flag.

## Results (raw)

- Click sweep (24 s, 118…134, 44.1 k / 48 k): requested 126 → default
  `123.046875`, variant `126.048019` (44.1 k) / `126.050423` (48 k). Variant max
  last-value relative error **≤ 0.94 %**; default up to 2.68 %.
- Tempo step 126 → 132 at nominal t = 12 s (generator anchor 12.004762 s):
  variant first within ±2 % on the **availability** clock at 13.865215 s,
  **lag 1.865215 s from nominal (~5 beats)** / 1.860454 s from anchor (4 beats);
  event-clock secondary 1.850703 s. Default never reaches 132 within ±2 %. Only
  the first hit is reported; no settling claim.
- Original 19, block 128: beat series **exactly equal**; SPEC 19 core within 2
  bars **4/11 → 6/11**, acquisition-anytime 8/19 → 13/19; worst core BPM error
  **0.0234 → 0.0077**; median steady **0.0212 → 0.0015**. Core gained
  `arpeggio`, `clean_sixteenths`, `missing_downbeats`; core within-2-bar lost
  `sparse_single_notes` (now late); all-19 anytime gained 6 / lost 1
  (`sustained_chords`); 6 fixtures acquired late.
- Repaired 19, block 128: exactly-equal beats; core within 2 bars **5/11 → 7/11**,
  anytime 8/19 → 14/19; same BPM improvement; no silence-acceleration change.
- Original, block 512 (variant run once): exactly-equal beats; core within 2
  bars **4/11 → 8/11**, anytime 7/19 → 14/19.
- F-measure, precision, recall, false-beats-in-silence and phase metrics are
  identical **on this corpus**; `halfDoubleTimeError` is BPM-derived and is not
  structurally invariant (no flips here).
- **Worse where reported raw:** original defective `sustained_chords` loses
  acquisition; variant derived values on ready beats are 66.256 / 51.423 /
  63.802 BPM (ratios 0.69 / 0.54 / 0.66 of the 96 BPM truth) versus base
  `95.703125` — a **sub-octave/low-tempo** lock, not an exact half-time; and
  `maxSilenceTempoIncreaseBpm` rises 0 → 44.28 on that fixture. The repaired
  corpus has neither regression.
- aubio is referenced from the stored verified TRACK-004 run (not re-run): 7/11
  core, worst core BPM error 0.0133.

Full tables and per-fixture event **and** causal-availability columns:
`docs/research/tempo-variant/compare/*.csv`, `method-history/*.csv`,
`click/*.csv`, `metrics/*.json`.

## Tests executed

```
tools/tempo-variant/check_failclosed.sh                # 22 checks, 0 failures
python3 tools/tempo-variant/tests/test_click_lag.py    # 6 tests, OK
tools/tempo-variant/build.sh                           # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-005/TempoVariantTests  # 67 checks, 0 failures
tools/tempo-variant/run-corpus.sh                      # all runs; artifact 522 KiB < 5 MiB
```

Before the corrections: 41 checks, 0 failures. After: 67 C++ checks + 6 python
checks + 22 fail-closed checks, all passing. The C++ tests pin the method
arithmetic, startup fallback, missing-beat/gap/reset semantics, missing-vs-wrapped
intervals (duplicate/backwards), frame-invalid and block-end-overflow rejection,
block-size-change history retention, invalid-rate fallback, silence retention,
half-time (no correction) / double-time (reset), bit-exact forwarding incl.
`transientDensity01`, and the generator step anchor. The fail-closed checks cover
`/dev/full` and the malformed-CLI matrix.

## Limitations

1. Synthetic corpus; SPEC 20 real-guitar tests still required.
2. One predeclared window (4 intervals), no parameter sweep.
3. Sub-octave locking on very sparse trains; no octave correction by design.
4. Silence-acceleration regression on the defective original `sustained_chords`.
5. Beat/phase metrics identical on this corpus only; `halfDoubleTimeError` is
   BPM-derived.
6. `cpuSeconds` differs between the metrics dumper and the trace CLI (resource
   only, never scored).
7. The optional EVAL-003 derived-24 paired robustness run was not executed.
8. No selection, no ADR, no gate/default change; G3 OPEN.

## Review corrections (round 2)

Applied on the reviewer's request, preserving the worktree and the 4-interval
method (no optimisation):

1. `click_lag.py`: primary response lag now on the **availability** clock and the
   variant hit is **readiness-gated**; event-time is an explicit secondary column;
   only the first hit is reported (the "settles in exactly 5 beats" claim is
   deleted); a trailing in-band flag is exported; the generator truth anchor /
   pre/post periods are exported (`_info.csv`, `_truth.csv`) and lag is reported
   from both nominal and anchor with an explicit lag-in-beats definition; new
   python unit tests.
2. `main.cpp`/`FixtureMetricsDump.cpp`: strict full-string validation before any
   I/O or library load; no infinite loop / stack overrun on `--block 0` or
   `> kMax`; bounded `rate*seconds`; null-factory checks; flush/close failures
   detected (e.g. `/dev/full`); `check_failclosed.sh` matrix.
3. `TempoVariant`: duplicate/out-of-order checked **before** the unsigned
   subtraction (no wrapped huge interval); `intervalMeasured` + empty cell for
   missing intervals; frame/block-end-overflow rejection; reset-rate fallback
   documented and tested; bit-exact forwarding test incl. `transientDensity01`;
   scope of the "bounded" claim narrowed to the wrapper; no callback logger on a
   real-time path.
4. `compare_runs.py`: measured flags + empty cells for `lockedBpm`/phase/silence
   acceleration; all four fixture sets validated identical/no-duplicates/exact
   count/complete; bpm-error non-negativity validated; acquisition reported as
   SPEC 19 core-within-2-bars **and** all-19 anytime, with late acquisitions
   distinguished; `halfDoubleTimeError` wording corrected.
5. `verify_hashes.py`: all 19 original + 19 repaired byte hashes verified from the
   exact CLI paths, **no fallback**, 18/18 refs asserted byte-identical.
6. Freeze disclosure: pre-correction and corrected hashes recorded; method
   unchanged.

## Proposed next contract (proposal only)

Separately scoped: characterise the sparse-train sub-octave lock and test a
predeclared half-time/low-tempo guard (new variant, new freeze) that removes the
`sustained_chords` regression without touching the regular-material gains or the
2 % gate. Must not be merged as default without an ADR.

## Handoff

Analysis: `docs/research/TEMPO-REPORT-VARIANT.md`. Evidence:
`docs/research/tempo-variant/`. Engine: `tools/tempo-variant/`.

## Final commit SHA

- First implementation + evidence: `bb30496`.
- Review corrections: this correction commit; the branch head SHA line is updated
  by the subsequent commit on `wp/TRACK-005-tempo-variant`.
