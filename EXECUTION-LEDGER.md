# EXECUTION-LEDGER.md — live orchestration state

**Owner:** orchestrator only. Workers must not edit this file.
**Governing documents:** `SPEC.md` (v0.1), `DEVPLAN.md` (v0.1).
**Format:** one row per task. A task is DONE only with diff + passing tests +
task note + commit SHA + successful integration.

---

## Repository state

| Item | Value |
|---|---|
| Branch | `main` (single integration branch; workers branch from it) |
| Frozen upstream SHA | `88f7e7c805c9c5e17388154a678c2c6a3633ff23` (verified zero drift) |
| Submodules initialised | `third_party/JUCE`, `third_party/NeuralAmpModelerCore` only (disk at 96 %) |
| Forge | Gitea (`.gitea/`), **not** GitHub — CI-001 must target Gitea Actions |
| Build seam | `jam-core/` — platform-neutral, no `juce::`, tests via ctest |

## Verification lanes

| Lane | Buildable here | Verifies |
|---|---|---|
| **core** (`jam-core`, CMake + Ninja, no audio device) | yes | SPEC §9, §10, §13, §14, §19, §21.1, §21.2 |
| **plugin** (JUCE `GuitarCompanion` / `GuitarCompanionTests`) | **no** — missing ALSA + freetype headers, no `sudo` (ADR-0003) | SPEC §7.1, §17, §18.1 on the real callback, §21.5 |

A gate whose acceptance condition lives only in the **plugin** lane is recorded
PARTIAL, never PASSED.

---

## Gate status

| Gate | State | Blocking items |
|---|---|---|
| **G0** fork/license/baseline | **PARTIAL** | provenance ✅ · history ✅ · submodules ✅ · RT audit ✅ · license inventory ✅ · **baseline build ⛔ plugin lane** |
| **G1** real-time foundation | **READY TO START (plugin-lane parts ⛔)** | RT-001 now unblocked by FND-003 |
| **G2** new module seams | **PARTIAL (advanced early)** | types ✅ · ring ⏳ tests running · adapter ⛔ · Jam UI ⛔ (needs JUCE) |
| **G3** tracker selected | not started | blocked on G2 |
| **G4** musical clock | not started | — |
| **G5** adaptive drummer | not started | — |
| **G6/G7** UX / release | not started | — |

### Scheduled deviations (orchestrator-authorised, recorded)

1. **D1 — MOD-001 and CLOCK-001 start before their nominal gates.** DEVPLAN
   marks MOD-001 `BLOCKED:G1` and CLOCK-001 `BLOCKED:G3`. Both are started
   early, in the **core** lane, because (a) the plugin lane cannot close G1 on
   this machine at all, and (b) `MusicalClock`'s input is `RhythmObservation`,
   not a tracker, so its correctness is tracker-independent and its tests
   remain valid whichever backend G3 selects. Neither task touches an audio
   thread, `PluginProcessor.*`, `PluginEditor.*` or `DrumEngine.*`.
2. **D2 — FND-002 cannot complete here.** Recorded as an external blocker with
   an exact procedure in `docs/research/BASELINE-BUILD.md` (deferred task),
   not as done.
3. **D3 — CI-001 targets Gitea Actions**, not `.github/workflows/`, because
   Gitea is the forge this repository actually uses.

---

## Model routing for delegated work

| Work class | Model |
|---|---|
| Bounded mechanical implementation, doc/evidence enumeration, checklist-driven tests | `opencode/space-bunny-free` |
| Substantive code or analysis needing real reasoning (call-graph tracing, musical-intelligence algorithm design) | `deepseek/deepseek-flash` |
| Escalation only: central integration patches, cross-branch conflict resolution, real-time safety review, blocked-worker repair | `openai/gpt-6.1-sol#xhigh` |

Escalation is not the default. A worker that reports a blocked contract is
reassigned at the same tier first; only a genuinely central, safety-critical, or
cross-cutting problem goes to `gpt-6.1-sol#xhigh`.

