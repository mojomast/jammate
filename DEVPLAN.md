# Adaptive Guitar Jam Companion — Development Plan

**Document:** DEVPLAN.md  
**Status:** Draft v0.1  
**Research date:** 2026-10-07  
**Companion spec:** `SPEC.md`  
**Base candidate:** `raphaelfukuda/Guitar-Companion@88f7e7c805c9c5e17388154a678c2c6a3633ff23`

This plan is intentionally structured for an orchestration agent directing **less-capable parallel subagents**. Work is decomposed so workers can execute bounded tasks with narrow file ownership and objective acceptance checks.

**Live status:** [`EXECUTION-LEDGER.md`](EXECUTION-LEDGER.md) is authoritative.
On resumption the user requested `deepseek/deepseek-flash` workers. Offline
G3/G4 work has advanced under recorded deviations; G0/G1 are still partial.

---

# 1. Orchestration model

## 1.1 Roles

### Orchestrator

Owns:

- architecture
- integration order
- interface changes
- task assignment
- conflict resolution
- selecting the production beat tracker
- final acceptance gates
- updates to `SPEC.md` / `DEVPLAN.md`
- edits to central legacy files unless a work package explicitly grants ownership

The orchestrator does not dump broad “improve the app” instructions on workers.

### Worker subagent

Owns exactly one work package.

A worker:

- reads the listed context;
- edits only allowed files;
- does not redesign architecture;
- writes tests before declaring done;
- produces evidence;
- commits a narrow change;
- records uncertainties instead of improvising around interfaces.

### Reviewer subagent

Optional but recommended after each wave.

Reviews:

- spec compliance
- real-time safety
- tests
- dependency/license impact
- accidental scope expansion
- merge conflicts or interface drift

The reviewer does not rewrite the whole implementation unless assigned a repair package.

---

# 2. Parallelism policy

Default maximum: **3 implementation workers in parallel**, plus the orchestrator.

Reason:

- the upstream project still has large central files;
- too many concurrent workers would create false speed through merge conflict;
- three lanes allow audio, analysis, and UI/content work to progress independently.

Increase parallelism only after the new modules have stable ownership boundaries.

---

# 3. Branch/worktree policy

Each work package gets:

```text
branch: wp/<TASK-ID>-<short-name>
worktree: ../worktrees/<TASK-ID>
evidence: task-notes/<TASK-ID>.md
```

Example:

```text
wp/RT-001-callback-safety
wp/EVAL-001-guitar-corpus
wp/UI-001-jam-overlay
```

Rules:

1. One task, one branch, one primary owner.
2. Workers do not merge other worker branches.
3. Workers do not reformat unrelated files.
4. Workers do not “fix nearby things” without a new task.
5. Every branch ends in a clean working tree.
6. Every handoff includes commit SHA and exact test commands.
7. Orchestrator performs integration.
8. If an interface is wrong, worker reports it; worker does not silently fork the contract.

---

# 4. Planned source ownership

Create these seams early.

```text
src/
  jam/
    RhythmTypes.h
    AnalysisAudioRing.h
    RhythmAnalyzer.h/.cpp
    IRhythmTracker.h
    BTrackBackend.h/.cpp
    AubioBackend.h/.cpp
    MusicalClock.h/.cpp
    JamDirector.h/.cpp
    JamConfig.h
    StyleCatalog.h/.cpp
    DrumTransportAdapter.h/.cpp
    Diagnostics.h/.cpp

  ui/
    JamOverlay.h/.cpp

tools/
  rhythm-eval/
    ...

tests/
  jam/
    ...

docs/
  adr/
  research/
```

Legacy ownership:

| Legacy file | Normal owner |
|---|---|
| `src/PluginProcessor.*` | Orchestrator/integration task only |
| `src/PluginEditor.*` | UI integration task only |
| `src/DrumEngine.*` | Drum integration task only |
| `src/DrumOverlay.*` | Existing advanced drum editor; avoid touching for Jam MVP |
| `CMakeLists.txt` | Build/integration task only after initial baseline |
| third-party manifests | dependency task only |

A worker implementing BTrack should not also wire it into `PluginProcessor.cpp`. It should expose a tested module; a later integration task performs the small central edit.

---

# 5. Required worker handoff format

Each worker creates `task-notes/<TASK-ID>.md`:

```markdown
# <TASK-ID>

## Goal
One paragraph.

## Base
Starting commit SHA.

## Files changed
Exact paths.

## Contract implemented
Interfaces/behavior.

## Tests
Exact commands and results.

## Evidence
Metrics, screenshots, traces, or fixture output.

## Known limitations
Concrete remaining issues.

## Integration notes
Anything the orchestrator must wire.

## Commit
Final SHA.
```

“No issues” is acceptable only when supported by test evidence.

---

# 6. Global gates

No later wave begins until its required gate passes.

## G0 — Fork/license/baseline accepted

- exact upstream SHA frozen
- dependency/license inventory captured
- clean baseline build documented
- baseline tests run
- baseline audio path measured
- AGPL distribution assumption accepted

## G1 — Real-time foundation accepted

- callback reachability audited
- known message-posting/blocking hazards removed or isolated
- callback-safety instrumentation exists
- CI builds/tests the project
- no new feature code yet

## G2 — New module seams accepted

- analysis queue/types compile
- drum adapter seam exists
- mock Jam UI compiles
- new modules do not require central-file co-ownership

## G3 — Tracker selected

- BTrack/aubio/optional BeatNet benchmark run
- guitar corpus metrics committed
- tracker ADR written
- chosen backend justified by evidence

## G4 — Musical Clock accepted

- synthetic tempo/phase tests green
- live analysis pipeline cannot block audio
- acquisition/lock/holdover/lost behavior deterministic
- diagnostics can explain decisions

## G5 — Adaptive drummer accepted

- styles/director/drum adapter integrated
- no direct raw detector -> drum BPM path
- fills/pattern transitions quantized
- low-confidence behavior safe

## G6 — MVP UX accepted

- guitarist can configure audio and jam without opening advanced drum editor
- user correction controls work
- persistence works
- diagnostics available

## G7 — Release candidate accepted

- offline evaluation
- hardware stress
- real-guitar play tests
- packaging
- license manifest
- regression suite

---

# 7. Wave 0 — Freeze and understand the base

Run three workers in parallel.

---

## FND-001 — Fork provenance and dependency inventory

**Goal:** Freeze the exact upstream source and prove what may legally/technically ship.

**Read:**

- `README.md`
- `LICENSE`
- `THIRD_PARTY.md`
- `.gitmodules`
- `CMakeLists.txt`
- `references/README.md` if present

**Allowed writes:**

- `docs/research/BASELINE.md`
- `docs/research/DEPENDENCIES.md`
- `docs/adr/0001-base-selection.md`

**Steps:**

1. Record upstream repository and SHA `88f7e7c805c9c5e17388154a678c2c6a3633ff23`.
2. Enumerate all submodules and pinned revisions.
3. Record license, redistribution status, and runtime/build-only status for each.
4. Separate “studied reference” repos from linked/vendored code.
5. Record sample/font/data licenses.
6. Confirm Guitar-Companion’s AGPLv3 implications.
7. Record BTrack, aubio, BeatNet, JJazzLab, Basic Pitch as proposed-but-not-yet-shipped dependencies.
8. Write ADR explaining why Guitar-Companion is preferred over Giada/Hydrogen for the MVP.

**Done when:**

- every shipped third-party component has source/version/license data;
- closed-source distribution is clearly marked incompatible with this fork assumption;
- no code changes.

---

## FND-002 — Reproducible baseline build and test record

**Goal:** Establish a known-good baseline before changing behavior.

**Allowed writes:**

- `scripts/build-baseline.ps1`
- `docs/research/BASELINE-BUILD.md`
- `task-notes/FND-002.md`

Do not edit production source.

**Steps:**

1. Fresh recursive clone/submodule init.
2. Configure Release build.
3. Build Standalone and VST3.
4. Build opt-in tests.
5. Run all current tests.
6. Record exact compiler/CMake/JUCE versions.
7. Record generated binary paths and hashes.
8. Launch standalone using deterministic dev flags.
9. Record reference audio settings.
10. Record current callback CPU reading and reported latency on available hardware if hardware is attached.

**Acceptance:**

- one script reproduces build/test from clean checkout;
- current failures are documented rather than hidden;
- no unrelated fixes.

---

## FND-003 — Callback and architecture reachability map

**Goal:** Produce an auditable map of everything reachable from `processBlock` and `DrumEngine::process`.

**Allowed writes:**

- `docs/research/RT-REACHABILITY.md`
- `docs/research/CODE-MAP.md`

**Steps:**

