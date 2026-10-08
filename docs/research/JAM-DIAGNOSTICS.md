# Jam rhythm diagnostics and trace (DIAG-001)

Status: **partial**. This is the injected-core foundation only. Nothing in the
plugin, the analyzer or the Musical Clock publishes to it yet. The production
processor integration and the callback-overhead measurement are still pending
(INT-ANALYSIS-001, Gate G4). G4 is not closed by this work.

Sources: `src/jam/Diagnostics.h`, `src/jam/Diagnostics.cpp`,
`tests/jam/DiagnosticsTests.cpp`. Spec: SPEC.md section 22; DEVPLAN DIAG-001.

## Purpose

Developer-only trace for "the drummer felt wrong" reports (SPEC.md 22). It
captures the rhythm evidence and clock state so a session can be replayed and
compared, and it exports CSV and JSON after the session.

## Thread roles

| Role | Entry point | Constraints |
|---|---|---|
| Publisher (one thread, analysis/control) | `DiagnosticsTrace::publish()` | Bounded, no allocation, no lock, no retry. Never call from a device callback. |
| Consumer (one thread, non-RT exporter) | `DiagnosticsTrace::pop()`, `DiagnosticsCollector::drain()` | Allocates. Runs off the audio thread. |
| Reader (one UI thread) | `DiagnosticsTrace::readViewModel()` | Latest-value telemetry only. |
| Lifecycle | `reset()`, `clear()` | Publisher and consumer both quiescent. The caller plays the consumer while discarding. |
| Any thread | `setEnabled()`, `counters()` | Relaxed multi-field sample, not one atomic snapshot. |

## Two channels, kept separate

1. **Ordered trace evidence**: `rt::CommandQueue<DiagnosticsEvent, 256>`. SPSC,
   drop-**incoming**-and-count when full. Accepted events are delivered in
   publication order. Queued events are never overwritten.
2. **Latest-value UI telemetry**: `rt::LatestValue<DiagnosticsEvent>`. A reader
   can miss intermediate values. Each read is a single attempt, and a raced read
   keeps the previous value. Only whole events are returned.

The UI never reads the trace queue, and the trace is never reconstructed from
the latest value.

## Event record

`DiagnosticsEvent` is trivially copyable, 256 bytes (measured).

- `envelope` (`ObservationEnvelope`, unaltered evidence): `observation`
  (`inputSampleTime`, `sourceSampleRate`, `bpmCandidate`, `beatPhase01`,
  `beatConfidence01`, `onsetStrength01`, `energyRmsDbfs`, `transientDensity01`,
  `beatEvent`, `silence`, `phaseValid`), `streamGeneration`,
  `blockStartSampleTime`, `inputHorizonSampleTime`, `sourceSampleRate`,
  `sequence`, `availabilityMeasured`.
- `traceSession`: set by `publish()`. Scopes latest-value reads to one session.
- `clockKnown` + `clock` (`ClockSnapshot`): generation, bpm, beat/bar phase,
  beat in bar, meter, confidence, lock state, tempo-frozen. Attached only when
  supplied.
- `MeasuredField<T>` (`value` + `measured`) for values that exist only when
  someone measured them:
  - `liveReceiptSampleTime`: externally measured consumer receipt.
  - `callbackLatencySeconds`, `processingDurationSeconds`.
  - `ringOverruns` (cumulative `AnalysisAudioRing` overruns) and
    `observationDrops` (cumulative `RhythmAnalyzer` output-queue drops). These are
    supplied by the integrator.
- `invalidFields`: count of non-finite or out-of-range inputs captured as-is.

Build the event with `makeEvent(envelope)`, then add optional data with
`attachClock`, `attachRingOverruns`, `attachObservationDrops`,
`attachLiveReceipt`, `attachCallbackLatency`, `attachProcessingDuration`.

## Provenance policy

- **Lossless uint64.** Sample times, horizon, block start, generation and
  sequence are stored and exported as exact unsigned integers. Wrap of the device
  clock is preserved, not normalised. CSV writes decimal digits. JSON writes
  unquoted integers, which is valid JSON with exact digits. A JavaScript consumer
  must parse them as BigInt or string, because doubles lose precision above 2^53.
- **Horizon is not availability.** `inputHorizonSampleTime` is the audio-data
  horizon, a lower bound on causality. It is never copied into a receipt or
  availability field. `availabilityMeasured` is passed through from the envelope
  and stays `false`. The only live-availability value is `liveReceiptSampleTime`,
  set by `attachLiveReceipt` from an externally measured time. The export column
  for it is null unless measured.
- **Unmeasured stays unmeasured.** Callback latency, processing duration, and
  live receipt are null (JSON) or empty (CSV) with `*_measured` = `false`. They
  are never 0. A non-finite or negative duration is rejected, leaves the field
  unmeasured, and increments `invalidFields`.

## Invalid, non-finite and missing values

- Non-finite inputs are kept as the raw evidence and counted in
  `invalidFields`. Serialisation renders them as null (JSON) or empty (CSV).
  `NaN` and `Inf` never appear in the output text. The tests check this for both
  formats.
- `sourceSampleRate` must be finite and positive to count as valid.
- Missing values: an absent clock writes null or an empty cell for all clock
  columns, and `clock_known` = `0`/`false`.
- The JSON parser in the tests is strict, so output that is not valid JSON
  (for example, a bare `nan`) fails the test.

## Tracing off

