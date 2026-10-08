# DIAG-001

## Goal

Portable, JUCE-free diagnostics foundation for rhythm analysis (SPEC.md 22,
DEVPLAN DIAG-001). Injected-core only (D10). Production processor/analyzer/clock
integration is pending (INT-ANALYSIS-001), so **DIAG-001 remains partial** and
**G4 is not closed**.

Worktree: `/home/mojo/projects/worktrees/DIAG-001-core`.
Branch: `wp/DIAG-001-core`.

## Base commit

`bcf540a`.

## Commits

- `2a9a929` — initial foundation.
- Second commit on this branch — integration review corrections (see below).

## Files changed (owned only)

- `src/jam/Diagnostics.h`
- `src/jam/Diagnostics.cpp`
- `tests/jam/DiagnosticsTests.cpp`
- `docs/research/JAM-DIAGNOSTICS.md`
- `task-notes/DIAG-001.md`

No existing source, CMake, ledger, HANDOFF, DEVPLAN, processor, editor or engine
file was edited. No `tools/jam-diagnostics/` was created, because no tool was
needed.

## Contract implemented

- Typed `DiagnosticsEvent` (256 bytes, trivially copyable) wrapping the unaltered
  `ObservationEnvelope` plus an optional `ClockSnapshot`. Optional external values
  use `MeasuredField<T>` and stay unmeasured unless attached.
- Ordered trace: `rt::CommandQueue<DiagnosticsEvent, 256>`, drop-incoming-and-count.
- UI telemetry: `rt::LatestValue<DiagnosticsEvent>`, one attempt per read, whole
  events only, never a substitute for trace history.
- Tracing off: one flag load, a relaxed skip count, no copy, no enqueue.
- Lossless uint64 provenance including wrap; **both** the observation and the
  envelope source rate are exported.
- The input horizon is never presented as live availability. Receipt is recorded
  only via `attachLiveReceipt()` from an external measurement.
- Domain policy expressed once in the header, applied consistently to the
  invalid-field count and the export mask, with raw evidence preserved.
- Locale-independent numeric export via `std::to_chars`.
- Versioned CSV metadata preamble; drops are reported even with zero retained
  events.
- Non-RT `DiagnosticsCollector` (bounded by `maxEvents`) with `drain()` (at most
  256 per call), plus `toCsv()` and `toJson()` (schema version 1).

## Integration review corrections

1. **Domain policy made real.** The header and docs promised out-of-range values
   would export as null/empty, but only non-finite values were detected, so a
   finite rate of 0 or -1 was written out as a valid number. The policy is now
   defined once in `Diagnostics.h` (positive rates, unit-interval fields,
   `>= 0` BPM where 0 means unknown, `<= 0` dBFS, meter `>= 1`, `beatInBar` 0
   meaning not-yet-known) and applied to both the invalid-field count and the
   export mask. Raw values stay in the record; `invalid_field_count` reports what
   was masked.
2. **Both source rates exported** (`source_sample_rate_hz` and
   `observation_source_sample_rate_hz`), so a rate disagreement is no longer
   silently collapsed into one column. Column count 39 -> 40.
3. **Locale-independent numerics.** `snprintf("%g")` followed the global C locale
   and could emit `0,30000000000000004`, which is invalid JSON. Replaced with
   `std::to_chars`, plus an `#error` guard so a toolchain without
   floating-point `to_chars` fails to compile instead of silently regressing.
4. **Versioned CSV with a summary record.** The per-row drop columns disappeared
   when nothing was retained, so a zero-event export hid all loss. CSV now begins
   with a `# key=value` metadata preamble carrying `schema_version`,
   `trace_capacity`, `events_retained`, `trace_dropped_at_drain` and
   `collector_dropped`. Schema stays at **v1**, deliberately frozen before any
   first integration exists, so no consumer has parsed the old layout.
5. **Rejected measurements clear the field.** An invalid duration after a valid
   one kept the stale measured value, contradicting the documented policy.
   `attachCallbackLatency` / `attachProcessingDuration` now reset the field and
   mark it invalid. Repeated invalid attachments count once per field.
6. **Heap claim corrected.** `mallinfo2` reports net in-use bytes, so it cannot
   prove zero allocation. The test is renamed to `publishShowsNoNetHeapGrowth`,
   its comment and the docs now say net evidence rather than a zero-malloc proof,
   and no runtime lock measurement is claimed anywhere.
7. **Quiescence tightened.** `setEnabled(false)` is documented as *not* publisher
   quiescence: the owner must stop and join the publisher before disabling and
   before the final drain. `reset()` / `clear()` now require **all** roles
   quiescent, including the UI reader and `counters()` readers, because the drop
   count and its baseline are read independently and a crossing reader can
   underflow the subtraction. The allocation docs were also corrected: the drain
   path *does* allocate, since the collector's vector grows.

## Verification

Build scratch: `/home/mojo/projects/guitars-build-resume/tmp/diag001-build`
(`/tmp` is full). `PATH=/tmp/opencode/venv/bin:$PATH`,
`TMPDIR=/home/mojo/projects/guitars-build-resume/tmp`.

- `jamTests Diagnostics.`: **30 tests, 1172 checks, 0 failed**
- Full `jamTests`: **177 tests, 211,794 checks, 0 failed**
- `ctest -R "jam.Diagnostics|jam.RtSignal|jam.AnalysisAudioRing"`: 3/3 passed
- `-Wall -Wextra -Wpedantic -Werror` on both new .cpp files: clean
- ThreadSanitizer, 3 consecutive runs: 0 warnings, 30 tests / 1166 checks
- AddressSanitizer + UBSan, 3 consecutive runs: 0 errors, 0 leaks,
  30 tests / 1166 checks
- Locale independence proven with a generated `de_DE.UTF-8` selected via
  `uselocale`: the fixture engages (14 checks vs 11) and passes. Negative control
  shows `snprintf("%.17g")` produces `0,30000000000000004` under that locale while
  `to_chars` produces `0.30000000000000004`.

Fewer checks under the sanitizers because the net-heap probe is skipped there
(sanitizers replace `malloc`, so `mallinfo2` is meaningless) and prints a note
instead of reporting a pass.

### Defects found while verifying

- The test harness's `JsonParser` held a `const std::string&` bound to
  `toJson(...)` temporaries, i.e. a dangling reference and undefined behaviour.
  It produced erratic failures in tests that had already passed. The parser now
  owns its text.
- Tests assumed one `drain()` empties the queue; it is bounded at 256 per call.
- Overflow assertions ignored that an already-full collector counts every
  further event as dropped.
- The quiescence test had a scheduler race (the publisher could start after
  `stop`), which flaked under ASan. It now uses a `produced`/`attempts` handshake
  and yields instead of spinning.

## Registration

No CMake change needed. jam-core auto-globs `src/jam/*.cpp` and
`tests/jam/*.cpp`, so the suite registers as ctest `jam.Diagnostics`. No Python
tool was added, so no Python registration is needed.

## Not done / open

- Live processor, analyzer and clock wiring into `publish()` (INT-ANALYSIS-001).
- Overhead measurement with tracing on and off in the real callback path. The
  "negligible overhead" criterion is still unproven.
- `ringOverruns`, `callbackLatency` and `processingDuration` need real measured
  values from the integrator; until then they export as null.
- G4 remains open. DIAG-001 remains partial.