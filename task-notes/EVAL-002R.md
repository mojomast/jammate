# EVAL-002R

## Goal

Produce the first real beat-tracker numbers on the guitar corpus, and fix the
false-beat metric so it measures the gate it exists for. Concretely: switch the
primary false-beat counter to the corpus's new `trueSilenceSpans` while keeping
the `silenceSpans` failure reported separately; make backend latency an explicit,
configurable, default-off scoring parameter and report compensated and
uncompensated results in one run; build the real `BTrackBackend` with
`-DJAM_ENABLE_BTRACK=ON` and run it over all 19 fixtures; report honestly,
including where BTrack fails.

## Base commit

`c68df60` (`merge(EVAL-001 repair): true silence declared separately; core
membership usable`). Worktree `/home/mojo/projects/worktrees/EVAL-002R`, branch
`wp/EVAL-002R`.

## Files changed

Modified (all inside the allowed set):

```
tools/rhythm-eval/Metrics.h
tools/rhythm-eval/Metrics.cpp
tools/rhythm-eval/Manifest.h
tools/rhythm-eval/Manifest.cpp
tools/rhythm-eval/main.cpp
tests/jam/RhythmEvalMetricsTests.cpp
```

Added:

```
tools/rhythm-eval/BtrackPlugin.cpp          # dlopen shim around BTrackBackend
tools/rhythm-eval/build-btrack-plugin.sh    # builds the GPL plugin against jam-btrack
docs/research/results-btrack.json           # full machine-readable evidence
docs/research/summary-btrack.md             # per-fixture, aggregate, gate table
docs/research/fixtures-btrack.csv           # combined CSV, all variants
docs/research/per-fixture/*.json,*.csv      # one JSON + CSV per fixture (38 files)
task-notes/EVAL-002R.md                     # this note
```

**No CMake file was modified.** In particular `tools/rhythm-eval/CMakeLists.txt`
was left byte-identical to `c68df60`: the GPL BTrack plugin is built by
`build-btrack-plugin.sh` against the `libjam-btrack.a` that jam-core's own
`-DJAM_ENABLE_BTRACK=ON` build already produces, and loaded by the CLI's existing
`--backend-lib` dlopen path. Nothing under `testdata/`, `src/`, `third_party/`,
`SPEC.md`, `DEVPLAN.md`, `EXECUTION-LEDGER.md`, `HANDOFF.md`, or an existing
`docs/` file was touched. `docs/research/results-btrack.json`,
`summary-btrack.md`, `fixtures-btrack.csv` and `per-fixture/` are **new** files,
required by the task.

## Contract implemented

- **`Metrics.h`/`Metrics.cpp`** — added `RhythmTruth::trueSilenceSpans`;
  `scoreFixture(truth, obs, tolerance, latencyCompensationSeconds = 0.0)`;
  `ScoringVariant` plus `scoringVariantToJson` and
  `fixtureMetricsCsvHeaderWithVariant`/`...RowWithVariant`; rewrote
  `markdownSummary` to cover every variant, the latency effect, and a per-gate
  PASS/FAIL/NOT-INFORMATIVE table with reasons. Still pure, deterministic,
  I/O-free.
- **`Manifest.h`/`Manifest.cpp`** — parses `trueSilenceSpans` as a *required*
  field (the corpus promises it on every fixture; silently defaulting it would
  let the primary metric score a corpus that is not the one on disk), sharing one
  span parser with `silenceSpans`.
- **`main.cpp`** — repeatable `--compensate-latency <seconds>` (default 0/off),
  `--json-file`, `--summary-file`, `--csv-file`, `--per-fixture-dir`; the backend
  runs **once** per fixture and every latency variant scores the same
  `ObservationSeries`, so the effect is a pure scoring shift, not a re-run.
- **`BtrackPlugin.cpp` + `build-btrack-plugin.sh`** — the documented
  `jam_rhythm_create`/`jam_rhythm_destroy` plugin convention for the GPL backend.

## False-beat metric fix

The primary counter was scored against `silenceSpans` — the corpus's ±30 ms
windows around beats deliberately *not* played while the phrase continued. Every
beat of a correct held grid falls inside one, so a correct tracker scored 16.667
false beats/s on `stop_start`: the metric measured correct behaviour. There are
now two independently computed, separately named counters:

| counter | scored against | meaning | failure it names |
|---|---|---|---|
| `falseBeatsInTrueSilencePerSecond` (primary) | `trueSilenceSpans` | a predicted beat in a region where the guitar genuinely stopped. Every such beat counts, on-grid or not: the guitar stopped, so the notional grid is irrelevant. | SPEC §19 "silence does not create false acceleration" |
| `falseBeatsInUnplayedBeatWindowsPerSecond` (secondary) | `silenceSpans` | a predicted beat in a ±30 ms window around a beat the player deliberately did not play. | inventing beats *inside playing material* |
| `falseBeatsOffGridInUnplayedBeatWindowsPerSecond` | `silenceSpans`, excluding beats within 30 ms of the held grid beat | literally "extra beats where none were played" | the off-grid variant kept from EVAL-002 |

Both are reported in JSON, CSV and the Markdown summary; neither is dropped or
merged. On `stop_start` a tracker holding the exact grid now reads **2.100/s**
against true silence (10 held beats over 4.762 s) versus **16.667/s** against the
unplayed-beat windows (8 held beats over 0.480 s) — ~8× lower, not the old
fabrication rate.

**Not-informative fixtures.** `sustained_chords` (8.734 s true silence, 77 % of
the fixture) and `tapping_muting_only` (9.963 s, 83 %) are flagged
`falseBeatMetricInformative = false`. The two-stage fast decay ends each note in
~0.2 s, so most of both files is genuinely silent and there is almost no playing
for a tracker to fabricate against: a low rate there is trivially low and must
not be read as a pass. The criterion is stated in code and in the report —
true silence ≥ 50 % of duration — and it selects exactly those two fixtures
(`sparse_single_notes` sits just below at 48.55 %, so it stays informative).

## Latency compensation

`--compensate-latency <seconds>` is a first-class, explicit, configurable
parameter. It is subtracted once from every predicted beat time before any metric
is computed, so F-measure, phase error, acquisition, false beats and recovery all
see the same compensated clock. **Default 0 (off): the harness never compensates
unless asked.** The flag may be repeated; the uncompensated variant is always
included, and each requested value adds a variant in the same run. The BTrack run
was made with the adapter's documented best- and worst-case framing latencies
(11.61 ms = one hop, 23.22 ms = one frame, `BTrackBackend::framingLatencySeconds`).

Measured effect (uncompensated → compensated; delta = compensated − uncompensated):

| metric | uncompensated | comp 11.61 ms | comp 23.22 ms |
|---|---|---|---|
| F-measure mean | 0.7099 | 0.7099 (+0.0000) | 0.7137 (+0.0038) |
| precision mean | 0.7039 | 0.7039 (+0.0000) | 0.7090 (+0.0052) |
| recall mean | 0.7235 | 0.7235 (+0.0000) | 0.7262 (+0.0027) |
| mean signed phase (ms) | +12.381 | +1.312 (−11.07) | −8.910 (−21.29) |
| mean \|phase\| (ms) | 16.150 | 10.587 (−5.56) | 14.916 (−1.23) |
| mean p95 \|phase\| (ms) | 28.159 | 21.504 (−6.66) | 29.529 (+1.37) |
| acquisition ≤ 2 bars, core fraction | 0.3636 | 0.3636 (+0.0000) | 0.3636 (+0.0000) |
| BPM rel error, core worst | 0.0234 | 0.0234 (0) | 0.0234 (0) |
| false beats/s, true silence (worst informative) | 1.3663 | 1.3663 (0) | 1.5371 (+0.171) |
| false beats/s, unplayed windows (worst, off-grid) | 0.0000 | 0.0000 (0) | 0.0000 (0) |

Interpretation:

- BTrack's beats are **~12.4 ms late on average** in this harness, i.e. close to
  its documented **best** case (one hop, 11.61 ms), not the worst-case frame
  (23.22 ms). Compensating 11.61 ms cuts mean |phase| by 34 % (16.15 → 10.59 ms)
  and p95 by 24 %. Compensating 23.22 ms **overshoots**: mean signed phase goes
  to −8.91 ms and p95 gets *worse* (28.16 → 29.53 ms).
