# EVAL-004 — timing-corrected tracker comparison

## Goal

Correct the evidence defects found after EVAL-002R, then produce a fair, real
aubio-vs-BTrack run over the identical 19-fixture corpus. Concretely: (1) stop
`BackendRunner` overwriting a backend's reported beat device time with the block
start, and separate event time from causal availability; (2) stop claiming
SPEC 19's "silence does not create false acceleration" gate from a count of beats
in silence, and mark the unsupported gates accurately; (3) measure stop/start
recovery from the real stop, not unplayed-beat windows; (4) replace the arbitrary
`>= 50 %` silence censoring and the "no lock = pass" aggregation with honest
coverage/missing-evidence semantics; (5) add the aubio plugin and run both real
adapters at two framing sizes, measuring the discarded-timestamp defect.

## Base and scope

- Base commit: `eac59ba` (`build(jam-core): wire optional aubio adapter and
  isolated backend suite`). Worktree `/home/mojo/projects/worktrees/EVAL-004-timing`,
  branch `wp/EVAL-004-timing`. Other workers own robustness tools/new tests, CI,
  `vendor/`, adapters and CMake; none of those was touched.
- No CMake file was modified. The new test is picked up by jam-core's existing
  `CONFIGURE_DEPENDS` glob; the standalone CLI target already existed.
- No change to `src/` frozen types; the new timing fields live in the
  evaluation-only `ObservationSeries`/`TempoSample`.

## Files changed / added (all inside the allowed set)

Modified:

```
tools/rhythm-eval/BackendRunner.h
tools/rhythm-eval/BackendRunner.cpp
tools/rhythm-eval/Metrics.h
tools/rhythm-eval/Metrics.cpp
tools/rhythm-eval/main.cpp
tests/jam/RhythmEvalMetricsTests.cpp
```

Added:

```
tools/rhythm-eval/AubioPlugin.cpp                 # dlopen shim around AubioBackend
tools/rhythm-eval/build-aubio-plugin.sh           # mirrors build-btrack-plugin.sh
tests/jam/BackendRunnerTests.cpp                  # suite BackendRunner
docs/research/tracker-comparison/**               # new evidence tree
task-notes/EVAL-004.md                            # this note
```

`docs/research/results-btrack.json`, `summary-btrack.md`, `fixtures-btrack.csv`
and `docs/research/per-fixture/` are EVAL-002R's historical record and were left
byte-identical.

## 1. Backend timestamps and causal availability

`BackendRunner` introduced `BlockObservation` (observation + block start + block
end) and a `toSeries(blocks, duration, rate, legacyStamp)` reduction:

- For a `beatEvent`, the backend's `inputSampleTime` is preserved as the reported
  device time when it is causal (`<= block end`). A non-causal report is rejected
  and falls back to the block start; **zero is not treated as "unset"**, so a beat
  at device sample 0 is valid. The declared `sourceSampleRate` is checked against
  the fed rate (`rateMismatchBlocks`); the fed clock is authoritative.
- Every beat and tempo sample carries its **causal availability** (the block end)
  as a separate series. `toSeries` never invents availability; hand-built input
  has none.
- Diagnostics report blocks, partial final block, beat events reported vs
  at-block-start vs non-causal, rate-mismatch blocks, and mean/max reported
  availability latency (block end minus event).

A new `--legacy-block-stamped-beats` CLI flag reproduces the pre-EVAL-004 defect
(discard every reported time, stamp the block start) so its effect is *measured*,
not asserted. It is diagnostic-only and must never be used for gate decisions.

## 2. SPEC 19 silence/ramp/syncopation gates

- The true-silence false-beat counter is kept as a **raw diagnostic** (all
  existing counters preserved). SPEC 19 forbids false *acceleration*, and a
  tracker that holds its grid through a short intentional holdover is doing what
  a holdover is for; counting held-grid beats as a gate failure was wrong. A new
  silence-acceleration diagnostic reports the reported-BPM change across each
  true-silence span, distinguishing "measured" from "insufficient evidence". The
  SPEC gate is **NOT-MEASURED** (the offline harness cannot attribute a tempo
  change to the silence rather than to legitimate follow).
- The Follow-ramp gate is **NOT-MEASURED**: the SPEC rule is "without abrupt
  audible discontinuities", which needs the Musical Clock and audition. The old
  `<= 2 %` backend-error test was an arbitrary reuse of the BPM threshold; the
  raw ramp error is preserved.
- The syncopation gate is **NOT-MEASURED**: a generic `syncopated_funk` fixture
  contains many syncopated events and cannot isolate ONE, so the raw max step is
  preserved but no threshold is claimed.

## 3. Recovery from the real stop

`scoreFixture` now derives `silenceEnd` from `trueSilenceSpans` (the real stop),
not `silenceSpans` (+/-30 ms unplayed-beat windows), then takes the first onset at
or after it. A pre-stop lock can no longer be reported as recovery. Pinned by
`RhythmEvalMetrics.recoveryUsesTrueSilenceStopNotUnplayedWindows`.

## 4. Coverage and false passes

- `FalseBeatCoverage` distinguishes `Measured`, `NoTrueSilence` (NOT MEASURED,
  not a zero-rate pass) and `CorpusDefect` (the two fixtures whose declared true
  silence is a two-stage-fast-decay synthesis artifact: `sustained_chords`,
  `tapping_muting_only`). The arbitrary `>= 50 %` censoring is gone; the genuine
  48.55 %-silent `sparse_single_notes` stays measured.
- `phaseMeasured` is false when no predicted beat matched; the zero phase fields
  are then shown as `n/a`/missing in the summary and JSON, not as perfect phase.
- `bpmCoreAllLocked` requires every steady core fixture to have a BPM lock; a
  partial set marks the 2 % gate NOT-MEASURED instead of passing on the residual.
