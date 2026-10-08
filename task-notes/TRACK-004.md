# TRACK-004 — real-adapter acquisition diagnosis (BTrack / aubio)

## Goal

Explain, from causal per-block evidence and without changing any gate, why both
real integrations fail SPEC 19's acquisition gate (BTrack 4/11, aubio 7/11) and
why BTrack's locked BPM is 123.05 against a 126 BPM truth (−2.34 %). Produce a
JUCE-free diagnostic executable, per-core-fixture reason tables, lock-run
start/confirmation timestamps, event-vs-availability acquisition, and a direct
numeric answer on the BPM quantisation-versus-resampler question.

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
- The GPL backends are `dlopen`ed through the existing plugin convention, so the
  diagnostic binary is GPL-free. Reused unmodified evaluation objects:
  `Metrics.cpp`, `Manifest.cpp`, `BackendRunner.cpp`, `Json.h`.

## Files added

```
tools/tracker-diagnostics/TraceRunner.{h,cpp}       # per-block causal trace + toSeries reduce
tools/tracker-diagnostics/AcquisitionReplay.{h,cpp} # independent scorer-acquisition replay + reasons
tools/tracker-diagnostics/BtrackGrid.{h,cpp}        # read-only BTrack tempo->lag arithmetic
tools/tracker-diagnostics/SyntheticClick.{h,cpp}    # pinned click-train generator
tools/tracker-diagnostics/ConfigPlugin.cpp          # diagnostic-only config shim (NOT default)
tools/tracker-diagnostics/main.cpp                  # CLI: corpus / click sweep / grid
tools/tracker-diagnostics/tests/TraceReplayTests.cpp
tools/tracker-diagnostics/build.sh
tools/tracker-diagnostics/run-corpus.sh
tools/tracker-diagnostics/make_reason_table.py
tools/tracker-diagnostics/verify_corpus_hashes.py
docs/research/TRACKER-ACQUISITION.md
docs/research/tracker-acquisition/**                # evidence tree (~1.7 MiB)
```

## Method

`TraceRunner` mirrors the unmodified `BackendRunner` block loop (asserted equal
in tests) and keeps each block's raw `RhythmObservation`; `toSeries()` reduces the
same blocks for scoring. `AcquisitionReplay` restates EVAL-002/004's
`findFirstLockFrom` (4 consecutive beats, 70 ms, forward-advancing, 2 % tempo
agreement) and is asserted to agree with `scoreFixture` on every real fixture.
Event time and causal availability are kept separate exactly as EVAL-004 defines.

The program runs the original 19-fixture corpus for **both** backends at
**128** and **512** frame blocks, plus a synthetic-click BPM sweep at 48 kHz
(adapter resamples) and 44.1 kHz (adapter pass-through), plus a bounded
adapter-config experiment with the held-silence gate disabled. All 19 WAV sha256
and byte sizes were re-verified before the run.

## Findings

1. **BTrack's acquisition failures are mostly a BPM-report failure.** 5/7 core
   failures have 16–19 positionally-locked beats with `longestTempoRun = 0`; the
   only failing clause is the 2 % tempo agreement. Its beats are at ≈126 BPM
   (`clean_eighths` steady interval 0.47746 s ≈ 125.7 BPM) while
   `bpmCandidate` is the quantised 123.046875.
2. **123.05 vs 126 is quantised lag, not the device resampler.** The reported
   value is exactly `60·44100/(512·42) = 123.046875`; 126 is representable
   (`lag 41 = 126.048018`) but the estimator commits to 42 hops. A click sweep
   through the real adapter is **identical at 48 kHz and 44.1 kHz** (max
   |Δ| = 0.000000), so the 48 k→44.1 k resampler is excluded; the mean inter-beat
   interval tracks the true period (ratio 1.0006), so beat placement is fine.
   Confidence: high on resampler exclusion and grid values; medium on BTrack's
   internal lag *selection* (no vendor patch was tested).
3. **aubio's failures are phase conflicts plus two silence-gate cases.**
   `clean_sixteenths`, `palm_mute_metal`, `power_chords_distorted` fail on phase
   (tempo is within ~1.3 %). The bounded gate-off experiment flips aubio
   `palm_mute_metal` and `sustained_chords` to acquired (labelled NOT default
   evidence).
4. **`insufficient onset evidence` and `octave mismatch` are not the core cause**
   (never triggered on core); recorded as missing/unpopulated rather than forced.
5. **Framing:** BTrack acquisition is framing-independent (4/11 both); aubio
   crosses the 2-bar boundary on a few fixtures (e.g. `missing_downbeats`
   1.504 → 1.255 bars) and its causal availability grows with block size.
6. The diagnostic reproduces EVAL-004/EVAL-005 exactly (F 0.7099/0.5357, acq
   4/11 and 7/11, worst core BPM 0.0234/0.0133, 400/288 beats).

## Tests executed

```
tools/tracker-diagnostics/build.sh                                  # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-004/diag/TraceReplayTests           # 35 checks, 0 failures
```

`TraceReplayTests` independently pins the BTrack grid arithmetic, lock-run
start/confirmation and availability clocks, tempo-disagreement rejection,
backtracking rejection, agreement with `scoreFixture`, reason classification, and
`TraceRunner == BackendRunner` equality on a fake backend.

## Evidence runs

```
tools/tracker-diagnostics/run-corpus.sh
# artifact: docs/research/tracker-acquisition  (1 738 108 bytes, budget 5 MiB)
```

- Corpus: `testdata/rhythm`, 19/19 WAV hashes OK.
- Backends: `build-EVAL-005/main-core/librhythm-eval-{btrack,aubio}.so`, adapter
  sources byte-identical to base `6287288`.
- Block 128 (full trace) and 512 (beats) for both backends; click sweeps at
  48000/44100; silence-gate-off variant.

## Limitations

1. Synthetic corpus; SPEC 20 real-guitar tests still required.
2. BTrack's internal choice of lag 42 was bounded, not patched.
3. `sustained_chords` / `tapping_muting_only` carry the known synthesis caveat.
4. Config experiment varies only `silenceRmsDbfs`; labelled not-default.
5. No tracker selection, no ADR, no live wiring; G3 OPEN.

## Handoff

Analysis: `docs/research/TRACKER-ACQUISITION.md`. Evidence:
`docs/research/tracker-acquisition/`. Proposed narrow next repair contracts are
in §9 of the analysis; they are proposals, not changes.

## Final commit SHA

- Implementation + evidence commit: `__IMPLEMENTATION_SHA__`
  (`diag-track-004: causal acquisition diagnosis for BTrack and aubio`).
- This note's SHA update is the subsequent commit on `wp/TRACK-004-acquisition`.
