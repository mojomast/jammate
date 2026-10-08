# DIAG-001

## Goal

Portable, JUCE-free diagnostics foundation for rhythm analysis (SPEC.md 22,
DEVPLAN DIAG-001). Injected-core only. Production processor/analyzer/clock
integration is pending (INT-ANALYSIS-001), so **DIAG-001 remains partial** and
**G4 is not closed**.

Worktree: `/home/mojo/projects/worktrees/DIAG-001-core`.
Branch: `wp/DIAG-001-core`.

## Base commit

`bcf540a` (`docs: record Windows CI pass and switch delegated work to Go Haiku`).

## Files changed (owned only)

- `src/jam/Diagnostics.h` (new)
- `src/jam/Diagnostics.cpp` (new)
- `tests/jam/DiagnosticsTests.cpp` (new)
- `docs/research/JAM-DIAGNOSTICS.md` (new)
- `task-notes/DIAG-001.md` (new)

No existing source, CMake, ledger, HANDOFF, DEVPLAN, processor, editor or engine
file was edited. No `tools/jam-diagnostics/` was created, because no tool was
needed.

## Contract implemented

- Typed `DiagnosticsEvent` (256 bytes, trivially copyable) that wraps the
  unaltered `ObservationEnvelope` and an optional `ClockSnapshot`. Optional
  external values use `MeasuredField<T>` and stay unmeasured unless attached.
- Ordered trace: `rt::CommandQueue<DiagnosticsEvent, 256>`, drop-incoming-and-count.
- UI telemetry: `rt::LatestValue<DiagnosticsEvent>`, one attempt per read, whole
  events only. It is never a substitute for trace history.
- Tracing off: `publish()` does one acquire load, a relaxed skip count, and
  returns. Nothing is copied or enqueued.
- Lossless uint64 provenance (sample times, horizon, generation, sequence).
- The input horizon is never presented as live availability. Receipt is
  recorded only via `attachLiveReceipt()` from an external measurement.
- Non-finite inputs are counted in `invalidFields` and serialised as null/empty.
- Non-RT `DiagnosticsCollector` (bounded by `maxEvents`) with `drain()` (at most
  256 per call), plus `toCsv()` and `toJson()` (schema version 1).
- Lifecycle: `reset()` discards queued events and bumps the session. The
  caller must have quiesced the publisher and consumer.

## Lifecycle limits

- Publisher is one thread. Consumer is one thread. Reader is one UI thread.
- `reset()` and `collector.clear()` require quiescence.
- Trace capacity 256. Drain at least once per 256 published events, or drops
  are counted.
- Memory: `DiagnosticsTrace` is 66,240 bytes, so allocate it on the heap.
- No file I/O in the library. The caller writes exported text.

## Verification

Build scratch: `/home/mojo/projects/guitars-build-resume/tmp/diag001-build`
(`cmake -S jam-core -B ... -G Ninja`, with `PATH=/tmp/opencode/venv/bin:$PATH`
and `TMPDIR=/home/mojo/projects/guitars-build-resume/tmp`).

Commands and results:

- `cmake --build <scratch> --target jamTests`: builds with no warnings.
- `jamTests Diagnostics.`: 20 tests, 1015 checks, 0 failed.
- `jamTests` (full): 167 tests, 211,637 checks, 0 failed.
- `ctest -R "jam.Diagnostics|jam.RtSignal|jam.AnalysisAudioRing"`: 3/3 passed.
- `g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -c` on `Diagnostics.cpp` and
  `DiagnosticsTests.cpp`: clean.
- ThreadSanitizer build of the Diagnostics tests (`-fsanitize=thread`): 0
  warnings.
- ASan + UBSan build of the Diagnostics tests (`-fsanitize=address,undefined`): 0
  errors.

Two issues came up and were fixed during verification:

1. The first TSan run failed the allocation probe. Under TSan, glibc's
   `mallinfo2` does not reflect the allocator, so the probe is now skipped under
   sanitizers. It still runs in the normal build, with a self-check that confirms
   it sees a real allocation.
2. The jam test harness cannot print an enum-class value. Those assertions were
   changed to plain `CHECK(... == ...)`.

## Registration

No CMake change needed. jam-core auto-globs `src/jam/*.cpp` and
`tests/jam/*.cpp`, so the suite registers as ctest `jam.Diagnostics`. No Python
tool was added, so no Python registration is needed.

## Not done / open

- Processor and analyzer wiring into `publish()` (INT-ANALYSIS-001). Still
  pending.
- Overhead measurement with tracing on and off in the real callback path.
  Still pending. The "negligible overhead" criterion is not yet proven.
- `ringOverruns`, `callbackLatency` and `processingDuration` are only available
  once the integrator supplies measured values. Until then they stay null.
- G4 remains open. DIAG-001 remains partial.
