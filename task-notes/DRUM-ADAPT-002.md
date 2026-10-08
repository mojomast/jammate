# DRUM-ADAPT-002 — adaptive drum pattern change / one-bar fill

## Goal
Advance the existing INT-DRUM-001 injected-clock seam from a single frozen Rock
slice to a bounded, adaptive one, without a second tempo authority and without
allocating, locking or messaging on the audio callback. The Jam Director (or the
orchestrator wiring it) must be able to request a groove change and/or a one-bar
fill for the next strictly future bar, with intensity/swing/humanization, and get
a truthful accepted/rejected answer.

## Base / worktree
- Base commit `984ad1d` (`wp/DRUM-ADAPT-002`, from published `c8f87a8`).
- Worktree `/home/mojo/projects/worktrees/DRUM-ADAPT-002`.
- Scratch `/home/mojo/projects/build-DRUM-ADAPT-002-worker` (objects, logs, manifest).

## Files changed / added
| File | Change |
|---|---|
| `src/jam/DrumClockBridge.h` | additive — `BarChange` verb, payload fields, `requestBarChange`, `requestFillAtNextBar`, staged-change state |
| `src/jam/DrumClockBridge.cpp` | additive — bounded off-callback staging, coalescing, stale/NaN/index rejection |
| `src/DrumEngine.h` | additive — bounded immutable pattern bank, adaptive state, `prepareInjectedBank`, `injectedFillPlaying` and bank/adaptive diagnostics |
| `src/DrumEngine.cpp` | additive — bank resolution off-callback, exact-boundary `BarChange`, one-bar fill + reversion, intensity/swing/humanization |
| `tests/jam/DrumAdaptiveBridgeTests.cpp` | new — portable JUCE-free bridge contract suite |
| `tests/DrumAdaptiveTests.cpp` | new — actual DrumEngine integration suite |
| `tools/drum-adaptive/run.py` | new — standalone build/run driver (probe + no-probe + portable) |
| `tools/drum-adaptive/README.md` | new — driver notes |
| `task-notes/DRUM-ADAPT-002.md` | new — this note |

Not touched: shared `CMakeLists.txt`/`jam-core/CMakeLists.txt`, CI, live
processor/editor/facade/session, JamDirector, StyleCatalog, earlier evidence,
ledgers and governing documents. The orchestrator owns test registration and the
live facade wiring.

## Contract implemented

### Worker (`jam::DrumClockBridge`)
- `bool requestBarChange (const QueuedBarChange&) noexcept` publishes ONE bounded
  `DrumClockCommandType::BarChange` command for the next **strictly future** bar
  boundary and returns whether it was accepted.
- `bool requestFillAtNextBar (LibraryIndex) noexcept` is a convenience wrapper
  that publishes a **fill-only** command: the selected groove and the director's
  intensity/swing/humanization are left untouched (the user Fill button path).
- `DrumClockCommandType::BarChange` is appended after `Clear`, so every
  pre-existing verb keeps its numeric value.
- The `DrumClockCommand` payload is still a flat trivially-copyable POD:
  `fill`, `changeFields` (a `DrumChangeField` mask), `intensity01`, `swing01`,
  `humanizeVelocity`, `humanizeTiming`, `humanizeRoundRobin` are added alongside
  the existing `groove`.
- **No false accepted state.** The staged change is latched only after the
  bounded SPSC publish is accepted. On a full queue the call returns `false`,
  counts it, and leaves the previous staging byte-identical so a later tick can
  retry. Identical repeated requests coalesce into the one staged command, so a
  director polling every tick cannot flood the 16-slot queue.
- **Rejections:** unprepared bridge, NaN/infinite parameters, index below
  `kNoLibraryEntry`, a generation older than the newest accepted bar change
  (stale), and queue overflow. Finite out-of-range parameters are clamped to the
  documented `[0,1]` domain rather than rejected.
- Stop/Clear/reset/discontinuity cancel a staged change.