## Controlled integration surfaces (orchestrator-only unless granted)

`src/PluginProcessor.*` · `src/PluginEditor.*` · `src/DrumEngine.*` ·
root `CMakeLists.txt` · `jam-core/CMakeLists.txt` · `EXECUTION-LEDGER.md` ·
`HANDOFF.md`

Workers build *around* these. A worker that believes it must edit one stops
and reports instead.

---

## Task table

| Task | State | Branch | Owner lane | Gate | Notes |
|---|---|---|---|---|---|
| FND-001 provenance/dependency inventory | **DONE** | `wp/FND-001-deps` → merged `724e6d9` | C — evidence | G0 | verified; 2 corrections applied by orchestrator |
| FND-002 baseline build record | BLOCKED:plugin lane | — | — | G0 | D2 |
| FND-003 RT reachability map | **DONE** | `wp/FND-003-rt-reach` → merged `931be23` | A — real-time | G0 | P0 `triggerAsyncUpdate` CONFIRMED reachable |
| — ADR-0003 build environment | **DONE** | `48f301c` | orchestrator | G0 | two-lane verification split |
| MOD-001 analysis ring tests | **DONE** | `wp/MOD-001-ring` → merged `89db28d` | B — intelligence | G2 | 19 tests / 95 236 checks; 2 header defects fixed in `efb820b` |
| CLOCK-001 musical clock | **DONE** | `wp/CLOCK-001-clock` → merged `b78c43c` | B — intelligence | G4 | 16 scenarios green; 2 policy gaps decided |
| RT-001 callback-safe control plane | **READY** | | A | G1 | unblocked by FND-003; ⛔ unverifiable here |
| CI-001 CI baseline | TODO | | C | G1 | Gitea; needs plugin lane to be green |
| TEST-001 foundation tests | BLOCKED:plugin lane | | C | G1 | cannot run here |
| MOD-002 drum transport adapter | BLOCKED:G2 | | B | G2 | seam `IDrumTransport.h` exists |
| MOD-003 Jam UI shell | BLOCKED:G2 | | C | G2 | **not verifiable here** — needs JUCE |

---

## Measured finding — grooves have no string ID

Scoping STYLE-001 against the real code, `src/DrumLibrary.cpp` has **no** string
identifier scheme. `grep -cE '"[a-z]+\.[a-z0-9]+\.[a-z0-9.]+"' src/DrumLibrary.cpp`
returns **0**. SPEC §13.2's `"grooves": {"low": ["rock.basic.01", …]}` example and
`IDrumTransport::queueBarChange(std::string grooveId)` therefore refer to an
identifier that does not yet exist.

What actually exists is `struct Groove` in `src/DrumEngine.h`:

```cpp
struct Groove
{
    const char* genre;   // "ROCK", "POP", "PUNK", "METAL", …
    const char* name;    // UTF-8 display name, e.g. "Half-time 16"
    int  bpm;            // 0 = keep current tempo
    int  swing;          // 0..60 percent
    bool fill;           // true = fill, false = groove
    const char* spec;    // compact pattern string
    int  num = 4;        // bar formula, default 4/4
    int  den = 4;
};
```

Entries are anonymous values in a `static const std::vector<Groove>` built inside
`drum::library()`; there is no lookup function, no index, and no key. Identity is
positional.

Consequences for planning, not yet actioned:

1. **The drift between SPEC §13.2 and the code is a real architectural decision,
   not a worker detail.** The cheapest correct path is a *derived* stable ID
   (`genre` + slug of `name`, e.g. `rock.half-time-16`) resolved once at catalogue
   build time, validated against the real library, with the positional index kept
   as the transport key. That avoids rewriting 673 lines of library data and keeps
   `DrumEngine` untouched.
2. **STYLE-001 must not be delegated until this is decided**, or a worker will
   invent an ID scheme and bake it into a catalog.
3. `Groove::bpm == 0` means "keep current tempo". Any style catalogue that treats
   a groove's `bpm` as an authoritative tempo will produce wrong behaviour; the
   clock owns tempo (SPEC §10.3), so the catalogue must treat groove BPM as a
   suitability hint only.
