# Live Jam pipeline — INT-LIVE-001 implementation notes

Companion to `docs/research/LIVE-JAM-CONTRACT.md` (frozen). This document
describes how the first audible live slice is wired by INT-LIVE-001 and records
the judgement calls a reviewer or the orchestrator needs to validate.

## Data flow

```
processBlock (audio owner)
  mono guitar post input-gain, pre-gate/effects/drums
        |  pushAudio(chunk <= 2048, absolute sampleTime)   [bounded, no alloc]
        v
  jam::LiveJamSession
     AnalysisAudioRing (fixed capacity, drop+count on full)
        |  (one analyzer thread)
     RhythmAnalyzer -> ObservationEnvelope queue (drop+count)
        |  (one control worker thread)
     drain commands (bounded) + observations (bounded)
        |  advance MusicalClock from the actual audio cursor
        v
     MusicalClock (sole tempo/phase authority)
        v
     JamJoinPolicy (minimal director: join/holdover/loss)
        v
     DrumClockBridge -> DrumClockCommandQueue (fixed capacity)
        |  (audio owner is the single consumer)
        v
     DrumEngine (injected transport; prepared 4/4 ROCK/Basic at index 0)
        |
  publishDrumEcho (audio owner) -> LatestValue -> control worker -> JamLiveState
        |
  readJamLiveState (one message-thread reader) -> UI-LIVE-001
```

The `LiveJamSession` is JUCE-free and lives in `src/jam/`, so it is compiled and
tested by the platform-neutral `jam-core` build with no audio device. The actual
`DrumEngine` crosses into `LiveJamSession` **only** as a queue pointer handed to
`DrumEngine::attachClockBridge`; no JUCE type appears in `src/jam/`.

## Thread and time ownership

- **Audio owner.** `processBlock` taps the mono guitar after the input sum and
  input gain and before the gate/effects and before the drum sum. A callback
  larger than `jam::kMaxAnalysisBlock` (2048) is split into chunks with exact
  absolute sample times; nothing is truncated. The callback advances one
  session-relative absolute `uint64` cursor (`jamAudioSampleTime`) every
  callback, including while Jam is stopped, so the worker clock and the attached
  engine share the same domain. The tap does no allocation, no lock and no
  message.
- **Analyzer worker.** Exactly one, existing `RhythmAnalyzer`, owning the
  injected `IRhythmTracker` off the audio thread.
- **Control worker.** Exactly one, in `LiveJamSession`, owning the
  `MusicalClock`, the `JamJoinPolicy` and `DrumClockBridge` publication.
- **UI.** One message-thread producer of `JamLiveCommand` and one message-thread
  reader of `JamLiveState`. UI never starts/joins workers and never reads plain
  engine/clock/analyzer state.

`prepare()`/`release()` are the only lifecycle calls. They stop and join the
worker and analyzer *before* the ring or bridge is reset, and the worker/analyzer
only start at `prepareToPlay` — the audio-init boundary — so no thread starts
before the audio side exists.

## Receipt, horizon and generations

- `ObservationEnvelope.observation.inputSampleTime` is the tracker's claimed
  **event** time; `inputHorizonSampleTime` is the **input-data horizon** (end of
  the block) produced by the analyzer and left unmodified.
- `lastReceiptSampleTime` is the **actual audio cursor** sampled when the control
  worker popped the envelope. It is a separate, block-resolution availability
  signal, not fabricated sub-block timing and not a device-latency claim.
- A session prepare bumps a generation. UI commands are tagged with the
  generation at submission; the worker rejects a mismatched tag, so a command in
  flight while the device re-prepared can never be applied to the new session.
  `userCommandDrops` reports queue drops plus stale-generation rejects.
- The worker counts an analyzer stream-generation change as a discontinuity, so
  evidence from a reset tracker is not silently fused into the old stream.
- Evidence that claims a future event time, or that is older than
  `maxObservationAgeSamples`, is dropped and counted (`observationDrops`) rather
  than applied late. This is the "fail closed" policy.

## Join / holdover / loss policy

`JamJoinPolicy` (`src/jam/JamJoinPolicy.h`) is the minimal director named by the
contract, not the full Jam Director. **Intent, sent and echo-ack are separate**:

- a join is wanted only from a `Locked` clock; it becomes *sent* only when the
  bridge accepts the command (queue space), and *engaged* only when the
  **audio-owner echo** confirms real playback. `joinPending` = wanted || sent;
  `drumsPlaying` is only ever the echo, never the scheduled command;
- a rejected join/stop stays wanted and is retried on the next tick — it is never
  latched as pending-forever;
- a wanted stop persists until the audio owner echoes that it actually stopped.
  A still-playing echo does **not** clear a pending stop, so a next-bar stop
  cannot be dropped;
- `Stop`/`Reset`/`Lost` immediately cancel any future join, and a stopped policy
  never auto-rejoins from a snapshot; only an explicit Start re-arms a join;
- `Holdover` **holds**; a discontinuity forces a cancel stop.

The previous P1 defects are covered by regressions: queue-full join then drain
joins; queue-full Stop under an ongoing playing echo retries and stops; a
Start/Stop before the downbeat cannot resurrect a queued join; an echo from
another generation is ignored.

## Bridge / tempo / stop authority

Exactly one tempo path reaches the engine: `DrumClockBridge::applySnapshot`,
which stages the clock's belief at the next bar boundary. No raw detector BPM is
ever written to the engine. Tap/Half/Double/Resync are applied to the
`MusicalClock` first (the phase/tempo authority); for phase-affecting commands
the session also asks the bridge to land its next downbeat on the clock's next
downbeat (`syncBridgePhaseToClock`), so the worker grid and the rendered grid
cannot drift into a permanent beat/bar offset.

Stop verbs are distinct (STOPDECISION):

