# Drum Clock Bridge (INT-DRUM-001)

**Task:** connect the actual, stable `jam::MusicalClock` belief to the actual
`DrumEngine` across the audio-thread boundary, without the engine learning that
beat tracking exists and without any UI-setter or atomic hack pretending to be
timing.

**Status:** implemented, reviewed and corrected in worktree
`/home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge`
(branch `wp/INT-DRUM-001-clock-bridge`, base `677727c`). This is a verified
integration seam, **not** full production wiring, and it makes **no G4 or G1
safety claim**.

## What was built

| Piece | File | Role |
|---|---|---|
| Worker-side bridge | `src/jam/DrumClockBridge.h/.cpp` | turns a `ClockSnapshot` + director intent into bounded POD commands, timed on an explicit audio sample |
| Audio owner | `src/DrumEngine.h/.cpp` | consumes the commands on the audio thread and renders the prepared groove at exact sample offsets |
| Shared heap probe | `tests/DrumHeapProbe.h/.cpp` | one symbol definition of the malloc/new wrapper for ALL drum test TUs |
| Portable tests | `tests/jam/DrumClockCommandTests.cpp` | device-free contract tests (queue/state/clock) in jam-core |
| Integration tests | `tests/DrumClockBridgeTests.cpp` | actual `DrumEngine` + hosted MIDI sink + internal/fallback audio |
| Driver | `tools/drum-clock-bridge/run.py` | fresh-compiles into combined probe and no-probe binaries, reuses read-only JUCE objects, runs the portable suite |

## The boundary

```
MusicalClock (worker)            DrumClockBridge (worker)                 DrumEngine (audio)
  snapshot()  ───applySnapshot──▶  stage tempo to next bar
  explicit sample ─setClockSample▶ boundary grid (re-anchored)   ──push──▶  SPSC queue (16)
  director join/stop/resync ─request*─▶ POD DrumClockCommand     ◀──pop──   service ≤4/block
                                                                             renders at exact offset
```

- The queue is `jam::rt::CommandQueue<DrumClockCommand, 16>`: fixed capacity,
  inline storage, no allocation, no locking, no blocking. On overflow it drops
  the **incoming** command and counts it (SPEC.md 8.1/8.3).
- A command is a trivially-copyable POD with an absolute `sampleTime`. The engine
  maps it to an exact within-block offset.
- The engine consumes at most 4 commands per callback and validates each command
  **as a whole**: unknown type, non-finite/out-of-range BPM, unprepared groove,
  invalid phase, or over-capacity schedule is rejected and counted, never
  half-applied.

## Correctness decisions (all test-backed)

1. **Tempo is quantised to the next bar boundary.** The clock is the only tempo
   authority. Phase is preserved: the interval entering the boundary is
   unchanged; only the new bar uses the new tempo.
2. **A join carries the effective (staged) BPM, and both command orders
   agree.** If a `SetTempo` for the same boundary is already staged, the
   `JoinAtBar` command carries that tempo; a join only supersedes events at or
   before its own boundary. Snapshot-then-join and join-then-snapshot therefore
   produce identical first-bar MIDI.
3. **Resync is one absolute phase statement.** `ResyncBeat`/`ResyncBar` carry
   `phaseStep` (the step within the bar that must land on `sampleTime`), computed
   by the worker with the same *floor-the-containing-beat* rule. The engine sets
   that exact step, so both grids agree on every later downbeat. (An earlier
   version rounded differently and produced a permanent one-beat/one-bar offset.)
   When a tempo is staged for a boundary before the resync target, the phase is
   computed **piecewise** (`beatsAtAccountingStaged`: old rate up to the staged
   boundary, staged rate after), matching the engine, which applies that tempo
   first. The tests pin the exact grid (kick 111000, snare 183000, kick 207000)
   so the old ceil phase fails them.
4. **Stop is a pending commitment.** `requestStopAtNextBar` does not flip the
   worker's `playing`; it stays true until the boundary is crossed, mirroring the
   engine which keeps rendering to the boundary. A dropped stop command does not
   enter the pending state.
5. **Clear/Stop release hosted notes immediately, in order.** The note-off flush
   is emitted when the stop/clear is applied (offset 0 for Clear, the exact
   stop offset for Stop), so a Clear+Join in the same callback orders the release
   before the new downbeat's note-ons. `MidiBuffer` preserves insertion order for
   equal timestamps, and the test asserts that order.
6. **Same-boundary events coalesce; genuine overflow is counted.** Repeated tempo
   snaps for one boundary replace the existing event (last wins) instead of
   filling the 8-slot store; distinct-target overflow is rejected and counted.
7. **Late attach declares the absolute origin.** `attachClockBridge(queue,
   audioSampleAtAttach)` sets the engine's injected timeline to the current audio
   device sample, so a late attach cannot delay the first join by the elapsed
   distance. The worker's first `setClockSample` uses the same absolute timeline.
8. **Late commands apply at the block origin and are counted** (`injectedLateCount`),
   never silently dropped.
