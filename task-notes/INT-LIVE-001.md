# INT-LIVE-001 — First audible live Jam: guitar → analysis → clock → drums

Worktree: `/home/mojo/projects/worktrees/INT-LIVE-001-pipeline`
Branch: `wp/INT-LIVE-001-pipeline`
Base: `88893e24be328f131b5df673078ff934a46ed5ab`
Handoff commit: `c561de31510bedb14d157178519d0dd90b725bf8`
Frozen inputs: `docs/research/LIVE-JAM-CONTRACT.md`, `src/jam/JamLiveInterface.h`
Research written: `docs/research/LIVE-JAM-PIPELINE.md`

## Scope delivered

Real processing path (not just contracts), JUCE-free control core plus processor
wiring.

### New (worker-owned) files

- `src/jam/LiveJamSession.h/.cpp` — owns the bounded analysis ring, the existing
  `RhythmAnalyzer`, the `MusicalClock`, the `JamJoinPolicy`, the
  `DrumClockBridge` and one control-worker thread. Audio entry `pushAudio`/
  `publishAudioCursor`/`publishDrumEcho`; UI entry `submitCommand`/`readState`.
- `src/jam/JamJoinPolicy.h/.cpp` — deterministic minimal director: join once from
  a `Locked` clock, hold through `Holdover`, safe stop on `Lost`/discontinuity,
  no auto-resume after UI Stop.
- `tests/jam/LiveJamSessionTests.cpp` — 24 deterministic cases (join/echo,
  half/double/freeze/resync/mode, stop/reset, chunking, ring pressure,
  generation reject, receipt/future/aged, clock-from-cursor, discontinuity,
  worker shutdown, second prepare, destructor join).
- `tests/jam/JamJoinPolicyTests.cpp` — 9 pure state-machine cases.
- `tools/live-jam-pipeline/live_jam_pipeline_driver.cpp` + `CMakeLists.txt` +
  `README.md` — portable end-to-end driver (links jam-core only), PASS.
- `docs/research/LIVE-JAM-PIPELINE.md` — architecture, ownership, judgement
  calls, build contract, verification.

### Edited (worker-owned)

- `src/PluginProcessor.h` — forward declarations; additive public
  `setJamTrackerForTesting`; private session/cursor/test-tracker members. Frozen
  `submitJamCommand`/`readJamLiveState` signatures and `IJamLiveControl`
  inheritance unchanged.
- `src/PluginProcessor.cpp` — guitar-only tap (post input gain, pre-effects/drums)
  chunked at 2048 with exact sample times; one absolute session cursor advanced
  every callback; session creation in the ctor; one-shot tracker handover,
  `session.prepare`, engine attach at origin 0 and 4/4 groove prepare in
  `prepareToPlay`; `release` + detach in `releaseResources`/dtor; bounded drum
  echo after `processDrums`; the frozen facade definitions.

No editor/UI, old-core, `DrumEngine`, shared-root CMake/CI, ledger or frozen
interface edits. `jam-core` and its tests pick the new sources up through the
existing `CONFIGURE_DEPENDS` globs, so no shared CMake change is required.

## Build / link contract (orchestrator-owned — NOT edited here)

To make the existing experimental BTrack adapter the default live backend, the
orchestrator must, on the plugin target:

1. `target_compile_definitions(GuitarCompanion PRIVATE JAM_LIVE_BTRACK_AVAILABLE)`
2. link the `jam-btrack` target (defined by `third_party/BTrack/CMakeLists.txt`,
   built by `-DJAM_ENABLE_BTRACK=ON` in `jam-core`).

Without the macro the session reports `unavailable`, `Start` is rejected with
`JamLiveFailure::unavailableBackend`, and there is no simulator fallback. To
register the portable driver in CI, `add_subdirectory(tools/live-jam-pipeline)`
after the `jam-core` target (registers `jam.LiveJamPipeline`).

## Commands run and results

```sh
# scratch (<=1 GiB, 2 jobs)
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
export PATH=/tmp/opencode/venv/bin:$PATH
cmake -S jam-core -B /home/mojo/projects/build-INT-LIVE-001-worker/jamcore \
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAM_CORE_BUILD_TESTS=ON
cmake --build /home/mojo/projects/build-INT-LIVE-001-worker/jamcore \
      --target jamTests -j 2
./jamTests                       # 234 tests, 212476 checks, 0 failed
./jamTests jamjoinpolicy.        # 9 tests,  0 failed
./jamTests livejamsession.       # 24 tests, 0 failed
ctest -R 'jam\.(livejamsession|jamjoinpolicy)'   # 100% passed

# portable driver
g++ -std=c++17 -O2 -I src tools/live-jam-pipeline/live_jam_pipeline_driver.cpp \
    <jamcore>/libjam-core.a -lpthread -o live_jam_pipeline_driver
./live_jam_pipeline_driver       # RESULT: PASS

# processor syntax check against the INT-DRUM-001 prebuilt JUCE/NAM environment
# (extracted compile command from the product's build.ninja, source repointed):
#   src/PluginProcessor.cpp -fsyntax-only  -> 0 errors
#   src/PluginEditor.cpp    -fsyntax-only  -> 0 errors
```

The `jam-core` library builds with `-Wall -Wextra -Wpedantic`; the existing
`AnalysisAudioRingTests` mismatch-new-delete warning is pre-existing. The
processor syntax check emits only pre-existing JUCE/plugin warnings.

## Limitations / not covered here

- No full product build or actual-processor replay (orchestrator /
  EVAL-LIVE-001): callback allocation/lock counters, device/ASIO deadlines and
  real-guitar lock trials remain separate evidence gates.
- The replay must document injected vs actual backend evidence separately; the
  additive `setJamTrackerForTesting` seam is the injected path.
- `publishAudioCursor` is monotonic; a backwards cursor only reaches the worker
  through a `prepare` that resets origin 0. The defensive backwards branch in
  `advanceClockTo` is untested through the public audio API but covered in
  principle by the bridge's own discontinuity tests.
- Half/Double re-phase: the clock stays the authority and the bridge follows via
  a next-downbeat resync; see `LIVE-JAM-PIPELINE.md` judgement call 3.
- Scratch build dir `/home/mojo/projects/build-INT-LIVE-001-worker` used; no
  shared scratch/product directory was rebuilt or deleted. Gitlinks untouched.