1. Trace `PluginProcessor::processBlock`.
2. Enumerate every directly called processing function.
3. Trace model/plugin swap paths.
4. Trace recording path.
5. Trace scene-change path.
6. Trace drum processing.
7. Flag:
   - alloc/free
   - locks
   - async/message posting
   - file/network
   - unbounded loops
   - third-party calls
8. Specifically examine `triggerAsyncUpdate()` reachable from the scene envelope.
9. Compare against current JUCE `AsyncUpdater` docs.
10. Rank findings P0/P1/P2.

**Acceptance:**

- every callback-reachable subsystem has a status;
- each P0 finding has a proposed bounded replacement;
- no implementation changes.

---

# 8. Gate G0

Orchestrator:

1. reviews the three reports;
2. confirms fork/license decision;
3. records actual baseline failures;
4. updates spec only if evidence changes assumptions;
5. creates integration branch for Wave 1.

Do not proceed if baseline cannot be reproduced.

---

# 9. Wave 1 — Stabilize before adding intelligence

Three parallel workers.

---

## RT-001 — Remove/contain callback-unsafe control-plane work

**Goal:** Make callback behavior conform to the real-time contract.

**Depends on:** FND-003.

**Primary ownership:**

- minimal approved edits to `src/PluginProcessor.*`
- new helper files under `src/rt/` if useful
- tests/instrumentation

**Required first target:**

Replace callback-reachable `triggerAsyncUpdate()` with a bounded RT-safe signal such as:

- atomic pending flag consumed by message-thread timer, or
- fixed-capacity command queue drained off the audio thread.

**Steps:**

1. Fix each P0 callback finding from `RT-REACHABILITY.md`.
2. Do not add blocking replacements.
3. Add debug counters/assertions for forbidden paths where practical.
4. Verify retired object destruction happens outside callback.
5. Verify recorder FIFO failure never causes a wait; count dropped recording blocks if applicable.
6. Preserve behavior.
7. Add regression tests or deterministic instrumentation.

**Acceptance:**

- no callback message posting;
- no known callback allocation/deallocation;
- no callback blocking lock/I/O;
- Release build green;
- existing audio behavior preserved.

---

## CI-001 — Continuous integration baseline

**Goal:** Make every branch prove that it builds and tests.

**Allowed writes:**

- `.github/workflows/*`
- build scripts
- test CMake files only where needed

**Steps:**

1. Add Windows x64 Release build job.
2. Initialize required submodules.
3. Build tests with `GUITAR_COMPANION_BUILD_TESTS=ON`.
4. Run `ctest`.
5. Build Standalone/VST3.
6. Cache safely where useful, but correctness must not depend on cache.
7. Add artifact upload for test logs only if small/useful.
8. Ensure secrets/store access are not required.

**Acceptance:**

- clean GitHub runner builds and tests;
- failure exits non-zero;
- no network credential required.

Optional later job: compile platform-neutral `jam-core` on Linux once it exists.

---

## TEST-001 — Expand foundation tests

**Goal:** Turn known fragile behavior into regression tests.

**Allowed writes:**

- `tests/*`
- minimal production changes only if the orchestrator pre-approves testability seams

**Add tests for:**

- strict/invalid groove parsing behavior
- duplicated groove identity handling
- meter step calculation
- drum bar serialization edge cases
- deterministic groove generation
- transport start/stop/restart
- BPM changes at safe boundaries
- state load of empty/missing fields where unit-testable

Known lenient-parser behavior must either become explicit compatibility behavior or be fixed with versioning; do not silently keep undefined parsing.

**Acceptance:**

- tests are deterministic;
- test failure is actionable;
- current behavior changes only with explicit approval.

---

# 10. Gate G1

Required:

- CI green
- RT-001 evidence green
- expanded tests green
- orchestrator signs off callback contract

No rhythm-analysis feature merges before G1.

---

# 11. Wave 2 — Create low-conflict module seams

Three parallel workers.

---

## MOD-001 — Rhythm types + fixed analysis queue

**Goal:** Implement infrastructure without choosing a tracker.

**Create:**

- `src/jam/RhythmTypes.h`
- `src/jam/AnalysisAudioRing.h`
- `tests/jam/AnalysisAudioRingTests.cpp`

**Do not edit:** `PluginProcessor.*`

**Requirements:**

- fixed capacity
- SPSC
- preallocated
- bounded push/pop
- explicit overflow result
- no allocation after prepare
- generation/sample-time metadata

**Tests:**

