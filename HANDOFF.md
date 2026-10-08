# HANDOFF.md — Adaptive Guitar Jam Companion

**Purpose of this file:** a fresh orchestrator subagent can take over this
repository, understand exactly what exists, what has been verified, what is
blocked, and what to delegate next — without re-deriving any of it.

**Updated:** 2026-10-08
**Governing documents:** `SPEC.md` (v0.1), `DEVPLAN.md` (v0.1). Treat both as
source of truth. Do not rewrite them to match the code.

## Current resumption state — read before the historical handoff below

`main` is the integration branch. `EXECUTION-LEDGER.md` holds current task/gate
status; the rest of this older handoff describes the original checkout setup.
The original uncommitted seams are long since integrated. The latest user
instruction is **Space Bunny while waiting**, exact temporary model
`opencode-go/space-bunny`. All three active Haiku continuations were
interrupted successfully and relaunched in the same sessions on DeepSeek Go.
All three continuations were rejected before execution: the model requires Global
regions in the workspace's Privacy settings. They are paused pending that change;
the requested model is retained. The user then confirmed the model is enabled;
all three continuations were retried and returned the same region rejection.
They have now been continued on Space Bunny Go with user authorisation.

New active wave starts at `bcf540a`: TRACK-007 longer paired acquisition evidence,
RT-004 broader NAM architecture callback probes, and DIAG-001 portable diagnostics
under D10. Sessions and exact ownership are in EXECUTION-LEDGER's Active development
table. None is integrated yet. The published merged-head run37716882463 at
`bcf540a` passed all six jobs: core18/19/19/20, NAM5/5, and Windows
Standalone/VST3 plus6/6 drum suites. Its receipt and downloaded-log hashes are
in `docs/research/github-ci/run-37716882463.json`.

The earlier queued model switch is superseded by the user's immediate switch.
TRACK-007 has protocol `492c5a8`; RT-004 `95e5eac` and DIAG-001 `2a9a929`
are undergoing review corrections. Commits and in-progress files are preserved.

Worker sessions and path ownership are in EXECUTION-LEDGER's resumption table:
TRACK-006 fixed paired robustness, MOD-003 isolated simulated Jam UI, and TEST-001
actual JUCE foundation regressions. All three returned committed clean handoffs;
their reviewed integration evidence follows below. The orchestrator owns CI-003
GitHub Actions plus shared build/editor/processor integration.

TEST-001 worker `0cfc252b` accepted: 12 new compatibility/transport regressions,
50 total cases; actual root test rebuild and all6/6 drum suites pass. Both forge
guards require `drums.foundation`. Scope excludes full processor migration,
concurrent control edits and device timing.

MOD-003 worker `34ab2cb3` is now accepted: engine-independent state/intent seam,
clearly simulated controls and telemetry, seven screenshot artifacts. Independent
main-source preview compilation and Xvfb checks reproduce all controls, silent
refresh, timer telemetry, queued stopping and resize coverage down to580×480.
This is a standalone silent preview; no production editor/processor wiring.

TRACK-006 worker `ce3c96e0` accepted after independent rerun: all72 current scored
fixtures/summaries match except CPU;24 beat pairs, readiness and method clocks
exact;3168 non-CPU paired rows and coverage exact (3240 total). The manifest has
22 perturbations plus2 matched baselines. Variant acquisition6→1 of24 clips,
with6 losses/1 gain; gap error34.3751%, clean0dB-noise11.7945%, funk noise2.8568%.
These are short-window diagnostics, not release-gate rates. G3 remains open.

CI-003's first fully green remote run is37715897283 at `0820bb5`: core18/19/19/20,
NAM5/5, Windows Standalone/VST3 and6/6 drum suites. The initial Windows CRLF
failure was fixed without changing any expected NAM hash. Physical audio/ASIO,
full callback timing and live join/follow remain unmeasured.

### Integrated since the original handoff

- Standalone JUCE-free core: ring, deterministic MusicalClock, transport seam,
  bounded RT signalling; optional isolated BTrack and aubio adapters.
- 19 synthetic base fixtures with hashes and distinct true-silence metadata;
  24 deterministic paired derived perturbations. Base audio remains unchanged.
- Corrected evaluation runner preserves backend event timestamps, records causal
  availability separately, and does not equate holdover beats with acceleration.
  Current comparison: `docs/research/tracker-comparison/comparison.md`.
