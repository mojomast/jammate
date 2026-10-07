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
| — library-identity decision | **DONE** | `f04c055` | orchestrator | G5 | `LibraryIndex` positional, not string id |
| RT-SIGNAL-001 RT-safe signal primitive | **DONE** | `wp/RT-SIGNAL-001` → merged `63259a3` | A — real-time | G1 | 14 tests / 110 090 checks |
| MOD-002 drum transport adapter | **DONE** | `wp/MOD-002` → merged `86544f1` | B — intelligence | G2 | 13 tests / 129 checks; 3 extra probes |
| **RT-001 remove callback-unsafe control plane** | **CODE DONE — UNVERIFIED** | orchestrator, `135b4b7` | A + orchestrator | G1 | F1 fixed; ⛔ cannot be compiled here |
| EVAL-001 guitar rhythm corpus | **DONE** | `wp/EVAL-001` → merged `f5f5a11` | C — evidence | G3 | 19 fixtures, 21 MB; hashes independently verified |
| — BTrack vendored + licence seam | **DONE** | `a8da4f2` | orchestrator | G3 | 2 vendor defects found by smoke-build before delegating |
| TRACK-001 BTrack backend | RUNNING | `wp/TRACK-001` | B — intelligence | G3 | |
| EVAL-002 evaluation harness + metrics | RUNNING | `wp/EVAL-002` | B — evidence | G3 | |
| CI-001 continuous integration | **DONE** | `wp/CI-001` → merged `4376672` | C — evidence | G1 | 3 Gitea workflows; Windows job never run |
| RT-001 remaining (F2 MidiBuffer) | BLOCKED:plugin lane | | A | G1 | needs a measured bound |
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

## Wave 3 — running, three non-overlapping lanes

| Lane | Task | Model | Owns | Gate |
|---|---|---|---|---|
| B intelligence | TRACK-001 | `deepseek/deepseek-flash` | `src/btrack/BTrackBackend.*`, `tests/jam/BTrackBackendTests.cpp` | G3 |
| B evidence | EVAL-002 | `deepseek/deepseek-flash` | `tools/rhythm-eval/**`, `tests/jam/RhythmEvalMetricsTests.cpp` | G3 |
| C evidence | CI-001 | `opencode/space-bunny-free` | `.gitea/workflows/**`, `docs/research/CI-SETUP.md` | G1 |

TRACK-001 and EVAL-002 are deliberately written against `IRhythmTracker` rather
than against each other: each is written before the other exists, so the seam is
being tested as a seam rather than as a convenient internal API. The integration
point is owned by the orchestrator.

### Vendoring decision, and two defects found before delegating

BTrack 1.0.7 is **vendored**, not a submodule. The evaluation corpus and harness
must run on a machine where `git submodule update --init` has never been executed,
and physical presence is what makes the GPL boundary auditable rather than merely
declared. Recorded in `third_party/BTrack/VENDORED-PATCHES.md`; updating BTrack
now means re-vendoring and re-reviewing by hand.

Smoke-building the vendor before writing the task brief found two defects a worker
would otherwise have inherited:

1. `BTrack.cpp` includes **libsamplerate unconditionally** for its *non-causal
   offline* beat-time helpers. This product is causal and never calls them.
   Guarded behind `BTRACK_WITH_LIBSAMPLERATE`.
2. `OnsetDetectionFunction.h` includes `kiss_fft.h` only under `USE_KISS_FFT`, so
   building it without that define fails with `complexOut was not declared`.

Neither was visible from the source alone; both would have consumed a worker's
time as a mysterious build failure.

### The trap handed to TRACK-001 in writing

`BTrack::calculateTempo()` hard-codes `44100.0` with a default 512-sample hop, so
its defaults assume **44.1 kHz**. This product runs at **48 kHz** (SPEC §17).
Feeding 48 kHz audio at that hop yields a tempo ~8.8 % fast — and because corpus
ground truth is in real seconds, a silent bias would be scored as a genuine BPM
error and could decide G3 wrongly. The brief requires the worker to choose
between hop scaling and resampling, document the numbers, and **prove unbiasedness
with a test**. This is the single most likely source of a wrong tracker decision.

Second, related: BTrack exposes `beatDueInCurrentFrame()` and **no beat phase at
all**. That is why `RhythmObservation::phaseValid` exists. The brief explicitly
forbids fabricating a phase — an honest `phaseValid = false` is a legitimate and
useful finding for the ADR, whereas an invented phase would corrupt both the
scoring and the Musical Clock's lock behaviour.

### CI-001 is scoped to be honest about what it cannot prove

No CI exists today, which is why two central-file edits are unverified. But the
Windows plugin job **cannot be validated from here at all** — the plugin target
has never been compiled anywhere in this project's history. The brief requires
that job to state in bold that its first run is expected to need fixes, and
forbids inventing runner labels and presenting them as known-good. A pipeline that
silently skips the build it exists to verify is worse than no pipeline.