- wraparound
- full
- empty
- producer/consumer sequence
- overflow count
- variable audio block sizes up to configured maximum

---

## MOD-002 — Drum transport adapter seam

**Goal:** Hide `DrumEngine` behind an interface the Jam Director can control.

**Create:**

- `src/jam/IDrumTransport.h`
- `src/jam/DrumTransportAdapter.h/.cpp`
- unit tests using a fake transport where possible

**Do not edit:** `DrumEngine.*` in this task.

**Contract should expose:**

- prepare
- start/stop
- set stable BPM/clock snapshot
- queue groove for bar boundary
- queue fill
- set intensity/humanization controls
- transport position snapshot

If existing DrumEngine cannot satisfy a method, record an integration requirement rather than bypassing the abstraction.

---

## MOD-003 — Jam UI shell with fake data

**Goal:** Create the user-facing Jam screen independently of live analysis.

**Create:**

- `src/ui/JamOverlay.h/.cpp`
- mock state provider for development

**Do not edit:** `PluginEditor.*` yet.

UI includes:

- style
- mode
- intensity
- complexity
- fill amount
- follow tightness
- start/stop
- tap/resync/half/double/fill
- candidate BPM
- clock BPM
- confidence
- lock state
- input meter

Use fake timer-driven data so UI can be tested without audio.

**Acceptance:**

- compiles;
- resizes cleanly;
- no audio-engine dependency beyond an interface;
- screenshot evidence.

---

# 12. Gate G2

Orchestrator performs three small integration edits:

1. register new sources in CMake;
2. verify interfaces do not overlap ownership;
3. optionally wire JamOverlay entry button using one central edit.

Do not wire real analysis yet.

---

# 13. Wave 3 — Build the guitar rhythm evaluation system

Three parallel workers.

---

## EVAL-001 — Guitar rhythm corpus and manifest

**Goal:** Create the evidence set used to choose algorithms.

**Create:**

```text
testdata/rhythm/
  manifest.json
  README.md
  <redistributable fixtures>
```

If larger/private recordings cannot be committed, manifest paths and hashes may point to local fixtures while synthetic fixtures remain in repo.

**Cases:**

- clean 8ths
- clean 16ths
- distorted power chords
- palm mute
- shuffle
- syncopated funk
- sparse single notes
- arpeggio
- sustained chord
- missing downbeat
- stop/start
- tempo ramp up
- tempo ramp down
- 3/4
- 6/8
- noisy mic
- low input
- clipping
- mute/tap percussion

**Metadata:**

- sample rate
- meter
- nominal BPM
- beat timestamps
- ownership/license
- scenario tags

No copyrighted commercial songs.

---

## EVAL-002 — Evaluation harness and metrics

**Goal:** Make tracker comparisons one command.

**Create:**

```text
tools/rhythm-eval/
tests/rhythm-eval/
```

**Inputs:**

- corpus manifest
- backend command/library adapter
- ground truth

**Outputs:**

- machine-readable JSON/CSV
- Markdown summary
- per-fixture diagnostics

**Metrics:**

- lock/acquisition time
- BPM relative error
- half/double error
- beat timing precision/recall/F-score
- mean/percentile phase error
- false beats in silence
- recovery delay
- CPU time

Harness must support deterministic synthetic observation tests separately from audio tests.

---

## TRACK-001 — BTrack backend

**Goal:** Integrate BTrack behind `IRhythmTracker` without touching the audio callback.

**Allowed:**

- `third_party` dependency declaration/pin
- `src/jam/BTrackBackend.*`
- backend tests
- evaluation tool wiring

**Do not edit:** `PluginProcessor.*`

**Requirements:**

- pinned version/SHA
- license entry
- sample-rate behavior documented
- any resampling performed off callback
- reset deterministic
- no hidden global state
- backend can run in offline evaluation harness

**Acceptance:**

- known click fixture passes;
- guitar corpus can run;
- memory/runtime metrics reported.

---

# 14. Wave 4 — Tracker shootout

Three parallel workers.

---

## TRACK-002 — aubio backend

Same contract as TRACK-001.

Do not claim superiority; provide measurements.

---

## TRACK-003 — BeatNet research benchmark

**Goal:** Determine whether BeatNet adds enough guitar-specific value to justify a Python/neural dependency.

This is **not a shipping integration**.

May be a standalone Python tool.

Measure:

- accuracy
- acquisition
- CPU/GPU usage
- startup time
- package size
- behavior on solo guitar vs mastered-song assumption

