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
| Publisher (one thread, analysis/control) | `DiagnosticsTrace::publish()` | Bounded, non retrying. Never call from a device callback. |
| Consumer (one thread, non-RT exporter) | `DiagnosticsTrace::pop()`, `DiagnosticsCollector::drain()` | Allocates. Off the audio path. |
| Reader (one UI thread) | `DiagnosticsTrace::readViewModel()` | Latest-value telemetry only. |
| Any thread | `setEnabled()`, `counters()` | Relaxed multi-field sample, not one atomic snapshot. |
| Lifecycle (all roles quiescent) | `reset()`, `clear()` | See "Lifecycle contract". |

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
- `clockKnown` + `clock` (`ClockSnapshot`). Attached only when supplied.
- `MeasuredField<T>` (`value` + `measured`) for values that exist only when
  someone measured them: `liveReceiptSampleTime`, `callbackLatencySeconds`,
  `processingDurationSeconds`, `ringOverruns` (cumulative
  `AnalysisAudioRing` overruns) and `observationDrops` (cumulative
  `RhythmAnalyzer` output-queue drops). The last two come from the integrator.
- `invalidFieldMask` + `invalidFieldCount()`: one bit per domain-invalid field.

Both `observation.sourceSampleRate` and `envelope.sourceSampleRate` are exported
as separate columns, so a rate disagreement between the backend and the capture
device stays visible instead of one silently replacing the other.

## Domain policy

Raw evidence is **always preserved in the record**. Only the export masks an
invalid field to null/empty, and `invalid_field_count` records how many fields
were masked. So a bad value can still be diagnosed from the in-memory record
without ever being written into an export.

| Field | Accepted | Rejected |
|---|---|---|
| `sourceSampleRate` (observation and envelope) | finite, > 0 | 0, negative, non-finite |
| `bpmCandidate`, `clock.bpm` | finite, >= 0 (0 means "unknown") | negative, non-finite |
| `beatPhase01`, `beatConfidence01`, `onsetStrength01`, `transientDensity01`, `clock.beatPhase01`, `clock.barPhase01`, `clock.confidence01` | finite, within [0, 1] | < 0, > 1, non-finite |
| `energyRmsDbfs` | finite, <= 0 | positive, non-finite |
| `clock.beatsPerBar`, `clock.beatUnit` | >= 1 | 0, negative |
| `clock.beatInBar` | 0 (not yet known) or within [1, beatsPerBar] | negative, beyond the bar |
| `callbackLatencySeconds`, `processingDurationSeconds` | finite, >= 0 | negative, non-finite |

The policy is expressed once, in `Diagnostics.h`, by the helpers
`isPositiveRate`, `isTempoValue`, `isUnitInterval`, `isDecibelFullScale`,
`isNonNegativeSeconds`, `isPositiveMeter` and `isKnownBeatInBar`, so the count
and the export mask cannot disagree.

Repeated attachments count once: validity is a property of the field, not of the
attempt. Attaching an invalid value twice marks one field; attaching a valid
value afterwards clears the mark.

`validate()` is a **recomputation**, not an accumulation. It clears the evidence
bits (observation, envelope and clock) and then re-marks whatever the raw values
now violate, so:

- correcting a raw value in place (a rate from `-48000` back to `48000`) and
  calling `validate()` unmasks it;
- re-attaching a valid clock clears every previous clock mark;
- a record whose clock is no longer known loses its clock marks, because the
  clock bits belong to the recomputed range.

The duration bits are deliberately **not** cleared by `validate()`. A rejected
measurement is not derivable from the stored value, since a rejected attachment
stores an unmeasured field, which is indistinguishable from never having attached
one. Only `attachCallbackLatency()` / `attachProcessingDuration()` own those bits.
The two ranges are kept disjoint by `kEvidenceInvalidFieldMask` and
`kDurationInvalidFieldMask`, with static asserts on the bit layout and on the
non-overlap.

### Rejected measurements clear the field

`attachCallbackLatency()` and `attachProcessingDuration()` do **not** keep a
previously measured value when the new value is rejected. The field returns to
unmeasured and zeroed, and is marked invalid. Otherwise an export could show a
stale valid reading next to a rejected measurement, which would look like the
rejected value had been accepted.

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
  set by `attachLiveReceipt()` from an externally measured time.
- **Unmeasured stays unmeasured.** Callback latency, processing duration and live
  receipt are null (JSON) or empty (CSV) with `*_measured` = `false`. Never 0.

## Locale independence

Numeric serialisation goes through `std::to_chars`, which is specified never to
consult the C locale. No global locale is changed, and no locale-aware stream is
used on the export path.

This matters: a comma decimal point would emit `0,30000000000000004`, which is
invalid JSON and a broken CSV row. Measured with a generated `de_DE.UTF-8`
locale on this machine:

