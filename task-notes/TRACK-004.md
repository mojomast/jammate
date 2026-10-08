# TRACK-004 — real-adapter acquisition diagnosis (BTrack / aubio)

## Goal

Explain, from causal per-block evidence and without changing any gate, why both
real integrations fail SPEC 19's acquisition gate (BTrack 4/11, aubio 7/11) and
why BTrack's locked BPM is 123.05 against a 126 BPM truth (−2.34 %). Produce a
JUCE-free diagnostic executable, per-core-fixture reason tables, lock-run
start/confirmation timestamps, event-vs-availability acquisition, and a scoped
direct answer on the BPM quantisation-versus-resampler question.

This is a diagnostic. **G3 stays OPEN. No tracker is selected, no ADR, no gate
tuned, no live integration.**

## Base, branch, scope

- Base `main` `6287288` (`merge(EVAL-005): integrate corrected paired tracker
  robustness evidence`), worktree
  `/home/mojo/projects/worktrees/TRACK-004-acquisition`, branch
  `wp/TRACK-004-acquisition`.
- Owned and changed: **new** `tools/tracker-diagnostics/**`, **new**
  `docs/research/TRACKER-ACQUISITION.md`, **new**
  `docs/research/tracker-acquisition/**`, **new** `task-notes/TRACK-004.md`.
- **Not touched:** `src/`, `vendor/`/`third_party/`, any CMake file,
  `testdata/` (corpus), `tools/rhythm-eval/**` (existing harness/scoring),
  `Metrics.*`, the execution ledger and shared docs. No new dependency.
- No tracker source is compiled into the tool binary; the plugins are separate
  artifacts loaded at run time through the existing factory convention. This is
  a structural separation, **not** a legal/copyright conclusion. Reused
  unmodified evaluation objects: `Metrics.cpp`, `Manifest.cpp`,
  `BackendRunner.cpp`, `Json.h`.

## Files added

```
tools/tracker-diagnostics/TraceRunner.{h,cpp}       # per-block causal trace + toSeries reduce
tools/tracker-diagnostics/AcquisitionReplay.{h,cpp} # scorer-acquisition replay + reasons + clause tally
tools/tracker-diagnostics/BtrackGrid.{h,cpp}        # read-only BTrack tempo->lag arithmetic
tools/tracker-diagnostics/SyntheticClick.{h,cpp}    # pinned click-train generator
tools/tracker-diagnostics/CliValidate.h             # strict block/size validation
tools/tracker-diagnostics/ConfigPlugin.cpp          # diagnostic-only config shim (NOT default)
tools/tracker-diagnostics/main.cpp                  # CLI: corpus / click sweep / grid
tools/tracker-diagnostics/tests/TraceReplayTests.cpp
tools/tracker-diagnostics/build.sh
tools/tracker-diagnostics/run-corpus.sh
tools/tracker-diagnostics/make_reason_table.py
tools/tracker-diagnostics/verify_corpus_hashes.py
docs/research/TRACKER-ACQUISITION.md
docs/research/tracker-acquisition/**                # evidence tree (~1.75 MiB)
```

## Method

`TraceRunner` mirrors the unmodified `BackendRunner` block loop (asserted equal
in tests) and keeps each block's raw `RhythmObservation`; `toSeries()` reduces the
same blocks for scoring. `AcquisitionReplay` restates EVAL-002/004's
`findFirstLockFrom` (4 consecutive beats, 70 ms, forward-advancing, 2 % tempo
agreement) and is asserted to agree with `scoreFixture` on every real fixture;
the CLI exits non-zero on any disagreement. Event time and causal availability
are kept separate exactly as EVAL-004 defines.

Every optional measurement carries a measured flag; missing is serialised as an
empty CSV cell / JSON null, and a measured zero stays `0` (failed-lock
acquisition fields, BPM error with no nominal, ratio/phase with no match).

The program runs the original 19-fixture corpus for **both** backends at
**128** and **512** frame blocks, plus a synthetic-click BPM sweep at 48 kHz and
44.1 kHz, plus a bounded adapter-config experiment with the held-silence gate
disabled. All 19 WAV sha256 and byte sizes were re-verified before the run.

## Findings

1. **BTrack's acquisition failures are dominated by a BPM-report failure.** The
   failing clause is the 2 % tempo agreement. At 512 framing five core failures
   (arpeggio, clean_eighths, clean_sixteenths, missing_downbeats,
   palm_mute_metal) fail **only** on the numeric band (`0/N/0/0/0`); at 128
   framing four do, while `palm_mute_metal` mixes 9 numeric-band with 9
   phase-invalid tempo samples and is labelled `TempoAgreementFailure` rather
   than a numeric-only claim. Its beats sit at ≈126 BPM while the report is the
   quantised 123.046875.
2. **123.05 vs 126 is a report artefact, scoped.** The value is exactly
   `60·44100/(512·42) = 123.046875`; 126 is representable (`lag 41 =
   126.048018`). The tested scalar reports (last and median) are identical at
   48 kHz and 44.1 kHz (max |Δ| = 0.000000), so the device resampler is **not
   the source of the tested bias**; the 44.1 kHz path still applies a
   one-sample-delayed linear step. The internal "why lag 42 rather than the
   representable 41" is **unproven** (no vendor patch tested). Mean beat
   interval tracks the truth in the tested examples (ratio 1.0006), not a global
   unbiased-placement claim.
