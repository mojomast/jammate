# ADR-0001 — Base selection: fork `raphaelfukuda/Guitar-Companion`

- **Status:** Accepted
- **Date:** 2026-10-07
- **Deciders:** orchestrator
- **Relates to:** SPEC.md §1, §2.1, §2.2, §4.2, §25 (especially §25.1 and
  §25.8), §27; DEVPLAN §7 FND-001, §8 (Gate G0)
- **Evidence:** [`docs/research/BASELINE.md`](../research/BASELINE.md),
  [`docs/research/DEPENDENCIES.md`](../research/DEPENDENCIES.md)
- **Numbering note:** 0002 is reserved for ADR-TRACKER-001 (rhythm-tracker
  selection). This record is 0001, matching
  [`0003-build-environment-constraint.md`](0003-build-environment-constraint.md).

---

## Context

The product is an **adaptive guitar jam companion**: a low-latency Windows-first
desktop application in which a guitarist plays through headphones while a
virtual drummer listens, decides for itself when and how to join, holds a
stable groove, follows intentional tempo changes, reacts to dynamics, chooses
musical fills, and survives silence, syncopation, mistakes and restarts
(SPEC.md §1, §5).

Three constraints shape the choice of base more than anything else:

1. **SPEC.md §4.1/§4.2 says the drums subsystem already exists** — *"Existing
   drum sample engine plus optional hosted drum VST3"* is in scope, and
   *"Replacing the existing amp/effects subsystem"* is an explicit non-goal.
   This wording presupposes a base that already has one.
2. **SPEC.md §17 and §18.3 set a hard latency target** (≤ ~12 ms end-to-end
   monitoring round trip at 48 kHz / 128 frames on ASIO) and a hard
   callback-safety gate. Whatever we pick must already have a real guitar input
   path, a working low-latency device layer, and a project rule forbidding
   allocation/locks/IO on the audio thread.
3. **SPEC.md §25 is a licensing gate, not paperwork.** The whole product's
   licence falls out of the base's licence. §25.1 makes "AGPLv3/open-source is
   acceptable" an explicit assumption, and §25.8 says that if closed-source
   commercial distribution ever becomes a requirement, the decision must be
   redone from scratch rather than re-licensed later.

The three real candidates were `raphaelfukuda/Guitar-Companion`, **Giada**
(`monocasual/giada`), and **Hydrogen** (`hydrogen-music/hydrogen`), plus the
"write a blank JUCE application" option. All four were researched on
2026-10-07 (SPEC.md §27).

### What the Guitar-Companion revision actually contains (verified)

Everything below was read in the tree at
`88f7e7c805c9c5e17388154a678c2c6a3633ff23`, whose tree SHA
`67ddc31f773020158ed0d071d531b0983653177c` is byte-identical to upstream
`master` (see `BASELINE.md` §1.2).

| Capability SPEC.md §2.1 relies on | Where it actually is in this tree |
|---|---|
| JUCE 8 standalone **and** VST3 targets | `CMakeLists.txt` line 127: `FORMATS Standalone VST3` |
| Real guitar input, low-latency device support | `src/PluginProcessor.cpp` (171 kB of DSP/parameter/preset code), `src/AudioOverlay.cpp`, `src/LookAndFeel.h`; `JUCE_WASAPI=1`, `JUCE_DIRECTSOUND=1` (`CMakeLists.txt` lines 209–210) |
| WASAPI + **optional** ASIO | `CMakeLists.txt` lines 226–239 — ASIO is off unless the user supplies the SDK |
| Guitar DSP chain + **Neural Amp Modeler** integration | `src/PluginProcessor.cpp`; `nam_core` built from NAM Core sources, `CMakeLists.txt` lines 16–29 |
| 23 effects with variations | `README.md` line 130; `docs/EFEITOS.md` lists all 23 with per-effect provenance |
| A **separate drum bus** | `README.md` line 426: the sequencer's bus *"does not go through the guitar chain"* |
| Sample-accurate drum sequencer | `README.md` line 426: *"sample-accurate sequencer in processBlock (2 bars × 16 steps, 9 voices, accent/ghost), swing, click, count-in"* |
| Internal sampler **plus** hosted drum VST3 | `README.md` line 427; `JUCE_PLUGINHOST_VST3=1` (`CMakeLists.txt` line 222) |
| Groove/fill library + procedural generator | `src/DrumLibrary.cpp` (57 kB of factory grooves), `src/DrumGenerator.cpp`, groove generator ported from `midi-drums` |
| Swing + velocity/timing/round-robin humanization | `README.md` line 431; per-bar time signature, 16-step grid |
| Song / scene concepts | `src/SongOverlay.cpp`; up to 8 sections with one rig snapshot each (`README.md` line 124) |
| Recording of guitar / drum / mix stems | `README.md` line 428, line 417; `src/SongOverlay.cpp` |
| Audio-device UI | `src/AudioOverlay.cpp`, `src/PluginEditor.cpp` (237 kB) |
| Real-guitar testing on 48 kHz / 128-sample ASIO | `README.md` lines 110–111: *"tested with a real guitar (Focusrite ASIO, 48 kHz, 128 samples)"* — i.e. exactly SPEC §17's reference validation configuration |
| An explicit no-allocation/locks/IO rule in `processBlock` | `CONTRIBUTING.md` "Golden rules" rule 1, `README.md` line 132 |
| Stem recording of guitar and drums | commit `f5e7732` *"vNext (F6): Song/Scenes por secao + gravacao de stems guitarra/bateria"* in the upstream ancestry |

