# First audible live Jam — frozen integration seam

Base: `b7e3be1`; interface freeze precedes worker implementation.
`src/jam/JamLiveInterface.h` is orchestrator-owned. Changes to its names or
semantics require explicit integration coordination.

## Target

Live guitar input → bounded analysis ring → analyzer → observations → Musical
Clock → minimal join/follow policy → DrumClockBridge → actual DrumEngine.
One prepared 4/4 Rock groove, real Start/Stop, tap, beat/bar resync, half/double,
freeze/resume and tempo-mode controls. Tracker selection remains open: the
existing default BTrack adapter is an explicitly experimental live backend.
The TRACK-008 decorator remains diagnostic. Builds without a live backend
report it unavailable and reject Start.

## Thread and time ownership

- The audio callback taps mono guitar post input gain, before gate/effects and
  before drums. It publishes bounded chunks of at most2048 samples. A larger
  callback must not silently truncate its remainder. Queue full drops incoming
  chunks and records loss; callback work is bounded by the actual input size.
- One analyzer thread owns the tracker. One control worker owns the clock,
  join policy and bridge publication. UI submits commands from one message
  thread; it never starts/joins workers or touches the renderer.
- One session-relative absolute uint64 audio sample counter advances for every
  callback, including while Jam is stopped. It is not a DAW playhead. Prepare
  establishes origin0 and the device rate; audio and worker use that same domain.
- Observation event time, input-data horizon and externally observed audio
  cursor at control-worker receipt stay distinct. The cursor provides block-
  resolution receipt availability, not fabricated sub-block timing or measured
  device latency. Preserve raw observations and stale-generation/drop evidence.
- The clock advances from the audio cursor even during silence. Tracker output
  never writes drum tempo. The minimal director joins only from a usable clock
  lock, holds through holdover, and stops safely on loss/reset. The published
  state distinguishes accepted Start, pending join and actual audio-owner
  playback echo; UI must not claim sound from a scheduled command.
- `DrumEngine` plain injected getters are read only on the audio owner; publish
  a bounded coherent echo for the worker. UI reads only `JamLiveState` through
  `IJamLiveControl`, never plain engine/clock/analyzer state.
- Bridge attach/detach, pattern preload, reset and prepare require audio,
  producer and readers quiescent. Lifecycle prepare/release/destruction stop
  and join workers before resetting queues or destroying dependencies. Closing
  the editor neither tears down the pipeline nor leaves dangling UI callbacks.

## Frozen public facade

`GuitarCompanionProcessor` implements `jam::IJamLiveControl` using the exact
`submitJamCommand` / `readJamLiveState` signatures. Both are bounded cross-thread
operations. No new synchronous renderer setters or UI-triggered joins.
The UI polls at its normal timer rate and retains the previous whole state on a
failed/unchanged read. Initial telemetry is zero/unavailable, never simulated.
Unimplemented style/intensity/complexity/fill controls are visibly unavailable.

## Ownership

| Worker | Owned surfaces |
|---|---|
| INT-LIVE-001 | `PluginProcessor.h/.cpp` (private implementation plus frozen facade definitions), new JUCE-free live session/policy modules and portable tests; new pipeline documentation/task note |
| UI-LIVE-001 | `PluginEditor.h/.cpp`, `ui/JamOverlay.h/.cpp`, new live-state presentation/intent mapping and UI tests; new UI documentation/task note |
| EVAL-LIVE-001 | new replay driver, actual-processor replay harness and evidence validator/tests; new replay documentation/task note |
| Orchestrator | frozen interface/contract, shared build/CI, ledger/plan/handoff, reviews and integration |

Each worker uses an isolated worktree and committed handoff. New session headers
are worker-owned; the replay may use the frozen processor facade and injected
tracker seam, but must document actual backend vs injected evidence separately.
The replay takes an explicit freshly built product/shared archive and verifies
hashes; it must not substitute a stub processor for actual callback evidence.

## Verification scope

Meaningful checks cover raw-tap exclusion of drums/effects, oversized callback
chunking, queue pressure, receipt lag, generation changes, silence/holdover/lost,
next-bar join, stop/resync, device re-prepare, editor recreation and final shutdown.
Measure callback allocation/free/lock counters and processing overhead for the
added tap/pipeline under actual processor replays. Device/ASIO deadlines and
representative real-guitar useful-lock trials remain separate evidence gates.
Synthetic lock/replay success does not establish the ≥95% within-two-bar target.