- BTrack core acquisition 4/11 and worst BPM error 2.34%; aubio 7/11 and 1.33%.
  Both acquisition gates fail. No production tracker selected; G3 remains open.
- Partial BeatNet feasibility review (`wp/TRACK-003-beatnet` handoff `1ef3d5c`)
  integrated with final scorer corrections; **51 research-tool tests pass**.
  Source/weight hashes and dependency blockers recorded; no inference benchmark.
  Repository CC-BY-4.0 is the only stated term found; weights redistribution
  remains unresolved. See `docs/research/BEATNET-FEASIBILITY.md`.
- Paired robustness diagnostics (`816a955`) integrated: **29 tests pass**, both
  real trackers over 24 derived clips, 2160 metric rows exactly reproduced from
  stored raw runs. Corrected numerical 2% diagnostic; missing data and structural
  noise-silence caveats retained. See `docs/research/robustness/README.md`.
- CI-002 revised workflows (`480f15f`) integrated with required research-suite
  guards. Executed locally against `6e03b40`: OFF/BTrack/aubio/both **11/12/12/13
  suites**; separate core workflow **11/11**. No remote or Windows CI run.
- EVAL-006 versioned long-decay sustain repair (`e0e3bde`) integrated with **37
  tests passing**, preserved original hashes, all 19 referenced WAVs and four raw
  result hashes checked. Repaired corpus acquisition is BTrack **5/11**, aubio
  **7/11** (both fail); original corpus evidence remains unchanged. EVAL-007
  supplies the version-aware coverage follow-up. See
  `docs/research/SUSTAIN-REPAIR.md` and `testdata/rhythm/repaired-sustain/`.
- TRACK-004 causal diagnosis (`98da13f`) integrated with **63 replay checks**.
  Both real backends' original-corpus 128/512 runs exactly reproduced, including
  fixture metrics, replay JSON and stored traces. BTrack's observed123.046875
  equals lag42; numerical report disagreement explains four primary core
  failures, while palm-mute has mixed numeric/invalid-phase evidence. Internal
  lag selection remains unresolved. See `docs/research/TRACKER-ACQUISITION.md`.
- EVAL-007 coverage correction (`514c154`) integrated with CSV citation fixes:
  reviewed original sustained hash stays defective even if renamed; repaired
  sustain and tapping are assessed, five noise-derived clips unassessed with raw
  counts retained. Six integration CLI runs preserve every non-coverage score;
  all 62 WAV entries verified. See `docs/research/SILENCE-COVERAGE.md`.
- RT-002 corrected probe (`6e89b1c`) integrated: all 26 CSV cases independently
  reproduced except wall time, 18 bounded dry/drum cases clean in the measured
  set, example LSTM two C allocations per model sample. Real editor-absent scene
  restoration at 30.5 ms is distinguished from the ~257 ms no-audio fallback.
  All 15 CLI/output checks and pipeline/source-pin failure injections pass.
  See `docs/research/PROCESSOR-RUNTIME-PROBE.md`; G1 remains partial.
- TRACK-005 diagnostic variant (`7b3a4d7`) integrated with framing corrections:
  all six runs, both click sweeps/step sidecars exactly reproduced; beat series
  unchanged. Original/repaired128 acquisition6/11 and7/11, original5128/11,
  worst core BPMerror0.77%; sparse-train regression retained. No tracker selected.
  See `docs/research/TEMPO-REPORT-VARIANT.md`.
- RT-003 LSTM repair (`401a6c1`) integrated with product include-root/verifier
  fixes:5/5 standalone checks, byte-identical numerical outputs, fresh Linux
  Standalone/VST3 builds and5/5 JUCE drum suites. Fresh product-archive probe is
  clean in all26 cases, including8 NAM cases; measured example-LSTM allocations
  removed. Other architectures/device/hosted-plugin safety unmeasured, G1 partial.
- ANALYSIS-001 lifecycle foundation (`5162e4d`) integrated after local completion
  of interrupted Flash review.29 tests/1210 checks and limited synthetic TSAN
  pass; real-plugin throughput61–75× realtime, five benchmark-failure checks pass.
  Frame-end is an input horizon, not live availability. No processor-clock wire.
