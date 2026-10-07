# EVAL-002 — Evaluation harness and metrics

## Goal

Make beat-tracker comparison one command, and define the metrics that decide it,
so SPEC §12's rule ("the production backend is selected by an ADR containing the
numbers") can be satisfied. This task builds the instrument, not the choice.

## Base commit

`a8da4f2` (`feat(deps): vendor BTrack 1.0.7 behind an optional, licence-explicit
seam`). Worktree `/home/mojo/projects/worktrees/EVAL-002`, branch `wp/EVAL-002`.

## Files changed

New, all under the allowed set:

```
tools/rhythm-eval/CMakeLists.txt
tools/rhythm-eval/main.cpp
tools/rhythm-eval/BackendRunner.h
tools/rhythm-eval/BackendRunner.cpp
tools/rhythm-eval/Metrics.h
tools/rhythm-eval/Metrics.cpp
tools/rhythm-eval/Manifest.h
tools/rhythm-eval/Manifest.cpp
tools/rhythm-eval/Json.h
tests/jam/RhythmEvalMetricsTests.cpp
task-notes/EVAL-002.md
```

Nothing under `src/`, `testdata/`, `jam-core/CMakeLists.txt`, root
`CMakeLists.txt`, or `docs/` was modified. `git status --short` at the end lists
only the files above.

## Contract implemented

- `Metrics.h/.cpp` — pure, deterministic, I/O-free scoring (plus pure
  serialisation helpers so the determinism test can cover JSON/CSV). No
  wall-clock, no randomness, no globals.
- `BackendRunner.h/.cpp` — reads 16-bit mono PCM WAV, drives any
  `jam::IRhythmTracker` with fixed-size blocks, reduces `RhythmObservation`s to
  the pure `ObservationSeries`. The runner owns the timeline and stamps each
  observation with the block sample time, so predicted-beat times are well
  defined even for a backend that leaves `inputSampleTime` at zero.
- `Manifest.h/.cpp` — strict manifest reader; missing required fields are
  errors, never silent defaults.
- `Json.h` — dependency-free, strict JSON reader + compact writer.
- `main.cpp` — CLI with built-in deterministic synthetic backends and a
  shared-library adapter for real backends.

### Build/test wiring note

`jam-core/CMakeLists.txt` is frozen for this task and globs only
`src/jam/*.cpp` and `tests/jam/*.cpp`. The metrics therefore reach the jam-core
test binary by direct inclusion in `tests/jam/RhythmEvalMetricsTests.cpp`
(`#include "../../tools/rhythm-eval/Metrics.cpp"` and `Manifest.cpp`). The CLI
builds the same `.cpp` files as ordinary translation units. There is no second
copy of the algorithm.

---

## Metric definitions