### Engine (`DrumEngine`)
- A **bounded immutable bank** of up to `kMaxInjectedBankPatterns = 128`
  pre-resolved library patterns (`InjectedBankPattern`, inline storage, no
  allocation after construction). 128 is fixed with headroom over the completed
  six-style catalogue's 106 distinct groove/fill indices (cross-worker
  integration fix): the parent prepares the catalogue's unique 4/4 entries once,
  quiescently, and the runtime UI never re-parses or re-prepares during the
  callback. Prepared off the audio callback by
  `prepareInjectedBank(grooves, n, fills, n)`; each entry is validated (index in
  range, 4/4 meter) and classified by the library's own `fill` flag with kind
  matching (a groove listed as a fill, or vice versa, is skipped). Duplicates
  collapse. `prepareInjectedGroove(index)` remains the single-groove compatibility
  path and seeds the same bank.
- A `BarChange` is scheduled as a bounded event at an exact bar boundary and
  applied there, never mid-bar. A command aimed at an already-passed boundary is
  rejected whole and counted (`injectedLateCount`); an index/kind not in the bank
  is rejected whole and counted (`injectedStaleCommandCount` /
  `injectedRejectedCount`).
- **Groove changes** select the requested prepared groove at the boundary.
- **A fill plays one bar then reverts to the selected groove** at the next bar
  wrap. `injectedGroove()` always reports the selected/base groove;
  `injectedFillPlaying()` is true only while the fill is the rendering pattern;
  `injectedActiveFill()` reports the fill.
- **Intensity** scales hit velocity by `0.5 + intensity01` (neutral `0.5` is the
  legacy velocity) inside the existing `[0.05,1]` clamp, so it is monotonic and
  neutral at the documented default.
- **Swing** lengthens even sixteenths and shortens odd ones by the same fraction
  (`swing01 * 60 * 0.009`), so the eight pairs in a 4/4 bar sum to exactly 16
  base steps: the next downbeat is unmoved and the engine stays on the worker
  grid.
- **Humanization** overrides the `humanVel`/`humanTime`/`humanRR` atomics only
  while an adaptive change is in force; it is bounded micro-timing jitter around
  the scheduler grid (the pre-existing `0.018 s` factor), which never moves a hit
  by more than that bound and never changes the schedule itself.
- **Defaults preserve the previous exact slice:** before any `BarChange` the
  injected renderer is byte-identical to the pre-adaptive single-groove path
  (`injAdaptive_` is false and the engine atomics are used unchanged). A join
  restarts that default slice.
- Stop/Clear/re-prepare/detach clear future events, the fill state and the
  adaptive overrides. `injFillPlaying()` is false after any of them.
- Audio-owner diagnostics added: `injectedBankSize`, `injectedBankCapacity`,
  `injectedSelectedGroove`, `injectedActiveFill`, `injectedFillPlaying`,
  `injectedAdaptiveActive`, `injectedIntensity01`, `injectedSwing01`,
  `injectedHumanize*`, `injectedBarChangeCount`, `injectedStaleCommandCount`.
  All obey the existing stop-the-world/audio-owner rule.

## Integration API the orchestrator calls
1. Message thread, all roles quiescent:
   `engine.prepareInjectedBank(grooves, nG, fills, nF)` (or
   `engine.prepareInjectedGroove(index)` for the single-slice compatibility path),
   then `engine.attachClockBridge(&bridge.commandQueue(), deviceSample)`.
2. Worker: `bridge.prepare`, `bridge.setClockSample(deviceSample)`,
   `bridge.applySnapshot(clock.snapshot())`, then
   `bridge.requestBarChange(change)` / `bridge.requestFillAtNextBar(fill)`.
3. Processor telemetry: `engine.injectedGroove()` (selected/base) and
   `engine.injectedFillPlaying()` published as `activeGroove` / `fillPlaying`,
   matching the appended live telemetry and playback echo. The processor is NOT
   edited here (orchestrator owns the live facade).

## Tests

### Portable (`tests/jam/DrumAdaptiveBridgeTests.cpp`, suite `DrumAdaptiveBridge`)
Device-free bridge contract: POD payload, strictly-next-bar publication,
coalescing, same-boundary updates, full-queue false-without-latch + retry, stale
generation, NaN/infinite, out-of-domain index, finite clamping, fill-only
payload, staging release after the boundary, Stop/StopNow/reset/discontinuity
cancellation, determinism, and that the legacy join is unaffected.

