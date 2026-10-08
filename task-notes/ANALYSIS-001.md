# ANALYSIS-001 — live RhythmAnalyzer worker (subset)

## Local review completion after provider interruption

Flash stopped with `Insufficient Balance` during correction of `20a015a`.
Preserved its owned-path edits and completed review locally: corrected running
publication, input-horizon semantics, wrap comparison, restart stale-audio
discard, per-session output drops and plugin-deleter validation. Also made
test-latch notifications synchronize with predicates, forwarded observation
representation explicitly and verified every field's bits, constrained failure
text to joined-owner access, and scoped the clock example to offline replay.

Independent corrected execution: **29 tests /1210 checks pass**, including the
100-iteration early-failure regression; the same suite passes under ThreadSanitizer
with no reported races. Benchmark runs8 cases with current pinned plugins;
throughput cases61–75× realtime with zero drops/overruns. Pressure retains32 and
reports exact envelope loss; failed ring-push attempts in that offline retrying
producer are not unique lost audio blocks. Strict benchmark input/backends/output
checks (5 Python tests) pass. Exact source/binary/log pins and results are under
`tools/analysis-worker-benchmark/results/integration-verification.json`.

The broader task remains PARTIAL; production tracker and live processor-clock
wiring await their gates. Future integration must stamp actual receipt availability.

## Goal and base

Build the platform-neutral rhythm-analysis worker of DEVPLAN ANALYSIS-001: a
single non-RT thread that drains `AnalysisAudioRing`, runs an injected
`IRhythmTracker`, and republishes every observation onto a second bounded SPSC
queue for a future clock/audio consumer — with a clean lifecycle and explicit
bounded-publication semantics.

- Worktree: `/home/mojo/projects/worktrees/ANALYSIS-001-worker`
- Branch: `wp/ANALYSIS-001-worker`
- Base: `af8b77a`
- Orchestrator-authorised deviation: this is the **tracker-neutral core** subset
  so no production tracker is selected and no processor/clock wiring is done.
  D6: no chosen backend, no application clock wire.

## Scope (owned paths only)

```
src/jam/RhythmAnalyzer.h
src/jam/RhythmAnalyzer.cpp
tests/jam/RhythmAnalyzerTests.cpp
tools/analysis-worker-benchmark/**
docs/research/ANALYSIS-WORKER.md
task-notes/ANALYSIS-001.md
```

No existing type, ring, tracker, clock, `PluginProcessor.*`, editor,
`DrumEngine`, CMake file, workflow, ledger, handoff, vendor or corpus file was
modified.

## What was done

- `RhythmAnalyzer` takes an `AnalysisAudioRing&` (lives until `stop()`) and an
  injected `IRhythmTracker` owned through a `unique_ptr`, constructed off the
  audio thread. A second constructor owns a `dlopen`ed plugin backend through its
  `jam_rhythm_destroy` deleter; a non-null tracker with a null deleter throws
  `std::invalid_argument` and **never** falls back to `delete` for a plugin
  object (validated before the owning member is constructed).
- Lifecycle: `start()` rejects a duplicate start, a null tracker and a
  non-finite/non-positive rate; `stop()` signals, notifies and joins; the
  destructor joins as a backstop. `running_` is published **before** the thread
  launch and never written by `start()` afterwards, so an immediate worker
  failure cannot be masked by a later store; the worker's exit store is
  definitive.
- The audio callback path is untouched: the worker polls, using a 1 ms bounded
  `condition_variable` idle wait and a bounded drain (256 frames/iteration).
- Publication reuses `jam::rt::CommandQueue` (bounded, allocation-free, drop-
  incoming-and-count), preserving beat events rather than coalescing to a latest
  blob. Capacity is 32. `droppedObservations` is the **per-session** delta;
  `lifetimeDroppedObservations` is cumulative; `ringOverruns` is the ring's
  cumulative, pre-existing counter.
- Each envelope carries the backend observation **unaltered** plus
  `streamGeneration`, `blockStartSampleTime`, `inputHorizonSampleTime` (the
  input block's end: an audio-data **horizon lower bound**, not a fabricated live
  availability), `sourceSampleRate`, `sequence` and `availabilityMeasured`
  (always false here). `observationWithinInputHorizon()` is a wrap-safe
  bounded-distance predicate, not a temporal availability claim.
- Continuity: a frame gap, an out-of-order frame, a rate change, and a
  non-finite/non-positive rate all reset the tracker worker-side, bump the
   generation and tag old-generation evidence for consumer rejection. The frame's declared device rate
  is passed to `reset()`/`process()` — no new resampling wrapper, so the
  adapter's own rate conversion is preserved. Timestamp wrap is exact
  `uint64_t` arithmetic and is not a false gap.
- A throwing `reset()`/`process()` sets a failure flag and message and lets
  `stop()` join; `IRhythmTracker::process`'s non-blocking contract is documented.
  `failureMessage()` returns an object-owned pointer stable until the next
  failure, safe only for the owner after join (documented), avoiding an
  allocating copy in the diagnostic path.
- Restart: the first `start()` preserves prequeued audio; every later restart
  flushes queued evidence AND discards queued audio (counted as
  `discardedAudioBlocks`/`discardedAudioFrames`), so a previous stream frame
  cannot be processed under the new generation. Producer and consumer must be
  quiescent (documented).

## Tests executed