| EVAL-001/002, TRACK-001 | BLOCKED:G2 | | | G3 | |
| ANALYSIS-001, DIAG-001 | BLOCKED:G4 | | | G4 | |
| STYLE-001, DIRECTOR-001, DRUM-001 | BLOCKED:G4 | | | G5 | |
| UI-001, AUDIO-001, PERSIST-001 | BLOCKED:G5 | | | G6 | |
| E2E-001/002/003 | BLOCKED:G6 | | | G7 | |

---

## Orchestrator corrections applied to merged worker output

Recorded so a later reader can tell worker claim from verified fact.

| Finding | Claim | Verified | Action |
|---|---|---|---|
| Upstream history missing | local `88f7e7c` was a root commit; ancestry unrecoverable without API walking | **true** — repo was shallow (`.git/shallow`) | `git fetch --unshallow upstream` executed. 175 commits now present, root `19bc59a`, `.git` 39→42 MB. **Tree hash `67ddc31f` unchanged**, working tree byte-identical, so the frozen-SHA guarantee still holds. SPEC §25.2 now satisfied. |
| "25 commits past v0.1" | `BASELINE.md` V19 | **wrong** — `git rev-list --count v0.1..88f7e7c` = **7** | corrected in `BASELINE.md` §1.1 and V19 |
| `references/*` submodules have no gitlink | declared in `.gitmodules`, absent from the tree | **true, and upstream's own defect** — `git ls-tree 88f7e7c:references/` returns only `README.md` | left open; no history operation can fix it. Closes only on a release decision. |
| AudioDSPTools licence | THIRD_PARTY says MIT, README says Apache-2.0/MIT | unverifiable (submodule not checked out) | stays `UNKNOWN` with the exact file to read |

---

## Wave 1 decisions taken

### The Musical Clock is in, and it is the only clock

`CLOCK-001` merged as the single tempo authority. Verified independently at
merge, not just accepted:

- `MusicalClock.cpp` contains no `new`, `malloc`, container, `printf`, sleep or
  I/O. Every `bpm_` write goes through `slewToward`, `applyMetricCorrection`, the
  tap path, or `reset`. **No observation value is ever written through to tempo.**
- Suite proven able to fail: zeroing `isolatedJumpRejectRatio` in the
  implementation turns ctest red; reverting turns it green.
- Both suites now build in one binary — `35 tests, 95 330 checks, 0 failed`.

Two policy gaps the worker flagged rather than resolved, both decided centrally:

1. **`SetMode`'s missing argument carrier — accepted, the comment is the defect.**
   `RhythmTypes.h` documents `// arg0 = int(TempoMode)` but `ClockCommand` has no
   `arg0`. The worker reads `tapSampleTime` as the carrier. Accepted as a
   bounds-checked shim, because the alternative is worse: `tapSampleTime` is a
   `uint64_t` sample clock, so a future caller who follows the comment and adds a
   real field breaks this code with no compiler help. The misleading comment is
   to be corrected in the pass that owns `RhythmTypes.h`.
2. **Explicit commands override a freeze — accepted deliberately.** SPEC 10.2 is
   silent on whether Half/Double/Tap/Resync may override a freeze. They may.
   SPEC 5.6 calls these controls "intentional human-in-the-loop features", and a
   guitarist who engaged Freeze during a low-confidence moment must still be able
   to correct a misread tempo. The freeze governs the *evidence* path only. This
   is recorded as a refactor hazard: collapsing `! tempoFrozen_` into `command()`
   would be a behavioural regression, not a cleanup.

### Two header defects fixed rather than documented

MOD-001 reported two mismatches between `AnalysisAudioRing.h` and its own
behaviour and correctly refused to edit the frozen seam. Both were fixed here
because both were diagnostics blind spots, and SPEC §22 exists so a starved
analysis path cannot hide:

- A capacity-0 ring reported `overrunCount() == 0` forever while dropping every
  block. A counter that reads a confident zero while data is discarded is worse
  than no counter. Now counted.