No BeatNet code/model is added to release packaging in this task.

---

## EVAL-003 — Robustness scenarios

Extend harness with:

- syncopation burst
- silence
- octave ambiguity
- tempo step
- gradual tempo ramp
- noisy onset injection
- low-level input
- clipping

Produce a normalized comparison table.

---

# 15. Gate G3 — Select the production tracker

**Orchestrator task: ADR-TRACKER-001**

Create `docs/adr/0002-rhythm-tracker-selection.md`.

Required:

- exact backend versions
- corpus SHA/version
- metric table
- packaging complexity
- CPU
- failure modes
- selection
- fallback backend if primary fails

The winner may be BTrack, aubio, or another candidate discovered by evidence.

Do not ship multiple trackers merely because they were tested unless redundancy has a clear product benefit.

---

# 16. Wave 5 — Musical Clock and live analysis pipeline

Three parallel workers.

---

## CLOCK-001 — Musical Clock core

**Create:**

- `src/jam/MusicalClock.h/.cpp`
- `src/jam/JamConfig.h`
- `tests/jam/MusicalClockTests.cpp`

**Input:** synthetic `RhythmObservation`.

**Implement:**

- Acquiring
- Locked
- Holdover
- Lost
- tempo candidates
- half/double resolution
- confidence history
- tempo smoothing/slew
- phase correction
- Fixed / Follow / Loose
- explicit Tap/Resync/Half/Double/Freeze commands

No audio/device code.

**Required tests:**

- perfect 120 BPM
- noisy 120 BPM
- extra offbeat onsets
- missing beats
- 120 -> 130 ramp
- half-time candidate
- double-time candidate
- 2-bar silence
- prolonged silence
- explicit resync
- freeze/unfreeze

Output must be deterministic.

---

## ANALYSIS-001 — Live RhythmAnalyzer worker

**Create:**

- `src/jam/RhythmAnalyzer.h/.cpp`
- selected tracker wiring
- worker lifecycle tests

**Input:** `AnalysisAudioRing`.

**Output:** latest `RhythmObservation` / clock input queue.

**Requirements:**

- worker starts/stops cleanly
- never owns audio device
- drains faster than real time in benchmark
- handles queue overrun
- no UI calls
- resampling only here, not callback
- thread-safe shutdown

Do not edit `PluginProcessor.*`.

---

## DIAG-001 — Rhythm diagnostics/trace

**Create:**

- `src/jam/Diagnostics.h/.cpp`
- trace serializer outside RT path
- debug view model

Capture:

- input sample time
- candidate BPM
- confidence
- clock BPM
- phase
- state
- overruns
- CPU/processing duration
- later: selected groove/fill

**Acceptance:**

- tracing off has negligible overhead;
- audio thread only publishes bounded data;
- export occurs outside callback.

---

# 17. Wave 5 integration task — INT-ANALYSIS-001

Owned by orchestrator or one senior integration agent.

**Allowed central edits:**

- `PluginProcessor.*`
- CMake

**Steps:**

1. Allocate `AnalysisAudioRing` in prepare path.
2. Insert analysis tap **post input gain, pre gate/effects**.
3. Push bounded audio block plus sample time.
4. Never wait.
5. Start/stop analyzer with processor lifecycle.
6. Expose clock snapshot to non-audio consumers.
7. Record queue drops.
8. Run RT instrumentation again.

**Gate failure:** any callback allocation/block/message post.

---

# 18. Gate G4

Required:

- MusicalClock synthetic tests green
- live analysis ring has no nominal overruns
- RT gate still green
- tracker + clock trace is understandable
- Free Jam can display “Acquiring/Locked” without starting drums

---

# 19. Wave 6 — Styles, director, and adaptive drum control

Three parallel workers.

---

## STYLE-001 — Style schema and catalog

**Create:**

- `src/jam/StyleCatalog.h/.cpp`
- `assets/styles/*.json` or equivalent
- schema tests

**Goal:** Map the existing groove/fill library into explicit musical roles without immediately rewriting the whole library.

Start with at least:

- Rock
- Hard Rock/Metal
- Blues
- Funk
- Pop
- Shuffle

Each has:

- BPM range
- supported meter
- low/medium/high groove lists
- fills
- humanization defaults
- repetition rules

Validate every referenced groove ID.

---

## DIRECTOR-001 — Jam Director core

**Create:**

- `src/jam/JamDirector.h/.cpp`
- tests

**Consumes:**

