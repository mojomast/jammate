# RT-SIGNAL-001

## Goal

Provide isolated, header-only RT signalling primitives answering FND-003 / F1,
with executable contracts and precise processor integration instructions.

Worktree: `/home/mojo/projects/worktrees/RT-SIGNAL-001`.
Branch: `wp/RT-SIGNAL-001`.

## Base commit

`f04c055ab35d1cac3c901537a6080b911570bfe6`

## Files changed

- `src/rt/RtSignal.h`
- `tests/jam/RtSignalTests.cpp`
- `task-notes/RT-SIGNAL-001.md`

## Contract implemented

All public operations are `noexcept`; all storage is inline from construction.
The header includes only `<atomic>`, `<cstddef>`, `<cstdint>`, `<type_traits>`.
Compile-time checks reject targets without always-lock-free bool, byte and
uint64 atomics, and reject non-trivially-copyable or cv-qualified payloads.
No allocation, user-defined payload copies, locks, syscalls or message posts.

- `jam::rt::SignalFlag`: `signal()`, `consume()`, `pending()`. Repeated signals
  coalesce to one edge; the atomic clearing exchange cannot erase a later edge.
- `jam::rt::LatestValue<T>`: `publish(const T&)`, `tryRead(T&) const`,
  `invalidate()`. One logical slot, newest unread publication wins. One writer
  and one reader. Invalidation may run on another lifecycle thread and forces
  one re-read of the retained value; it does not discard or fabricate a payload.
  Failed reads leave output and reader generations unchanged.
- Optional `jam::rt::CommandQueue<Command, N>` is fully implemented: `push`,
  `pop`, `droppedCount`, `capacity`. SPSC FIFO with drop-incoming overflow.
  Capacity zero is a legal sink counting every rejected command. Monotonic
  uint64 indices track occupancy, while thread-owned storage cursors wrap
  independently so non-power-of-two capacities also survive index rollover.

## Memory-ordering argument

### SignalFlag

- `pending_`: `signal` stores true with **release**; `consume` exchanges false
  with **acq_rel**; read-only `pending` loads with **acquire**.
- The exchange is the clearing linearisation point: a concurrent signal is
  ordered before it and consumed, or ordered after it and left pending. There
  is no load/clear gap. Release/acquire publishes preceding state updates to the
  thread inspecting the notification. The flag is not an ownership protocol
  for subsequently modified non-atomic state; associated state must separately
  be synchronised.

### LatestValue

- `sequence_`: the single publisher loads its own previous counter **relaxed**,
  stores the odd writing marker **seq_cst**, stores the even completed marker
  **seq_cst**. The reader's before/after marker loads are **seq_cst**.
- Every shared payload byte is an `atomic<unsigned char>`; all payload stores
  and loads are **seq_cst**. This conservative choice supplies a single total
  order containing both writing markers and all speculative byte accesses.
  Equal nonzero even markers enclosing the loads exclude an intervening writer
  publication. SC includes release/acquire ordering for completed publication.
  Merely using release/acquire markers with plain payload bytes would be wrong.
- `invalidation_`: **relaxed fetch_add** and **relaxed load**. This is only a
  re-read token; it does not publish external lifecycle state or payload.
  Invalidation observed during a successful read is remembered; invalidation
  racing after its initial load is still outstanding on a subsequent poll.
- `lastReadSequence_` and `lastReadInvalidation_` are ordinary reader-owned
  uint64 fields. `const tryRead` allows a const reference, not multiple readers.
- Payload, sequence, invalidation and reader bookkeeping occupy separate
  `alignas(64)` boundaries.

### CommandQueue

- Producer owns `writePos_` and loads it **relaxed**; consumer owns `readPos_`
  and loads it **relaxed**. Each thread loads the other index with **acquire**.
- Producer publishes a copied slot via `writePos_.store(..., release)`;
  consumer releases the copied slot via `readPos_.store(..., release)`.
  These paired handoffs ensure the reader never sees an unfinished command
  and the writer never overwrites a command still being copied. Plain slot
  bytes are safe because reuse requires the consumer's acknowledgement.
- Only the producer writes `dropped_`; it increments using **relaxed load/store**,
  avoiding an RMW on the audio overflow path. Diagnostics load it **relaxed**.
  It carries a count, not payload visibility. Storage cursors are thread-owned.
- Shared indices and drop diagnostics are cache-line separated with `alignas(64)`.

## Torn-read analysis

A naive seqlock copying an ordinary multiword `T` has a C++ data race even if
the reader detects the generation change and discards the copy. An ordinary
double buffer also needs a slot-reuse protocol: the writer can lap a reader.