9. **A re-prepare is a full reset.** `DrumClockBridge::prepare` clears anchors,
   staged state, stop/snapshot bookkeeping, drains the queue and rebaselines the
   per-session queue drop count; `DrumEngine::prepare` resets transport runtime and
   its injected drop baseline but **preserves a prepared groove** (patterns are
   rate-independent), so a second prepare at a new rate keeps working.
10. **Discontinuity / domain.** Backwards or implausibly large movement re-anchors
    and publishes `Clear`; samples above `maxExplicitSample` (2^53, where doubles
    stop representing consecutive integers) are refused and counted. uint64 wrap
    is out of scope (2^64 samples).
11. **Getters are audio-owner/quiescent-only diagnostics.** `injected*()` reads
    plain audio-thread state and must not be polled from the message thread or the
    director during the callback; `prepareInjectedGroove` is message-thread and
    all roles must be quiescent. This is documented in the header.

## Threading / RT safety

- Publication (`setClockSample`, `applySnapshot`, `request*`) allocates nothing
  and never blocks; consumption (`process`) services a fixed ≤4 commands/block
  from inline arrays. Measured allocation-free with the ELF `--wrap` probe when
  the MIDI scratch is reserved (4000 blocks, tempo event applied mid-window).
- `isAudible()` returns true while a bridge is attached, so `PluginProcessor`'s
  skip guard does not starve the injected transport before the first join on the
  internal-sampler path.
- **Lifecycle is stop-the-world.** `attachClockBridge`, `detachClockBridge`,
  `DrumEngine::prepare`, `DrumClockBridge::prepare` and `prepareInjectedGroove`
  are only safe while the publisher, the audio callback and every reader are
  stopped and joined. `isAudible()` and the getters read plain pointers/state, so
  a live pointer swap or a concurrent getter is a data race; this is explicitly
  not a thread-safe hot-swap. The product must perform this wiring once, before
  the audio stream starts, and on session boundaries only.

## Evidence (actual engine, actual audio)

Run:
```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-INT-DRUM-001-worker/tmp   # /tmp is a full tmpfs
cd /home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge
python3 tools/drum-clock-bridge/run.py
```

- **Combined JUCE binary WITH `DRUM_MIDI_HEAP_PROBE` / `--wrap`** (new
  integration + all six existing drum suites in one link — proves the shared
  probe has a single definition): **75 cases, 0 failed**.
- **Combined JUCE binary WITHOUT the macro and without the wrap flags** (default
  and Windows-style config; allocation checks explicitly skipped): **74 cases,
  0 failed**.
- Build logs: **observed 0 warnings / 0 errors / 0 duplicate definitions** in
  both builds (an observation, not a `-Werror` guarantee).
  New cases include: exact next-bar join for blocks 333/700/1000; coherent tempo
  in both orders; phase-continuous boundary tempo change; exact stop + ordered
  release; pending-stop position; **exact resync grid** (kick 111000, snare
  183000, kick 207000, no bogus 183000 downbeat) then stop and tempo at the same
  actual downbeat, for both `ResyncBeat` and `ResyncBar`; piecewise resync phase
  across a staged tempo boundary; Clear+Join ordering; coalesced tempo snaps +
  counted overflow; pressure drops with asserted counts and per-session
  rebaselining; late command; **internal sampler** (embedded GMRockKit,
  `samplesLoaded`) and **fallback synth** energy; processor skip guard; late
  attach at a nonzero sample; second prepare at 96 kHz; 30-minute zero-drift;
  30-minute **fractional BPM (127) verified through actual MIDI events**;
  standalone manual regression; measured allocation-free callback.
- **Portable JUCE-free suite:** **18 tests, 150 checks, 0 failed**.
- **jam-core:** `jam.DrumClockBridge` passes; full ctest 21/21 with `TMPDIR` set.

## Honest limitations

- **Not full production wiring.** Nothing in `PluginProcessor`/`PluginEditor`/
  `JamDirector` was changed. The orchestrator owns calling `setClockSample` with
  the real device sample position, `applySnapshot` with the clock belief, and
  `prepareInjectedGroove`/`attachClockBridge` on the message thread before audio.
  The plugin links neither `jam-core` nor the bridge today.
- **No safety claim for arbitrary third-party plugins.** The allocation result
  measures *this* engine with a no-op `juce::AudioPluginInstance` sink; arbitrary
  VST3 guests can allocate. The hosted MIDI scratch is bounded by
  `drum::midiScratchBytesForBlock()` for this engine's generation only.
- **One 4/4 Rock groove.** No style/fill/planner policy, no swing in the injected
  grid. Velocity humanisation still comes from the existing `humanVel`/`humanTime`/
  `humanRR` atomics (tests zero them for exact timing).
- **No `JamTypes.h` exists**; the frozen core types are in `src/jam/RhythmTypes.h`.
- **G1/G4 remain open.** No live callback-timing trace and no full end-to-end
  director run were performed.