- `reset()`'s comment claimed it stops accepting data; it does not, and that is
  *correct* for the device lifecycle (`prepareToPlay` reruns after every device
  change). The code was right and the comment was wrong, so the comment was
  corrected and now records why there is deliberately no `disable()`: a stale
  flag survives a device change and starves analysis forever.

The two `push()` guards are now deliberately distinct: a *disabled* ring drops
real audio and is counted; a null pointer or zero-length block is a caller error
with nothing to drop and is **not** counted as an overrun.

---

## Wave 1 findings that change planning

### The G0 real-time risk is real and now precisely located

FND-003 answered the question SPEC §2.1 left open:

- **`PluginProcessor.cpp:1227` — `triggerAsyncUpdate()` IS reachable from
  `processBlock`**, inside `SceneEnv::fadeOut`, guarded by `if (g1 <= 0.0f)`.
  The condition: a scene envelope is armed and its fade reaches silence in this
  block. Reached via `processBlock:1270 → processDrums:2604 → armSceneEnvelope:2648`.
- `PluginProcessor.cpp:1070` is inside `prepareToPlay` (`:896`) and is **not**
  callback-reachable.
- The JUCE documentation quote backing the P0 was verified against the pinned
  submodule: "beware of calling it from a real-time (e.g. audio) thread, because
  it involves posting a message to the system queue, which means it may block".

So DEVPLAN RT-001's premise holds. It is now the top G1 task.

Second P0: conditional `juce::MidiBuffer` growth on the hosted-drum-VST path.
`ensureSize(256)` is called in `prepareToPlay` (`:1019`) but `clear()` maps to
`clearQuick()`, which keeps storage — so the risk is a *per-block event count*
over 256 bytes, not a lost reservation. `DrumEngine::fireStep` can emit up to
`numVoices` (9) note-ons per step, plus pending note-offs from `pendingOffs[64]`.
Bounded, but the bound has not been measured. RT-001 must size it properly
rather than assume.

---

## Base commit for wave 1

The `src/jam` seams plus `jam-core/` were uncommitted. They are committed as the
base for all wave-1 workers, together with the fixes found while verifying them:

- `jam-core/CMakeLists.txt` had **no `cmake_minimum_required`** and resolved
  `src/jam` relative to `jam-core/` instead of the repository root, so it could
  not configure at all. Fixed; `JAM_ROOT` is now derived explicitly.
- `jam-core` had **no translation units**, so the library could never link.
  `src/jam/JamCore.{h,cpp}` added: identification + the schema version
  constants that STYLE-001 and PERSIST-001 need.
- `jam-core` was not reachable from the root build. Root `CMakeLists.txt` now
  adds it unconditionally, so the real-time contract is compiled on every
  configuration.
- `tests/jam/JamTest.h` `CHECK_EQ` was ambiguous for any integral type other
  than `long long`/`bool`, i.e. for `CHECK_EQ(ring.capacity(), 4)`. Fixed with
  a narrowing overload plus a `const char*` overload.

Verified before commit: `cmake -S jam-core -B build -G Ninja` configures,
builds warning-free with `-Wall -Wextra -Wpedantic`, and a temporary probe suite
confirmed ctest registration, a real failing assertion, and non-zero failure.

---

## Rules that survive every wave

- Real-time contract: SPEC §7.1. No allocation, free, lock, wait, I/O, JSON,
  logging, message posting, `triggerAsyncUpdate()`, device enumeration, plugin
  scanning or unbounded retry anywhere reachable from `processBlock`. Analysis
  is disposable; audio is not.
- The tracker never drives the drummer:
  `guitar → analysis → RhythmObservation → MusicalClock → ClockSnapshot →
  JamDirector → IDrumTransport → DrumEngine`. Never `tracker BPM →
  DrumEngine BPM`.
- Every delegated task ends with `task-notes/<TASK-ID>.md` and a commit SHA.
- A gate is passed only when every acceptance condition has evidence. Unknown
  means not passed.