This implementation solves the question using **atomic payload bytes** and SC
generation validation. A speculative torn representation is only a local
unsigned-char array, never interpreted as `T`. It is copied to the output only
after equal completed generations establish coherence. Object-representation
copying is permitted for trivially-copyable values and invokes no user code.

`tryRead` has **one attempt, zero retries** (`readAttemptLimit == 1`, compile-time
and runtime asserted). If the initial marker is odd, or the final marker differs,
it returns false immediately. `publish` executes exactly `sizeof(T)` payload
stores and two marker stores, without looking at reader progress. Reader byte
loops also have exactly `sizeof(T)` bounds. SPEC 8.2 callers retain the previous
valid output after false, rather than waiting for a coherent snapshot.

## Tests executed

The prescribed standalone jam-core build was used, with `CXXFLAGS` adding
`-Wpedantic` to the test target as well as the library target:

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
export CXXFLAGS='-Wall -Wextra -Wpedantic'
cd /home/mojo/projects/worktrees/RT-SIGNAL-001
rm -rf /tmp/opencode/build-rtsig
cmake -S jam-core -B /tmp/opencode/build-rtsig -G Ninja
cmake --build /tmp/opencode/build-rtsig
ctest --test-dir /tmp/opencode/build-rtsig --output-on-failure
/tmp/opencode/build-rtsig/jamTests RtSignal.
/tmp/opencode/build-rtsig/jamTests Nope.
```

An additional standalone header-only compilation used C++17, `-O2`,
`-Wall -Wextra -Wpedantic -Werror`, with wrappers instantiating all operations
on a 64-byte payload. `nm -u` and `objdump -dr` inspected the resulting object.

## Test results

- Configure registered `AnalysisAudioRing MusicalClock RtSignal`.
- All 3 ctest entries pass in the same `jamTests` executable.
- `RtSignal`: **14 tests, 110090 checks, zero failures**.
- Coverage: empty/single/coalesced edges, non-consuming observation, second
  edge, release/acquire handoff, uninitialised/read-once/latest-wins snapshots,
  coalesced and concurrent invalidation, no payload default construction,
  multiword torn-read hammer, capacity-zero/full/FIFO/wrapped command slots,
  concurrent command coherence/order/conservation and exact drop accounting.
- Snapshot hammer: 100000 publications and 150000 read calls on joined threads.
  Every accepted value must match all fields of a published generation; failed
  reads must preserve output. No assertion depends on the scheduler interleaving.
- Command hammer: 100000 pushes and 100000 pops, then at most capacity drain
  attempts. Every accepted command is coherent and ordered; accepted == popped,
  accepted + dropped == incoming, and diagnostic drops == rejected pushes.
- Compile-time checks cover trivial copyability, nothrow payload copying,
  nothrow construction, deleted primitive copying, alignment, and every public
  operation's `noexcept` contract.
- **Zero compiler warnings** in initial, broken, reverted and final build logs.
  Exact compile command includes `-Wall -Wextra -Wpedantic` for RtSignalTests.
  Configure emits existing CMake author warnings for jam-core's missing
  `project()`; these are separate from compiler diagnostics.
- Zero-match guard: `Nope.` reports zero matches and exits **2**.
- Allocation proof is **structural, not a runtime allocation count**.
  AnalysisAudioRingTests owns anonymous-namespace counters and replacement
  global operators; no duplicate replacements or invalid extern declarations
  were introduced. That suite's allocation checks and RtSignal run together.
  The standalone optimized object additionally has **zero undefined symbols**,
  no runtime calls, and only fixed-size copy loops. On the inspected GNU 14.2
  x86-64 target, signal is a single `movb`, consume uses `xchg`, publication uses
  byte/marker `xchg`, and invalidation uses one native atomic increment.

## Evidence

Captured logs outside the repository:

- `/tmp/opencode/rtsig-configure.log`
- `/tmp/opencode/rtsig-build.log`
- `/tmp/opencode/rtsig-build-broken.log`
- `/tmp/opencode/rtsig-build-reverted.log`
- `/tmp/opencode/rtsig-build-final.log`
- `/tmp/opencode/rtsig-ctest-green-initial.log`
- `/tmp/opencode/rtsig-ctest-red.log`
- `/tmp/opencode/rtsig-ctest-green-reverted.log`
- `/tmp/opencode/rtsig-ctest-green-final.log`
- `/tmp/opencode/rtsig-compile-commands.log`
- `/tmp/opencode/rtsig-zero-match.log`
- `/tmp/opencode/rtsig-undefined-symbols.log` (empty)
- `/tmp/opencode/rtsig-standalone-assembly.log`

### Deliberately broken invariant: red

Temporarily changed only `SignalFlag::signal()` from
`pending_.store(true, release)` to `pending_.store(false, release)`, rebuilt,
and ran the full ctest command. Exact ctest output, including reported exit:

```text
Test project /tmp/opencode/build-rtsig
    Start 1: jam.AnalysisAudioRing
