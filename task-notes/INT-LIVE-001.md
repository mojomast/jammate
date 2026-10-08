# INT-LIVE-001 — First audible live Jam: guitar → analysis → clock → drums

Worktree: `/home/mojo/projects/worktrees/INT-LIVE-001-pipeline`
Branch: `wp/INT-LIVE-001-pipeline`
Base: `88893e24be328f131b5df673078ff934a46ed5ab`
Handoff commit: `c561de31510bedb14d157178519d0dd90b725bf8` (initial implementation)
Extension commit: `900afeddc278c193a0421e6f6028efca5cd51345`
Frozen inputs: `docs/research/LIVE-JAM-CONTRACT.md`, `src/jam/JamLiveInterface.h`
Research: `docs/research/LIVE-JAM-PIPELINE.md`

## Ownership extension (post-review rework)

The independent review blocked `f3151f0`. This worktree now also owns a minimal,
bounded runtime-stop / transport-release extension:

| Surface | Change |
|---|---|
| `src/jam/DrumClockBridge.h/.cpp` | new `requestStopNow()` (bounded cancel/Clear, next serviced block, accept-only worker state update) |
| `src/DrumEngine.h/.cpp` | `Clear` and `StopAtBar` release injected mode (`injActive_=false`) + flush voices/notes, so the legacy manual transport resumes without a device prepare |
| `tests/jam/DrumClockCommandTests.cpp` | 4 new `requestStopNow` bridge regressions |
| `tests/DrumClockBridgeTests.cpp` | 3 new actual-engine join→Stop→manual-resume regressions |
| `src/jam/JamJoinPolicy.h/.cpp` | intent / sent / echo-ack separated; reject-retry; stop persists until stopped echo |
| `src/jam/LiveJamSession.h/.cpp` | explicit backend tag; discontinuity counted once; cold state on prepare/release; generation-tagged echo |
| `src/PluginProcessor.h/.cpp` | `setJamTrackerForTesting` returns bool and rejects while prepared; explicit backend tag |

The frozen `src/jam/JamLiveInterface.h` and `docs/research/LIVE-JAM-CONTRACT.md`
were **not** edited. `src/jam/DrumClockBridge.h` changed by addition only
(`requestStopNow()`); the EVAL replay's original bridge-header pin needs an
orchestrator-side pre-measurement amendment that preserves the original hash.
No editor/UI, old clock/analyzer, shared root CMake/CI or ledger edits; no
agents.

## Scope delivered

### New (worker-owned) files

- `src/jam/LiveJamSession.h/.cpp` — owns the bounded analysis ring, the existing
  `RhythmAnalyzer`, the `MusicalClock`, the `JamJoinPolicy`, the
  `DrumClockBridge` and one control-worker thread. Audio entry `pushAudio`/
  `publishAudioCursor`/`publishDrumEcho`; UI entry `submitCommand`/`readState`.
- `src/jam/JamJoinPolicy.h/.cpp` — deterministic minimal director.
- `tests/jam/LiveJamSessionTests.cpp` — 32 deterministic cases.
- `tests/jam/JamJoinPolicyTests.cpp` — 13 pure state-machine cases.
- `tools/live-jam-pipeline/*` — portable end-to-end driver (links jam-core only).
- `docs/research/LIVE-JAM-PIPELINE.md` — architecture, judgement calls, hashes.

### Edited (worker-owned)

- `src/PluginProcessor.h/.cpp` — guitar-only chunked tap, absolute session cursor,
  session lifecycle, drum echo, frozen facade definitions.
- `src/DrumEngine.h/.cpp`, `src/jam/DrumClockBridge.h/.cpp` — see extension table.
- `tests/jam/DrumClockCommandTests.cpp`, `tests/DrumClockBridgeTests.cpp`.

`jam-core` and its tests pick the new sources up through the existing
`CONFIGURE_DEPENDS` globs, so no shared CMake change is required.

## P1/P3 fixes

- **Join/stop accept semantics.** The session no longer treats a bridge join/stop
  as applied before `requestJoinAtNextBar`/`requestStopNow`/`requestStopAtNextBar`
  returns true. A rejected join/stop stays wanted and is retried; `joinPending`
  cannot latch forever on a full queue.