- Real Linux JUCE drum tests now build and pass **5/5** with local development
  headers; **Standalone and VST3 both build** (`677ce9f`). The processor scene
  timer patch compiles against real JUCE. Combined enabled-tracker core tests
  pass **20/20**, including worker lifecycle, acquisition/tempo diagnostics, silence coverage, derived/sustain Python acoustic
  checks, robustness aggregation and BeatNet research-tool contracts.
  Default-OFF passes **18/18**.
  Tests-OFF registers zero tests.
- Drum MIDI prepare-time reservation now uses a scheduling bound. Old 256-byte
  storage grows to **2115 bytes** in the test; bounded storage shows zero observed
  allocations/frees across 16 rate/block combinations. Cosmetic meter CAS no
  longer retries. This is engine-path evidence, not the full G1 callback gate.

### Environment correction

Root access is unnecessary for extracted development headers plus existing
runtime libraries. NAM's pinned Eigen/AudioDSPTools submodules are initialized.
See `docs/research/LOCAL-LINUX-BUILD.md` and ADR-0003's correction.
`/tmp` is a separate full tmpfs; use disk-backed build roots and `TMPDIR` under
`/home/mojo/projects/guitars-build-resume/`. CMake/Ninja still live in
`/tmp/opencode/venv/bin` and must be added to PATH.

### Historical Flash lane continuations and dated execution log

The following dated entries preserve earlier pending/staged states. They are
superseded by the current resumption summary and EXECUTION-LEDGER task table.
Do not use their old routing or blocker statements as present instructions.

| Task | Branch / worktree | Session |
|---|---|---|
| CI-002 integrated workflows (worker finished) | `wp/CI-002-trackers`, `../worktrees/CI-002-trackers` | `ses_ee75a1084ffep3AxLvnl0Kqa3d` |
| TRACK-003 integrated partial feasibility (worker finished) | `wp/TRACK-003-beatnet`, `../worktrees/TRACK-003-beatnet` | `ses_ee745589effeU0Q5VSCHfvX51T` |
| EVAL-005 integrated paired diagnostics (worker finished) | `wp/EVAL-005-robustness`, `../worktrees/EVAL-005-robustness` | `ses_ee7424bb0ffeWJ5y7br6zXfhme` |
| EVAL-006 integrated sustain repair + tapping audit (worker finished) | `wp/EVAL-006-sustain`, `../worktrees/EVAL-006-sustain` | `ses_ee732c849ffeFasCrEAjS9Jvx7` |
| TRACK-004 integrated acquisition/BPM diagnosis (worker finished) | `wp/TRACK-004-acquisition`, `../worktrees/TRACK-004-acquisition` | `ses_ee732c82fffeKhlV6klzraURBv` |
| RT-002 integrated processor runtime probe (worker finished) | `wp/RT-002-processor-probe`, `../worktrees/RT-002-processor-probe` | `ses_ee72ac30effelERypLligi7fSR` |
| EVAL-007 integrated silence coverage (worker finished) | `wp/EVAL-007-silence-coverage`, `../worktrees/EVAL-007-silence-coverage` | `ses_ee7215f59ffeWQ6ZWQvH5z229z` |
| TRACK-005 integrated diagnostic variant (worker finished) | `wp/TRACK-005-tempo-variant`, `../worktrees/TRACK-005-tempo-variant` | `ses_ee71ab2f2ffeNbYXo23DrPP7Da` |
| RT-003 integrated LSTM repair (worker finished) | `wp/RT-003-nam-lstm`, `../worktrees/RT-003-nam-lstm` | `ses_ee70c33feffeq8pD9Eid5zfHob` |
| ANALYSIS-001 lifecycle foundation integrated (review finished locally) | `wp/ANALYSIS-001-worker`, `../worktrees/ANALYSIS-001-worker` | `ses_ee70baf44ffeg2zw0hfVyeQjc1` |
| TRACK-006 blocked before code/evidence (Flash balance error) | `wp/TRACK-006-variant-robustness`, `../worktrees/TRACK-006-variant-robustness` | `ses_ee6ef54b8ffeuAc4vH05SyS44E` |

Each owns new isolated files or workflows, commits task notes, returns a clean
tree and SHA. Review/merge returned work rather than rerunning a worker's topic
concurrently. The user asked for Flash; escalation, if genuinely necessary, is
`openai/gpt-6.1-sol#xhigh`, not the OpenCode provider.

