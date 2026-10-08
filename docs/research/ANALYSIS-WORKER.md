# ANALYSIS-WORKER — live RhythmAnalyzer design and evidence

**Task:** ANALYSIS-001 (subset) — live rhythm-analysis worker, no production
tracker selection and no processor wiring.
**Base commit:** `af8b77a`.
**Status:** PARTIAL. The worker, its contracts and its tests are complete and
executed. Production tracker selection (G3) and the processor/clock wiring
(INT-ANALYSIS-001) are out of scope and remain open.

---

## 1. Scope

This document covers the **platform-neutral rhythm-analysis worker** introduced
by DEVPLAN ANALYSIS-001:

- `src/jam/RhythmAnalyzer.h` / `src/jam/RhythmAnalyzer.cpp`
- `tests/jam/RhythmAnalyzerTests.cpp`
- `tools/analysis-worker-benchmark/**`

It does **not** touch, select or tune a tracker backend, and it does not edit
`PluginProcessor.*`, the editor, `DrumEngine`, the `AnalysisAudioRing` /
`IRhythmTracker` / `RhythmTypes` contracts, `MusicalClock`, any CMake file, or
any ledger/handoff document. The worker is therefore fully unit-testable with no
audio device and no JUCE.

## 2. Position in the pipeline (SPEC 6/7/9)

```text
audio callback ──push──> AnalysisAudioRing ──pop──> RhythmAnalyzer ──push──> bounded
   (existing)             (existing, SPEC 8.1)       (this task)         evidence queue
                                                                              │
                                                                              v
                                        MusicalClock.observe(observation) (SPEC 7.3/9.3)
```

The analyzer emits **evidence**, never a command. It never transforms a BPM into
a drum decision: the pipeline remains `RhythmObservation → MusicalClock →
ClockSnapshot` (SPEC 9.1). A future clock/audio consumer drains the evidence
queue; no drum engine reads it directly.

## 3. Threading contract (SPEC 7)

- The **audio callback only calls `AnalysisAudioRing::push`**. The analyzer adds
  no notify, no lock, no message and no work to that path. The worker discovers
  work by polling.
- The **worker thread** only touches the ring's consumer side, the injected
  `IRhythmTracker`, this object's atomics, and the evidence queue's producer
  side. It performs no UI, device or file call and never blocks the audio thread.
- `start()` / `stop()` are a **single-owner** lifecycle: one non-RT thread. The
  destructor calls `stop()` as a backstop and joins. `stop()` must run before the
  ring is reset or destroyed, and before the analyzer is destroyed.
- The worker's idle wait is a short `condition_variable::wait_for` (1 ms) with a
  stop predicate; the audio callback never notifies it. `stop()` sets the flag and
  notifies, then joins.

## 4. Evidence envelope: event time vs causal availability

Each queued unit is an `ObservationEnvelope`:

| field | meaning |
|---|---|
| `observation` | the backend's `RhythmObservation`, **byte-for-byte unaltered** |
| `streamGeneration` | bumped on every tracker reset / stream discontinuity |
| `blockStartSampleTime` | device time of the block's first sample |
| `availabilitySampleTime` | device time at the **end** of the input block — when the evidence became causally knowable |
| `sourceSampleRate` | device rate the block was captured at |
| `sequence` | monotone enqueue order |
| `observationCausal()` | true iff `observation.inputSampleTime <= availabilitySampleTime` |

The two times are deliberately distinct. `observation.inputSampleTime` is the
*event* time a backend claims (often the block start, sometimes a predicted
onset). `availabilitySampleTime` is the *earliest* time a real-time consumer
could have known it. A clock consumer must advance on availability and never on
event time, or it can step backwards; the optional clock test in the suite
demonstrates exactly this (see §7).

## 5. Continuity, discontinuity and wrap safety

The device sample clock is expected to advance contiguously. The worker tears
down the stream when any of these hold after the first anchored frame:

- `frame.sampleTime != expectedNextSampleTime` (**gap** or **out-of-order**),
- `frame.sourceSampleRate != streamRate` (**rate change**),
- a pending forced reset from an earlier invalid-rate frame.

Teardown means: `tracker->reset(newRate)`, `streamGeneration++`, re-anchor. Any
envelope already queued keeps its own (older) generation, so stale evidence is
distinguishable and is not silently attributed to the new stream. This is the
"dump old-generation stale obs" requirement.

The device rate handed to `reset()`/`process()` is the frame's declared
`sourceSampleRate`. The worker performs **no resampling** of its own: an adapter
that must convert (e.g. `BTrackBackend` resampling to 44.1 kHz) already does so
internally, and double-resampling here would bias the evidence. This preserves
the adapter's rate conversion inside the worker.

**Wrap safety.** `expectedNextSampleTime = frame.sampleTime + frame.numSamples`
is computed in exact `uint64_t` modular arithmetic, and continuity is tested with
equality. A contiguous stream that wraps the 64-bit device counter therefore
compares equal and is **not** a false gap. The suite drives a real wrap
(`UINT64_MAX − 64` plus 128-sample blocks) and asserts zero discontinuities.

**Invalid rate.** A non-finite or non-positive `sourceSampleRate` is never handed
to a backend. The frame is dropped and counted (`invalidRateFrames`), and the next
valid frame is forced through the reset path so the backend never carries an
unknown-rate state forward.

### Bounds and the 2048-frame ring clip

`AnalysisAudioRing::push` clips producers to `jam::kMaxAnalysisBlock` (2048)
frames. A device block larger than that is clipped at the ring — the ring's
documented decision, not the worker's. The worker operates on whatever frame it
receives and does **not** re-chunk. Callers that can exceed 2048 frames per
callback must bound their analysis tap into chunks ≤ 2048 before the future
processor integration. This worker does not edit the caller.