- **No SPEC §19 gate flips** between the variants. The phase improvement is real
  but none of the five measurable gates is phase-thresholded, and the ramps are
  invariant to a constant shift by construction. The one metric that moves the
  wrong way under over-compensation is the true-silence false-beat rate
  (1.366 → 1.537 /s), because shifting beats earlier drags a couple into a span.
- This is the key fairness result: for *this* backend the compensation effect is
  small relative to the gate thresholds and changes no decision, but it is not
  negligible to phase, and choosing the wrong latency is demonstrably worse than
  choosing none or choosing the right one.

The summary states plainly that **the orchestrator, not the harness, decides
whether to compensate at ADR time**, and why compensating can be as wrong as not
compensating: it asserts a latency that may not be constant for a non-causal or
tempo-adaptive backend, and subtracting a latency the backend does not have is
just a bias in the other direction.

## Real corpus results

Run:

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/EVAL-002R
rm -rf /tmp/opencode/build-e2r
cmake -S jam-core -B /tmp/opencode/build-e2r -G Ninja -DJAM_ENABLE_BTRACK=ON
cmake --build /tmp/opencode/build-e2r
ctest --test-dir /tmp/opencode/build-e2r --output-on-failure

rm -rf /tmp/opencode/build-e2r-cli
cmake -S tools/rhythm-eval -B /tmp/opencode/build-e2r-cli -G Ninja \
      -DCMAKE_EXE_LINKER_FLAGS=-rdynamic
cmake --build /tmp/opencode/build-e2r-cli
tools/rhythm-eval/build-btrack-plugin.sh /tmp/opencode/build-e2r \
      /tmp/opencode/librhythm-eval-btrack.so

/tmp/opencode/build-e2r-cli/rhythm-eval \
  --corpus testdata/rhythm --out docs/research \
  --backend btrack --backend-lib /tmp/opencode/librhythm-eval-btrack.so \
  --json-file results-btrack.json --summary-file summary-btrack.md \
  --csv-file fixtures-btrack.csv --per-fixture-dir per-fixture \
  --compensate-latency 0.02322 --compensate-latency 0.01161