- ClockSnapshot
- energy/onset features
- user settings
- StyleCatalog

**Emits:**

- JamIntent
- pending groove/fill/break decisions

**Implement:**

- Idle/Listening/ReadyToJoin/Playing/Holdover/Reacquiring/Stopping
- join next safe bar
- intensity envelope
- complexity tiers
- fill probability
- phrase boundary counter
- low-confidence fill suppression
- deterministic seeded selection
- no immediate pattern repeat

No direct DrumEngine edits.

---

## DRUM-001 — Adapt DrumEngine for clock-safe control

**Primary ownership:** `DrumEngine.*`, `DrumTransportAdapter.*`

**Goal:** Make existing drum scheduling consume stable Musical Clock control without raw per-block detector chasing.

Tasks:

1. Determine whether current `stepLenSamples()` can safely accept smoothed clock BPM.
2. Add boundary-safe BPM update mechanism if required.
3. Add queued pattern change at bar boundary.
4. Add queued fill at requested bar.
5. Preserve existing advanced drum-editor behavior.
6. Add transport tests.
7. Keep scheduling sample accurate.

Do not add music-selection policy; that belongs in JamDirector.

---

# 20. Gate G5

Integration task `INT-JAM-001`:

- connect MusicalClock -> JamDirector -> DrumTransportAdapter;
- verify detector cannot write DrumEngine BPM directly;
- run synthetic end-to-end scenarios;
- run real guitar with simple style;
- trace join/fill/tempo decisions;
- re-run real-time safety gate.

At this gate, UI may still be ugly. The drummer behavior matters first.

---

# 21. Wave 7 — MVP user experience

Three parallel workers.

---

## UI-001 — Production Jam screen

Build on MOD-003.

Wire to a view-model interface, not directly to engine internals.

Required:

- mode
- style
- intensity
- complexity
- fills
- tightness
- balance
- Start Listening
- Stop
- Tap
- Resync
- Half
- Double
- Fill
- lock/confidence display
- input and drum levels
- current/next musical action

Accessibility:

- keyboard focus
- readable status without color alone
- large stage-friendly actions
- scalable layout

---

## AUDIO-001 — Jam-focused device/latency diagnostics

Extend or reuse `AudioOverlay`.

Add:

- backend
- device
- channel
- sample rate
- buffer
- block duration
- reported latency
- CPU
- analysis overrun
- clear low-latency setup help

Do not hard-code “ASIO always best”; present actual available backends/capabilities.

---

## PERSIST-001 — Jam session persistence

Add versioned Jam settings.

Test:

- save/load
- missing fields
- future fields ignored
- corrupted file fallback
- atomic write
- no live clock lock persisted

---

# 22. Gate G6

A non-developer test flow must work:

1. install/run;
2. pick input and headphone output;
3. see input meter;
4. select Rock;
5. choose Loose Follow;
6. play;
7. see Acquiring -> Locked;
8. drummer joins;
9. use Fill;
10. use Half/Double/Resync;
11. stop on bar;
12. reopen app and retain preferences.

No advanced drum editor required for this journey.

---

# 23. Wave 8 — End-to-end behavior and stress

Three parallel workers.

---

## E2E-001 — Free Jam journey tests

Automate what can be deterministic with synthetic/recorded input.

Scenarios:

- 100 BPM clean strum
- 140 BPM distorted
- 90 BPM shuffle
- syncopation
- stop/start

Assert:

- lock state
- join boundary
- stable BPM
- no impossible fill placement
- reacquisition

---

## E2E-002 — Follow behavior suite

Compare Fixed / Follow / Loose with identical observation stream.

Require measurable difference:

- Fixed: no tempo movement
- Follow: follows ramp
- Loose: follows ramp more slowly and ignores short disturbance

Generate plots/CSV outside production UI if useful.

---

## E2E-003 — Real-time stress suite

Reference:

- 48 kHz / 128
- guitar DSP active
- drums active
- analyzer active
- diagnostics publishing
- recorder optionally armed

Measure:

- callback p50/p95/p99/max
- deadline misses
- analysis drops
- recorder FIFO drops
- CPU
- memory growth

Run long enough to catch drift/leaks; use 30-minute reference stress for release gate.

---

# 24. Wave 9 — Musician tuning and hardening

This wave is evidence-driven. Do not invent features.

Three possible parallel lanes:

## TUNE-001 — Tempo/phase tuning

Use traces from real guitar sessions.

Adjust only centralized clock config.