All three corrected handoffs are integrated: TRACK-003 `1ef3d5c` as partial
feasibility with final scorer fixes, EVAL-005 `816a955` as scoped paired
diagnostics, and CI-002 `480f15f` as workflow definitions with local execution
evidence. BeatNet has no inference result and remains a partial feasibility
review. Remote CI and Windows execution remain unmeasured.

EVAL-006 and TRACK-004 started from `6287288` in new isolated Flash lanes. The
first owns a versioned sustained replacement corpus, generator/tests and acoustic
report; it preserves the historical corpus and audits tapping before proposing
changes. The second owns new causal trace/replay diagnostics and its report,
with no tracker/harness/scoring edits. CI-002 has finished. Current
integration merges: TRACK-003 `e61784d`, EVAL-005 `6287288`.

CI-002 integration merge is `cd9f97f`. RT-002 starts from that SHA in a third
isolated Flash lane: new independent tools/report only, reuse real JUCE build
objects, instrument actual callbacks and actual editor-absent timer delivery.
No legacy callback changes authorised; any runtime/build blocker must be
recorded as measured partial evidence. Device latency, NAM/arbitrary plugins
and full G1 coverage are not inferred from a bounded non-device probe.

EVAL-006 corrected `e0e3bde` is integrated: custom-output WAV references resolve,
generation runs acoustic validators, and physical-realism/continuity claims are
limited to the measured evidence. The recorded raw tracker runs used the earlier
manifest before metadata-only correction; waveform, truth and silence spans are
unchanged. The by-name CorpusDefect scorer caveat remains reported.

EVAL-006 integration merge is `664041c`. EVAL-007 starts from that SHA in the
vacated third Flash lane, with explicit ownership of existing rhythm-eval
Metrics/Manifest files and their tests. It replaces the name-based defect list
with reviewed audio-identity evidence, retains raw counts, marks noise-inherited
structural silence as unassessed, and reruns both candidates on original,
repaired and derived corpora. It owns a new report/artifact directory only;
historical manifests/WAVs/results and metric thresholds remain preserved. Its
corrected worker `514c154` is now integrated with CSV citation/escaping fixes and
independently rerun evidence; its own raw artifacts retain the original worker
schema while `integration-verification.json` records current source/binary pins.

TRACK-004 corrected `98da13f` is integrated with final overflow/availability
checks and corrected framing explanations. Missing values remain null/empty;
per-clause evidence distinguishes numerical errors from invalid phase. The
tested last/median BPM reports retain the bias without device-rate conversion;
this does not establish universal resampler exclusion or explain internal lag
selection. Stored provenance records the pre-correction HEAD used during the
worker's uncommitted regeneration; `ca47376` commits those exact corrections.
Integration independently reproduced all four corpus runs. No tracker fix or
selection is merged; G3 remains open.

TRACK-004 integration merge is `bf61598`. TRACK-005 starts from it in the vacated
Flash lane: new diagnostic-only wrapper/report/artifacts, a causal fixed-history
beat-interval BPM variant frozen before corpus scoring. It must preserve beat
events and every other observation field, compare baseline/variant on original
and repaired corpora, and record event-start versus causal-confirmation times.
No default adapter, vendor, scorer, gate or live transport changes authorised.

RT-002 corrected `6e89b1c` is integrated with final pipeline/output/source-pin
fixes and independent reruns. Fresh inputs and measured sampler activity support
the bounded dry/drum findings. Real message dispatch plus short/long no-audio
controls discriminate processor scene delivery from fallback. Example LSTM
allocations remain an open callback blocker; no whole-callback safety or device
gate pass is inferred. Worker raw artifacts retain their original pins, while
`processor-probe/integration-verification.json` records main-source measurements.
EVAL-007 has finished.

Latest integrations: EVAL-007 `90e1839`, RT-002 `af8b77a`. Three Flash lanes now:

- TRACK-005 initial `55827de` returned a median-of-four diagnostic variant with
  unchanged beat series, acquisition/BPM gains and a sparse-train regression.
  Review found `click_lag.py` labels event time as causal availability, click
  step changes intervals after a scheduled onset rather than exactly at nominal
  12 s, unvalidated CLI blocks permit a zero loop/oversized frame write, and some
  missing metrics are exported as zero. Same worker corrects these and scopes
  first-hit versus settling and wrapper allocation claims. Await revised SHA.
