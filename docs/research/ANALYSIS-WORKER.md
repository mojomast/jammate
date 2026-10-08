# ANALYSIS-WORKER — live RhythmAnalyzer design and evidence

**Task:** ANALYSIS-001 (subset) — live rhythm-analysis worker, no production
tracker selection and no processor wiring.
**Base commit:** `af8b77a`.
**Status:** PARTIAL. The worker, its contracts and its tests are complete and
executed. Production tracker selection (G3) and the processor/clock wiring
(INT-ANALYSIS-001) are out of scope and remain open.

**Review continuation:** Flash stopped with `Insufficient Balance` while its
corrections were uncommitted. The orchestrator preserved those edits, completed
review locally and independently executed the checks below. Initial handoff was
`20a015a`; the corrected source and result pins are in
`tools/analysis-worker-benchmark/results/integration-verification.json`.

Accepted correction commit: **`5162e4d`**. Current-main full verification passes
**20/20 suites with both trackers enabled**, **18/18 default-OFF**. Both Linux
workflows require the analyzer suite and the five-check scripted benchmark suite;
all22 workflow shell bodies pass syntax checks. The actual audio callback remains
unwired to this worker; this completes the tracker-injected lifecycle foundation.

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

## 4. Evidence envelope: event time vs the input horizon

Each queued unit is an `ObservationEnvelope`:

| field | meaning |
|---|---|
| `observation` | the backend's `RhythmObservation`, **byte-for-byte unaltered** |
| `streamGeneration` | bumped on every tracker reset / stream discontinuity |
| `blockStartSampleTime` | device time of the block's first sample |
| `inputHorizonSampleTime` | device time at the **end** of the input block: an audio-data **horizon** and **lower bound**, not a measured live availability |
| `sourceSampleRate` | device rate the block was captured at |
| `sequence` | monotone enqueue order |
| `availabilityMeasured` | always false here; true only once a future integration stamps a real consumer receipt |
| `observationWithinInputHorizon()` | wrapsafe bound test that the reported event is at/before the horizon |

The two times are deliberately distinct. `observation.inputSampleTime` is the
*event* time a backend claims (often the block start, sometimes a predicted
onset). `inputHorizonSampleTime` is the end of the frame that was processed, so
it is the **earliest** the evidence could possibly have been knowable. It is NOT
the live time it became knowable: with a backlog, the worker's current frame can
lag the audio the device has already produced. A future INT-ANALYSIS-001
integration must stamp the real availability from the device/sample master or a
consumer receipt (with rate and generation) before advancing a clock; this worker
never fabricates that. Reading a wall clock here would not be a device
measurement and is deliberately not attempted.

`observationWithinInputHorizon()` compares the event to the horizon with a
bounded unsigned distance (wrap-safe). Direct `event <= horizon` would wrongly
reject a valid event just before a `uint64_t` wrap whose horizon lands just
after; a future event one sample past the horizon is still rejected. The clock
test is a synchronous **offline replay** advancing on the horizon, not a live
consumer example. A live consumer must use its current sample master and stamp
receipt time; advancing a live clock to an old frame horizon can move it backward.

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

- `enqueuedObservations` counts accepted envelopes this session.
- `droppedObservations` is the **per-session** delta of the cumulative
  `CommandQueue::droppedCount()`; `lifetimeDroppedObservations` is the cumulative
  value since construction. The loss is explicit; the worker never pretends a
  dropped beat is still queued.
- `ringOverruns` is read through from `AnalysisAudioRing::overrunCount()`; it is
  a **cumulative, pre-existing** counter that can predate this analyzer.

The queue retains **beat events**, not just a latest blob: every observation is
enqueued in order, so a burst of beats cannot coalesce into one. A test submits
30 consecutive beats and asserts all 30 arrive. Under overflow the retained set
is the FIRST 32 envelopes, never the latest.

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

`running_` is published **before** the worker thread is launched and is never
written by `start()` afterwards, so a worker that fails immediately and clears it
cannot be masked by a later store; the worker's exit store is definitive. A
100-iteration test drives a first-frame failure and asserts that after join
`failed() == true` and `running() == false` with no resurrection.

`failureMessage()` returns an object-owned pointer that stays stable until the
next failure. It is safe only for the lifecycle owner after `stop()`/join (or a
diagnostic reader that accepts quiescence); `start()` does not mutate it. This is
stated in the header as a quiescent owner-only diagnostic API.

`start()` also rejects, without starting, a null tracker (`noTracker`), a
non-finite or non-positive rate (`invalidSampleRate`) and a duplicate start
(`alreadyRunning`). The plugin-ownership constructor throws
`std::invalid_argument` if a non-null tracker is paired with a null deleter, and
never falls back to `delete` for a plugin object.

## 8. Restart contract

`start()` is a fresh, reproducible session. The producer (audio callback) and the
evidence consumer must be quiescent for its duration:

- it flushes any evidence left queued by a previous session;
- on a **restart** (not the first start) it additionally **discards** whatever
  audio is still queued in the ring, counting `discardedAudioBlocks` /
  `discardedAudioFrames`, so a previous stream frame cannot be processed under
  the new generation;
- the first start preserves prequeued audio, so a caller that primes audio before
  starting is supported;
- it resets the per-session counters and the drop baseline, and bumps
  `streamGeneration`, so any envelope a consumer retained from before the restart
  is unambiguously stale.

Tests: the same input run twice on one analyzer, and once on a fresh analyzer,
produces matching payload sequences with `sequence` restarting and the generation
advancing; a restart with queued audio and queued evidence discards/flushes both
and processes nothing from the old stream; and the first start does not discard.

## 9. Tests executed

`tests/jam/RhythmAnalyzerTests.cpp`, run through the platform-neutral `jamTests`
binary (`ctest -R jam.RhythmAnalyzer`):

```
29 tests, 1210 checks, 0 failed check(s) in 0 test(s)
```

Coverage: duplicate/invalid/null start rejection; plugin-ownership null-deleter
rejection; 20× start/stop; destructor join; in-order publication with unaltered
observations; all beats retained; the input horizon vs event time and the
wrap-safe within-horizon predicate; a blocked backend proving the horizon is the
frame end (not a fabricated live availability); gap / out-of-order / rate-change
/ invalid-rate resets and generation bumps; `uint64_t` wrap safety; per-session
queue-pressure drops with `enqueued + drops == processed`; ring overrun
surfacing; reproducible restart, audio discard and flush; a 100× first-frame
failure proving `running` cannot be resurrected; throwing reset (start and
discontinuity) and process; a slow backend with a bounded `stop()`; a concurrent
producer/consumer stress run (thread IDs prove `reset` ran on the lifecycle
thread and `process` on the worker); bit-preserving forwarding of every backend
field (including signed zero and a NaN payload); and a synchronous offline
`MusicalClock` replay advancing on the input horizon.

The worker initially reported full OFF/ON lanes of 15/17 suites. The corrected
orchestrator build independently executes this suite at
`/home/mojo/projects/build-ANALYSIS-001-integration/worker-core` (Ninja, Release,
GCC14.2; both backends OFF). Current-main full suite counts are recorded at
integration, since TRACK-005 added two suites after this task's base.

A limited ThreadSanitizer run of all 29 analyzer tests reports no data races;
compiler and runtime logs are preserved with the integration pins. This covers
the synthetic suite, not the third-party plugins or a running audio device.

## 10. Benchmark (throughput, not a gate)

`tools/analysis-worker-benchmark/` compiles this worktree's `RhythmAnalyzer.cpp`,
`dlopen`s the pinned EVAL-005 BTrack/aubio plugins (no GPL code is **compiled
into** the binary; the loaded plugins' own licences still apply), and feeds a
deterministic 120 BPM click train in bounded bursts.

Throughput mode (5 s, 128-frame blocks, ring 64, burst 32), wall vs CPU.
Run-to-run wall time varies by roughly ±10%; the exact committed values are in
`tools/analysis-worker-benchmark/results/throughput.csv`.

| backend | rate | wall s | CPU s | realtime × |
|---|---|---|---|---|
| btrack | 44100 | 0.0753 | 0.0954 | 66× |
| btrack | 48000 | 0.0817 | 0.1012 | 61× |
| aubio | 44100 | 0.0668 | 0.0739 | 75× |
| aubio | 48000 | 0.0737 | 0.0810 | 68× |

`ringOverruns == 0` in every throughput run. The pressure mode (consumer does not
drain) keeps the FIRST 32 envelopes, drops the rest, and reports the matching
`droppedObservations`, demonstrating the bounded drop-and-count policy. The
throughput `wall_seconds` includes the burst hand-off and the worker's idle poll
between bursts (a conservative floor); the `cpu_seconds` column is process CPU
across threads. Plugin and source SHA-256 are in
`tools/analysis-worker-benchmark/results/manifest.txt`.

The benchmark's producer retries failed ring pushes **offline** in pressure
mode, so `ringOverruns` there counts failed push attempts, not unique lost audio
blocks. Every unique block is eventually processed; observation drops count
unique incoming envelopes. That retry loop is never used in the audio callback.
Pressure results now drain the retained first32 envelopes at the end and verify
`retained + dropped == processed`. The runner rejects invalid numeric/mode/
capacity arguments, failed or null backends and output flush/close failures;
five Python checks exercise these cases with scripted plugins. A 60-second
progress deadline bounds producer retries for a returning backend. As with
`stop()`, it cannot force a backend that blocks indefinitely to return.

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
- The offline clock replay consumes observations non-RT and asserts only that
  horizon-driven replay never moves backwards; it is not a musical validity
  proof, and the horizon is a lower bound, not a measured live availability.
- The benchmark is synthetic and non-device; it makes no latency/dropout claim.
- The 2048-frame ring clip requires the future integration to bound its chunks.