1/3 Test #1: jam.AnalysisAudioRing ............   Passed    1.61 sec
    Start 2: jam.MusicalClock
2/3 Test #2: jam.MusicalClock .................   Passed    0.01 sec
    Start 3: jam.RtSignal
3/3 Test #3: jam.RtSignal .....................***Failed    0.10 sec
[suite] RtSignal
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:137
      CHECK failed: flag.pending()
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:138
      CHECK failed: flag.pending()
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:139
      CHECK failed: flag.consume()
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:144
      CHECK failed: flag.consume()
  FAIL RtSignal.emptySignalAndSingleEdge
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:155
      CHECK failed: flag.pending()
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:158
      CHECK_EQ failed: consumedEdges == uint64_t { 1 }
    actual:   0
    expected: 1
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:163
      CHECK_EQ failed: consumedEdges == uint64_t { 1 }
    actual:   0
    expected: 1
  FAIL RtSignal.tenThousandSignalsCoalesceToExactlyOneEdge
    FAIL /home/mojo/projects/worktrees/RT-SIGNAL-001/tests/jam/RtSignalTests.cpp:190
      CHECK failed: flag.consume()
  FAIL RtSignal.signalHandoffPublishesPrecedingState
  PASS RtSignal.uninitialisedLatestReadLeavesOutputUntouched
  PASS RtSignal.latestPublicationIsReadExactlyOnce
  PASS RtSignal.newestUnreadPublicationWinsWithoutAQueue
  PASS RtSignal.invalidationForcesOneRereadAndCoalesces
  PASS RtSignal.representationsNeedNoPayloadConstruction
  PASS RtSignal.concurrentLatestReadsNeverAcceptTornValues
  PASS RtSignal.concurrentInvalidationForcesACompletedValueToBeReread
  PASS RtSignal.commandQueueDropsIncomingAndPreservesFifo
  PASS RtSignal.commandQueueZeroCapacityCountsEveryDrop
  PASS RtSignal.commandQueueWrapsSlotsWithoutLosingCommands
  PASS RtSignal.concurrentCommandsStayCoherentAndOrderedWithCountedDrops

14 tests, 110090 checks, 8 failed check(s) in 3 test(s)


67% tests passed, 1 tests failed out of 3

Total Test time (real) =   1.72 sec

The following tests FAILED:
	  3 - jam.RtSignal (Failed)
Errors while running CTest
exit=8
```

### Restored invariant: green

Restored the true store and rebuilt. Exact ctest output:

```text
Test project /tmp/opencode/build-rtsig
    Start 1: jam.AnalysisAudioRing
1/3 Test #1: jam.AnalysisAudioRing ............   Passed    1.58 sec
    Start 2: jam.MusicalClock
2/3 Test #2: jam.MusicalClock .................   Passed    0.00 sec
    Start 3: jam.RtSignal
3/3 Test #3: jam.RtSignal .....................   Passed    0.09 sec

100% tests passed out of 3

Total Test time (real) =   1.68 sec
exit=0
```

After the command queue's independent slot cursors and producer-only drop
increment were finalised, the final full run remained green:

```text
Test project /tmp/opencode/build-rtsig
    Start 1: jam.AnalysisAudioRing
1/3 Test #1: jam.AnalysisAudioRing ............   Passed    1.47 sec
    Start 2: jam.MusicalClock
2/3 Test #2: jam.MusicalClock .................   Passed    0.00 sec
    Start 3: jam.RtSignal
3/3 Test #3: jam.RtSignal .....................   Passed    0.09 sec

100% tests passed out of 3