All metrics are computed per fixture by `scoreFixture()` and aggregated by
`aggregateFixtures()`. "Core" means the fixture carries the `core` tag in the
manifest (see Known limitations #1 for a corpus inconsistency here).

| # | Metric | Definition | Unit | Aggregation | SPEC §19 target / pass |
|---|---|---|---|---|---|
| 1 | **Acquisition time** | Time from the first ground-truth beat to the start of the first *sustained lock* (definition below) | beats, bars, seconds | Per fixture; gate = fraction of core fixtures with `acquisitionBars <= 2` | "acquire useful lock within 2 bars for ≥ 95 % of core fixtures" |
| 2 | **BPM relative error** | `abs(median(bpmCandidate over the steady-state window) − nominalBpm) / nominalBpm`; steady fixtures only | fraction | mean, median across steady fixtures; **worst-case across core** is the gate | "locked BPM relative error ≤ 2 % on steady-tempo core fixtures" |
| 3 | **Beat-event F-measure** | Optimal one-to-one matching of predicted beats to ground-truth beats within the documented tolerance; `P = TP/pred`, `R = TP/truth`, `F = 2PR/(P+R)` | fraction | mean across fixtures; P and R reported separately | Not directly gated; used as the detection-quality summary |
| 4 | **Phase error** | For each matched predicted beat, signed error to the nearest ground-truth beat, in ms and in local beats; mean signed, mean abs, p50/p95 of abs | ms, beats | mean / percentile across matched beats within a fixture | Not directly gated; the ramp gate proxies it |
| 5 | **Half/double-time error rate** | Fraction of steady fixtures whose locked BPM ratio to nominal is within ±10 % of 2× (double) or 0.5× (half). **Separate from metric 2** | fraction | rate over applicable fixtures; rate over core is the gate | "half/double-time errors < 5 % on core fixtures" |
| 6 | **False beat rate in silence** | Predicted beat events inside the manifest's declared `silenceSpans`, divided by total declared silence duration. Off-grid variant additionally excludes predictions within ±30 ms of a maintained grid beat | events/s | per fixture; worst reported | "silence does not create false acceleration" (qualitative; see #2 in Known limitations) |
| 7 | **Recovery delay after stop/start** | For `stop_start`: lock time minus the first onset after the last declared silence span | seconds, beats | single fixture; worst reported | "stop/start recovery succeeds without restarting the audio device" (offline proxy) |
| 8 | **Syncopation stability** | For `syncopated_funk`: std-dev, worst deviation from nominal, and worst first-difference ("jump") of reported BPM over the steady window | BPM, fraction of nominal | per fixture; worst reported | "no tempo jump from one isolated syncopated event": max step ≤ 5 % nominal |
| 9 | **Tempo-drift tracking** | For ramps: local tempo implied by consecutive predicted beats vs the local tempo of the **non-uniform ground-truth grid** at the same time; relative error | fraction | mean and worst across predicted intervals | "Follow handles controlled gradual tempo ramps": mean ≤ 2 % |
| 10 | **CPU time** | Process CPU seconds consumed by the `process()` loop (`std::clock`), resource-only, never scored | seconds | sum across fixtures | Not gated; reported for the ADR |
| 11 | **Allocation count** | Whole-process allocations during the measured run window (global `new`/`delete` counters in the CLI) | count | sum across fixtures | Not gated; reported for the ADR |

Metrics required by SPEC §12.3 that are **not** computed here, with reasons, are
listed under Known limitations.

---

## Tolerances chosen

- **Beat-matching tolerance: 70 ms** (`kBeatMatchToleranceSeconds = 0.070`).
  Read from `manifest.conventions.beatToleranceSeconds` and overridable per run;
  the corpus supplies 0.07. The constant is the default and is pinned by test 2c.
  - *Musical justification.* A beat at the corpus's tempi is 0.41–0.63 s. 70 ms
    is therefore 11–17 % of a beat: wide enough to accept the ~20–30 ms
    just-noticeable difference in human beat placement and well above the
    generator's ±10 ms humanisation, but far below half a beat, so it cannot
    silently swallow a half/double-time lock. The corpus README's own rationale
    is the same: "chosen well above the generator's worst-case ±10 ms timing
    humanisation so the tolerance measures the tracker, not the humanisation."
  - The test pins the value from both sides: a 40 ms constant offset must still
    score F ≈ 1, and halving the tolerance to 35 ms must drop recall to 0.
    Doubling the documented tolerance makes the suite fail (see Evidence).
- **Lock run length: 4 predicted beats** (`kLockRunLength`), one 4/4 bar.
- **Lock tempo agreement: 2 %** (`kBpmAgreementFraction`), the SPEC §19 BPM
  target, so a correct-tempo/incorrect-phase or half-time run cannot count as a
  lock.
- **Half/double band: ±10 % of the exact ratio** (`kHalfDoubleBandFraction`):
  double ∈ [1.8, 2.2], half ∈ [0.45, 0.55]. Tight enough that a merely wrong
  tempo is not labelled a metrical-multiple error.
- **Silence onset window: 30 ms** (`kSilentOnsetWindowSeconds`), the manifest's
  own definition of a silent beat, used only by the off-grid diagnostic.
- **Syncopation jump: 5 % of nominal** (`kSyncopationJumpFraction`).
- **Ramp gate: mean local-tempo relative error ≤ 2 %** (reuses
  `kBpmAgreementFraction`).

---

## Acquisition definition

A **sustained lock** begins at the first predicted beat `i` such that the next
`kLockRunLength = 4` consecutive predicted beats satisfy, for each `k`:

1. the beat lies within **70 ms** of a ground-truth beat, and
2. its matched ground-truth index is **strictly greater** than the previous
   one (advancing, not clustered or backtracking), and
3. a phase-valid tempo estimate near that prediction is within **2 %** of the
   local ground-truth tempo (from the ground-truth grid gap).

Then:

- `acquisitionSeconds = max(0, lockTime − truth.beats.front())`
- `acquisitionBeats = max(0, beatPositionAt(lockTime))`, a fractional position in
  the ground-truth grid (0 at beat 0), so it is well defined on non-uniform
  ramps.
- `acquisitionBars = acquisitionBeats / beatsPerBar`.

Rationale for strictly-advancing rather than strictly-sequential matched beats:
dropping an occasional beat does not lose a lock, but a half/double cluster does
— and the tempo condition independently rejects half/double. The 2-bar gate
therefore measures *sustained correct lock*, not one lucky hit. The task's
example ("N consecutive beat predictions within a tolerance of a ground-truth
beat") is the basis; the advance + tempo conditions make it precise.

---

## Tests executed

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/EVAL-002
rm -rf /tmp/opencode/build-eval002
cmake -S jam-core -B /tmp/opencode/build-eval002 -G Ninja
cmake --build /tmp/opencode/build-eval002
ctest --test-dir /tmp/opencode/build-eval002 --output-on-failure; echo "exit=$?"
```

Suite `RhythmEvalMetrics`, registered ctest target `jam.RhythmEvalMetrics`
(7 cases). CLI built and run separately:

```bash
cmake -S tools/rhythm-eval -B /tmp/opencode/build-eval002-cli -G Ninja
cmake --build /tmp/opencode/build-eval002-cli
/tmp/opencode/build-eval002-cli/rhythm-eval --corpus testdata/rhythm \
        --out /tmp/opencode/eval-out-ideal
/tmp/opencode/build-eval002-cli/rhythm-eval --corpus testdata/rhythm \
        --out /tmp/opencode/eval-out-degraded --backend synthetic-degraded
```

## Test results

Full ctest, zero warnings in all sources under `-Wall -Wextra -Wpedantic`:

```
100% tests passed out of 6
Total Test time (real) =   2.06 sec
exit=0
```

`RhythmEvalMetrics` contains 7 tests / 205 checks. Existing 5 suites
(`AnalysisAudioRing`, `DrumTransportAdapter`, `MusicalClock`, `RhythmCorpus`,
`RtSignal`) are unaffected.

The CLI ideal-backend run scores perfectly and all gates PASS: F = 1.000,
BPM error 0, acquisition 0 bars for all 11 core fixtures, 0/17 half/double
errors, ramp mean error 0.0018. The degraded backend produces F mean 0.831,
recall 0.7515, half/double 2/11 core, acquisition 9/11 core, ramp mean 0.148,
and FAILs four gates — the expected direction.

## Evidence

### Broken-then-reverted (suite can fail)

Perturbation: `kBeatMatchToleranceSeconds` 0.070 → 0.140 in
`tools/rhythm-eval/Metrics.h`, rebuild, run the suite directly:

```
$ /tmp/opencode/build-eval002/jamTests RhythmEvalMetrics.
[suite] RhythmEvalMetrics
  PASS RhythmEvalMetrics.perfectPredictionsScorePerfectly
    FAIL .../RhythmEvalMetricsTests.cpp:283
      CHECK_NEAR failed: tol ~= 0.070
    actual:   0.140000
    expected: 0.070000
    tolerance: 0.000000
    FAIL .../RhythmEvalMetricsTests.cpp:291
      CHECK failed: half.fMeasure < 0.99
    FAIL .../RhythmEvalMetricsTests.cpp:292
      CHECK_NEAR failed: half.recall ~= 0.0
    actual:   1.000000
    expected: 0.000000
    tolerance: 0.000000
  FAIL RhythmEvalMetrics.knownDegradationsProduceKnownMetricValues
  ...
7 tests, 205 checks, 3 failed check(s) in 1 test(s)

$ ctest --test-dir /tmp/opencode/build-eval002 -R jam.RhythmEvalMetrics
0% tests passed, 1 tests failed out of 1
```

Reverted, rebuilt, same suite:

```
7 tests, 205 checks, 0 failed check(s) in 0 test(s)
ctest: 100% tests passed out of 1
```

### CLI end-to-end (ideal backend)

Machine-readable outputs written: `results.json`, `fixtures.csv`,
`summary.md`, and per-fixture `per_fixture/<name>.{json,csv}` (19 each). JSON
validated with `python3 -c "import json; json.load(...)"`. `summary.md` excerpt:

```
| fixture | core | pred | TP | P | R | F | acq bars | BPM err | h/d | false/s | false off-grid/s | recovery s | sync dev | ramp err | p95 phase ms |
| clean_eighths | yes | 20 | 20 | 1.000 | 1.000 | 1.000 | 0.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 2.57 |
...
| stop_start | yes | 32 | 32 | 1.000 | 1.000 | 1.000 | 0.00 | 0.000 | - | 16.667 | 0.000 | 0.006 | 0.0000 | 0.0000 | 2.61 |
...
## SPEC 19 gates
| acquire within 2 bars for >= 95% of core | PASS |
| locked BPM relative error <= 2% on core | PASS |
| half/double-time errors < 5% on core | PASS |
| no tempo jump from isolated syncopation | PASS |
| follow ramps (mean local-tempo error <= 2%) | PASS |
```

## Known limitations

1. **Corpus inconsistency: `tagVocabulary.core` vs fixture tags.** The manifest's
   `tagVocabulary.core` lists 10 scenarios and omits `missing_downbeats`, but the
   `missing_downbeats` fixture carries the `core` tag, so 11 fixtures are
   tagged core. The harness uses the fixture tags (the actual statement about
   which fixtures SPEC §19 scores against), giving gates over 11 core fixtures.
   This should be reconciled by EVAL-001, not silently by the harness.
2. **`silenceSpans` are narrow.** They are coalesced per-silent-beat ±30 ms
   windows, so a correct tracker that maintains the grid through a stop is
   counted by the literal false-beat metric (e.g. `stop_start` 16.7/s). The
   `falseBeatsOffGridInSilence` column excludes beats matching the maintained
   grid and reads 0 for the ideal backend. A `trueSilenceSpans` field
   (first-to-last silent beat, merged) would make the primary metric meaningful;
   it must come from EVAL-001's manifest, **not** be reverse-engineered from the
   WAVs here.
3. **Half/double does not cover triple.** `compound_6_8`'s scored grid is the
   dotted quarter, but the manifest exposes a `subdivision.bpm` of 288 (three
   per beat); a tracker locking to the eighths reports exactly 3× and would not
   be flagged, because SPEC §19 names only half/double. A metrical-multiple
   metric belongs in EVAL-003.
4. **Two SPEC §12.3 metrics are not corpus metrics.** `analysis queue overrun
   count` is a runtime integration property (no queue runs in this offline
   harness), and `platform/build complexity` is not an output of scoring a
   corpus. Both are honestly out of scope for EVAL-002; they must be recorded by
   the integration/ADR work.
5. **Phase error is over matched beats only** (true positives), using the
   nearest ground-truth beat. Unmatched predictions are covered by precision and
   the half/double metric, not by phase.
6. **Ramp "steps" are not audio discontinuities.** Metric 9 measures error
   against the analytic grid; it cannot hear an abrupt audible correction. That
   still requires the real-guitar play tests of SPEC §20.
7. **Synthetic, not a guitar.** Per the corpus README, a backend that scores
   well here may still fail on a real instrument.

## Integration notes

**Exact adapter point.** The real BTrack backend is written against the frozen
`jam::IRhythmTracker` (`src/jam/IRhythmTracker.h`). The harness knows only that
interface: `BackendRunner::run(jam::IRhythmTracker&, const WavData&)` calls
`reset(sampleRate)` and then `process(frame)` per 128-frame block, with
`frame.sourceSampleRate = 48000` and `frame.sampleTime` = absolute frame index.
The backend is never referenced by name.

**CLI plugin convention.** A real backend is loaded from a shared library
exporting:

```cpp
extern "C" jam::IRhythmTracker* jam_rhythm_create();
extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
```

via `--backend-lib <path>` (POSIX `dlopen`/`dlsym`). The backend is created
once and run over every fixture. The current `src/btrack/BTrackBackend.*` worker
only has to add these two C-linkage shims (or the CLI can be pointed at a
link-time target) — no BTrack-specific code lives in `tools/rhythm-eval/`.

**What a backend must guarantee for a fair comparison.**

- *Latency.* Predicted-beat time is the **block start** of the observation whose
  `beatEvent` is true; the runner stamps it, overriding any backend value, so a
  backend that detects a beat at the end of its internal frame carries up to one
  block (~2.7 ms at 128 frames) plus its own algorithmic latency. 70 ms absorbs
  small lags, but a backend with material latency (e.g. a windowed
  autocorrelation reporting the beat several tens of ms late) **must document
  its latency**, and the ADR should either (a) subtract a documented,
  constant `latencySeconds` before scoring, or (b) justify why it is negligible.
  Scoring raw event times without this would unfairly penalise a correct but
  latency-heavy tracker and would decide G3 wrongly. This is the single most
  important fairness caveat.
- *Sample-rate convention.* `reset(sampleRate)` receives the rate the backend
  will be fed (the WAV's 48000 Hz here). If it resamples internally, it must not
  report beats on the resampled clock: the runner overrides
  `sourceSampleRate` with the feed rate, so beats land on the audio timeline.
  Any internal latency introduced by resampling is subject to the latency rule
  above.
- *Determinism.* `reset()` must clear all state (no hidden globals), so
  repeated runs over the same fixture produce identical metrics. The harness's
  determinism test proves the scorer is stable; a backend's own determinism is
  its responsibility and is testable by running the CLI twice and diffing
  `results.json`.
- *One observation per block.* `process()` is called once per 128-frame block
  and must return an observation for that block; a backend that needs larger
  internal frames must buffer and emit from its own state.
- *Block size is not sacred.* `--block` selects the harness block; 128 frames is
  the default and is small enough that the synthetic ideal backend's ramp error
  is <0.3 %. Backends that prefer 512/1024 should buffer.

**EVAL-003 extension.** `ObservationSeries`, `RhythmTruth`, `scoreFixture`,
`aggregateFixtures` and the serialisation helpers are all public and pure.
Adding a robustness scenario means adding a `RhythmTruth` (or a synthetic
observation decorator) and a metric function; no I/O or CLI change is required.
The `RhythmTruth` struct is intentionally a superset of what the current metrics
need so EVAL-003 can add metrics without touching `Manifest.cpp`.

## Final commit SHA

- Implementation commit: `26397ee2ce88828f7430ed3dd9b9b5b3cca73210`
  (`feat(eval-002): rhythm evaluation harness, metrics, and one-command CLI`).
- This note's SHA-record commit is the branch head reported to the
  orchestrator; the code under test is the implementation commit above.