`tests/jam/RhythmAnalyzerTests.cpp` (29 tests, 1210 checks, 0 failures), run via
the platform-neutral `jamTests` binary.

```
$ ctest -R jam.RhythmAnalyzer
jam.RhythmAnalyzer  Passed
29 tests, 1210 checks, 0 failed check(s) in 0 test(s)
```

Full `jam-core` ctest suite, GCC 14.2.0, CMake 3.31.6, Unix Makefiles, Release,
two lanes:

```
OFF (build-ANALYSIS-001):           100% tests passed, 0 tests failed out of 15
ON  (build-ANALYSIS-001-backends):  100% tests passed, 0 tests failed out of 17
```

These full-suite counts are the initial worker's historical report; current-main
full verification is recorded during integration. The corrected analyzer suite
and a limited ThreadSanitizer run were independently executed and passed.

Coverage includes: 20× start/stop; duplicate/invalid/null rejection;
plugin-ownership null-deleter rejection; destructor join; in-order unaltered
publication; all beats retained; the input horizon vs event time and the
wrap-safe within-horizon predicate (wrap, equal, future +1, old out-of-range);
a blocked backend proving the horizon is the frame end, not a current-master
fabrication; gap / out-of-order / rate-change / invalid-rate resets; `uint64_t`
wrap; per-session queue pressure with `enqueued + drops == processed`;
per-session vs lifetime drop counters; ring-overrun surfacing; reproducible
restart, audio discard and flush; a 100× first-frame failure with no `running`
resurrection; throwing reset (start and discontinuity) and process; bounded stop
with a slow backend; a concurrent producer/consumer stress run with thread-ID
ownership checks; and a synchronous offline `MusicalClock` replay advancing on
the input horizon without moving backwards.

## Benchmark (throughput only; not a gate)

`tools/analysis-worker-benchmark/run_benchmark.sh` compiles this worktree's
worker, `dlopen`s the read-only EVAL-005 BTrack/aubio plugins — no GPL code is
compiled into the binary; the loaded plugins' own licences still apply — and
feeds a deterministic 120 BPM click train in bounded bursts.

Throughput mode (5 s, 128-frame blocks, ring 64, burst 32), wall vs CPU:

| backend | rate | wall s | CPU s | realtime × |
|---|---|---|---|---|
| btrack | 44100 | 0.075 | 0.095 | 66× |
| btrack | 48000 | 0.082 | 0.101 | 61× |
| aubio | 44100 | 0.067 | 0.074 | 75× |
| aubio | 48000 | 0.074 | 0.081 | 68× |

`ringOverruns == 0` and no queue drops in every throughput run. The throughput
`wall_seconds` includes the burst hand-off and the idle poll between bursts (a
conservative floor); `cpu_seconds` is process CPU across threads. Pressure mode
keeps the **first 32** envelopes, drops the rest and reports the matching
`droppedObservations`. Plugin/source SHA-256 are in
`tools/analysis-worker-benchmark/results/manifest.txt`; the results directory is
≪ 1 MiB.

## Threads linkage

No CMake file was edited. `jam-core/CMakeLists.txt` already finds `Threads` and
links `Threads::Threads` PUBLIC, and `jamTests` links `jam-core`; the benchmark
script links `-pthread` and `-ldl`. `Threads::Threads` is the portable
requirement — modern glibc folds pthread into libc, which is why an ad-hoc link
may appear to work without `-pthread`. **No shared CMake patch is needed.**

## Review round (on `20a015a`)

All five review items were applied on the same owned paths:

1. **Running race** — `running_` is now published before the launch and the
   worker's exit store is definitive; `threadStartFailed` resets it. Added the
   100× first-frame-failure test.
2. **Live availability** — renamed to `inputHorizonSampleTime`, added
   `availabilityMeasured=false`, documented it as a lower bound, replaced the
   direct `<=` with a wrap-safe bounded-distance predicate, added wrap/future/
   old-range tests and a blocked-backend test proving the horizon is the frame
   end, not a current-master availability.
3. **Restart** — restarts discard queued audio (counted) and flush evidence; the
   first start preserves prequeued audio; per-session drop deltas added with a
   lifetime total.
4. **failureMessage** — no longer mutated by `start()`; documented as owner
   after join/quiescent only; lock-free static asserts and a pop-empty test added.
5. **Benchmark wording / lanes** — "no GPL compiled in" (not "no GPL linkage"),
   wall-vs-CPU and first-32-not-latest documented, both backends-ON ctest lane
   executed (17/17) and the OFF lane (15/15).

## Limitations / handoff

- **PARTIAL.** Broader ANALYSIS-001 awaits the selected backend and the
  processor/clock integration wire (INT-ANALYSIS-001). No tracker selected, no
  production wiring, no ADR; G3 remains open.
- Read-only plugin inputs:
  `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-{btrack,aubio}.so`
  (SHA-256 recorded in the manifest).
- The clock-consumer test is not a musical-validity proof; the horizon is a
  lower bound, and a future integration must stamp real availability from a
  device/sample master or consumer receipt. The benchmark is synthetic and
  non-device and makes no latency/dropout claim.
- `AnalysisAudioRing` clips producers to 2048 frames; callers with larger device
  blocks must bound their tap into ≤ 2048-frame chunks before integration.

## Commit provenance

Initial handoff: `20a015a` (implementation `19a2f86`). Review edits were completed
locally after the worker interruption and committed separately; integration
records the accepted correction SHA in the execution ledger and handoff.