- RT-003 owns a new tracked NAM patch/build overlay and **only** the top-level
  NAM CMake section plus NAM dependency provenance. Submodule pins/bytes stay
  pristine. Require independent multilayer/multichannel numerical comparison
  and actual processor cold/warm probes before accepting the allocation repair.
- ANALYSIS-001 owns new tracker-injected core worker files/tests/benchmark only.
  D6 advances the lifecycle subset before selection; bounded publication must
  preserve beat events and causal availability. Full task remains partial;
  application wiring is not authorised. Shared CMake/CI registration is reserved
  for integration review.

Updates to the above: TRACK-005 corrected `7b3a4d7` is integrated with separately
measured512 full metrics and a framing mismatch guard. Core18/18 enabled16/16
OFF passes; no production selection. RT-003 initial `c96134b` returns a minimal
Eigen temporary/view repair and actual processor zeros; same worker is correcting
non-finite differential acceptance, production patch-file verification and
reconfigure/copied-source refresh before integration. ANALYSIS-001 `20a015a`
returned tracker-neutral worker code/tests/benchmark; review is pending and the
broader task remains partial until selected-backend/processor wiring.

TRACK-005 merge is `85e1cf4`. RT-003 corrected `401a6c1` now passes independent
numerical and real-processor checks; integration fixes the product's missing
AudioDSPTools include-root dependency, strengthens probe-artifact validation,
and adds a standalone Linux repair workflow.5/5 standalone suites pass; the
separate product rebuild is pending (shell `sh_1190b067e001rd5dzESzU2Xz1y`, do not
poll). Merge is staged but not committed until product verification returns.
ANALYSIS-001 same worker corrects early-exit state, stale restart audio and the
incorrect claim that frame-end is actual live availability. TRACK-006 starts
from `85e1cf4` in the vacated Flash lane, measuring the unchanged variant against
all24 derived perturbations with bounded new artifacts only. No default changes.

Update: RT-003 is integrated after fresh product Standalone/VST3 builds,5/5 drum
tests and a clean fresh-archive26-case processor probe. Independent standalone
checks5/5 pass; original baseline archives are preserved. Both remaining Flash
sessions stopped with provider `Insufficient Balance`: TRACK-006 has no changes
or result; ANALYSIS-001 has uncommitted corrections in its worktree (nine owned
files) that must be preserved and reviewed before continuing. No corrected
analyzer handoff or integration is accepted yet.

Latest: local review completed the preserved ANALYSIS-001 edits and committed
`5162e4d`; that foundation is now integrated with CMake/CI registration. Current
core20/20 enabled and18/18 OFF suites pass.29 analyzer tests/1210 checks and a
limited synthetic TSAN run pass; all22 workflow shell bodies valid. All worker
trees are clean; TRACK-006 remains blocked by Flash balance with no result.

### Remaining gate work

G0/G1 remain partial: Windows/ASIO, hardware timing, full callback heap/locking
and other NAM architectures need work. The measured example-LSTM allocation
issue is repaired. Bounded editor-absent scene delivery
has runtime evidence. G2 lacks live analyzer/UI and real DrumEngine
transport wiring. G3 still needs acquisition improvement and an evidence-backed
selection ADR; version-aware silence-defect classification is now integrated.
The historical
`sustained_chords` collapses within about 0.5 s; the separate repaired fixture has
measured persistence at 1.5 s. Tapping's onset-energy audit supports retaining
the fixture. See `SUSTAIN-REPAIR.md` and `CORPUS-ACOUSTIC-REVIEW.md`. G4's deterministic implementation does
not establish live musical behavior. Keep the earliest join/follow slice small.

---

## Historical initial handoff (superseded where noted above)

---

## 1. Read this first: the premise of DEVPLAN did not hold

`DEVPLAN.md` was written as though the repository already contained a fork of
`raphaelfukuda/Guitar-Companion`. **It did not.** At the start of this session the
working directory contained exactly two files:

```text
SPEC.md
DEVPLAN.md
```

No git repository, no source tree, no submodules, no build system.