---

## Wave 2 outcomes

### EVAL-001 accepted, including its size

19 fixtures, one per SPEC §12.2 bullet, all synthesised (Karplus-Strong plucked
strings through a simulated mic/line capture chain). Licensing is satisfied **by
construction** rather than by provenance review, which is the only way SPEC
§12.2 gets met without a rights question.

Verified independently at merge: all 19 on-disk SHA-256 match their manifest
entries; all declare beats, onsets and CC0; all are 48 kHz/16-bit/mono; and the
ramp beat spacing is genuinely non-uniform (1.333× and 0.671×), consistent with
the declared 108→146 and 152→100 BPM.

The worker's most valuable output was not the corpus but the **audit of it**. It
ran an independent STFT onset detector over the synthesised audio and found four
synthesis bugs that were entirely silent: the audio rendered fine, the manifest
looked correct, and the declared ground truth was simply unfindable in the file.
The worst was a damping term missing a factor of `freq`, which made every string
drone on at roughly 2 dB/s. A corpus whose ground truth cannot be recovered from
its own audio would have produced a confident, entirely wrong tracker comparison
at G3 — exactly the failure the gate exists to prevent.

**On the 21 MB: my 4 MB budget was arithmetic, not judgment, and I was wrong.**
At the mandated 48 kHz / 16-bit / mono, 96 000 B/s × 19 fixtures × the minimum
length that supports SPEC §19's "acquire within 2 bars" is arithmetically
unreachable. The worker flagged it and enumerated options instead of quietly
picking one, which is the behaviour I asked for.

Accepted at 21 MB (`.git` 42 → 58 MB). The mitigation that already exists is
better than trimming: the generator is byte-reproducible and `gen_fixtures.py
--check` proves it, so the corpus can be dropped from version control and
regenerated on demand if size ever becomes a real constraint. Trimming to hit a
number I invented would have weakened the benchmark to satisfy an arbitrary
target.

**Consequence for later waves:** these fixtures isolate *algorithmic* behaviour.
They do not prove anything about real strings, pickups, room noise, or a real
player's microtiming. SPEC §20's musical-quality gate still requires real-guitar
play tests, and this corpus must never be cited as evidence for it.

---

### RT-001 F1 is fixed in code — and is explicitly NOT verified

`135b4b7`. `processBlock` no longer posts a system message, takes a blocking
CriticalSection, or can allocate on the scene path.

**This does not close G1 and must not be reported as done.** The JUCE target
cannot be built in this environment (ADR-0003), so the patch is reviewed and
type-checked but never compiled against JUCE. What *was* verified:

- the member / base-class / override shape and the `signal()`/`consume()` call
  pattern compile clean under `-Wall -Wextra -Wpedantic`, and under
  `-std=c++20 -Werror`, against a stub `Timer`;
- zero `AsyncUpdater`, `triggerAsyncUpdate` or `handleAsyncUpdate` references
  remain anywhere in `src/`;
- jam-core stays green (3 suites).

**What a Windows/ASIO build must confirm before G1 is called:** that the class
compiles with a real `juce::Timer` private base, that `startTimer` in the
processor constructor is safe for every host (VST3 in particular), and that scene
changes still fire with the editor window closed.

### A design cost accepted deliberately

The replacement polls at **25 Hz for the lifetime of the plugin**, so the message
thread wakes 25×/second per plugin instance even when no scene is pending. The
alternative — arming the timer only while the scene system is active — is cheaper
but needs a state machine around a flag the audio thread sets, which is exactly
the kind of stale-state bug that leaves scene changes silently dead. A 40 ms
`std::atomic` load is not worth that risk. Revisit only if measurement shows
message-thread contention.

### RT-001 F2 is still open, and is now better specified

The second P0 (conditional `juce::MidiBuffer` growth on the hosted-drum-VST path)
is untouched. The mechanism is now pinned down: `ensureSize(256)` runs once in
`prepareToPlay`, and JUCE's `MidiBuffer::clear()` delegates to `clearQuick()`,
which *keeps* storage — so the reservation is not lost. The real risk is a
per-block event count exceeding 256 bytes. `DrumEngine::fireStep` emits up to
`numVoices` (9) note-ons per step, plus pending note-offs from a 64-entry
`pendingOffs` array, and `midi.clear()` runs once per block.

This needs a **measured bound**, not an assumed one, and it is plugin-lane work.

### MOD-002 verified beyond its own suite

Three probes written at merge, for behaviour the worker's suite did not cover:
a tempo change while a change is pending still applies exactly once on a
downbeat of the new grid (within 2 µs); stop-at-boundary supersedes and drops a
pending change as documented; and a 400→60 BPM collapse does not strand a queued
change. The first probe run failed and the cause was **my own** arithmetic — at
120 BPM a bar is 96 000 samples and I had fed 25 000 — not a defect in the
adapter. Recorded because the near-miss is the useful part.

