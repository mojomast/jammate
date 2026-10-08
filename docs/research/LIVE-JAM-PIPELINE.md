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
contract, not the full Jam Director:

- a join is requested exactly once, only from a `Locked` clock;
- `joinPending` is true from the join request until the **audio-owner echo**
  confirms real playback; `drumsPlaying` is only ever the echo, never the
  scheduled command;
- `Holdover` **holds**: the drummer keeps the last anchored grid;
- `Lost` stops the grid safely but keeps the running intent, so a re-locked
  clock rejoins (loss recovery, not an automatic resume after a UI Stop);
- an explicit UI Stop/Reset clears the intent and never auto-resumes;
- a discontinuity clears the engagement and asks for a safe stop.

## Bridge / tempo authority

Exactly one tempo path reaches the engine: `DrumClockBridge::applySnapshot`,
which stages the clock's belief at the next bar boundary. No raw detector BPM is
ever written to the engine. Tap/Half/Double/Resync are applied to the
`MusicalClock` first (the phase/tempo authority); for phase-affecting commands
the session also asks the bridge to land its next downbeat on the clock's next
downbeat (`syncBridgePhaseToClock`), so the worker grid and the rendered grid
cannot drift into a permanent beat/bar offset. The bridge and the engine share
the same absolute phase statement and event ordering. Whether the half/double
re-phase should instead be a bridge-only operation is flagged for the
orchestrator; the implementation keeps the clock authoritative and the bridge a
follower.

## Processor integration and build contract

- `GuitarCompanionProcessor` keeps the frozen `submitJamCommand` /
  `readJamLiveState` signatures and `IJamLiveControl` inheritance unchanged.
- The session is created once in the constructor and persists across
  `prepareToPlay` re-entry; the latest-value state slot is never destroyed under
  a UI reader.
- `prepareToPlay` performs the one-shot tracker handover, calls
  `session.prepare(...)`, attaches the engine at origin 0 and prepares the one
  4/4 groove. `releaseResources` releases the session and detaches the engine.
- Tracker selection: this build uses `ExperimentalBTrack` only when the
  orchestrator defines `JAM_LIVE_BTRACK_AVAILABLE` and links `jam-btrack`
  (`src/btrack/BTrackBackend.h` is included behind that macro). Without it the
  session reports the backend unavailable and **rejects Start**; there is no
  simulator fallback.
- **CMake/link needs (orchestrator-owned, not edited here):** define
  `JAM_LIVE_BTRACK_AVAILABLE` on the plugin target and link `jam-btrack` to make
  BTrack the default. Optionally `add_subdirectory(tools/live-jam-pipeline)`
  after `jam-core` for the portable driver.
- Additive test/replay seam: `setJamTrackerForTesting(std::unique_ptr<IRhythmTracker>)`
  replaces the tracker adopted at the next prepare. It is public and additive;
  the frozen facade is unchanged. EVAL-LIVE-001 must document injected vs actual
  backend evidence separately.

## Judgement calls (for review)

1. **Stop semantics.** `Stop` and `StopAtNextBar` both commit a stop at the next
   bar boundary; there is no immediate hard stop from the UI path, because the
   bridge's only non-quiescent stop verbs are bar-bounded. `Reset` is the hard
   stop (and the clock forgets its belief).
2. **Loss keeps the intent.** A Lost clock stops the grid but leaves
   `requestedRunning` true so recovery is automatic; only UI Stop/Reset clears
   the intent. This is deliberate loss recovery, not an automatic resume.
3. **Half/Double re-phase.** `syncBridgePhaseToClock` issues a bar resync after a
   tempo-octave command so both grids agree at the next downbeat. The tempo
   itself still only reaches the engine through `applySnapshot`.
4. **Reset does not call `DrumClockBridge::resetForNewSession`**, which requires
   audio-consumer quiescence and cannot run on the control worker. Reset stops
   the grid through the bounded `requestStopAtNextBar` verb instead.
5. **`publishAudioCursor` is monotonic.** A device reset always passes through
   `prepare` (origin 0), so a backwards cursor is not reachable through the audio
   API; the defensive backwards branch in `advanceClockTo` remains for a
   corrupt/overflowed caller. Forward jumps beyond one minute are discontinuities.

## Verification performed by this task

- `jam-core` standalone build (`-Wall -Wextra -Wpedantic`, 0 warnings in the new
  TUs) and full `jamTests`: 234 tests, 212476 checks, 0 failures.
- New deterministic suites: `jamjoinpolicy` (9 cases) and `livejamsession`
  (24 cases), registered automatically by the `jam-core` glob and runnable as
  `ctest -R 'jam\.(livejamsession|jamjoinpolicy)'`.
- `tools/live-jam-pipeline/live_jam_pipeline_driver` end-to-end trace: PASS.
- `src/PluginProcessor.cpp` and `src/PluginEditor.cpp` syntax-checked against
  the prebuilt JUCE/NAM include environment (0 errors).

Not verified here (orchestrator / EVAL-LIVE-001): actual-processor callback
allocation/lock counters, device/ASIO deadlines, and real-guitar lock trials.