Total Test time (real) =   1.57 sec
exit=0
```

Zero-match guard:

```text
ERROR: filter "Nope." matched zero tests
exit=2
```

## Known limitations

- Single publisher/reader for LatestValue; single producer/consumer for commands.
  `const tryRead` mutates only reader bookkeeping, so it is not a multi-reader API.
  Object construction/destruction require callers to be quiescent.
- A continuously racing snapshot reader can return false indefinitely; this is
  intentional SPEC 8.2 behaviour, preserving its previous valid value. There is
  no promise of a successful read during uninterrupted publication.
- LatestValue must be recreated before 2^63 publications or 2^64 invalidations
  to avoid generation aliasing. Command occupancy arithmetic tolerates counter
  wrap; its diagnostic count wraps naturally at uint64 overflow.
- Sequentially-consistent byte stores favour a straightforward C++ memory-model
  proof over snapshot throughput. Keep payloads small; cost scales with sizeof(T).
- Native atomic latency is platform-dependent. Lock freedom is compile-time
  checked; instruction lowering was inspected on x86-64. Other targets must
  separately check RMW lowering before claiming instruction-level wait freedom.
  Signal/publish/queue-producer paths contain only atomic loads/stores.
- No JUCE build, device test or SPEC 18.2 callback timing gate was run. The P0
  removal becomes effective when the orchestrator applies the integration below.

## Integration notes

**Described only; processor/editor/drum files were not modified.** All source
locations below are against the base commit. Use a **processor-owned** timer so
automatic scene changes work while the editor is closed.

1. In `src/PluginProcessor.h`, include `"rt/RtSignal.h"`. Change
   `private juce::AsyncUpdater` at line 25 to `private juce::Timer`.
2. Beside `scenePendingSection` in the private scenes members (around line 370),
   add `jam::rt::SignalFlag sceneReadyToApply;`. This is processor-owned inline
   state, constructed once; no callback setup or dynamic storage is needed.
3. Replace `void handleAsyncUpdate() override;` at line 365 with
   `void applyPendingScene();` and `void timerCallback() override;`.
   Beside the named scene timing constants add
   `static constexpr int sceneSignalPollIntervalMs = 40;` with a comment stating
   that 40 ms is 25 Hz, within the audit's proposed 20–30 Hz message-thread polling
   range and comfortably below the 1.5-second hold safety net.
4. At `PluginProcessor.cpp:1227`, inside the existing `g1 <= 0.0f` fade-out
   completion branch, replace **only** `triggerAsyncUpdate();` with
   `sceneReadyToApply.signal();`. Keep the preceding hold counter/state stores.
   The release store announces their completion without touching JUCE messaging.
5. With AsyncUpdater removed, also replace the lifecycle wake-up at
   `PluginProcessor.cpp:1070` with `sceneReadyToApply.signal();`, preserving its
   existing `scenePendingSection >= 0` guard and the subsequent idle reset.
   The timer must consume the flag itself; do not require `SceneEnv::hold` as an
   additional timer guard because this device-change path intentionally resets
   the envelope to idle.
6. At the end of the processor constructor, after `writeDefaultChain()`
   (line 836), call `startTimer(sceneSignalPollIntervalMs);`. At the beginning of
   the destructor (line 839), call `stopTimer();` before tearing down state used
   by scene application, under the processor's message-thread lifetime discipline.
   Timer setup/teardown must be on control/lifecycle paths, never processBlock.
7. Add the message-thread polling callback:

   ```cpp
   void GuitarCompanionProcessor::timerCallback()
   {
       if (sceneReadyToApply.consume())
           applyPendingScene();
   }
   ```

   This polls once per tick and coalesces all pending wake-ups. It directly runs
   the application helper on JUCE's message thread, rather than posting another
   AsyncUpdater message. `pending()` is observation only; use `consume()` here.
8. Rename the definition at line 2538 from `handleAsyncUpdate()` to
   `applyPendingScene()`. Correct its inaccurate “Called by the audio thread”
   comment to say that the message-thread timer/safety net calls it after the
   audio thread signals readiness. **Retain every statement and its order**:

   ```cpp
   const int gen = sceneGen.load();
   const int sec = scenePendingSection.exchange (-1);
   if (sec >= 0)
       applySceneNow (sec);
   sceneAppliedGen.store (gen);
   ```

   The generation must still be read FIRST and applied generation published
   LAST, after `applySceneNow` increments scene load tracking. File/model/state
   work remains in `applySceneNow`/`applyState` on the message/loader threads.
   Do not put that work in `signal`, processBlock, or a worker polling the flag.
9. In the existing no-device `Timer::callAfterDelay` safety net at line 2478,
   rename `self->handleAsyncUpdate();` to `self->applyPendingScene();`. Retain
   the weak-lifetime guard, fadeOut/pending-section guards and fadeIn store.
   Update the scene comments around lines 2442–2444 and header line 401 to name
   the signal/timer/helper handoff. Do not clear the flag in the helper: that
   could discard a new signal arriving while scene application runs.

The nominal new wake-up latency is one 40 ms timer interval plus message-thread
scheduling delay; the audio callback never waits for that thread. No CMake
change is needed: the new header lives under the existing src include root.

## Final commit SHA

The commit containing this note is the tip of `wp/RT-SIGNAL-001`; resolve with
`git rev-parse wp/RT-SIGNAL-001`. Its exact full SHA is reported in the worker's
final handoff (a committed file cannot embed its own final commit hash).