```

`-rdynamic` exports the CLI's global `operator new/delete` overrides so the
allocation counter also sees allocations made inside the dlopen()ed plugin
(38 742 counted, 7.4–8.2 s CPU for the whole corpus).

Per-fixture, **uncompensated** (`TP/pred`, F, acquisition bars; `-` = no
sustained lock ever observed; `NO` = not informative for the true-silence
metric):

| fixture | core | pred | TP | F | acq bars | BPM err | trueSil F/s | unplayed F/s | ramp err | p95 phase ms |
|---|---|---|---|---|---|---|---|---|---|---|
| accelerando | | 25 | 9 | 0.367 | - | 0.000 | 0.000 | 0.000 | 0.0755 | 56.45 |
| arpeggio | yes | 22 | 19 | 0.905 | - | 0.021 | 0.000 | 0.000 | 0.0000 | 20.67 |
| blues_shuffle | yes | 22 | 16 | 0.762 | 1.75 | 0.003 | 0.000 | 0.000 | 0.0000 | 28.67 |
| clean_eighths | yes | 21 | 17 | 0.829 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 18.76 |
| clean_sixteenths | yes | 22 | 19 | 0.905 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 30.95 |
| compound_6_8 | | 20 | 6 | 0.375 | - | 0.455 | 0.000 | 0.000 | 0.0000 | 23.33 |
| line_input_clipping | | 22 | 17 | 0.810 | 2.75 | 0.000 | 0.000 | 0.000 | 0.0000 | 19.14 |
| line_input_low_level | | 18 | 17 | 0.895 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 19.52 |
| missing_downbeats | yes | 21 | 18 | 0.878 | - | 0.021 | 0.000 | 11.111 | 0.0000 | 24.67 |
| noisy_microphone | | 22 | 16 | 0.762 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 19.14 |
| palm_mute_metal | yes | 19 | 18 | 0.923 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 19.52 |
| power_chords_distorted | yes | 21 | 0 | 0.000 | - | 0.023 | 0.000 | 0.000 | 0.0000 | 0.00 |
| ritardando | | 28 | 15 | 0.577 | 0.51 | 0.000 | 0.000 | 0.000 | 0.0920 | 56.22 |
| sparse_single_notes | yes | 22 | 15 | 0.714 | 1.26 | 0.018 | 1.366 | 9.722 | 0.0000 | 47.14 |
| stop_start | yes | 26 | 22 | 0.759 | 0.27 | 0.021 | 0.000 | 0.000 | 0.0000 | 15.82 |
| sustained_chords | yes (NO) | 11 | 6 | 0.444 | 2.01 | 0.003 | 0.801 | 5.556 | 0.0000 | 23.33 |
| syncopated_funk | yes | 21 | 18 | 0.878 | 0.77 | 0.018 | 0.000 | 13.333 | 0.0000 | 50.57 |
| tapping_muting_only | (NO) | 18 | 17 | 0.895 | 0.75 | 0.018 | 0.000 | 0.000 | 0.0000 | 15.52 |
| waltz_3_4 | | 19 | 15 | 0.811 | 1.01 | 0.014 | 0.000 | 0.000 | 0.0000 | 17.42 |

Aggregate (uncompensated): F mean **0.7099**, precision 0.7039, recall 0.7235;
BPM relative error steady mean 0.0432 / median 0.0212 / core worst **0.0234**;
half/double 0/17 all, 0/11 core; acquisition ≤ 2 bars **4/11 core**;
false beats true silence worst informative **1.3663/s**; false beats unplayed
windows worst 13.333/s with off-grid worst **0.000/s**; ramp local-tempo error
mean **0.0838**, worst 0.341; CPU 7.4–8.2 s, 38 742 allocations.

### What the numbers say about BTrack

- **BTrack is ~2.3 % flat on clean material** (123.047 Hz reported vs 126 BPM on
  `clean_eighths`, `clean_sixteenths`, `palm_mute_metal`,
  `power_chords_distorted`, `line_input_low_level`, `noisy_microphone`;
  117.454 vs 120 on `arpeggio` and `missing_downbeats`). This is exactly the
  residual the adapter's own documentation predicted for the 44.1 kHz
  work-around ("44.1 kHz, hop 512: −2.34 %"), and it is **above the SPEC §19 2 %
  target**, so BTrack as integrated cannot pass the BPM gate on clean guitar.
- That same 2.34 % is why acquisition fails: the sustained-lock definition
  requires the tempo estimate within 2 %, so six core fixtures match beats
  (`clean_eighths` 17/21 within 70 ms) but never register a lock. The acquisition
  and BPM failures are the same underlying fact, not two independent ones.
- **`power_chords_distorted` matched 0 of 21 predicted beats** while reporting a
  near-correct 123.05 BPM. It found the pulse but not the phase. This is a
  tracker failure, not a metric bug: the same metric scores 0.71–0.92 on the
  other steady fixtures.
- **`compound_6_8` reports 139.67 BPM against a 96 BPM dotted-quarter grid**
  (+45 %), F 0.375 — neither the eighths (3×) nor the beat. Not core, so not
  gated, but worth recording.
- Ramp tracking lags: accelerando 7.55 %, ritardando 9.20 % mean local-tempo
  relative error, both far above the 2 % target.
- **Post-stop recovery shows no sustained lock** (`stop_start`
  `hasRecovery = false`), again because the lock rule requires the tempo within
  2 % and BTrack reports 2.1 % off there — the same bias, not a separate failure.

## Gate assessment

SPEC §19 gates over the run, **uncompensated** (compensated variants are
identical except the silence rate noted below). "A gate you cannot measure is not
a pass."

| SPEC §19 gate | Result | Reason |
|---|---|---|
| acquire useful lock within 2 bars for ≥ 95 % of core fixtures | **FAIL** | 4/11 core = 0.364. Passing: `blues_shuffle`, `sparse_single_notes`, `stop_start`, `syncopated_funk`. No sustained lock ever on `arpeggio`, `clean_eighths`, `clean_sixteenths`, `missing_downbeats`, `palm_mute_metal`, `power_chords_distorted`; `sustained_chords` at 2.01 bars. |
| locked BPM relative error ≤ 2 % on core | **FAIL** | worst core 2.34 % (2.34 % is above 2 %). This gate was a false PASS at 2.34 % before the threshold bug below was fixed. |
| half/double-time errors < 5 % on core | **PASS** | 0/11 core, 0/17 steady. No metrical-multiple lock. |
| no tempo jump from one isolated syncopated event | **PASS** | max first-difference of reported BPM on `syncopated_funk` = 0.0000 of nominal (BTrack holds a constant tempo). |
| silence does not create false acceleration | **FAIL** | worst informative true-silence false-beat rate **1.3663/s** on `sparse_single_notes` (8 held-grid beats); compensated-23.22 ms 1.5371/s. See the metric-vs-tracker caveat below. |
| explicit resync establishes new phase within the requested boundary | **NOT-MEASURED** | the offline harness has no resync command path; only the Musical Clock can test this. |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | **FAIL** (partial) | mean local-tempo relative error 0.0838 over the two ramp fixtures (> 2 %). "Audible discontinuity" is not measurable offline and remains open. |
| Loose Follow is measurably less reactive than Follow | **NOT-MEASURED** | requires the Musical Clock and a real controller; no clock runs in this offline harness. |
| stop/start recovery succeeds without restarting the audio device | **NOT-INFORMATIVE** | audio-device restart is not observable offline. The offline proxy is itself negative: `stop_start` shows `hasRecovery = false`, i.e. no sustained post-stop lock under the 2 % tempo-agreement rule (BTrack is 2.1 % off there), so this cannot be read as a success. |

**Result: BTrack fails or cannot decide four of the five measurable gates**
(acquisition, BPM, silence, ramps). It passes half/double and syncopation
stability. This is reported as-is; nothing was tuned, re-weighted, thresholded or
excluded to change it.

### A second metric defect, found by the real run

`gateBpm2Core` compared the worst core error against `2.0 *
kBpmAgreementFraction` = **4 %**, although `kBpmAgreementFraction` is documented
as 2 %, the field is named `gateBpm2Core`, and SPEC §19 says 2 %. At 4 % it
reported BTrack's 2.34 % as a PASS. Fixed to compare against
`kBpmAgreementFraction` directly, and pinned by
`RhythmEvalMetrics.bpmGateUsesTheSpecTwoPercent` (1.0 % passes, 2.08 % and 3.0 %
fail). This makes the gate stricter, not looser — it is a correction, not tuning
to pass.

### Did the tracker do badly, or is the metric wrong?

- The 2.34 % BPM bias and the acquisition failure: **the tracker did badly.**
  The adapter documented this residual; the gate and the lock definition both
  correctly measure the real shortfall.
- `power_chords_distorted` F = 0.000: **the tracker did badly.** It reports a
  plausible tempo but no beat within 70 ms of any ground-truth beat; the metric
  scores the other steady fixtures correctly.
- `sparse_single_notes` 1.366 true-silence false beats/s: **I cannot fully
  tell.** SPEC §12.2's sparse fixture explicitly asks a tracker to "hold lock
  through the gaps", and BTrack's off-grid rate there is 0.000/s, so the 8 beats
  are the held grid, not fabricated extras. The mandated primary metric counts
  held-grid beats in `trueSilenceSpans` as fabricated. For `stop_start` (a real
  4 s stop) that is clearly right; for a sparse fixture whose purpose is gap
  holdover it may be over-penalising correct behaviour. Resolving this needs the
  Musical Clock's holdover semantics (does "hold time through a stop" mean
  "continue emitting beat events"?), which this offline harness cannot decide.
  Flagged, not hidden. Note the sensitivity: the 0.5 not-informative threshold
  puts `sparse_single_notes` at 0.4855, just inside "informative"; if the
  orchestrator decided it were not informative the silence gate would read PASS
  — but that would be a reclassification of evidence, not a change in BTrack, and
  the raw counts are in the JSON either way.

## Tests executed / Test results

```
$ cmake -S jam-core -B /tmp/opencode/build-e2r -G Ninja -DJAM_ENABLE_BTRACK=ON
$ cmake --build /tmp/opencode/build-e2r
$ ctest --test-dir /tmp/opencode/build-e2r --output-on-failure
Test project /tmp/opencode/build-e2r
    Start 1: jam.BTrackBackend