This has been corrected: the repository is now the real upstream fork with
`SPEC.md` and `DEVPLAN.md` committed on top. See §3.

---

## 2. Verified environment facts

These were measured, not assumed. Re-verify before relying on them, but do not
spend a wave re-deriving them.

| Fact | Value | How verified |
|---|---|---|
| Platform | Linux x86_64, 4 cores, 15 GB RAM | — |
| Compiler | `g++` 14.2.0 (Debian) | `g++ --version` |
| `cmake` | **not on PATH**; 4.4.4 available at `/tmp/opencode/venv/bin/cmake` | installed via venv |
| `ninja` | 1.13.2 at `/tmp/opencode/venv/bin/ninja` | installed via venv |
| `make` | GNU Make 4.4.1 | present |
| `sudo` | **NOT AVAILABLE** — password required | `sudo -n true` failed |
| ALSA headers (`alsa/asoundlib.h`) | **MISSING** | `ls /usr/include` |
| freetype headers (`freetype2/ft2build.h`) | **MISSING** | `ls /usr/include` |
| X11 headers (`X11/Xlib.h`) | present | `ls /usr/include` |
| Free disk on `/home` | **~8.8 GB (96% used)** | `df -h` |
| Network access | working | `git ls-remote` succeeded |

### 2.1 Consequences — do not rediscover these

1. **The JUCE application target cannot be compiled in this environment.**
   `juce_audio_devices` / `juce_gui_basics` require ALSA and freetype headers at
   configure/build time. There is no `sudo`, so they cannot be installed. The
   upstream test binary `GuitarCompanionTests` links
   `juce::juce_audio_formats` and `juce::juce_audio_processors`
   (`tests/CMakeLists.txt`), so **the upstream test suite cannot be run here
   either.**

2. `pip3 install` is blocked by PEP 668. Use a venv
   (`python3 -m venv /tmp/opencode/venv`). Do not pass
   `--break-system-packages`.

3. Disk is tight (8.8 GB). Avoid full-history clones and avoid cloning the
   large `references/*` submodules (`airwindows`, `guitarix`, `GxPlugins.lv2`,
   `lsp-plugins`, `BYOD`, `dragonfly-reverb`, `rkrlv2`, `ToobAmp`). Only
   `third_party/JUCE` and `third_party/NeuralAmpModelerCore` were initialised,
   and only those are needed for building.

---

## 3. Repository state

```text
HEAD:   ad3ce6c  docs: add SPEC.md and DEVPLAN.md on top of upstream baseline 88f7e7c
parent: 88f7e7c  align: mirar -30 d e nunca subir lane - -18 estourava a saida
```

`88f7e7c` **is** the frozen upstream SHA named in both documents. Verified:
`git ls-remote https://github.com/raphaelfukuda/Guitar-Companion HEAD` returned
`88f7e7c805c9c5e17388154a678c2c6a3633ff23` — **zero drift since the spec was
written.** Provenance requirement of gate G0 is satisfied.

Submodules initialised (shallow, pinned by upstream):

```text
third_party/JUCE                     91ad83ae34a81e0833b1a2b0866f54846370ae53   (95 MB)
third_party/NeuralAmpModelerCore     1f42f88535884450104b8711d7595019afa0495b   (3.9 MB)
```

Upstream source layout confirmed:

```text
src/  AudioOverlay  DrumEngine  DrumGenerator  DrumLibrary  DrumOverlay
      LookAndFeel   PluginCatalog  PluginEditor  PluginProcessor
      SongOverlay   StoreOverlay   Tone3000Client  ToneWebView
tests/ CMakeLists.txt DrumCodecTests DrumGeneratorTests DrumLibraryTests
      DrumSpecTests TestHarness.h TestMain.cpp README.md
```

Relevant sizes: `src/PluginProcessor.cpp` = 4188 lines, `src/DrumEngine.cpp` = 598.

---

## 4. The key architectural finding, already verified

`SPEC.md` §2.1 and `DEVPLAN.md` FND-003 claim that `triggerAsyncUpdate()` is
reachable from `processBlock`, and treat that as a P0 release blocker.

**The claim is true.** `git grep -n triggerAsyncUpdate HEAD -- src/` returns:

```text
src/PluginProcessor.cpp:1070:            triggerAsyncUpdate();
src/PluginProcessor.cpp:1227:                        triggerAsyncUpdate();
src/PluginProcessor.cpp:2443:  (comment only)
```

