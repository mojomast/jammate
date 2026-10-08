# INT-DRUM-001 — Real MusicalClock → actual DrumEngine clock bridge

## Goal
Replace the integration gap left by MOD-002 (the adapter's output could not touch
`DrumEngine`) with a real, boundary-safe bridge: the stable `jam::MusicalClock`
belief must drive the **actual** `DrumEngine`, across the audio-thread boundary,
through a fixed-capacity POD command queue with no allocation, locking or
blocking on either side. The engine must render the groove at exact sample
offsets on an absolute sample timeline.

## Base / worktree
- Base commit `677727c` (`main`).
- Branch `wp/INT-DRUM-001-clock-bridge`.
- Worktree `/home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge`.
- Scratch `/home/mojo/projects/build-INT-DRUM-001-worker` (build, objects, logs).

## Files changed / added
| File | Change |
|---|---|
| `src/jam/DrumClockBridge.h` | new — POD command, fixed-capacity queue, explicit-clock bridge |
| `src/jam/DrumClockBridge.cpp` | new — boundary/state implementation |
| `src/DrumEngine.h` | additive — injected-clock audio owner surface/state |
| `src/DrumEngine.cpp` | additive — command service + injected step scheduler |
| `tests/jam/DrumClockCommandTests.cpp` | new — portable contract suite |
| `tests/DrumClockBridgeTests.cpp` | new — actual engine + hosted MIDI sink |
| `tools/drum-clock-bridge/run.py` | new — fresh-compile/link/run driver |
| `tools/drum-clock-bridge/README.md` | new — driver notes |
| `docs/research/DRUM-CLOCK-BRIDGE.md` | new — design/evidence/limitations |
| `task-notes/INT-DRUM-001.md` | new — this note |

Not touched: `PluginProcessor.*`, `PluginEditor.*`, `JamDirector.*`,
`DrumTransportAdapter.*`, `MusicalClock.*`, `RhythmTypes.h`, `IDrumTransport.h`,
`JamConfig.h`, `AnalysisAudioRing.h`, all existing tests, root/jam-core
`CMakeLists.txt`, CI, ledgers. The orchestrator owns registration.

## Contract implemented
- **Worker:** `prepare`, `setClockSample(explicitSample)`, `applySnapshot`,
  `requestJoinAtNextBar`, `requestStopAtNextBar`, `requestResyncNextBeat/Bar`,
  `resetForNewSession`, diagnostics. Tempo enters through exactly one path
  (`applySnapshot` → `stageTempo` → clamp) and is applied only at the next bar
  boundary.
- **Audio:** `DrumEngine::attachClockBridge`, `prepareInjectedGroove`,
  `detachClockBridge`; `process()` services ≤4 commands per callback and renders
  the injected groove. `Clear`/`StopAtBar` release hosted notes at the exact
  sample; the injected timeline advances every callback.
- **Queue:** `jam::rt::CommandQueue<DrumClockCommand, 16>` — drop-incoming and
  count on overflow, byte-copy publication, one producer / one consumer.
- **Discontinuity:** backwards or >`maxForwardJumpSamples` movement re-anchors
  and publishes `Clear`; `position()` comes from the explicit clock.
- **No double authority:** meter fields in `ClockSnapshot` are ignored; the
  clock is the only tempo source; no `playing`/`syncFlag` atomic is flipped by
  the worker to fake timing.

## Commands executed
```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-INT-DRUM-001-worker/tmp
cd /home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge
python3 tools/drum-clock-bridge/run.py

# platform-neutral integration the orchestrator will also exercise:
cmake -S jam-core -B /home/mojo/projects/build-INT-DRUM-001-worker/jamcore -G Ninja
cmake --build /home/mojo/projects/build-INT-DRUM-001-worker/jamcore -j 2
ctest --test-dir /home/mojo/projects/build-INT-DRUM-001-worker/jamcore --output-on-failure
```

## Results
- Driver build log: **0 warnings, 0 errors**.
- Actual `DrumEngine` integration (`DrumClockBridgeTests`): **10 cases, 0 failed**.
  - exact next-bar join across block sizes 333/700/1000;
  - phase-continuous boundary tempo change (120→150 BPM);
  - exact stop + sample-exact note release; resync bar/beat on arbitrary targets;
  - pressure drops counted, invalid/late commands handled without partial apply;
  - **30-minute** horizon with zero drift (step count + exact last-step sample);
  - standalone manual transport unchanged;
  - injected callback allocates nothing under `DRUM_MIDI_HEAP_PROBE`.
- Portable bridge suite: **11 tests, 96 checks, 0 failed**.
- Existing drum regression suites against the changed engine: **50 cases, 0 failed**.
- `jam-core`: new ctest `jam.DrumClockBridge` **1/1 Passed**; full ctest
  **21/21** with `TMPDIR` set. (Without `TMPDIR`, `jam.RhythmDerivedGenerator`
  fails with `OSError: [Errno 28] No space left on device` — the environment's
  `/tmp` is a 100%-full tmpfs, unrelated to this change.)

## Evidence (actual engine)
The integration tests host a real `juce::AudioPluginInstance` MIDI sink and read
the engine's `MidiBuffer` note-ons/offs with their absolute sample positions. The
join test proves the first kick lands on sample 96000 for block sizes that do not
divide a bar; the tempo test proves the old bar's last interval stays 6000 while
the new bar's first interval is 4800 with the downbeat unmoved; the stop test
proves no note fires at/after 192000 and note-offs are emitted on 192000; the
30-minute test proves the last fired step is exactly on the closed-form grid.
Portable and JUCE binaries and the reused-input hashes are recorded in
`/home/mojo/projects/build-INT-DRUM-001-worker/manifest.json`.

## Limitations
- Not full production wiring and no G4/G1 safety claim. The orchestrator must
  feed `setClockSample` from the real device timeline, feed `applySnapshot` from
  the clock, and call `prepareInjectedGroove`/`attachClockBridge` on the message
  thread before audio.
- No safety claim for arbitrary third-party plugins; the allocation measurement
  covers this engine with a no-op guest and the MIDI scratch bounded by
  `drum::midiScratchBytesForBlock`.
- One 4/4 Rock groove only (no style/fill/planner, no injected swing); velocity
  humanisation still flows from the existing `humanVel`/`humanTime`/`humanRR`
  atomics.
- Injected mode engages on the first `JoinAtBar`; after that the manual UI
  timeline is ignored for rendering (preserved standalone when no bridge is
  attached).
- No `JamTypes.h` exists; the frozen core types are in `src/jam/RhythmTypes.h`.

## Final commit
`<filled below>` on `wp/INT-DRUM-001-clock-bridge`.