```
snprintf("%.17g")  ->  0,30000000000000004     (invalid)
std::to_chars      ->  0.30000000000000004    (correct)
```

The source fails to compile if floating-point `std::to_chars` is unavailable,
rather than silently falling back to a locale-sensitive formatter.

## Sizes and memory scope

| Item | Size |
|---|---|
| `DiagnosticsEvent` | 256 bytes |
| `DiagnosticsTrace` object (queue + latest value + counters) | 66,240 bytes |
| Trace queue capacity | 256 events (`kTraceCapacity`) |
| Collector | growable `std::vector`, bounded by `maxEvents`, 256 bytes per retained event |

Allocate `DiagnosticsTrace` on the heap (`std::make_unique`), not on the stack.

Allocation scope, stated precisely:

- `publish()` and `pop()` perform no heap work. The evidence for this is a
  **net** measurement: glibc `mallinfo2().uordblks` shows no change across 20,000
  publish/pop pairs, 10,000 disabled publishes, and 512 queue-filling publishes.
  A probe self-check confirms the method sees a real allocation first.
  This is **net evidence, not a proof of zero allocation**: a malloc immediately
  followed by a matching free would be invisible to it. What it does rule out is
  retained growth, which is the realistic failure mode for a publication path.
  The probe is skipped under sanitizers (they replace malloc) and on platforms
  without `mallinfo2`, and prints a note when skipped rather than reporting a
  pass.
- **The drain path does allocate.** `DiagnosticsCollector::drain()` may grow its
  `std::vector` when it retains an event. That is the consumer role, off the
  audio path, and is intentional.
- `toCsv()` / `toJson()` allocate, and are off the RT path.

Locking: `publish()` and `pop()` contain no mutex and no lock primitive, only the
lock-free atomics of `rt::CommandQueue` and `rt::LatestValue`. This is a
source-level statement about the code, not a runtime lock measurement; no
lock-contention timing has been taken.

## Drain cadence, capacity and drops

- Drain at least once per `kTraceCapacity` (256) published events, otherwise the
  trace queue refuses incoming events and counts them.
- `DiagnosticsCollector::drain()` pops **at most 256 events per call**, so
  emptying a full queue takes more than one call. The cadence rule follows from
  that bound.
- A publisher retrying after a refusal is counted as a drop, so
  `published + dropped == attempts` holds for any publisher that tries only via
  `publish()`. This identity is asserted by a test.
- Drops are never hidden. Every export reports the trace queue drops as of the
  last drain (`trace_dropped_at_drain`) and the collector's own drops when
  `maxEvents` was reached (`collector_dropped`). CSV repeats both on each event
  row; JSON carries them once at the top level.
- Counters are per session. `reset()` zeroes them and bumps the session number,
  re-baselining the drop count so nothing underflows.

## Export format

Schema version `kSchemaVersion` = **1**. It was frozen before any first
integration existed: no consumer has parsed this format, because the trace is not
wired into the processor yet, so the layout was settled while still uninhabited.
Any later layout change must bump it (SPEC.md 23).

### CSV

A metadata preamble, then a header line, then one row per retained event:

```
# jam-diagnostics-csv
# schema_version=1
# trace_capacity=256
# events_retained=2
# trace_dropped_at_drain=5
# collector_dropped=10
session,sequence,...,clock_tempo_frozen,trace_dropped_at_drain,collector_dropped
1,1,...,0,5,10
```

- Preamble lines start with `"# "`. The first is the format marker, the rest are
  `key=value`. A reader should skip `#` lines and use the preamble as the
  summary.
- **The preamble is written even when zero events are retained**, so a collector
  with `maxEvents == 0`, or one that lost everything to queue pressure, still
  reports its losses. Without it, a CSV whose only data was the per-row drop
  columns would silently hide all loss.
- The header has `kEventColumnCount + 2` = **42** columns: 40 event columns plus
  `trace_dropped_at_drain` and `collector_dropped`.
- Missing, unknown or domain-invalid values are empty cells. Text fields use RFC
  4180 quoting. Booleans are `1`/`0`. Rows are LF-separated.

### JSON

One document: `schema_version`, `trace_capacity`, `events_retained`,
`trace_dropped_at_drain`, `collector_dropped`, then an `events` array whose keys
match the CSV column names. Missing or domain-invalid values are `null`,
booleans are `true`/`false`, strings are escaped. The summary is present at the
top level regardless of how many events were retained.

Both exporters are non-RT only, after the publisher has stopped. The library
writes no files; the caller does the file I/O.

## Lifecycle contract

1. Construct `DiagnosticsTrace` (heap) and `DiagnosticsCollector(maxEvents)`.
   Tracing starts disabled.
