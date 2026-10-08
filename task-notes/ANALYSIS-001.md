# ANALYSIS-001 — live RhythmAnalyzer worker (subset)

## Goal and base

Build the platform-neutral rhythm-analysis worker of DEVPLAN ANALYSIS-001: a
single non-RT thread that drains `AnalysisAudioRing`, runs an injected
`IRhythmTracker`, and republishes every observation onto a second bounded SPSC
queue for a future clock/audio consumer — with a clean lifecycle and explicit
bounded-publication semantics.

- Worktree: `/home/mojo/projects/worktrees/ANALYSIS-001-worker`
- Branch: `wp/ANALYSIS-001-worker`
- Base: `af8b77a`
- Orchestrator-authorised deviation: this is the **tracker-neutral core** subset,
  so no production tracker is selected and no processor/clock wiring is done.

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
  `jam_rhythm_destroy` deleter, so the benchmark honours the plugin ABI.
- Lifecycle: `start()` rejects a duplicate start, a null tracker and a
  non-finite/non-positive rate; `stop()` signals, notifies and joins; the
  destructor joins as a backstop. Only non-RT callers create or join the thread.
- The audio callback path is untouched: the worker polls, using a 1 ms bounded
  `condition_variable` idle wait and a bounded drain (256 frames/iteration).
- Publication reuses `jam::rt::CommandQueue` (bounded, allocation-free, drop-
  incoming-and-count), preserving beat events rather than coalescing to a latest
  blob. Capacity is 32. `enqueuedObservations`, `droppedObservations` and the
  ring's `overrunCount` are surfaced in `stats()`.
- Each envelope carries the backend observation **unaltered** plus
  `streamGeneration`, `blockStartSampleTime`, `availabilitySampleTime` (end of
  input block = causal availability), `sourceSampleRate` and `sequence`, so
  event time and availability are distinct.
- Continuity: a frame gap, an out-of-order frame, a rate change, and a
  non-finite/non-positive rate all reset the tracker worker-side, bump the
  generation and dump old-generation evidence. The frame's declared device rate
  is passed to `reset()`/`process()` — no new resampling wrapper, so the
  adapter's own rate conversion is preserved. Timestamp wrap is exact
  `uint64_t` arithmetic and is not a false gap.
- A throwing `reset()`/`process()` sets a failure flag and message and lets
  `stop()` join; `IRhythmTracker::process`'s non-blocking contract is documented.
- `start()` flushes a previous session's queued evidence and bumps the
  generation, giving a reproducible restart; the consumer must be quiescent.

## Tests executed

`tests/jam/RhythmAnalyzerTests.cpp` (22 tests, 565 checks, 0 failures), run via
the platform-neutral `jamTests` binary.

```
$ ctest -R jam.RhythmAnalyzer
jam.RhythmAnalyzer  Passed
22 tests, 565 checks, 0 failed check(s) in 0 test(s)
```

Full `jam-core` ctest suite, built from the worktree with GCC 14.2.0 and CMake
3.31.6 (Unix Makefiles, Release), in scratch
`/home/mojo/projects/build-ANALYSIS-001`:

```
100% tests passed, 0 tests failed out of 15
```

Coverage includes: 20× start/stop; duplicate/invalid/null rejection; destructor
join; in-order unaltered publication; all beats retained; availability vs event
(including non-causal); gap / out-of-order / rate-change / invalid-rate resets
and generation bumps; `uint64_t` wrap; output-queue pressure with exact
`enqueued + dropped == processed`; ring-overrun surfacing; reproducible restart
and flush; throwing reset (start and discontinuity) and process; bounded stop
with a slow backend; a concurrent producer/consumer stress run with thread-ID
ownership checks; and a future clock consumer advancing on availability without
moving backwards.

## Benchmark (throughput only; not a gate)

`tools/analysis-worker-benchmark/run_benchmark.sh` compiles this worktree's
worker, `dlopen`s the read-only EVAL-005 BTrack/aubio plugins (no GPL linkage)
and feeds a deterministic 120 BPM click train in bounded bursts.

Throughput mode (5 s, 128-frame blocks, ring 64, burst 32), wall vs CPU:

| backend | rate | wall s | CPU s | realtime × |
|---|---|---|---|---|
| btrack | 44100 | 0.078 | 0.098 | 64× |
| btrack | 48000 | 0.081 | 0.102 | 61× |
| aubio | 44100 | 0.079 | 0.086 | 64× |
| aubio | 48000 | 0.070 | 0.078 | 71× |

`ringOverruns == 0` in every throughput run. Pressure mode drops
`processed − 32` envelopes and reports the matching `droppedObservations`.
Plugin/source SHA-256 are in
`tools/analysis-worker-benchmark/results/manifest.txt`; the whole results
directory is 5.9 KiB (≤ 1 MiB budget).

## Threads linkage

No CMake file was edited. `jam-core/CMakeLists.txt` already finds `Threads` and
links `Threads::Threads` PUBLIC, and `jamTests` links `jam-core`; the benchmark
script links `-pthread` and `-ldl`. `Threads::Threads` is the portable
requirement — modern glibc folds pthread into libc, which is why an ad-hoc link
may appear to work without `-pthread`. **No shared CMake patch is needed.**

## Limitations / handoff

- **PARTIAL.** Broader ANALYSIS-001 awaits the selected backend and the
  processor/clock integration wire (INT-ANALYSIS-001). No tracker selected, no
  production wiring, no ADR; G3 remains open.
- Read-only plugin inputs:
  `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-{btrack,aubio}.so`
  (SHA-256 recorded in the manifest).
- The clock-consumer test is not a musical-validity proof; the benchmark is
  synthetic and non-device and makes no latency/dropout claim.
- `AnalysisAudioRing` clips producers to 2048 frames; callers with larger device
  blocks must bound their tap into ≤ 2048-frame chunks before integration.

## Final commit SHA

- Implementation, tests, benchmark and docs: `TO_BE_FILLED` on
  `wp/ANALYSIS-001-worker`. The branch head is the handoff SHA reported to the
  orchestrator.