- Detection/ramp means are taken over measured fixtures only.

## Evidence runs

Build (PATH=`/tmp/opencode/venv/bin:$PATH`, GCC 14, Release):

```bash
cmake -S jam-core -B /tmp/opencode/eval004-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DJAM_ENABLE_BTRACK=ON -DJAM_ENABLE_AUBIO=ON
cmake --build /tmp/opencode/eval004-core
ctest --test-dir /tmp/opencode/eval004-core --output-on-failure          # 9/9
cmake -S jam-core -B /tmp/opencode/eval004-core-off -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/opencode/eval004-core-off
ctest --test-dir /tmp/opencode/eval004-core-off --output-on-failure      # 7/7
cmake -S tools/rhythm-eval -B /tmp/opencode/eval004-cli -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-rdynamic
cmake --build /tmp/opencode/eval004-cli
tools/rhythm-eval/build-btrack-plugin.sh /tmp/opencode/eval004-core
tools/rhythm-eval/build-aubio-plugin.sh  /tmp/opencode/eval004-core
```

Six corpus runs (both adapters: 128-frame primary with documented compensation
offered, 512-frame framing diagnostic uncompensated, and a legacy block-stamped
defect reproduction) produced `docs/research/tracker-comparison/{btrack,aubio}/`.
Full commands, semantics and caveats are in `comparison.md`.

### Measured summary (uncompensated, 128-frame)

| metric | BTrack | aubio |
|---|---|---|
| F mean / precision / recall | 0.7099 / 0.7039 / 0.7235 | 0.5357 / 0.6547 / 0.4725 |
| acquisition within 2 bars, core | 4/11 = 0.3636 | 7/11 = 0.6364 |
| BPM rel. error worst core | 0.0234 (FAIL) | 0.0133 (PASS) |
| half/double core errors | 0/11 (PASS) | 0/11 (PASS) |
| mean signed / abs / p95 phase | +2.64 / 10.87 / 22.01 ms | -3.07 / 14.55 / 26.43 ms |
| reported / block-start / non-causal beats | 400 / 0 / 0 | 288 / 0 / 0 |
| causal availability mean / worst | 12.88 / 14.25 ms | 5.73 / 10.62 ms |
| true-silence worst measured | 1.366/s (`sparse_single_notes`) | 1.366/s (`sparse_single_notes`) |
| CPU / C++ new-delete allocations | 1.66 s / 38 761 | 0.46 s / 76 |

**Discarded-timestamp defect effect.** Reproducing the old block-stamping on
BTrack gives mean signed/abs/p95 phase +12.38 / 16.15 / 28.16 ms — exactly the
EVAL-002R published numbers. With the reported hop timestamps preserved, BTrack
is +2.64 / 10.87 / 22.01 ms: the ~12 ms "BTrack latency" in that record was a
runner artifact, absolute phase error drops 33 %, and the documented 11.61 ms
compensation now overshoots (to -7.79 ms). aubio's defect effect is ~1.4 ms
signed because its sub-hop offset is small relative to a 128-frame block.

**G3 stays OPEN. No tracker is selected, no ADR is written, no production
wiring.** Neither backend passes the acquisition gate; aubio is the only one that
passes the BPM gate. The comparison is deliberately not a decision.

## Tests executed

```
ctest (JAM_ENABLE_BTRACK=ON, JAM_ENABLE_AUBIO=ON):  9/9 passed
  jam.BTrackBackend, jam.AubioBackend, jam.AnalysisAudioRing,
  jam.BackendRunner, jam.DrumTransportAdapter, jam.MusicalClock,
  jam.RhythmCorpus, jam.RhythmEvalMetrics, jam.RtSignal
ctest (default, backends OFF):                      7/7 passed
```

- `BackendRunner`: **8 tests / 49 checks**. New coverage: reported device time
  preserved vs block, sample-zero valid, partial final block, non-causal fallback,
  rate-mismatch diagnosis, legacy defect reproduction, availability does not
  affect scoring, determinism.
- `RhythmEvalMetrics`: **17 tests / 300 checks** (was 12/272). New: recovery from
  the true stop, silence coverage is not a duration threshold, missing BPM lock
  cannot pass the core gate, phase undefined with no match, silence acceleration
  vs insufficient evidence.
- `-Wall -Wextra -Wpedantic` clean on every changed TU and both test TUs. The
  only warnings in the build are the pre-existing GCC 14 `-Wmismatched-new-delete`
  false positive on the global `operator new/delete` in `main.cpp` and
  `AnalysisAudioRingTests.cpp`; confirmed present at base `eac59ba` before any
  EVAL-004 edit.

## Limitations

1. Synthetic corpus; G3 still needs real-guitar play tests (SPEC 20).
2. The two adapters are not equally configured (BTrack resamples to 44.1 kHz at a
   ~557-device-sample hop; aubio runs at the device rate with a 512 hop; their
   internal silence gates differ). Documented in `comparison.md`; this is a
   comparison of integrations, not of library cores.
3. The allocation counter is C++ `new`/`delete` only (including the CLI's own),
   not C `malloc`, and is not an RT-safety claim.
4. Syncopation stability, ramp continuity, resync, Loose-Follow reactivity and
   audio-device restart remain unmeasurable offline and are marked as such.

## Handoff

Authoritative numbers, commands and per-run JSON/CSV/summary live in
`docs/research/tracker-comparison/`. G3 remains OPEN.

## Final commit SHA

- Implementation + evidence commit: `8ae3249` (`fix(eval-004): preserve backend
  beat timestamps, honest timing/gate semantics, aubio comparison`).
- This note's SHA update is the subsequent commit on `wp/EVAL-004-timing`; the
  branch head is the handoff SHA reported to the orchestrator.

