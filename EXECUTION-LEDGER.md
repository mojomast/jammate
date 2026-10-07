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
| **G0** fork/license/baseline | **PARTIAL** | provenance ✅ · submodules ✅ · RT audit ⏳ · **build ⛔ plugin lane** · license inventory ⏳ |
| **G1** real-time foundation | not started | blocked on G0 (and FND-003) |
| **G2** new module seams | **PARTIAL (advanced early)** | types ✅ · ring ⚠️ untested · adapter ⛔ · Jam UI ⛔ |
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
| FND-001 provenance/dependency inventory | RUNNING | `wp/FND-001-deps` | C — evidence | G0 | docs only |
| FND-002 baseline build record | BLOCKED:plugin lane | — | — | G0 | D2 |
| FND-003 RT reachability map | RUNNING | `wp/FND-003-rt-reach` | A — real-time | G0 | read-only on `src/` |
| MOD-001 analysis ring tests | RUNNING | `wp/MOD-001-ring` | B — intelligence | G2 | D1 |
| CLOCK-001 musical clock | RUNNING | `wp/CLOCK-001-clock` | B — intelligence | G4 | D1 |
| RT-001 callback-safe control plane | BLOCKED:G0 | | A | G1 | needs FND-003 |
| CI-001 CI baseline | TODO | | C | G1 | Gitea; needs plugin lane to be green |
| TEST-001 foundation tests | BLOCKED:plugin lane | | C | G1 | cannot run here |
| MOD-002 drum transport adapter | BLOCKED:G2 | | B | G2 | seam `IDrumTransport.h` exists |
| MOD-003 Jam UI shell | BLOCKED:G2 | | C | G2 | |
| EVAL-001/002, TRACK-001 | BLOCKED:G2 | | | G3 | |
| ANALYSIS-001, DIAG-001 | BLOCKED:G4 | | | G4 | |
| STYLE-001, DIRECTOR-001, DRUM-001 | BLOCKED:G4 | | | G5 | |
| UI-001, AUDIO-001, PERSIST-001 | BLOCKED:G5 | | | G6 | |
| E2E-001/002/003 | BLOCKED:G6 | | | G7 | |

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