3. **aubio fails on no sustained positional match run plus two silence-gate
   cases.** `clean_sixteenths`, `palm_mute_metal`, `power_chords_distorted` have
   only a 2–3 beat forward-advancing run; the reason is precise generic wording
   (`NoSustainedMatchRun`), not a proven phase conflict. The bounded gate-off
   experiment flips aubio `palm_mute_metal` and `sustained_chords` to acquired
   (NOT default evidence). `aubio/sustained_chords` is the mirror mixed-magnitude
   case: median +1.3 % but 1 of 4 beats outside the 2 % band, so numeric band is
   the sole failing clause type.
4. **`InsufficientBeatEvents` and `octaveSuspect` never fire on core**, but these
   are not claims of "insufficient onset evidence" or that no transient octave
   exists — the octave test is on the median ratio. Recorded as unpopulated.
5. **Framing:** BTrack acquisition is framing-independent in count (4/11) but
   `sustained_chords` **fails to lock entirely at 512** (match run 2,
   `NoSustainedMatchRun`) rather than crossing the 2-bar line; aubio moves a few
   fixtures across the boundary. aubio's causal-availability mean does **not**
   grow with block size (5.7 ms both); BTrack's does (12.9 → 17.0 ms).
6. The diagnostic reproduces EVAL-004/EVAL-005 exactly (F 0.7099/0.5357, acq
   4/11 and 7/11, worst core BPM 0.0234/0.0133, 400/288 beats).

## Tests executed

```
tools/tracker-diagnostics/build.sh                                  # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-004/diag/TraceReplayTests           # 60 checks, 0 failures
```

`TraceReplayTests` independently pins the BTrack grid arithmetic, lock-run
start/confirmation and availability clocks, tempo-disagreement rejection with the
clause tally, backtracking rejection, agreement with `scoreFixture` plus
fabricated-mismatch detection, missing-vs-zero serialisation, a no-nominal
fixture, a zero-beat series, a phase-invalid series, strict block validation, and
`TraceRunner == BackendRunner` equality on a fake backend.

## Evidence runs

```
tools/tracker-diagnostics/run-corpus.sh
# artifact: docs/research/tracker-acquisition  (≈1.75 MiB, budget 5 MiB)
```

- Corpus: `testdata/rhythm`, 19/19 WAV hashes OK.
- Backends: `build-EVAL-005/main-core/librhythm-eval-{btrack,aubio}.so`, adapter
  sources byte-identical to base `6287288`.
- Block 128 (full trace) and 512 (beats) for both backends; click sweeps at
  48000/44100; silence-gate-off variant. `--state-max-rows 500` is a sampling
  target, not a hard bound (actual max 614 BTrack / 599 aubio, recorded in
  `summary.json`).

## Limitations

1. Synthetic corpus; SPEC 20 real-guitar tests still required.
2. BTrack's internal choice of lag 42 was bounded, not patched; the rate
   experiment compares the tested scalar reports, not full BPM trajectories.
3. `sustained_chords` carries a pending sustain-repair caveat; no fixture is
   labelled a synthesis defect here.
4. Config experiment varies only `silenceRmsDbfs`; labelled not-default.
5. No tracker selection, no ADR, no live wiring; G3 OPEN.

## Review corrections (round 2)

Requested at `51554f1` and applied here:

1. Missing values are no longer serialised as 0: measured flags + empty CSV /
   null JSON for failed locks, no-nominal BPM error, no-match ratio and no-match
   phase; tests added. `core-reasons.csv` line endings fixed to `\n`
   (`git diff --check` clean).
2. `LockTempoAgreementFailure` split into per-clause reasons
   (`TempoOutsideBand` / `TempoEvidenceMissing` / `TempoPhaseInvalid` /
   `TempoBpmInvalid` / mixed `TempoAgreementFailure`) with a per-beat clause
   tally; `PhaseConflictOrDropouts` renamed to the precise generic
   `NoSustainedMatchRun`.
3. Report contradictions corrected: BTrack `sustained_chords` 512 lock loss,
   aubio causal mean flat, `sparse_single_notes` (not `clean_eighths`) lock
   excerpt, `tapping_muting_only` synthesis-defect grouping removed,
   insufficient-beat vs insufficient-onset separated, octave wording scoped, the
   robustness "bimodal" value is not octave evidence.
4. Resampler claim scoped to the tested last/median scalar reports and the
   44.1 kHz no-device-conversion path; "why lag 42" left unproven.
5. Licence wording removed: no tracker source compiled into the tool; run-time
   loading is structural separation, not a copyright conclusion.
6. `--state-max-rows` documented as a sampling target with measured maxima;
   `--block` strictly validated (`0`, `> kMax`, malformed rejected).
7. Plugin instances are destroyed through the exported `jam_rhythm_destroy()`
   via a custom deleter in both corpus and click modes.
8. CLI exits non-zero on any replay/scorer disagreement; mismatch detection and
   the missing/rejection cases are unit-tested.

## Handoff

Analysis: `docs/research/TRACKER-ACQUISITION.md`. Evidence:
`docs/research/tracker-acquisition/`. Proposed narrow next repair contracts are
in §9 of the analysis; they are proposals, not changes.

## Final commit SHA

- Initial implementation + evidence: `e7b1037`
  (`diag-track-004: causal acquisition diagnosis for BTrack and aubio`).
- Review corrections (this revision): `__CORRECTION_SHA__`
  (`diag-track-004: address review — missing-value semantics, clause-level tempo
  evidence, scoped claims, validation`).
- The SHA update for this line is the subsequent commit on
  `wp/TRACK-004-acquisition`.