Every change reruns corpus metrics.

## TUNE-002 — Drummer musicality tuning

Tune:

- intensity thresholds
- fill frequency
- anti-repeat distance
- crash rules
- dynamics smoothing
- style metadata

Do not change clock algorithm.

## BUG-001 — Hardware/edge-case repairs

Target concrete findings:

- device disconnect
- odd buffer sizes
- low signal
- clipped signal
- VST drum source
- recording
- preset restore

---

# 25. Gate G7 — Release candidate

Required artifacts:

- CI green
- dependency/license manifest
- tracker selection ADR
- RT audit
- rhythm benchmark report
- clock simulation report
- 30-minute stress report
- real-guitar play-test notes
- known issues
- install/uninstall test
- clean checkout build instructions

No release if the drummer merely “works technically” but still feels unstable.

---

# 26. Phase 2 backlog — full band

Do not begin until G7 unless explicitly reprioritized.

---

## BAND-001 — Generic accompaniment provider API

Extract drum behavior behind `IAccompanimentProvider`.

Add scheduler contracts for non-drum MIDI/audio voices.

No actual bass generation yet.

---

## HARM-001 — Harmony analyzer benchmark

Compare:

- manual chord input baseline
- Basic Pitch-derived note evidence
- Chordino or other compatible local chord estimators
- lightweight chroma/key methods

Build a guitar-specific chord corpus.

Do not place harmony inference on the audio callback.

---

## JJAZZ-001 — JJazzLab Toolkit proof of concept

Test a sidecar/provider that:

- receives chord progression + clock
- generates several bars ahead
- outputs MIDI
- never sits on the hard real-time path

Measure startup, generation latency, packaging, license impact.

---

# 27. Subagent task template

The orchestrator can paste this with one work package.

```markdown
You are worker <TASK-ID>.

Your job is ONLY the task below. Do not broaden scope.

BASE COMMIT:
<sha>

GOAL:
<one sentence>

READ FIRST:
- SPEC.md: <sections>
- DEVPLAN.md: <task>
- <source files>

YOU MAY MODIFY:
- <exact paths/globs>

YOU MUST NOT MODIFY:
- src/PluginProcessor.*
- src/PluginEditor.*
- src/DrumEngine.*
(unless explicitly granted)

STEPS:
1. ...
2. ...
3. ...

REQUIRED TESTS:
- ...

DONE WHEN:
- ...

HANDOFF:
Create task-notes/<TASK-ID>.md with:
- files changed
- behavior
- exact tests/results
- evidence
- known limitations
- integration notes
- final commit SHA

If the specified interface is insufficient, STOP IMPLEMENTING THAT PART and
document the interface problem. Do not invent an incompatible parallel design.
```

---

# 28. Reviewer task template

```markdown
Review <TASK-ID> against SPEC.md and DEVPLAN.md.

Check only:
1. scope compliance
2. correctness
3. real-time safety if callback-adjacent
4. tests
5. dependency/license changes
6. public interface drift
7. evidence quality

Return:
- PASS
or
- FAIL with a numbered list of concrete blockers.

Do not request stylistic rewrites unless they affect maintainability or safety.
```

---

# 29. Merge protocol

For every completed work package:

1. worker finishes and commits;
2. reviewer optionally reviews;
3. orchestrator inspects diff;
4. orchestrator reruns affected tests;
5. merge into integration branch;
6. run full CI;
7. update `DEVPLAN.md` status;
8. only then start dependent tasks.

When multiple tasks touch the same legacy file, they are **not parallel tasks**.

---

# 30. Suggested status table

Keep this near the top of the live devplan during implementation.

