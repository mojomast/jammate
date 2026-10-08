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

## Results (post-review)
- Driver build log: **0 warnings, 0 errors**.
- Combined JUCE binary (new integration + all six existing drum suites in ONE
  link, same `DRUM_MIDI_HEAP_PROBE`/`--wrap` flags): **72 cases, 0 failed**. This
  proves the shared probe (`tests/DrumHeapProbe.cpp`) has a single definition.
- Portable bridge suite: **16 tests, 140 checks, 0 failed**.
- `jam-core`: `jam.DrumClockBridge` **1/1 Passed**; full ctest **21/21** with
  `TMPDIR` set. (Without `TMPDIR`, `jam.RhythmDerivedGenerator` fails with
  `ENOSPC` because the environment's `/tmp` is a full tmpfs, unrelated.)

## Review correction (BLOCK1–BLOCK3, GAP1–GAP8)
- **BLOCK1** — a join now carries the staged effective BPM, and supersedes only
  events at/before its boundary, so snapshot-then-join and join-then-snapshot
  produce the same first bar (test `intdrum_join_coherent_tempo_snapshot_before_and_after`
  + portable `joinCarriesStagedEffectiveTempo`).
- **BLOCK2** — resync is a single absolute phase statement (`phaseStep`, floor of
  the containing beat) applied identically by both roles; the worker's next bar
  and the engine's next downbeat now coincide (tests
  `intdrum_resync_reconciles_worker_and_engine_phase`,
  `intdrum_resync_then_tempo_at_actual_downbeat`, portable `resyncCarriesPhaseStep`).
- **BLOCK3** — the malloc/new probe is a single shared TU
  (`tests/DrumHeapProbe.h/.cpp`); `tests/DrumMidiTests.cpp` was minimally
  refactored onto it; the driver links the new suite and all six existing suites
  into one binary to prove no duplicate symbols.
- **GAP1** — `isAudible()` includes an attached bridge so the processor skip
  guard serves the first join on the internal path; added embedded-sampler and
  fallback-synth energy tests and a processor-like guarded test; added
  `samplesLoaded()`.
- **GAP2** — `attachClockBridge(queue, audioSampleAtAttach)` declares the absolute
  timeline origin; late attach at 48000 puts the first join at 144000 with no
  delay; `maxExplicitSample` (2^53) domain reject and uint64-wrap note documented.
- **GAP3** — `DrumClockBridge::prepare` clears the grid and drains the queue;
  `DrumEngine::prepare` preserves a prepared groove, so a second prepare at
  96 kHz still works (tests `intdrum_second_prepare_at_new_rate_keeps_groove`,
  portable `prepareClearsGridAndDrainsQueue`).
- **GAP4** — same-type/same-target events coalesce (last wins); distinct-target
  overflow is rejected and counted (test `intdrum_same_boundary_tempo_snaps_coalesce`).
- **GAP5** — documented that `injected*()` getters are audio-owner/quiescent-only
  and `prepareInjectedGroove` is message-thread with all roles quiescent.
- **GAP6** — Clear/Stop release hosted notes immediately at the correct offset,
  before any later Join in the same callback (test
  `intdrum_clear_then_join_same_callback_orders_release_first`).
- **GAP7** — stop is a pending state committed at its boundary; `playing()` stays
  true until then (tests `intdrum_stop_pending_keeps_position_until_boundary`,
  portable `stopIsPendingUntilItsBoundary`).
- **GAP8** — real `injectedLateCount()` replaces the fake claim; the 30-minute
  drift test now also runs a non-divisor BPM (127) and verifies an actual late
  MIDI kick against the closed form; tautological queue getter checks were
  replaced with asserted counts (24 drops / 16 serviced).

## Evidence (actual engine)
The integration tests host a real `juce::AudioPluginInstance` MIDI sink and read
the engine's `MidiBuffer` note-ons/offs with their absolute sample positions, and
separately render the internal sampler (embedded GMRockKit, `samplesLoaded`) and
the fallback synth to real audio energy. They prove: the first kick lands on
sample 96000 for block sizes that do not divide a bar; the old bar's last interval
stays 6000 while the new bar's first interval is 4800 with the downbeat unmoved,
in both command orders; a resync on a non-aligned target makes the worker's next
bar and the engine's next downbeat the same sample (207000), where a subsequent
stop/tempo land exactly; stop/Clear release hosted notes before any same-callback
join's note-ons; a 30-minute run at 127 BPM has an actual late MIDI kick on the
closed-form sample. Combined binary SHA-256 and per-source/reused-input hashes are
in `/home/mojo/projects/build-INT-DRUM-001-worker/manifest.json`.

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

## Integration notes (orchestrator)
- `src/jam/DrumClockBridge.cpp` is globbed into `jam-core` automatically, so the
  portable suite and the library pick it up with no CMake edit.
- The JUCE test target does not link `jam-core`; registering
  `tests/DrumClockBridgeTests.cpp` therefore also requires adding
  `src/jam/DrumClockBridge.cpp` to `tests/CMakeLists.txt` (or linking `jam-core`).
- Production wiring additionally needs a `DrumClockBridge` instance owned
  alongside `DrumEngine`, `engine.prepareInjectedGroove(rockIndex)` on the
  message thread, `engine.attachClockBridge(&bridge.commandQueue())`, and a
  worker that calls `bridge.setClockSample(deviceSample)` +
  `bridge.applySnapshot(clock.snapshot())` and requests the join/stop/resync
  from the director. The plugin currently links neither `jam-core` nor the
  bridge, so this is net-new wiring, not a toggle.

## Final commit
`8950873cb32a796590b730d7a7bf84a81459142d` on `wp/INT-DRUM-001-clock-bridge`.
This note is finalized in the immediate follow-up commit; the worktree is clean
after both.