### RT-SIGNAL-001's `LatestValue` design, and why it was worth escalating

A conventional seqlock over a plain `T` copy is **not sufficient in C++**: a
writer overlapping that copy is a data race even when the reader later rejects
the result on generation mismatch. The delivered design makes every shared
payload byte a lock-free atomic, keeps any speculative mixture in a local byte
array that is never materialised as a `T`, and uses sequentially consistent
operations so the reader's two generation loads bracket the writer's stores in
one total order. The reader makes **one** attempt and never retries, which
satisfies SPEC §7.1's no-unbounded-retry rule structurally rather than by
arguing a bound.

Compile-time `static_assert`s reject any target where the required atomics are
not natively lock-free, so a silent fallback to a library lock becomes a build
failure instead of an audio glitch. That is the property that justifies the
escalation: cheap models get correctness-provable work; this was
correctness-unprovable-by-inspection work.

---

## Wave 2 lane assignment (as launched)

Deliberately **not** delegating `src/PluginProcessor.*`. The confirmed P0 needs an
edit to a central file, so the pattern is the one DEVPLAN §4 prescribes: isolated
module → unit tests → review → small orchestrator-controlled patch. The worker
builds and proves the primitive; I apply the one-line change to the callback.

| Lane | Task | Model | Owns | Proves |
|---|---|---|---|---|
| A real-time | RT-SIGNAL-001 | `openai/gpt-6.1-sol#xhigh` | `src/rt/RtSignal.h`, `tests/jam/RtSignalTests.cpp` | coalescible `SignalFlag` + `LatestValue<T>`; torn-read analysis |
| B intelligence | MOD-002 | `deepseek/deepseek-flash` | `src/jam/DrumTransportAdapter.*`, `tests/jam/DrumTransportAdapterTests.cpp` | boundary quantisation, anti-drift position model |
| C evidence | EVAL-001 | `opencode/space-bunny-free` | `testdata/rhythm/**`, `tools/gen_fixtures.py`, `tests/jam/RhythmCorpusTests.cpp` | the corpus G3 needs |

RT-SIGNAL-001 is the one task escalated to the top model. A wrong memory ordering
in a primitive whose entire purpose is to be trusted from an audio callback is
not a bug that tests will catch later — it is the kind of defect that survives
review and glitches audio. Cheap models get correctness-provable work; this is
correctness-*unprovable*-by-inspection work.

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

## Architecture decision — library identity is a positional index (ADR pending)

Settled before STYLE-001 could be delegated, because a worker would otherwise
have invented a scheme and baked it into a catalogue.

**Measured facts** (`src/DrumLibrary.cpp`, parsed exhaustively):

| Fact | Value |
|---|---|
| Library entries | 570 across 16 genres |
| Grooves / fills | 358 / 212 |
| String identifiers present | **0** |
| Unique `(genre, name)` pairs | 553 of 570 — **16 collide** |
| Collisions that are identical duplicates | **0** — every collision is a different pattern |
| Collisions that are a groove in one place and a fill in another | **1** (`FUNK`, `Linear funk` — groove at 63, fill at 187) |
| Entries with `bpm == 0` ("keep current tempo") | 74 |

**Rejected: the derived slug.** SPEC §13.2 illustrates `"rock.basic.01"` and my
frozen `IDrumTransport` inherited `std::string grooveId`. Building `genre` +
slug(`name`) looks clean and is wrong: it resolves to the *wrong pattern* for all
16 collisions, and for `FUNK/Linear funk` to the wrong *kind* as well. A musically
incorrect result that appears correct is the worst available failure mode.

**Adopted: `LibraryIndex` = position in `drum::library()`.** Unique by
construction, and it is already what the product ships — `src/DrumOverlay.cpp`
identifies library rows by the drag id `"f:" + index` and resolves them via
`library()[i]`. The seam adopts the key the UI already uses instead of inventing
a second one.

`IDrumTransport.h` has been changed: `QueuedBarChange::grooveId`/`fillId` became
`LibraryIndex groove`/`fill` with a `kNoLibraryEntry` sentinel, and
`requestFillAtNextBar` takes a `LibraryIndex`. `JamConfig`-style tunables are
untouched.

**Carried into PERSIST-001:** an index is only stable while the library is
unchanged. Anything persisted must also record genre, name, fill flag and a hash
of the pattern spec, so a load against a changed library fails loudly instead of
playing a different groove.

**Second rule, for STYLE-001:** `Groove::bpm == 0` means *keep current tempo*
(74 of 570 entries). A style catalogue must treat a groove's BPM as a
suitability hint only. Tempo belongs to the Musical Clock (SPEC §10.3); a
catalogue that treats groove BPM as authoritative would fight the clock.

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