2. `setEnabled(true)` at session start.
3. The publisher thread calls `publish()`. The consumer thread calls `drain()`
   periodically, and the UI reads `readViewModel()`.
4. **Stop and join the publisher.** Only then call `setEnabled(false)`.
5. Final `drain()`, then `toCsv()` / `toJson()`.
6. For a new session, with all roles quiescent, call `reset()` and
   `collector.clear()`. `reset()` discards queued events and returns the count.

Limits, and why they are hard:

- `setEnabled(false)` is **not** publisher quiescence. A `publish()` already in
  flight can still enqueue after it returns. The owner must stop and join the
  publisher first; disabling only stops new calls from being accepted. Events
  already queued are never discarded by disabling.
- `reset()` and `clear()` require **all** roles to be quiescent: publisher,
  consumer, UI reader and any `counters()` reader. `counters()` samples
  `dropped_` and `dropBase_` independently; a reader crossing a reset could see a
  pre-reset drop count against a post-reset baseline, which underflows when
  subtracted. Making the UI and counter readers part of the precondition is the
  simplest way to keep the arithmetic honest.
- `readViewModel()` supports one reader; its cache fields are owned by it.

## Verification

Build scratch: `/home/mojo/projects/guitars-build-resume/tmp/diag001-build`
(`/tmp` is full, so the brief's build area was used). `PATH` and `TMPDIR` as in
the brief.

- `jamTests Diagnostics.`: **34 tests, 1474 checks, 0 failed**.
- Full jam binary: **181 tests, 212,096 checks, 0 failed**.
- `ctest -R "jam.Diagnostics|jam.RtSignal|jam.AnalysisAudioRing"`: 3/3 passed.
- `-Wall -Wextra -Wpedantic -Werror` on `Diagnostics.cpp` and
  `DiagnosticsTests.cpp`: clean.
- ThreadSanitizer (`-fsanitize=thread`): 0 warnings, 34 tests / 1468 checks
  (fewer checks: the net-heap probe is skipped under sanitizers).
- AddressSanitizer + UBSan (`-fsanitize=address,undefined`): 0 errors, 0 leaks,
  34 tests / 1468 checks.
- Locale independence: with a generated `de_DE.UTF-8` installed and selected
  thread-locally via `uselocale`, the locale test runs its full fixture
  (14 checks instead of 11) and passes. With no comma-decimal locale installed it
  prints a skip note for the fixture and still asserts the exact numeric strings.
  Under ASan, that pass needs `detect_leaks=0`, because glibc's own locale
  allocation is reported as a leak; the leak is in glibc, not in this code.

### Defects found and fixed during review

- **Stale invalid-field masks.** `validate()` set bits but never cleared them, so
  a value that had been corrected (a rate from `-48000` to `48000`), a clock
  re-attached with valid values, or a withdrawn clock kept masking a now-valid
  field and kept inflating `invalid_field_count`, contradicting the documented
  "valid after invalid clears the mark". `validate()` now clears the evidence
  range before recomputing it, while duration bits stay owned by their
  attachments.
- **Dangling reference in the test harness.** `JsonParser` held
  `const std::string&` and was routinely constructed from a temporary
  `toJson(...)`. That is undefined behaviour and produced erratic failures in
  tests that had passed. The parser now owns its text.
- **Unbounded drain assumption.** Tests assumed one `drain()` call empties the
  queue; it is bounded at 256 events per call. Tests now drain in a bounded loop,
  which is also what the cadence rule requires.
- **Wrong drop expectations.** A collector that is already full counts every
  further event as dropped, so overflow totals were lower than the real counts.
  Assertions now use a fresh collector per phase.
- **Scheduler race in the quiescence test.** The publisher thread could start
  after `stop` was already set, publishing nothing. The test now uses a
  `produced`/`attempts` handshake and yields instead of spinning, which also
  removes the flake under ASan.

## Registration

No CMake change is needed. `jam-core/CMakeLists.txt` globs `src/jam/*.cpp` and
`tests/jam/*.cpp`, and the suite name `Diagnostics` (from
`JAM_TEST(Diagnostics, ...)`) is registered automatically as ctest
`jam.Diagnostics`. No Python tool was added, so no Python registration is
needed. The orchestrator owns any top-level CMake change.

## Open items (not done here)

- Wire the analyzer output and the clock snapshot into `publish()` from the
  control thread (INT-ANALYSIS-001).
- Supply `ringOverruns` from the audio-origin counters, and `callbackLatency`
  and `processingDuration` from real measurements. Until then they stay null.
- Measure publish overhead in the RT path with tracing on and off. The
  "negligible overhead" criterion is unproven.
- Add a file exporter outside the library, off RT, if a CLI or dev view is
  wanted.