1/7 Test #1: jam.BTrackBackend ................   Passed    7.13 sec
    Start 2: jam.AnalysisAudioRing
2/7 Test #2: jam.AnalysisAudioRing ............   Passed    1.93 sec
    Start 3: jam.DrumTransportAdapter
3/7 Test #3: jam.DrumTransportAdapter .........   Passed    0.01 sec
    Start 4: jam.MusicalClock
4/7 Test #4: jam.MusicalClock .................   Passed    0.01 sec
    Start 5: jam.RhythmCorpus
5/7 Test #5: jam.RhythmCorpus .................   Passed    2.31 sec
    Start 6: jam.RhythmEvalMetrics
6/7 Test #6: jam.RhythmEvalMetrics ............   Passed    0.01 sec
    Start 7: jam.RtSignal
7/7 Test #7: jam.RtSignal .....................   Passed    0.10 sec

100% tests passed out of 7
Total Test time (real) =  11.50 sec
```

`RhythmEvalMetrics` is now **12 tests / 272 checks** (was 7/205):

```
PASS RhythmEvalMetrics.perfectPredictionsScorePerfectly
PASS RhythmEvalMetrics.knownDegradationsProduceKnownMetricValues
PASS RhythmEvalMetrics.trueSilencePredictionsAreFalseBeats
PASS RhythmEvalMetrics.unplayedBeatWindowsAreNotTrueSilence
PASS RhythmEvalMetrics.stopStartCountersAreIndependentlyCorrect
PASS RhythmEvalMetrics.latencyCompensationShiftsEveryBeatTimeMetric
PASS RhythmEvalMetrics.halfTimeDetectionIsDistinctFromPlainBpmError
PASS RhythmEvalMetrics.bpmGateUsesTheSpecTwoPercent
PASS RhythmEvalMetrics.rampScoringUsesTheNonUniformGroundTruthGrid
PASS RhythmEvalMetrics.boundaryConditionsAreFinite
PASS RhythmEvalMetrics.scoringAndSerialisationAreDeterministic
PASS RhythmEvalMetrics.manifestParsingAndMalformedJsonRejection
```

Mapping to the brief's required tests: the perfect case still scores `F = 1.0`
and zero error everywhere (anchor); `trueSilencePredictionsAreFalseBeats` proves
beats in true silence — including a held grid beat — are counted;
`unplayedBeatWindowsAreNotTrueSilence` proves `silenceSpans` beats are *not*
counted by the primary but are seen by the secondary counter;
`stopStartCountersAreIndependentlyCorrect` proves the two counters are
independently right on the real `stop_start` (16.667/s vs 2.100/s, and a
fabricator lights the primary without touching the secondary);
`latencyCompensationShiftsEveryBeatTimeMetric` proves exact phase shift, no-op at
0, and consistent application to F, acquisition and false beats.

Zero warnings from `-Wall -Wextra -Wpedantic` across `Metrics.cpp`,
`Manifest.cpp`, `BackendRunner.cpp`, `main.cpp`, `BtrackPlugin.cpp` (CLI and
plugin builds) and the test translation unit (checked with a
`-fsyntax-only -Wpedantic` pass, since jam-core's frozen CMake passes only
`-Wall -Wextra` to `jamTests`).

## Evidence

Broken-then-reverted proof the metric switch is load-bearing. In
`Metrics.cpp`, the primary loop was changed to read `truth.silenceSpans` instead
of `truth.trueSilenceSpans`:

```
12 tests, 272 checks, 8 failed check(s) in 5 test(s)
  FAIL RhythmEvalMetrics.knownDegradationsProduceKnownMetricValues
  FAIL RhythmEvalMetrics.trueSilencePredictionsAreFalseBeats
  FAIL RhythmEvalMetrics.unplayedBeatWindowsAreNotTrueSilence
  FAIL RhythmEvalMetrics.stopStartCountersAreIndependentlyCorrect
  FAIL RhythmEvalMetrics.latencyCompensationShiftsEveryBeatTimeMetric