The two call sites have **not yet been confirmed to be inside `processBlock`'s
call graph** — that analysis is DEVPLAN task **FND-003** and is still open. Do
not assume either way; verify call-graph reachability from the actual function.

---

## 5. What has been created (uncommitted)

Orchestrator-owned architectural seams. These are **not** committed yet — commit
them as the base commit for the first worker wave before delegating.

```text
src/jam/RhythmTypes.h          frozen core types (SPEC §9.1, §9.3, §9.4)
src/jam/AnalysisAudioRing.h    bounded SPSC analysis queue (SPEC §8.1)
src/jam/IRhythmTracker.h       tracker backend interface (SPEC §9.2)
src/jam/IDrumTransport.h       drum transport abstraction (DEVPLAN MOD-002)
src/jam/JamConfig.h            ALL tunable thresholds in one struct (SPEC §10.2)
tests/jam/JamTest.h            dependency-free test harness macros
tests/jam/JamTestMain.cpp      test registry + runner
jam-core/CMakeLists.txt        platform-neutral build, see below
```

### 5.1 Why these exist and why the orchestrator wrote them

`SPEC.md` §9 freezes the semantics of these interfaces and DEVPLAN §4 puts
`src/jam/` under worker ownership. Interface design is explicitly the
orchestrator's job ("it touches central architectural seams … an interface must
be designed"), so the seams are provided as a starting point rather than delegated
to a weaker worker who might fork the contract.

### 5.2 The `jam-core` build decision — read before editing `jam-core/CMakeLists.txt`

