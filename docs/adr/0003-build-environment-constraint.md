# ADR-0003 — The JUCE application target cannot be built or tested in this environment

- **Status:** Accepted
- **Date:** 2026-10-07
- **Deciders:** orchestrator
- **Relates to:** SPEC.md §2.1, §18, §25; DEVPLAN.md G0, G1, G2, §9 (CI-001)
- **Numbering note:** 0001 is FND-001 (base selection) and 0002 is
  ADR-TRACKER-001 (tracker selection). This record is 0003 so that the two
  worker-owned ADRs keep their planned numbers.

## Resumption correction (2026-10-07)

The environment restriction is narrower than originally recorded. Root access
is **not** required to extract Debian development packages into a user prefix
and link the already-installed runtime libraries. The local JUCE configure,
including building/testing `juceaide`, now succeeds with ALSA/freetype/fontconfig
headers supplied this way. NAM's nested Eigen/AudioDSPTools gitlinks have also
been initialized. The original rejected alternative #1 was factually too strong.

`/tmp` is a separate full 7.9 GB tmpfs; the approximately 8 GB free on `/home`
does not help builds placed there. The retry uses
`/home/mojo/projects/guitars-build-resume/` for the prefix, build and `TMPDIR`.
The first application compile exposed real upstream Linux portability defects
(the `juce::jmin<int64>` SIMD overload and unguarded `windowsLocalAppData`), which
are being repaired narrowly. Successful configuration alone is not an app build.

The two-lane architecture and evidence requirements remain valid. Local Linux
compilation/tests can supply real JUCE evidence, while Windows/ASIO, scene
delivery with the editor closed, physical-device latency and complete callback
instrumentation still require their own runs. Current outcomes are recorded in
`EXECUTION-LEDGER.md`; the historical context/decision below describes the
limitations known when this ADR was written.

---

## Context

SPEC.md and DEVPLAN.md both assume the fork of
`raphaelfukuda/Guitar-Companion@88f7e7c8` is the working tree, and that gates
G0/G1 (baseline build, baseline tests, callback-safety instrumentation, CI
green) can be closed by building the JUCE plugin.

Measured facts about the machine this repository is being developed on
(Linux x86_64, 4 cores, 15 GB RAM):

| Fact | Value | How measured |
|---|---|---|
| Compiler | `g++` 14.2.0 (Debian 14.2.0-19) | `g++ --version` |
| CMake | **not on `PATH`**; 4.4.4 at `/tmp/opencode/venv/bin/cmake` | installed into a venv |
| Ninja | 1.13.2 at `/tmp/opencode/venv/bin/ninja` | installed into a venv |
| `sudo` | **unavailable**, password required | `sudo -n true` fails |
| ALSA headers | **absent** (`/usr/include/alsa` does not exist) | `ls /usr/include` |
| freetype headers | **absent** | `ls /usr/include` |
| X11 headers | present | `ls /usr/include` |
| Free disk on `/home` | ~8.8 GB (96 % used) | `df -h` |
| Network | working | `git ls-remote` succeeded |

`juce::juce_audio_devices` and `juce::juce_gui_basics` require the ALSA and
freetype development headers at configure/build time. They are not present and
cannot be installed without root. The upstream test target
`tests/CMakeLists.txt` links `juce::juce_audio_formats` and
`juce::juce_audio_processors`, so `GuitarCompanionTests` cannot be built either.

Consequence: **no gate that requires the JUCE target to compile can be closed on
this machine.** That set is, at minimum: FND-002 (baseline build/tests), the
hard callback-safety gate of G1 (§18.1 zero-allocation proof on the real
callback), and every hardware item in SPEC.md §21.5.

## Decision

1. **Split verification into two independent lanes.**

   - **Lane "core" (platform-neutral).** A new static library `jam-core`
     compiles `src/jam/*.cpp` — the Musical Clock, the analysis ring, tracker
     backends, the Jam Director, style parsing — with **no `juce::`
     dependency**. It builds and runs its tests on this machine and on any CI
     runner, with no audio hardware. This lane proves everything SPEC.md §10,
     §13, §14, §19, §21.1 and §21.2 require, which is the overwhelming majority
     of the musical-intelligence risk.
   - **Lane "plugin" (JUCE).** Remains open and unverifiable here. It is
     verified on a Windows/ASIO machine via CI, and by the hardware tests of
     §21.5.

2. **`jam-core` must never gain a `juce::` dependency.** This is a review-time
   rejection, not a preference. If it did, the real-time contract could no
   longer be reviewed or tested in isolation and this ADR's whole premise would
   be void.

3. **`src/jam/*.cpp` is the single source of the algorithm.** `jam-core` is not
   a copy or a shim; the JUCE plugin links the same objects. A parallel
   implementation is the failure mode this decision is preventing.

4. **`src/jam/` deliberately contains no audio-thread code yet.** The analysis
   tap that will connect the plugin's `processBlock` to `AnalysisAudioRing` is
   orchestrator-owned and arrives with INT-ANALYSIS-001 (DEVPLAN §17), after
   the callback-reachability audit has described the insertion point.

5. **Gates are recorded as PARTIAL, not PASSED.** A gate whose acceptance
   condition cannot be measured in either lane is not passed (DEVPLAN §25 of
   the operating rules, and G0/G1 in DEVPLAN §8/§10).

6. **`.gitea` is the forge, not `.github`.** The repository carries Gitea
   pull-request and issue templates. CI-001 will target the forge that actually
   exists rather than adding workflows to a forge that does not; that is a
   DEVPLAN correction recorded in the execution ledger.

## Consequences

**Positive**

- The highest-risk, highest-value work — a Musical Clock that behaves
  deterministically under tempo drift, syncopation, silence and resync — is
  verifiable today, on every commit, without hardware.
- The real-time contract for new code becomes reviewable in isolation.
- The Windows/ASIO lane, when it runs, is a much smaller surface: it only has
  to prove the callback wiring, because the intelligence underneath is already
  proven.

**Negative / accepted cost**

- The upstream `GuitarCompanionTests` suite cannot be executed here, so TEST-001
  (expanding foundation tests) cannot be *proven* green in this environment.
  Tests may be written; their run is deferred to the plugin lane.
- FND-002 cannot be completed here. It is recorded as an external blocker with
  an exact procedure, not as "done".
- Two build systems must be kept in step. Mitigated by rule 1 and 2 above plus
  the ban on workers editing `jam-core/CMakeLists.txt`.
- Disk is at 96 %. Reference submodules (`airwindows`, `guitarix`,
  `GxPlugins.lv2`, `lsp-plugins`, `BYOD`, `dragonfly-reverb`, `rkrlv2`,
  `ToobAmp`) must not be cloned.

**Explicitly unchanged**

- The product target, the architecture, and the real-time contract. This ADR
  changes only *how the work is verified on this machine*.
- SPEC.md is not amended by this ADR. It is an implementation/environment
  record.

## Rejected alternatives

1. **Install ALSA/freetype headers into the user prefix and point JUCE at
   them.** Rejected: it still needs root for the linker paths and system
   libraries, and a JUCE build here would produce a binary that proves nothing
   about the Windows/ASIO target that actually ships.
2. **Skip the JUCE target and treat the gates as unverifiable, doing only
   documentation work.** Rejected: it produces a plan with no product and no
   verifiable musical behaviour.
3. **Add a Python sidecar or service for the clock.** Rejected twice over: it
   is forbidden by DEVPLAN §18 ("no Python production sidecar") and it would
   put an inter-process dependency on the path between evidence and the
   drummer.