## 6. Bounded publication and drop policy (SPEC 8.1/8.2/8.3)

The output is reusing `jam::rt::CommandQueue` — a fixed-capacity, allocation-free
SPSC queue whose full policy is **drop the incoming item and count it**, never
overwrite queued evidence, never retry, never wait. Capacity is
`jam::kObservationQueueCapacity = 32`.

- `enqueuedObservations` counts accepted envelopes.
- `droppedObservations` (`CommandQueue::droppedCount`) counts the dropped
  incoming envelopes. The loss is explicit; the worker never pretends a dropped
  beat is still queued.
- `ringOverruns` is read through from `AnalysisAudioRing::overrunCount()` so a
  consumer sees audio-side drops on the same stats object.

The queue retains **beat events**, not just a latest blob: every observation is
enqueued in order, so a burst of beats cannot coalesce into one. A test submits
30 consecutive beats and asserts all 30 arrive.

## 7. Failure handling

`IRhythmTracker::process` is contractually **non-blocking**: it must return the
observation for the block it was given, never wait for future audio and never
block indefinitely. `stop()` cannot force an uncooperative backend to return; it
signals and joins. This is stated in `RhythmAnalyzer.h`.

If a backend throws from `reset()` or `process()`:

- `start()` returns `AnalyzerStartResult::trackerResetFailed` for a throwing
  priming reset and does not start a thread;
- a throwing `process()`/discontinuity `reset()` sets the **failure flag** and
  `failureMessage`, then requests stop; the worker clears `running` on exit and
  `stop()` joins safely.

`start()` also rejects, without starting, a null tracker (`noTracker`), a
non-finite or non-positive rate (`invalidSampleRate`) and a duplicate start
(`alreadyRunning`).

## 8. Restart contract

`start()` is a fresh, reproducible session:

- it flushes any evidence left queued by a previous session (the consumer must be
  quiescent during start);
- it resets the session counters;
- it bumps `streamGeneration`, so any envelope a consumer retained from before
  the restart is unambiguously stale.

A test runs the same input twice on one analyzer and once on a fresh analyzer and
asserts the payload sequences match, that `sequence` restarts, and that the
generation advanced; a second test starts with a non-empty queue and asserts the
queue is empty immediately after the restart.

## 9. Tests executed

`tests/jam/RhythmAnalyzerTests.cpp`, run through the platform-neutral `jamTests`
binary (`ctest -R jam.RhythmAnalyzer`):

```
22 tests, 565 checks, 0 failed check(s) in 0 test(s)
```

Coverage: duplicate/invalid/null start rejection; 20× start/stop; destructor
join; in-order publication with unaltered observations; all beats retained;
availability vs event time (including a non-causal event); gap / out-of-order /
rate-change / invalid-rate resets and generation bumps; `uint64_t` wrap safety;
output-queue pressure drops with exact `enqueued + dropped == processed`; ring
overrun surfacing; reproducible restart and flush; throwing reset at start, at a
discontinuity, and throwing process; a slow backend with a bounded `stop()`;
a concurrent producer/consumer stress run (thread IDs prove `reset` ran on the
lifecycle thread and `process` on the worker); and a future `MusicalClock`
consumer advancing on availability without moving backwards.

The full `jam-core` ctest suite (existing suites plus this one) is green:
`100% tests passed, 0 tests failed out of 15`.

## 10. Benchmark (throughput, not a gate)

`tools/analysis-worker-benchmark/` compiles this worktree's `RhythmAnalyzer.cpp`,
`dlopen`s the pinned EVAL-005 BTrack/aubio plugins (no GPL linkage), and feeds a
deterministic 120 BPM click train in bounded bursts.

Throughput mode (5 s, 128-frame blocks, ring 64, burst 32), wall vs CPU.
Run-to-run wall time varies by roughly ±10%; the exact committed values are in
`tools/analysis-worker-benchmark/results/throughput.csv`.

| backend | rate | wall s | CPU s | realtime × |
|---|---|---|---|---|
| btrack | 44100 | 0.0777 | 0.0976 | 64× |
| btrack | 48000 | 0.0814 | 0.1025 | 61× |
| aubio | 44100 | 0.0786 | 0.0861 | 64× |
| aubio | 48000 | 0.0700 | 0.0781 | 71× |

`ringOverruns == 0` in every throughput run. The pressure mode (consumer does not
drain) drops `processed − 32` envelopes and reports the matching
`droppedObservations`, demonstrating the bounded drop-and-count policy. Plugin
and source SHA-256 are in `tools/analysis-worker-benchmark/results/manifest.txt`.

These are throughput numbers only. No latency threshold is asserted, no tracker
is selected and the SPEC gates are unchanged (G3 open).

## 11. Threads linkage

The new sources use `std::thread`, `std::mutex` and `std::condition_variable`.
No CMake file was edited: `jam-core/CMakeLists.txt` already does
`find_package(Threads REQUIRED)` and links `Threads::Threads` PUBLIC, and the
`jamTests` target links `jam-core`. `Threads::Threads` is the portable
requirement; on glibc ≥ 2.34 the pthread symbols happen to live in libc, which is
why an ad-hoc `g++` link may appear to work without `-pthread`. **No shared CMake
patch is needed.** The benchmark script links `-pthread` explicitly and `-ldl`
for the plugin loader.

## 12. Limitations / handoff

- No production tracker selection, no processor/editor/DrumEngine wiring, no
  device. G3 remains open.
- The clock-consumer test consumes observations non-RT and asserts only that
  availability-driven advancement never moves backwards; it is not a musical
  validity proof.
- The benchmark is synthetic and non-device; it makes no latency/dropout claim.
- The 2048-frame ring clip requires the future integration to bound its chunks.