### Actual engine (`tests/DrumAdaptiveTests.cpp`)
Drives the real `DrumEngine` through the bounded queue with a hosted MIDI sink and
the internal sampler: default slice byte-identical to the pre-adaptive path;
groove change exactly on the next bar; one-bar fill then reversion to the
selected groove with correct `injectedGroove`/`injectedFillPlaying`; coalesced
last-wins; monotonic intensity (velocity + internal-sampler peak); swing moves
offbeats but not downbeats; bounded humanization jitter; unprepared index
and late command rejected whole with the pattern still playing; join + same-bar
change composition; StopNow/StopAtBar cancellation and manual recovery;
bank meter/kind rejection; re-prepare preserving bank and selection; a fixed
128-slot bank capacity/regression loading the actual 4/4 library with a
high-slot change and high-slot fill; and the heap-probe callback-allocation gate
with adaptation active (including a large-bank bounded allocation-free scan).
## Commands executed
```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-DRUM-ADAPT-002-worker/tmp
cd /home/mojo/projects/worktrees/DRUM-ADAPT-002
python3 tools/drum-adaptive/run.py

# platform-neutral integration the orchestrator will also exercise:
cmake -S jam-core -B /home/mojo/projects/build-DRUM-ADAPT-002-worker/jamcore -G Ninja
cmake --build /home/mojo/projects/build-DRUM-ADAPT-002-worker/jamcore -j 2
ctest --test-dir /home/mojo/projects/build-DRUM-ADAPT-002-worker/jamcore --output-on-failure
```

## Results
Driver (`tools/drum-adaptive/run.py`, command log
`/home/mojo/projects/build-DRUM-ADAPT-002-worker/driver-run.log`):
- Combined JUCE binary **with** `DRUM_MIDI_HEAP_PROBE` + `--wrap`: **98 cases,
  0 failed** (15 new adaptive actual-engine cases + the 128-slot bank
  capacity/bounded-scan case + 32 INT-DRUM-001 cases + all existing drum suites
  in ONE link, proving the shared probe has a single definition).
- Combined JUCE binary **without** the macro and without the wrap flags:
  **95 cases, 0 failed** (the three allocation-probe cases are skipped by the
  macro), proving every test compiles and runs on a default/Windows-style
  configuration.
- Portable JUCE-free suites (`DrumClockBridge` + `DrumAdaptiveBridge`):
  **47 tests, 500 checks, 0 failed**.
- Binary SHA-256s, hashes of every consumed project input (all compiled sources
  **and** every consumed header plus the driver itself, `projectInputs`),
  reused-input hashes, the exact `sourceHead`, the dirty status
  (`gitClean`/`gitStatus`), and exact commands are in the corrected clean-commit
  receipt folder
  `/home/mojo/projects/build-DRUM-ADAPT-002-worker/receipt-<head>/manifest.json`
  (earlier logs/receipts are preserved, not overwritten).

`jam-core` (platform-neutral integration):
- Auto-globbed `tests/jam/DrumAdaptiveBridgeTests.cpp` and registered
  `jam.DrumAdaptiveBridge` with no CMake edit; full ctest **28/28 passed** with
  `TMPDIR` set; zero build warnings/errors. The new suite itself is
  **20 tests, 202 checks, 0 failed**.

## Limitations
- Not full production wiring and no G4/G1 safety claim. The orchestrator feeds
  `setClockSample` from the real device timeline and `applySnapshot` from the
  clock, and owns processor/editor/facade changes.
- The bridge cannot validate library index kind/meter (the library is JUCE); the
  engine rejects a command naming an unprepared entry and counts it, but the
  bridge has already returned `true`. The orchestrator must request only prepared
  indices; this is the one remaining director-side contract.
- The allocation measurement covers this engine with a no-op guest and the MIDI
  scratch bounded by `drum::midiScratchBytesForBlock`; no claim about arbitrary
  third-party plugins.
- One meter (4/4) and one-bar patterns, matching the frozen library and the
  existing bridge grid. Swing correction is exact only for the even step count of
  4/4 (sixteen); other meters are refused at bank preparation.
- `injAdaptive_` is an audio-thread flag toggled only inside the injected loop;
  manual/audition/song paths read the legacy atomics unchanged.

## Final commit
This note ships in the tested handoff commit; the worktree is clean and the
driver re-run records that exact commit as `sourceHead` in
`/home/mojo/projects/build-DRUM-ADAPT-002-worker/manifest.json`. The commit SHA is
reported in the handoff message.