| Task | State | Branch | SHA | Gate |
|---|---|---|---|---|
| FND-001 | DONE | main | 724e6d9 + corrections | G0 |
| FND-002 | Linux formats/tests verified; hardware/Windows open | main | 677ce9f | G0 |
| FND-003 | DONE | main | 931be23 | G0 |
| RT-001 | F1 JUCE-compiled; F2 engine heap bound verified; full runtime open | main | 135b4b7 + resumption fixes | G1 |
| CI-001 | definitions integrated; actual runner execution open | main | 4376672 | G1 |
| TEST-001 | DONE: bounded foundation coverage (D8) | main | 0cfc252b + suite registration | G1 |
| MOD-001 | DONE (offline seam) | main | 89db28d + efb820b | G2 |
| MOD-002 | DONE (offline seam); real engine wiring open | main | 86544f1 | G2 |
| MOD-003 | DONE: isolated simulated shell (D7); editor wiring pending | main | 34ab2cb3 + reviewed preview | G2 |
| EVAL-001 | integrated historical corpus; versioned sustain repair + tapping audit in EVAL-006 | main | f5f5a11 + c68df60 + EVAL-006 | G3 |
| EVAL-002 | integrated; timestamp/gate follow-up underway | main | 3e5bb7b + wp/EVAL-002R | G3 |
| TRACK-001 | DONE (offline candidate) | main | c3dad10 | G3 |
| TRACK-002 | DONE (offline candidate) | main | wp/TRACK-002 + eac59ba | G3 |
| TRACK-003 | integrated feasibility; benchmark PARTIAL (no inference) | main | 1ef3d5c + integration corrections | G3 |
| EVAL-003 | DONE (scoped derived corpus) | main | 0a15eef | G3 |
| EVAL-004 | DONE (timing/gate audit + real comparison) | main | 64b39ee | G3 |
| EVAL-005 | DONE (scoped paired diagnostics; G3 open) | main | 816a955 | G3 |
| CI-002 | DONE (definitions + local execution; remote/Windows open) | main | 480f15f + research-suite guards | G1/G3 |
| EVAL-006 | DONE (versioned synthetic sustain repair + tapping audit) | main | e0e3bde + integration registration | G3 |
| TRACK-004 | DONE (scoped causal acquisition/BPM diagnosis) | main | 98da13f + integration corrections | G3 |
| RT-002 | DONE (bounded callback/timer evidence) | main | 6e89b1c + integration fixes | G1 |
| EVAL-007 | DONE (version-aware silence coverage) | main | 514c154 + CSV citation corrections | G3 |
| TRACK-005 | DONE (diagnostic-only causal BPM-report variant) | main | 7b3a4d7 + framing corrections | G3 |
| RT-003 | DONE (measured LSTM repair) | main | 401a6c1 + product/verifier fixes | G1 |
| TRACK-006 | IN PROGRESS (user-authorised Sol replacement) | wp/TRACK-006-variant-robustness | base45fa333, review pending | G3 |
| CI-003 | GitHub workflow locally verified; scope authorised, remote execution pending | main | 8f18e03 | G1 |
| CLOCK-001 | deterministic implementation integrated; G4 open | main | b78c43c | G4 |
| ANALYSIS-001 | lifecycle foundation DONE (D6); full task PARTIAL / BLOCKED:G3 | main | 5162e4d + CMake/CI registration | G4 |
| DIAG-001 | BLOCKED:G3 | | | G4 |
| STYLE-001 | BLOCKED:G4 | | | G5 |
| DIRECTOR-001 | BLOCKED:G4 | | | G5 |
| DRUM-001 | BLOCKED:G4 | | | G5 |
| UI-001 | BLOCKED:G5 | | | G6 |
| AUDIO-001 | BLOCKED:G5 | | | G6 |
| PERSIST-001 | BLOCKED:G5 | | | G6 |
| E2E-001 | BLOCKED:G6 | | | G7 |
| E2E-002 | BLOCKED:G6 | | | G7 |
| E2E-003 | BLOCKED:G6 | | | G7 |

---

# 31. What the orchestrator must resist

Do **not**:

- let a worker put a neural model in `processBlock`;
- update drum BPM directly from every detector result;
- add bass/chord AI before drummer quality is proven;
- rewrite all of Guitar-Companion before a working join/follow loop exists;
- let UI workers own audio state;
- let audio workers redesign the UI;
- merge “temporary” blocking code into the callback;
- accept a beat tracker because its README says state-of-the-art;
- use commercial songs as committed evaluation fixtures;
- allow each future instrument to maintain its own tempo estimate;
- hide queue overflow or recorder drops;
- call a feature complete without a reproducible test/evidence artifact.

---

# 32. Earliest useful vertical slice

The first meaningful end-to-end slice, after G1, is:

```text
guitar input
  -> bounded analysis tap
  -> one selected tracker
  -> MusicalClock
  -> fixed Rock groove
  -> DrumEngine
  -> headphones
```

No adaptive fills.  
No chord analysis.  
No style browser.  
No bass.  
No model generation.

Success criterion:

> Start playing steady rhythm guitar; after acquisition, drums join on a sensible boundary and remain stable while the player continues.

Once that feels solid, add musical intelligence around it instead of underneath it.