`jam-core/` is a **platform-neutral** static library containing the same
`src/jam/*.cpp` sources the JUCE plugin will link. There is no second copy of the
algorithm. It exists because §2.1 proves the JUCE target cannot build here, and
because a `jam-core` Linux CI job is already anticipated by DEVPLAN §9 (CI-001,
"Optional later job: compile platform-neutral `jam-core` on Linux once it
exists").

Two deliberate, non-obvious rules are documented in the file itself:

1. **`file(GLOB CONFIGURE_DEPENDS)` is used instead of an explicit source
   list.** This is intentional and must not be "cleaned up". Workers run in
   parallel in isolated worktrees; an explicit list would make every wave
   collide on a single shared line of `CMakeLists.txt`. Auto-discovery keeps file
   ownership boundaries intact. Consequence: **no worker may edit
   `jam-core/CMakeLists.txt`.**
2. **`jam-core` must never gain a `juce::` dependency.** If it does, the
   real-time contract can no longer be reviewed or tested in isolation and a
   build machine without audio libraries stops being able to prove the musical
   gates. Reject such a change at review.

ctest entries are generated by scanning test sources for `JAM_TEST(suite, …)`
registrations, so every suite automatically gets its own ctest name and no suite
can exist without one.

### 5.3 Test harness

`tests/jam/JamTest.h` provides `JAM_TEST(suite, name)`, `CHECK`, `REQUIRE`,
`CHECK_EQ`, `CHECK_NEAR`, `CHECK_LE`, `CHECK_GE`. No JUCE, so the musical
intelligence can be tested with no audio device.

Two properties are deliberate and must be preserved:
- a ctest filter matching **zero** tests is a hard failure, so a deleted or
  renamed suite can never report success silently (mirrors the policy already in
  upstream `tests/CMakeLists.txt`);
- no sleeping, no wall-clock dependence, no hidden random seeding. Determinism
  comes from a seed the caller supplies.

---

## 6. Gate and task ledger

Gates G0–G7 are defined in `DEVPLAN.md` §6. Status as of this handoff:

| Gate | State | Notes |
|---|---|---|
| **G0** fork/license/baseline | **PARTIAL — not passed** | provenance ✅, submodules ✅, RT audit ❌, build ❌, license inventory ❌ |
| **G1** real-time foundation | not started | blocked on G0 |
| G2–G7 | not started | |

| Task | State | Notes |
|---|---|---|
| FND-001 provenance/dep inventory | TODO | high value, needs no build — safe to delegate now |
| FND-002 reproducible baseline build | **BLOCKED: environment** | JUCE target cannot compile here; needs Windows/ASIO machine |
| FND-003 callback reachability map | TODO | documentation only, needs no build — safe to delegate now |
| RT-001, CI-001, TEST-001 | blocked on G0 | CI-001 can be written but not proven green here |
| MOD-001/002/003 and all later tasks | blocked | seams now exist |

**Nothing has been marked complete.** No gate has passed.

---

## 7. What the next orchestrator should do, in order

1. **Commit the seams in §5** as the base commit for the worker wave.
   Suggested message:
   `feat(jam-core): add frozen types, bounded analysis ring, and test harness`
2. **Delegate wave 1.** Three non-overlapping workers, isolated worktrees
   (`wp/<TASK-ID>-<name>`, worktrees at `../worktrees/<TASK-ID>` per DEVPLAN §3).
   Highest-value unblocked work, chosen so no two touch the same file:
   - **Lane A (real-time infra):** FND-003 — callback reachability audit.
     Documentation only, reads the real `src/PluginProcessor.cpp`. Confirms or
     refutes the `triggerAsyncUpdate` P0 in §4 above.
   - **Lane B (musical intelligence):** MOD-001 — tests for `AnalysisAudioRing`,
     plus `CLOCK-001` skeleton only if ownership is split cleanly
     (`src/jam/MusicalClock.*` + `tests/jam/MusicalClockTests.cpp`).
   - **Lane C (evidence/product):** FND-001 — dependency and license inventory
     into `docs/research/DEPENDENCIES.md` and `docs/adr/0001-base-selection.md`.
3. **Require `task-notes/<TASK-ID>.md`** from every worker (DEVPLAN §5) with the
   exact template, including final commit SHA and exact test commands.
4. **Merge incrementally**, running affected tests after each merge. Never merge
   three unreviewed branches and debug afterwards.
5. **Write an ADR for the environment constraint** (recommend
   `docs/adr/0003-build-environment-constraint.md`) recording: JUCE target not
   buildable on this box, which gates are therefore unverifiable locally, and
   what must be executed on a Windows/ASIO machine. This is an architectural
   departure from DEVPLAN and needs reason + evidence + tradeoff + impact.

---

## 8. Rules that must survive the handoff

Non-negotiable. Violating any of these is a defect regardless of what the tests
say.

- **Real-time contract** (SPEC §7.1, §18.1). Anything reachable from the audio
  callback must not allocate, free, lock, block, wait, do file/network I/O,
  parse JSON, log, post UI messages, call `AsyncUpdater::triggerAsyncUpdate()`,
  or spin unbounded. Analysis is disposable; audio is not.
- **The tracker never drives the drummer.** Required flow:
  `guitar → analysis → RhythmObservation → MusicalClock → ClockSnapshot →
  JamDirector → DrumTransportAdapter → DrumEngine`. Never
  `tracker BPM → DrumEngine BPM`.
- **Central files are controlled surfaces:** `src/PluginProcessor.*`,
  `src/PluginEditor.*`, `src/DrumEngine.*`, top-level `CMakeLists.txt`, and now
  `jam-core/CMakeLists.txt`. Workers build *around* them.
- **Evidence or it didn't happen.** No task is complete without a diff, passing
  tests, a task note, and a commit SHA.
- **No scope creep.** No chord recognition, bass, keys, song-form inference,
  neural generation, or style-browser work before the drummer passes its gates.
- **Keep the vertical slice small:** guitar input → bounded tap → one tracker →
  Musical Clock → one fixed Rock style → DrumEngine → headphones. No adaptive
  fills yet.
- **Do not overengineer.** No microservices, no databases, no IPC, no Python
  production sidecar, no DI container. C++ and small interfaces only.

---

## 9. Useful commands

```bash
# build + test the platform-neutral core (no JUCE, no audio device)
export PATH=/tmp/opencode/venv/bin:$PATH
cmake -S jam-core -B /tmp/opencode/build -G Ninja
cmake --build /tmp/opencode/build
ctest --test-dir /tmp/opencode/build --output-on-failure

# upstream tree without building
git ls-tree HEAD src/ --name-only
git grep -n "triggerAsyncUpdate" HEAD -- src/

# verify provenance is still exact
git ls-remote https://github.com/raphaelfukuda/Guitar-Companion HEAD
```

`cmake` is **not** on the default `PATH`; export the venv first.