`publish()` does one acquire load of the enabled flag. When disabled it does a
relaxed load and store of a skip counter and returns. It copies nothing into
either channel and enqueues nothing. The skip count is an honest record of calls
made while off.

The overhead is not measured here. The acceptance criterion "negligible overhead"
in DEVPLAN DIAG-001 needs a measurement in the real callback path, which is
pending integration.

## Sizes and memory scope

| Item | Size |
|---|---|
| `DiagnosticsEvent` | 256 bytes |
| `DiagnosticsTrace` object (queue + latest value + counters) | 66,240 bytes |
| Trace queue capacity | 256 events (`kTraceCapacity`) |
| Collector | growable `std::vector`, bounded by `maxEvents`, about 256 bytes per retained event |

Allocate `DiagnosticsTrace` on the heap (`std::make_unique`), not on the stack.
Nothing in the publish or drain path allocates. Only the collector's
`push_back` (consumer role) and the export functions allocate.

## Drain cadence, capacity and drops

- Drain at least once per `kTraceCapacity` (256) published events. Otherwise the
  trace queue drops incoming events and counts them. At 48 kHz with 2048-frame
  analysis blocks, the relevant rate is the analyzer's output rate, not the audio
  rate. The output rate is at most one envelope per analyzed block.
- `DiagnosticsCollector::drain()` pops at most 256 events per call, so one call
  is bounded.
- Drops are never hidden. Every export reports the trace queue drops as of the
  last drain (`trace_dropped_at_drain`). It also reports the collector's own
  drops, when `maxEvents` was reached (`collector_dropped`). CSV repeats these
  two values on each row. JSON carries them once at the top level.
- Counters are per session. `reset()` zeroes them and bumps the session number.
  Dropped and published counts are relative to the queue's lifetime counter,
  so the baseline is captured at reset.

## Export

- `toCsv(collector)`: header then one row per event, LF line endings. Missing or
  non-finite values are empty cells. Text fields use RFC 4180 quoting
  (`csvEscape`). Booleans are `1`/`0`. The header has 41 columns: 39 event
  columns plus `trace_dropped_at_drain` and `collector_dropped`.
- `toJson(collector)`: one document with `schema_version` (1), `trace_capacity`,
  `events_retained`, `trace_dropped_at_drain`, `collector_dropped` and `events`.
  Event keys match the CSV column names. Missing or non-finite values are `null`.
  Booleans are `true`/`false`. Strings use `jsonEscape`.
- Both are non-RT only, after the publisher has stopped. The library writes no
  files. The caller does the file I/O.
- Schema version `kSchemaVersion` = 1. Bump it on any column or key change
  (SPEC.md 23).

## Lifecycle

1. Construct `DiagnosticsTrace` (heap) and `DiagnosticsCollector(maxEvents)`.
   Tracing starts disabled.
2. `setEnabled(true)` at session start.
3. The publisher thread calls `publish()`. The consumer thread calls `drain()`
   periodically, and the UI reads `readViewModel()`.
4. `setEnabled(false)` stops publication. Events already queued remain for the
   final drain.
5. Final `drain()`, then `toCsv()` / `toJson()`.
6. For a new session, quiesce the publisher and consumer, then call `reset()` and
   `collector.clear()`. `reset()` discards queued events and returns the count.
   Stale latest-value telemetry from the previous session is ignored.

Limits:
- `reset()` and `clear()` require quiescence. Calling them while the publisher
  or consumer is running is a data race.
- `readViewModel()` supports one reader. Its cache state is reader-owned.
- A tracker or analyzer that is still running must not be reset under the trace.

## Verification

Run from the worktree. The build directory is a small scratch area under
`guitars-build-resume/tmp`, not the full `/tmp`.

- `jamTests Diagnostics.`: 20 tests, 1015 checks, 0 failures (the sanitizer
  builds below report 1011 checks, because the allocation probe is skipped under
  sanitizers).
- Full jam binary: 167 tests, 211,637 checks, 0 failures.
- `ctest -R "jam.Diagnostics|jam.RtSignal|jam.AnalysisAudioRing"`: all pass.
- `g++ -Wall -Wextra -Wpedantic -Werror` on `Diagnostics.cpp` and
  `DiagnosticsTests.cpp`: clean.
- ThreadSanitizer (`-fsanitize=thread`) on the Diagnostics tests: no warnings.
- AddressSanitizer and UBSan (`-fsanitize=address,undefined`): no errors.

The allocation check uses glibc `mallinfo2` (in-use bytes) across 20,000
publish/pop pairs and 10,000 disabled publishes. It asserts zero change in
in-use bytes. A self-check first confirms the probe sees a real allocation. The
probe is skipped under sanitizers, and on platforms without `mallinfo2`. The
test prints a note when it is skipped, so it is not reported as passing.

## Registration

No CMake change is needed. `jam-core/CMakeLists.txt` globs `src/jam/*.cpp` and
`tests/jam/*.cpp`, and the suite name `Diagnostics` (from `JAM_TEST(Diagnostics, ...)`)
is registered automatically as ctest `jam.Diagnostics`. No Python tool is added,
so no Python registration is needed. The orchestrator owns any top-level CMake
change.

## Open items (not done here)

- Wire the analyzer output and the clock snapshot into `publish()` from the
  control thread (INT-ANALYSIS-001).
- Supply `ringOverruns` from the audio-origin counters, and `callbackLatency`
  and `processingDuration` from real measurements. Until then they stay null.
- Measure publish overhead in the RT path with tracing on and off.
- Add a file exporter (outside the library, non-RT) if a CLI or dev view is
  wanted.
