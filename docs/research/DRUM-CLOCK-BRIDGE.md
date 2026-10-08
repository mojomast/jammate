# Drum Clock Bridge (INT-DRUM-001)

**Task:** connect the actual, stable `jam::MusicalClock` belief to the actual
`DrumEngine` across the audio-thread boundary, without the engine learning that
beat tracking exists and without any UI-setter or atomic hack pretending to be
timing.

**Status:** implemented and tested in worktree
`/home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge`
(branch `wp/INT-DRUM-001-clock-bridge`, base `677727c`). This is a verified
integration seam, **not** full production wiring, and it makes **no G4 or G1
safety claim**.

## What was built

| Piece | File | Role |
|---|---|---|
| Worker-side bridge | `src/jam/DrumClockBridge.h/.cpp` | turns a `ClockSnapshot` + director intent into bounded POD commands, timed on an explicit audio sample |
| Audio owner | `src/DrumEngine.h/.cpp` | consumes the commands on the audio thread and renders the prepared groove at exact sample offsets |
| Portable tests | `tests/jam/DrumClockCommandTests.cpp` | device-free contract tests (queue/state/clock) in jam-core |
| Integration tests | `tests/DrumClockBridgeTests.cpp` | actual `DrumEngine` + hosted MIDI sink against the bridge |
| Driver | `tools/drum-clock-bridge/run.py` | fresh-compiles the changed sources, reuses read-only JUCE objects from an existing product build, runs all suites |

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
  the **incoming** command and counts it (same policy as `AnalysisAudioRing`,
  SPEC.md 8.1/8.3).
- A command is a trivially-copyable POD with an absolute `sampleTime`,
  so publication copies bytes and never constructs anything.
- The engine consumes at most 4 commands per callback (`kMaxInjectedCommandsPerBlock`)
  and validates each command **as a whole**: an unknown type, a non-finite/out
  of-range BPM, an unprepared groove, or an over-capacity event schedule is
  rejected and counted, never half-applied.

## Why this is not `DrumTransportAdapter`

MOD-002's adapter is the right semantics (boundary quantisation, last-write-wins,
stale-generation drops, overflow policy) but the wrong time source for this
task: `advance(numSamples)` accumulates a free-running block counter. The bridge
has no such counter. `setClockSample(explicitSample)` is handed the absolute
audio sample and re-anchors its grid from it, so:

- the worker cannot drift from what is rendered;
- a backwards or implausibly large forward jump is detected as a
  **discontinuity** (re-anchor + publish `Clear`) instead of being absorbed;
- `position()` is a pure function of the explicit clock, not of how many blocks
  happened to have been processed.

The boundary arithmetic (anchored sample→beat map, ceil-snapped boundary,
phase-preserving re-anchor) mirrors the adapter and `MusicalClock::applyResync`.

## Contract decisions (judgement calls)

1. **Tempo is quantised to the next bar boundary.** The clock is the only tempo
   authority; the bridge never smooths, predicts or re-derives. This is stricter
   than the adapter (which applies tempo at the next block start) and is what
   makes "tempo updates only at musical boundaries" true. Phase is preserved:
   the interval entering the boundary is unchanged; only the new bar uses the
   new tempo.
2. **Meter is fixed and single-authority.** `beatsPerBar`/`beatUnit` from the
   snapshot are ignored; accepting them live would reinterpret the grid. The
   engine's `prepareInjectedGroove` refuses non-4/4 grooves.
3. **Lost holds, it does not clear.** `ClockLockState::Lost` means the clock
   stopped asserting a grid, so the bridge holds the last tempo and ignores new
   snapshots until the lock recovers. `Clear` is reserved for the explicit
   `Reset`/lifecycle and for discontinuities.
4. **A discontinuity loses the injected transport.** `Clear` stops rendering and
   flushes hosted notes at the block origin; a fresh `JoinAtBar` is required to
   play again.
5. **Stale snapshots never win.** A snapshot whose generation is not newer than
   the last accepted one is dropped and counted.
6. **Late commands apply, never vanish.** A command whose target sample is
   already in the past is applied at the current block origin and still counted.
7. **The engine owns note release.** `StopAtBar`/`Clear` cause the engine to add
   note-offs for every GM voice at the exact stop sample; the worker never
   reaches into MIDI/UI state.

## Timing properties proven by tests

- **Next-bar join is exact.** For block sizes that do not divide a bar
  (333/700/1000), the first downbeat lands on sample 96000 exactly, at the
  correct within-block offset — not rounded to the block start.
- **Tempo change is phase-continuous and boundary-exact.** Joining at 120 BPM
  and applying a 150 BPM snapshot: the last interval of the old bar is 6000
  samples, the downbeat stays at 192000, and the first interval of the new bar is
  4800 samples.
- **Stop / resync are sample-exact.** Stop fires no note at/after its boundary
  and releases the hosted kit there; resync-bar and resync-beat place their
  target on an arbitrary sample (110000 / 111000).
- **Pressure is bounded.** 40 join requests into capacity 16 drop 24, each drop
  is counted by the queue and reported by the engine; invalid commands are
  rejected whole and leave the running tempo untouched.
- **No drift over 30 minutes.** At 120 BPM / 48 kHz, after 30 minutes the fired
  step count matches the closed form and the last step sample is exactly
  `96000 + (steps-1)*6000` — no per-step rounding accumulated.
- **Standalone is unchanged.** With no bridge attached, the manual transport
  reproduces the historical sample timings (and the existing 50-case drum
  regression suite passes against the changed engine).
- **The injected callback allocates nothing** when the MIDI scratch is reserved
  (`DRUM_MIDI_HEAP_PROBE`, ELF-wrapped malloc/realloc, 4000 blocks with a tempo
  event applied mid-window).

## Reproduction

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-INT-DRUM-001-worker/tmp   # /tmp is a full tmpfs
cd /home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge
python3 tools/drum-clock-bridge/run.py
```

The product Ninja build is only read for its compile/link flags; nothing in it is
modified. The driver fresh-compiles `DrumEngine.cpp`, the new bridge and the new
tests, and reuses the read-only JUCE module objects plus
`libGuitarCompanionAssets.a` from
`/home/mojo/projects/build-RT-003-integration/product`.

## Honest limitations

- **Not full production wiring.** Nothing in `PluginProcessor`/`PluginEditor`/
  `JamDirector` was changed. The orchestrator owns calling `setClockSample` with
  the real device sample position, `applySnapshot` with the clock belief, and
  `prepareInjectedGroove`/`attachClockBridge` on the message thread.
- **No safety claim for third-party plugins.** The no-allocation result measures
  *this* engine with a no-op `juce::AudioPluginInstance` sink. Arbitrary VST3
  guests can allocate; the hosted MIDI scratch is bounded by
  `drum::midiScratchBytesForBlock()` for this engine's generation only.
- **One 4/4 Rock groove.** No style/fill/planner policy, no swing in the injected
  grid, no crash/break handling. Velocity humanisation still comes from the
  existing `humanVel`/`humanTime`/`humanRR` atomics (tests zero them for exact
  timing); swing is not applied in the injected grid.
- **`prepareInjectedGroove` is a message-thread preload** that must run before
  the injected transport is used; it parses the library spec with JUCE and
  writes plain POD bytes the audio thread reads.
- **No `JamTypes.h` exists** in this repository: `ClockSnapshot` and the other
  frozen core types live in `src/jam/RhythmTypes.h`.
- **G1/G4 remain open.** No live callback-timing trace and no full end-to-end
  director run were performed.