$ ctest -R jam.RhythmEvalMetrics
0% tests passed, 1 tests failed out of 1
```

Reverted, rebuilt:

```
12 tests, 272 checks, 0 failed check(s) in 0 test(s)
$ ctest -R jam.RhythmEvalMetrics
100% tests passed out of 1
```

Outputs written and validated: `docs/research/results-btrack.json` (parses),
`docs/research/summary-btrack.md`, `docs/research/fixtures-btrack.csv`, and 19
JSON + 19 CSV per-fixture files (all parse). `git status` clean at handoff.

## Known limitations

1. **The primary false-beat metric counts held-grid beats in true silence** (see
   the ambiguity above). This is the mandated definition; it is the one place the
   metric may over-penalise a holdover. The secondary off-grid counter is the
   clean measure of invented beats and reads 0.000/s for BTrack.
2. **`analysis queue overrun count` and `platform/build complexity` (SPEC §12.3)
   are still not corpus metrics** — no queue runs offline and build complexity is
   not a scoring output. Recorded by integration/ADR work, as before.
3. **Ramp "steps" are not audible discontinuities.** The ramp gate measures
   error against the analytic grid; it cannot hear a correction.
4. **`sustained_chords`/`tapping_muting_only` are not informative for the
   true-silence metric** and are flagged as such; their numbers must not be read
   as passes.
5. **`compound_6_8` triple-time is not caught by the half/double metric** (SPEC
   names only half/double), so its +45 % error is only visible in F/BPM, not in
   the metrical-multiple flag. Carried over from EVAL-002; belongs to EVAL-003.
6. **Synthetic, not a guitar.** Per the corpus README, a tracker that scores
   badly here deserves a second look with real audio before being discarded.
7. **BTrack's `--backend-lib` plugin requires `-rdynamic` on the CLI** for the
   allocation counter to see plugin allocations; without it the count would read
   0 for reasons unrelated to the backend.

## Integration notes

**What the tracker ADR must decide (G3):**

1. **Compensation.** Whether to subtract a documented latency before judging a
   backend, and by how much. The measured effect here is real but does not flip
   any gate for BTrack; the harness now makes the decision visible rather than
   making it. The orchestrator owns it (stated in `summary-btrack.md`).
2. **Whether a 2.3 % tempo bias is disqualifying.** BTrack's residual is a
   property of the 44.1 kHz work-around, not of the corpus or the gate. The ADR
   must decide if that is acceptable or if aubio/BeatNet must be tried; the
   corpus can measure it but cannot decide it.
3. **The silence-vs-holdover semantics** (limitation #1): does a tracker that
   holds time through a real stop count as correct (no audible restart) or as
   manufacturing beats? This decides the sparse fixture's reading and belongs in
   the ADR, not in the harness.

**What the next backend (aubio) must provide:**

- The same `jam_rhythm_create`/`jam_rhythm_destroy` dlopen plugin, or a
  link-time target; the harness knows only `IRhythmTracker`.
- Its documented output latency, so it can be scored with and without
  `--compensate-latency`; "negligible" must be justified, not assumed.
- Beats on the feed-rate clock (`RhythmObservation::inputSampleTime` overridden
  by the runner); internal resampling latency is subject to the latency rule.
- `phaseValid` honestly true only when the phase is usable; BTrack's adapter
  already does this.
- Determinism across repeated runs.

The `ScoringVariant`/`--compensate-latency` machinery is backend-agnostic and
needs no change to compare aubio: run the same CLI against its plugin and the
same variants will be produced.

## Final commit SHA

- Implementation commit: `798aa4f32d5dcaed60bb3eb7ec9f05c3566e0f9a`
  (`feat(eval-002r): true-silence false-beat metric, latency compensation, real
  BTrack results`).
- This note is the subsequent commit on `wp/EVAL-002R`; the branch head is the
  handoff SHA reported to the orchestrator.