| Command | Bridge verb | Effect |
|---|---|---|
| `Stop` | `requestStopNow()` | bounded cancel/**Clear** at the next serviced block; cancels queued join/tempo/resync; releases injected mode |
| `StopAtNextBar` | `requestStopAtNextBar()` | musical stop at the next bar boundary |
| `Reset` | `requestStopNow()` + clock forget | immediate-serviced stop; no quiescent bridge reset while audio is active |
| Loss / discontinuity | `requestStopNow()` | cancel the grid and any future join |

`requestStopNow()` returns false (and counts) on a full queue and leaves the
worker grid state untouched, so the policy can retry. It updates `playing_`,
staged tempo/resync **only on accept**. The engine's `Clear` and `StopAtBar`
handlers now set `injActive_ = false` and release voices/notes, so the legacy
manual drum sequencer/song controls are usable again **without a device
prepare**; the engine stays attached and keeps servicing the clock queue, and a
UI Stop never forces manual play. The single bridge publisher plus FIFO command
ordering means an already-queued join is cancelled by the Clear ordered after it
— never resurrected, never overwritten.

## Processor integration and build contract

- `GuitarCompanionProcessor` keeps the frozen `submitJamCommand` /
  `readJamLiveState` signatures and `IJamLiveControl` inheritance unchanged.
- The session is created once in the constructor and persists across
  `prepareToPlay` re-entry. `prepare()` publishes a cold prepared state for the
  new generation before the worker starts, and `release()` publishes a cold
  released state after the worker is joined, so a stale prepared/playing payload
  can never leak. The latest-value slot is never destroyed under a UI reader.
- UI commands are tagged with the generation at submission; the worker rejects a
  mismatched tag (counted in `userCommandDrops`). The command queue is never
  index-reset while a producer may be racing.
- `prepareToPlay` releases the session (quiescent), adopts either the injected
  test tracker (`injectedTest`) or, when `JAM_LIVE_BTRACK_AVAILABLE` is defined,
  a fresh `BTrackBackend` (`experimentalBTrack`), calls `session.prepare(...)`,
  attaches the engine at origin 0 and prepares the one 4/4 groove.
  `releaseResources` releases the session and detaches the engine.
- **Backend identity is explicit**: the session never defaults to
  `experimentalBTrack`. `setTracker(ptr, backend)` carries the tag; a missing
  tracker is `unavailable` and Start is rejected, never simulated.
- **CMake/link needs (orchestrator-owned, not edited here):** define
  `JAM_LIVE_BTRACK_AVAILABLE` on the plugin target and link `jam-btrack` to make
  BTrack the default. Optionally `add_subdirectory(tools/live-jam-pipeline)`
  after `jam-core` for the portable driver.
- Additive test/replay seam: `setJamTrackerForTesting(std::unique_ptr<IRhythmTracker>)`
  now returns `bool` and refuses while the session is prepared (audio active);
  it is valid before the first prepare and after `release`, so a later injection
  is never silently ignored. The frozen facade is unchanged. EVAL-LIVE-001 must
  document injected vs actual backend evidence separately.

## Judgement calls (for review)

1. **Stop vs StopAtNextBar vs Reset.** `Stop` is the bounded next-serviced-block
   cancel/clear (`requestStopNow`), `StopAtNextBar` is the musical bar stop, and
   `Reset` is the immediate-serviced stop plus clock forget. The earlier
   contradiction (Reset documented as a next-bar stop) is eliminated.
2. **Loss keeps the intent.** A Lost clock cancels the join and stops the grid
   but leaves `requestedRunning` true so recovery is automatic; only UI
   Stop/Reset clears the intent. This is deliberate loss recovery, not an
   automatic resume after a UI Stop.
3. **Half/Double re-phase.** `syncBridgePhaseToClock` issues a bar resync after a
   tempo-octave command so both grids agree at the next downbeat. The tempo
   itself still only reaches the engine through `applySnapshot`.
4. **Cancellation is ordered, not erased.** A stop cannot remove a command
   already in the bounded FIFO; it publishes a `Clear` after it. The engine
   applies the join then the clear, net stopped. This is the enqueue-overwrite
   proof.
5. **`publishAudioCursor` is monotonic.** A device reset always passes through
   `prepare` (origin 0), so a backwards cursor is not reachable through the audio
   API; the defensive backwards branch in `advanceClockTo` remains for a
   corrupt/overflowed caller. Forward jumps beyond one minute are discontinuities
   and are counted **once** per event (the cursor re-anchor and the bridge's
   `setClockSample` rejection are the same event).

## Verification performed by this task

- `jam-core` standalone build (`-Wall -Wextra -Wpedantic`) and full `jamTests`:
  **250 tests, 212605 checks, 0 failed** (was 234 before this extension).
- New/updated deterministic portable suites: `jamjoinpolicy` (13 cases) and
  `livejamsession` (32 cases), plus 4 new `DrumClockBridge` cases; runnable as
  `ctest -R 'jam\.(livejamsession|jamjoinpolicy|DrumClockBridge)'`.
- Actual `DrumEngine` + bridge driver, linked against the **readonly** prebuilt
  JUCE objects of `build-INT-DRUM-001-integration/product` with the changed
  `DrumEngine.cpp`/`DrumClockBridge.cpp`/`DrumClockBridgeTests.cpp` recompiled
  from this worktree: **79 cases, 0 failed**, including 3 new
  join→Stop→manual-resume engine cases and the callback allocation probe.
- `tools/live-jam-pipeline/live_jam_pipeline_driver` end-to-end trace: PASS.
- `src/PluginProcessor.cpp` and `src/PluginEditor.cpp` syntax-checked against
  the prebuilt JUCE/NAM include environment (0 errors).

Not verified here (orchestrator / EVAL-LIVE-001): actual-processor callback
allocation/lock counters, device/ASIO deadlines, and real-guitar lock trials.
`JamLiveState` is frozen and has no `stopPending` field; a pending stop is
visible to the UI as `requestedRunning == false && drumsPlaying == true`.

## Changed bridge header hashes (for the orchestrator's EVAL pin amendment)

The explicit-ownership extension touched `src/jam/DrumClockBridge.h` (added
`requestStopNow()`), `src/DrumEngine.h`/`.cpp` (Clear/Stop release injected mode)
and `src/jam/JamJoinPolicy.h` (intent/sent/echo-ack). The frozen
`src/jam/JamLiveInterface.h` and `docs/research/LIVE-JAM-CONTRACT.md` were **not**
edited; the EVAL replay's original bridge-header pin needs an orchestrator-side
pre-measurement amendment that preserves the original hash.