`README.md` lines 429 and 123 give the content scale: ~460 factory
grooves+fills across 16 genres (and, inconsistently, "~157 grooves and ~53
fills across 14 genres" in the same file) — of which SPEC §4.1's "style
selection", "intensity", "complexity", "fill frequency" and "swing" controls
are refinements, not green-field work.

### The licence consequence, stated up front

`LICENSE` is the **GNU Affero General Public License, Version 3** (661 lines,
read directly). `README.md` line 456 states why: *"required by using JUCE 8 in
the open-source tier"* — JUCE 8 is AGPLv3 or proprietary, and JUCE 8's AGPL
option propagates to everything linked with it, including NAM Core, Eigen,
nlohmann/json, the drum samples and every original line of this project's own
code. There is no seam in the architecture where the AGPL stops.

The complete licence ledger is in
[`DEPENDENCIES.md`](../research/DEPENDENCIES.md). The load-bearing rows:

- AGPLv3: the product, JUCE, and the Steinberg **VST3 SDK** under its GPLv3
  option.
- MIT: Neural Amp Modeler Core, nlohmann/json 3.12.0, `midi-drums`, and the
  three ported Airwindows effects (Tape, Console, Valve Drive) — all with
  notices already present in `THIRD_PARTY.md`.
- MPL2: Eigen — file-level copyleft, and its four `COPYING.*` files are not yet
  shipped.
- GPL: the 27 embedded GMRockKit drum samples.
- CC BY 4.0: the derived Groove MIDI Dataset grooves.
- OFL 1.1: four embedded fonts.
- **Non-redistributable by their own terms:** the Steinberg ASIO SDK and the
  Microsoft WebView2 NuGet package — both gitignored, both fetched or manually
  extracted at configure time.

---

## Decision

**Adopt `raphaelfukuda/Guitar-Companion` at
`88f7e7c805c9c5e17388154a678c2c6a3633ff23` as the MVP base.** Giada and
Hydrogen are rejected as bases and kept as references. Writing a blank JUCE
application is rejected outright.

This decision is scoped to Wave 0–9 (the MVP) and is explicitly **conditional**
on the product owner continuing to accept AGPLv3 (see Consequences).

Sub-decisions bundled here because they are consequences of the same choice:

1. **The existing guitar DSP chain, amp engine and effects catalog are kept,
   not rewritten.** SPEC §4.2 already made that an explicit non-goal; the base
   makes it free.
2. **The existing drum engine is the drummer's rendering layer.** SPEC §6 draws
   `Jam Director → Drum Adapter / Scheduler → DrumEngine`, and `DrumEngine`
   already exists with a separate bus, sample-accurate scheduling, swing,
   accent/ghost and a pending/retired VST3 swap protocol. Adopting the base is
   what makes SPEC §13 "style is not one loop" a matter of *selection
   policies over existing content* instead of writing a sampler and a sequencer
   first.
3. **All new intelligence goes into `src/jam/`, which must never gain a `juce::`
   dependency** — already ADR-0003's rule, restated here because this base is
   what makes that rule worth having: the inherited code is large, JIT-free but
   JUCE-coupled, and the only way to keep the SPEC §10 Musical Clock testable on
   a machine with no audio hardware is to keep it away from JUCE.
4. **The base is treated as a beta codebase, not a trusted foundation.** SPEC
   §2.1 says so and DEVPLAN FND-003/RT-001 act on it: the upstream July 2026
   audit found real-time-safety and test-coverage problems, and inspection
   still finds `triggerAsyncUpdate()` reachable from `processBlock` — which
   current JUCE documentation explicitly warns may block on a real-time thread
   and which SPEC §7.1 forbids. **Adopting the base is not the same as trusting
   its real-time behaviour; G1 is not closed by this ADR.**
5. **The frozen SHA is the contract.** Any change to the dependency baseline
   goes through `DEPENDENCIES.md`; SPEC §28 item 12 ("the exact dependency and
   licence manifest is current") is a release acceptance condition, not a
   nicety.
6. **The missing upstream history is a G0 item to resolve, and it is a
   decision, not a code change.** See Consequences.

---

## Consequences

### Positive

- **The MVP's expensive half already exists and is real.** Sample-accurate drum
  scheduling, a separate drum bus, an internal sampler with three velocity
  layers per voice, a hosted drum VST3 path, a groove/fill library, swing and
  humanization, per-bar meters, sections and stem recording. SPEC §13, §14 and
  §16 become control and selection work, not instrument-building work.
- **The latency gate is winnable because the reference configuration is
  already proven.** `README.md` line 110–111 documents real-guitar validation on
  Focusrite ASIO at 48 kHz / 128 samples — SPEC §17's exact reference config, and
  the same config SPEC §18.2/§18.3 measure against. A blank JUCE app would
  start with *zero* measured baseline, which SPEC §18.3 explicitly requires
  ("the exact measured baseline is recorded before new work").
- **Offline-by-construction.** There is no cloud in the jam path. The only
  network features (Tone Store, TONE3000 OAuth, the plugin catalogue) are
  UI/message-thread code that SPEC §24 already requires to stay isolated from
  the real-time engine. SPEC §1's "must work fully offline" is satisfiable
  without removing anything.
- **A disciplined legal baseline that will pass an audit.** `THIRD_PARTY.md`
  distinguishes *studied* from *ported* per effect; `docs/EFEITOS.md` labels
  ports explicitly; the four OFL texts and the drum-sample notice ship in the
  tree. That is a better starting position than any alternative.
- **The distribution question is answerable before it is asked.** Every
  redistributable item's licence and obligations are now enumerated, including
  the four Eigen `COPYING.*` files and the four OFL texts that the installer
  does not yet ship.
- **`jam-core` can be built and tested here without the base being buildable
  here.** ADR-0003's lane split means the Musical Clock — the highest-risk,
  highest-value new component — is verifiable on this machine immediately, and
  the JUCE lane is reduced to proving callback wiring.

### Negative / accepted cost

- **AGPLv3 is not negotiable for this code.** Section "The licence consequence"
  above is the whole argument, and it is irreversible in the useful direction:
  adding MIT/BSD/Apache material to an AGPL product is easy; removing AGPL from
  code that links JUCE 8 is not. See the next item.
- **🚫 Closed-source commercial distribution is INCOMPATIBLE with this
  decision.** Not "discouraged", not "difficult" — **incompatible**, as SPEC.md
  line 8 and §25.8 already state. If it ever becomes a requirement, this ADR is
  **void** and the decision must be taken again from a clean base, per SPEC
  §25.8: *"stop and perform a clean architecture/license review rather than
  trying to 'remove the license later.'"* The reason is structural, not
  procedural: AGPL §13 and the whole JUCE arrangement mean the licence attaches
  to the derivative work, so a closed-source variant is a different product
  built on a different (or proprietary-JUCE) base — it is not a relabelling of
  this one. Any closed-source path must re-evaluate the base (blank JUCE +
  commercial JUCE, or a non-JUCE audio stack), and it must redo
  `DEPENDENCIES.md`, because GPL-licensed drum samples, the VST3 SDK under
  GPLv3, and Eigen under MPL2 would each have to be re-derived rather than
  inherited.
- **We inherit a beta codebase's defects.** SPEC §2.1 is explicit that the
  July 2026 audit found real-time-safety and test-coverage concerns and that
  `triggerAsyncUpdate()` is still reachable from `processBlock`. G1 (DEVPLAN
  RT-001, TEST-001) is where that is paid down, and no amount of "the code
  mostly works" shortens it.
- **We inherit 237 kB of `PluginEditor.cpp` and 171 kB of
  `PluginProcessor.cpp`** — huge translation units that will slow every change
  and make merges painful in parallel-worktree development. Mitigated by
  DEVPLAN §4's source-ownership map and by the rule that new work goes in
  `src/jam/`, not into those files.
- **~460 grooves of GPL/CC-BY/midi-drums content is a permanent licence
  obligation** that must survive any future content replacement. Removing the
  content later does not remove the audit trail.
- **The upstream ancestry is missing locally.** `88f7e7c` is a **root commit
  here** while upstream's has a parent (`1d307b6e…`) and at least 59 more
  ancestors behind it — the tree content is byte-identical but the history was
  replaced by a snapshot commit. `README.md` line 48 and `CONTRIBUTING.md`
  line 82 promise preserved history, so SPEC §25.2 ("preserve notices **and
  history** required by its license") is only half met. **Action: the
  orchestrator must decide between (a) keeping the snapshot and pointing every
  document at the upstream SHA + URL + `BASELINE.md` (preserves the frozen-SHA
  guarantee everything else depends on) or (b) re-importing the upstream
  ancestry before release.** This ADR does not choose, because both are
  defensible and the choice is the orchestrator's; but it must not be left
  undecided past G0.
- **Six release gaps are open**, enumerated in `DEPENDENCIES.md` §9: the
  AudioDSPTools MIT-vs-Apache-2.0 conflict, Eigen's four `COPYING.*` files, the
  four OFL texts, the exact GPL version of GMRockKit, the TONE3000 mark
  permission, and the missing history. All are packaging/provenance decisions,
  not code.
- **`references/*` has no pins at all.** Eight submodules are declared in
  `.gitmodules` but have no gitlink — upstream or here — so the "pinned so any
  claim can be checked" promise in `README.md` line 91 is currently unmet. This
  matters because Airwindows is the only reference project whose code may be
  ported, and three effects *have been* ported from it; the port is legal under
  MIT, but the exact revision that was ported is currently unrecoverable.

### Explicitly unchanged

- SPEC.md is not amended by this ADR. It is a base-selection record.
- The product scope, the architecture (§6), the thread model (§7), the
  real-time contract (§7.1) and the Musical Clock design (§10) are untouched.
- The tracker question is untouched. BTrack / aubio / BeatNet are decided in
  ADR-0002 (ADR-TRACKER-001), on evidence, not here.

---

## Rejected alternatives

### 1. Start from a blank JUCE 8 application

**Rejected.** It is the only option that would resolve the AGPL problem
outright, so it deserves a real answer rather than a gesture.

- **It throws away the entire MVP.** A blank app means writing a guitar input
  path, a low-latency device layer with ASIO, a guitar DSP chain, a separate
  drum bus, a sample-accurate sequencer, a sampler, a groove library and stem
  recording *before* a single line of Musical Clock exists. SPEC §3 principle 8
  says *"No giant refactor before proof"* — a from-scratch rebuild is the
  largest possible refactor, and it delays the proof the whole plan is built
  around.
- **It forfeits the measured latency baseline.** SPEC §18.3 requires the exact
  monitoring baseline to be recorded *before* new work. Guitar-Companion ships
  with 48 kHz / 128-sample ASIO validation on real hardware (README line 110).
  A blank app has nothing to compare against, so the gate would have nothing to
  regress against either.
- **It does not remove AGPL, it just relocates it.** JUCE 8 is
  AGPLv3-or-commercial. A blank JUCE app is AGPLv3 too, unless a commercial JUCE
  licence is bought — which is a business decision, not an engineering one, and
  is not on the table here. The licence problem is a property of JUCE 8, not of
  the base.
- **It would not reuse the assets we are already committed to.** The GPL
  GMRockKit samples, the CC BY 4.0 grooves, the four OFL fonts and the
  MIT-licensed Airwindows ports would have to be re-derived for no benefit.

### 2. Giada as the base

**Rejected as a base; retained as a fallback and reference** (SPEC §2.2:
*"Fallback if Guitar-Companion fails stabilization/licensing gate"*).

- **It is a different product.** Giada is a live-looper / audio-engine tool
  built around clip launching, loop recording and overdub. The product here is a
  low-latency **monitoring** application for a guitarist: input monitoring at
  ≤ ~12 ms, a headphone path, and an autonomous drummer that decides when to
  join. Looper architecture does not give that; it gives a different thing
  very well.
- **The drum content would have to be thrown away or rebuilt.** The ~460
  grooves, the procedural generator, swing/humanization and the per-bar meter
  engine exist *here* and nowhere else. SPEC §4.1 lists "existing drum sample
  engine plus hosted drum VST3" as in scope.
- **The guitar path would have to be written.** Giada is not a guitar-DSP
  product; there is no NAM-integrated chain, no 23-effect pedalboard, no rig
  scene model.
- **Licence does not decide this one.** Giada's licence was not verified from
  this tree (it is not vendored here), so it is recorded `UNKNOWN — verify at
  the project's LICENSE`. Had it been the obvious licence answer, the
  engineering mismatch above would still reject it.

### 3. Hydrogen as the base

**Rejected as a base; already in use as a *content source*.**

- **It is a drum machine, not a guitar jam companion.** It has no guitar input
  path, no monitoring latency story, no guitar DSP chain, and no standalone
  low-latency plugin target. SPEC §1 and §17 are about the guitar path winning
  every deadline; Hydrogen cannot host that requirement.
- **Its pattern model is a linear grid, not an adaptive engine.** The product's
  whole thesis is a Musical Clock and a Jam Director making musical decisions at
  musical boundaries (SPEC §3 principles 2–4, §10, §13, §15). Hydrogen's
  sequencer is a pattern player.
- **It is already being used, correctly, as content.** SPEC §2.2 calls it
  *"Reference/content source where licensing permits; no runtime dependency
  required"*, and in practice the fork took exactly that: GMRockKit's samples
  and nothing of Hydrogen's code (`assets/drums/ORIGEM.txt`, `DEPENDENCIES.md`
  §1). Hydrogen is therefore a satisfied part of the decision, not a rejected
  one — the rejection is only of *Hydrogen-as-codebase*.
- ⚠ One licence loose end comes with it: the tree ships Hydrogen's GMRockKit
  audio stating only *"Licença: GPL"*, with no version. Recorded as a release gap
  in `DEPENDENCIES.md` §9 item 8.

### 4. Buy JUCE commercial and keep the base closed-source later

**Rejected explicitly**, and it is listed here because it is the tempting
option that SPEC §25.8 exists to prevent.

- **You cannot retro-fit it.** JUCE's licence is evaluated on the linked work.
  Once GUITAR-Companion's AGPLv3 code links AGPL JUCE, the result is an AGPL
  work. Adding a commercial JUCE licence later does not retroactively relicense
  code that was, for its whole life, under AGPL terms.
- **SPEC §25.8 forbids the plan before it starts:** *"stop and perform a clean
  architecture/license review rather than trying to 'remove the license later.'"*
- **It would also require re-deriving the GPL/MPL material**, not just the JUCE
  part — see the Consequence above.
- **If it is genuinely wanted, it is a fresh base decision**, and it should be
  argued in a new ADR against a blank JUCE build with commercial terms — not
  bolted onto this one.

### 5. BTrack (or aubio) as the base

**Not a coherent option, recorded so it is not mistaken for one.** A tracker is
a ~1 kLOC library behind an interface, not an application. They remain
candidates for the rhythm backend and are decided on evidence in ADR-0002
(ADR-TRACKER-001), per SPEC §12.

---

## Revisit triggers

This decision must be reopened — not patched — if **any** of these becomes
true:

1. Closed-source commercial distribution becomes a requirement (**the** trigger;
   SPEC §25.8).
2. Guitar-Companion fails the G1 stabilization gate badly enough that the
   inherited real-time hazards outnumber its useful capabilities.
3. Licensing review finds an undischarged obligation in the shipped artefact
   that cannot be closed by packaging (the AudioDSPTools conflict,
   `DEPENDENCIES.md` §9 item 5, is the current example).
4. JUCE 8's licence terms change in a way that alters the AGPL conclusion.