- **Stop cannot be dropped.** A still-playing echo no longer clears a pending
  stop; the stop resolves only on the stopped echo. `Stop` publishes a bounded
  cancel/Clear (`requestStopNow`) ordered after any queued join.
- **No auto-resume.** `Stop`/`Reset`/`Lost` cancel any future join; a stopped
  policy never rejoins from a snapshot. Only an explicit Start re-arms.
- **Engine release.** `Clear`/`StopAtBar` set `injActive_=false` and flush notes,
  so manual transport resumes without a device prepare; the engine stays attached
  and keeps servicing the clock queue.
- **Backend identity.** `setTracker(ptr, backend)` tags the backend explicitly;
  the session never defaults to `experimentalBTrack`. Missing tracker →
  `unavailable`, Start rejected.
- **Release coherent.** `release()` publishes a cold released state after the
  worker is joined; `prepare()` publishes a cold prepared state for the new
  generation before the worker starts. No stale payload leaks.
- **Discontinuity counted once** (cursor re-anchor and bridge rejection are the
  same event).
- **Generation-tagged echo** ignored when stale.

## Build / link contract (orchestrator-owned — NOT edited here)

1. `target_compile_definitions(GuitarCompanion PRIVATE JAM_LIVE_BTRACK_AVAILABLE)`
2. link `jam-btrack` (build `jam-core` with `-DJAM_ENABLE_BTRACK=ON`).

Without the macro the session reports `unavailable`, Start is rejected with
`JamLiveFailure::unavailableBackend`, no simulator fallback. Portable driver:
`add_subdirectory(tools/live-jam-pipeline)` after `jam-core`.

## Commands run and results (this rework)

```sh
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
export PATH=/tmp/opencode/venv/bin:$PATH

# portable core (scratch <=1 GiB, 2 jobs)
cmake -S jam-core -B /home/mojo/projects/build-INT-LIVE-001-worker/jamcore \
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAM_CORE_BUILD_TESTS=ON
cmake --build .../jamcore --target jamTests -j 2
./jamTests                         # 250 tests, 212605 checks, 0 failed
./jamTests jamjoinpolicy.          # 13 tests, 0 failed
./jamTests livejamsession.         # 32 tests, 0 failed
ctest -R 'jam\.(livejamsession|jamjoinpolicy|DrumClockBridge)'  # 100% passed

# portable driver
g++ -std=c++17 -O2 -I src tools/live-jam-pipeline/live_jam_pipeline_driver.cpp \
    .../jamcore/libjam-core.a -lpthread -o live_jam_pipeline_driver
./live_jam_pipeline_driver         # RESULT: PASS

# actual DrumEngine + bridge driver, readonly prebuilt JUCE objects
# (relink reused build-INT-DRUM-001-integration/product JUCE objs + assets,
#  with DrumEngine/DrumLibrary/DrumGenerator/DrumClockBridgeTests recompiled from
#  this worktree and jam-core replaced by the new build)
./GuitarCompanionTests             # 79 cases, 0 failed (incl. intdrum_ + heap probe)

# processor syntax check against the INT-DRUM-001 prebuilt JUCE/NAM env
#   src/PluginProcessor.cpp -fsyntax-only  -> 0 errors
#   src/PluginEditor.cpp    -fsyntax-only  -> 0 errors
```

## Limitations / not covered here

- No full product build or actual-processor replay (orchestrator / EVAL-LIVE-001):
  callback allocation/lock counters, device/ASIO deadlines and real-guitar lock
  trials remain separate evidence gates.
- The actual-engine driver recompiles the changed production/test TUs and links
  the readonly prebuilt JUCE objects; it is strong evidence for the engine seam
  but not the full processor.
- `JamLiveState` is frozen: a pending stop is surfaced as
  `requestedRunning == false && drumsPlaying == true`.
- No scoped TSan run was possible here; the concurrent UI-read/worker test is a
  bounded smoke test, not a race proof.
- Shared scratch/product directories were not rebuilt or deleted; gitlinks
  untouched.
