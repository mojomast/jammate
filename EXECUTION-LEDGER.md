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
| Submodules initialised | JUCE, NAM Core, and NAM's pinned Eigen / AudioDSPTools; no reference repositories |
| Forge | Public GitHub repository `mojomast/jammate`; GitHub verification green; historical Gitea definitions retained |
| Build seam | `jam-core/` — platform-neutral, no `juce::`, tests via ctest |

## Verification lanes

| Lane | Buildable here | Verifies |
|---|---|---|
| **core** (`jam-core`, CMake + Ninja, no audio device) |27/28/28/29 suites OFF/BTrack/aubio/both pass | deterministic seams, worker lifecycle, live policy/session, injected drum bridge, acquisition/tempo diagnostics and replay validators; not all SPEC acceptance conditions |
| **plugin** (JUCE `GuitarCompanion` / `GuitarCompanionTests`) | Linux36/36 registered suites and20 UI cases pass; Windows8/8 suites; Standalone/VST3 build on both | live wiring and actual003 full54-cell/lifecycle/audio proof accepted; all six CI jobs green at74cdde4; ASIO/device gates remain open |

A gate whose acceptance condition lives only in the **plugin** lane is recorded
PARTIAL, never PASSED.

---

## Gate status

| Gate | State | Blocking items |
|---|---|---|
| **G0** fork/license/baseline | **PARTIAL** | Linux/Windows formats and drum tests verified; baseline physical audio/device and ASIO evidence incomplete |
| **G1** real-time foundation | **PARTIAL** | remote CI green; bounded Linux callbacks, LSTM/PReLU repair and editor-absent scene delivery measured; wider model/control, full callback/device/hosted-plugin and ASIO coverage incomplete |
| **G2** new module seams | **PARTIAL (advanced early)** | analyzer/clock/processor/editor connected and actual lifecycle/audio proof accepted; broader seam/host coverage remains open |
| **G3** tracker selected | **IN PROGRESS (advanced early)** | frozen stability candidate recovers diagnostic regressions in preserved matrix; representative guitar useful-lock evidence and production selection ADR incomplete |
| **G4** musical clock | **PARTIAL (advanced early)** | actual003 initial/restarted joins, stops, resync, release and drum-only output pass; broader musical/physical acceptance remains open |
| **G5** adaptive drummer | **PARTIAL (first slice)** | one live4/4 Rock groove; adaptive styles/dynamics/fills and broader musical acceptance remain open |
| **G6/G7** UX / release | **PARTIAL UX / release open** | real Jam controls fit1100×700 and20 UI cases pass; physical play/release acceptance remains open |

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
4. **D4 — offline tracker work advanced before G0/G1/G2 closure.** This yields
   evaluation evidence only; no production tracker has been selected or wired.
5. **D5 — local JUCE verification retried without root.** Extracted Debian
   development packages and existing runtime libraries can supply a user prefix.
   ADR-0003's assertion that this necessarily requires root was too strong.
   `/tmp` is a full tmpfs, distinct from `/home`; plugin build and compiler
    scratch use `/home/mojo/projects/guitars-build-resume/`.
6. **D6 — ANALYSIS-001 lifecycle subset starts before G3.** The worker accepts an
   injected `IRhythmTracker`; lifecycle, bounded publication, discontinuities and
   shutdown can be verified without selecting a production backend. No processor
   wiring is authorised. Full ANALYSIS-001 stays partial until selected-backend
    wiring and live integration are measured.
7. **D7 — MOD-003 isolated UI shell starts before full G1/G2.** A separately
   compiled JUCE preview uses explicit simulated state and an intent interface.
   No processor/editor wiring or live adaptive behavior is authorised.
8. **D8 — TEST-001 resumes using the verified local JUCE lane.** Regression
   tests may document existing parser/transport compatibility, without changing
   production behavior. The former claim that JUCE cannot run here is superseded
   by the executed Linux builds and drum suites.
9. **D9 — CI-003 adds GitHub Actions after publication.** User requested and
   approved public `mojomast/jammate`. D3 described the earlier assumed forge;
   GitHub is now the measured destination. Remote CI status is recorded only
   after execution, separately from local workflow checks.
10. **D10 — DIAG-001 portable foundation starts before full G4.** Injected
    observation/clock records, bounded publication and off-thread trace export
    can be verified independently of tracker selection and processor wiring.
    No production analyzer/clock/editor integration is authorised by this task;
    full DIAG-001 stays partial until that integration and overhead are measured.

### Active adaptive/evaluation/device wave (base `c8f87a8`, 2026-10-08)

The user authorised Flash subagents to build the remaining work. Four isolated
workers use `opencode-go/deepseek-v4.1-flash`; contract:
`docs/research/ADAPTIVE-WAVE-CONTRACT.md`. Orchestrator owns shared build/CI,
live/UI integration and independent review. Physical/guitar gates require real
measurements and are not closed by software or synthetic tests.

| Task | State | Worktree / ownership |
|---|---|---|
| STYLE-DIRECTOR-002 | BUILDING `ses_ee48b3de2ffe1Y3nPOHAj9Hrks` | `../worktrees/STYLE-DIRECTOR-002`; new catalog/director and portable tests |
| DRUM-ADAPT-002 | BUILDING `ses_ee48af76cffe2JZ2Uc0FTbgI1S` | `../worktrees/DRUM-ADAPT-002`; bridge/engine adaptation and actual-engine tests |
| EVAL-GUITAR-009 | REVIEW FIX `d64a598`, worker `ses_ee48ab4e4ffe1yZOM5auV6mf2P` | `../worktrees/EVAL-GUITAR-009`; representative-guitar import/useful-lock evaluation |
| DEVICE-002 | REVIEW `dbd261d`, worker `ses_ee48a7730ffe4JXF99n4BDv8R4` | `../worktrees/DEVICE-002`; physical-device/play evidence tooling |

The orchestrator appended adaptive command IDs without renumbering the live
commands, added worker-setting/audio-echo telemetry and wired the UI mapping.
22 mock-facade UI cases plus posted-click checks pass; live adaptive integration
is pending the reviewed core/engine handoffs. The additive processor replay
protocol is preregistered in `docs/research/ADAPTIVE-REPLAY-PROTOCOL.md`.

Independent guitar-evaluator review `ses_ee47aee4dffepdCyRvCHlZTUya` reproduces
37 passing tests and the honest synthetic baseline, but rejects handoff `d64a598`
for missing generated-provenance consistency and requested-backend/derived-trace
gate binding. The worker is correcting these additively; no merge is accepted.
Independent UI/replay review `ses_ee4818d5fffejWM58Hi7Lw43BZ` accepts UI and
instrumentation under their scope and requests stronger replay source/hash
closure, per-style mapping and actual fill duration. `a6a5161` repairs provenance,
retained-run policy, finite mode conversion and actual fill-duration checks;
per-style catalogue checks await the core handoff.

DEVICE-002 reports45 passing tests, a labelled synthetic96-sample/2 ms latency
self-check and correctly failing physical gates. Independent read-only review
`ses_ee47517e0ffe4app9SU0Y82PUX` is active. The environment has no audio device or
Windows/ASIO host; no physical evidence is claimed. The local source-tree product
build completes Standalone/VST3/tests in the backend-OFF variant; the experimental
BTrack-enabled build is now running. Adaptive live wiring remains pending.

### Completed live-Jam wave (base `88893e2`, 2026-10-08)

The user authorised the first audible live loop after the reviewed bridge wave.
The orchestrator committed the JUCE-free command/telemetry facade and lifecycle
contract before launching three **`opencode-go/deepseek-v4.1-flash`** workers:

| Task | State | Session | Ownership |
|---|---|---|---|
| INT-LIVE-001 live processor pipeline | DONE `016c0c8` | `ses_ee61b0353ffeSHR8l1MAWzTbHT` | `../worktrees/INT-LIVE-001-pipeline`; processor implementation, new live session/join policy and portable checks |
| UI-LIVE-001 real Jam screen | DONE `8ac4517` | `ses_ee61a7b1fffeYYPxryxjoq8po3` | `../worktrees/UI-LIVE-001-jam-screen`; editor/overlay, live-state presentation/intent mapping and JUCE checks |
| EVAL-LIVE-001 actual processor replay | DONE `f554884` + `24af413`/actual003 | `ses_ee619cf9affebFyi8MbdHaUD1Y` | `../worktrees/EVAL-LIVE-001-replay`; preregistered actual replay, validator and preserved evidence |

Contract: `docs/research/LIVE-JAM-CONTRACT.md` and
`src/jam/JamLiveInterface.h`. Start/Stop do bounded command publication; lifecycle
start/join happens off the callback. Observation event/horizon and observed audio
cursor at receipt stay distinct. UI receives coherent telemetry and actual
audio-owner playback echo. Default BTrack is experimental live wiring, not a G3
selection or TRACK-008 promotion. Advanced style/fill controls are unavailable
for the one-Rock-groove slice. Shared build/CI and integration remain
orchestrator-owned. Final UI `8ac4517` and pipeline `016c0c8` are accepted and
integrated after focused review and fresh product verification. Replay `5c0d72e`
is accepted and integrated after its additive outcome/input-timeline corrections.
The first actual processor full-matrix measurement is running.

All three implementation workers have submitted clean handoffs: INT-LIVE-001
`f3151f0`, UI-LIVE-001 `f9d1d15`, EVAL-LIVE-001 `07021ae`. Independent reviews
are running (`ses_ee6044162ffegWyo0h1pDwpGkn`,
`ses_ee6058a62ffediVmS5lESFimid`, `ses_ee602d8ffffeo0kMc3fzyki35r`).
Fresh UI verification passes9 JUCE cases and the editor TU compile; fresh
pipeline verification passes25 portable suites and the driver. Replay validator
units pass42 tests. Combined candidate product build is running in the external
integration worktree; it is not an accepted/main implementation merge.
Actual processor scoring is pending review and Stop-semantics resolution.

Independent reviews block the initial handoffs. INT-LIVE-001 is correcting
ignored bridge enqueue failure (stuck join/lost stop), injected-backend identity,
manual transport suppression after Jam, stale release telemetry and duplicate
discontinuity counts. UI-LIVE-001 is correcting off-screen primary controls at
1100×700, rapid intent handling and the legacy preview self-check. EVAL-LIVE-001
is correcting asynchronous snapshot assumptions, missing BTrack link archives,
backend/synthetic identity, smoke scope, timeouts and lag metrics under an
additive pre-measurement amendment. Stop semantics are frozen in
`docs/research/LIVE-JAM-STOP-CONTRACT.md`; original replay protocol/raw remain
preserved. Combined product building exposed a public libsamplerate config flag
leaking into JUCE PNG; the orchestrator's private-configuration fix is committed
at `c2137d9` and rebuilding. No actual processor scoring has run.

The combined candidate now builds Linux Standalone, VST3 and the JUCE test
executable. Product static libraries require PIC (`1df104b`) for VST3; the
private samplerate configuration and PIC fixes resolve both observed build
failures. Candidate source/artifact/log identities are preserved externally in
`build-INT-LIVE-001-integration/candidate-build-receipt.json`. Registered-suite
verification is running after building the portable test executable as well.
The initial handoffs remain review-blocked; corrected worker handoffs and actual
processor scoring are pending.

Initial combined candidate passes all36 registered suites (28 core/backend/
research and8 drum/UI); both registration/linkage guards pass after matching
real backend symbols instead of the `bTrack` substring in `StubTracker`.
Corrected pipeline handoff `ba9748a` addresses the review findings and is under
re-review. Its StopNow bridge header is pinned pre-measurement in
`docs/research/live-jam-bridge-pin-amendment.json`; pinning is not acceptance.
UI/replay corrections remain active and actual live processor scoring has not run.

Corrected pipeline `ba9748a` independently rebuilds Linux Standalone/VST3 and
passes36/36 registered suites, but focused re-review found repeated stop commands
on persistent Lost and an unhandled Stop-next-bar→Stop upgrade. Those policy
fixes are back with the worker. Corrected UI `49cbc76` independently rebuilds
the product and passes14/14 JUCE cases; its re-review is active. Corrected replay
`a3ecf1f` preserves the original freeze/raw, adds a preregistered amendment and
reports67 validator tests; fresh validator/harness-link verification and
re-review are running. Exact bridge/processor pins are committed in
`docs/research/live-jam-replay-source-pins.json` before actual measurements.

Fresh corrected replay verification passes67 validator units, instrumentation/
support/facade self-checks and actual-product harness linking. Preflight reports
LIVE-READY and detects the injected test seam; the real measurement binary was
not invoked. Policy/UI/replay focused reviews still gate scoring and acceptance.

Further focused review: UI `49cbc76` layout and legacy preview are accepted, but
the cold local intent latch sends Start from a STOP-labelled button after editor
recreation; reconciliation is being corrected. Pipeline `976250e` resolves loss
spam and stop upgrades; its new staged-tempo dedupe exposed acceptance-before-
latch handling on a full queue, which is being corrected. Replay `a3ecf1f` fixes
the initial ten findings and links to the product, but runtime backend probing
must occur after prepare, and validators must gate scenarios, measured counters
and complete findings/timeout semantics. Those fixes are active. All original
protocol/raw artifacts remain preserved; no actual processor measurement has run.

Final acceptance update: UI `8ac4517` passes20/20 fresh product UI cases and
focused review accepts its effective-intent reconciliation. Pipeline `016c0c8`
passes fresh Linux Standalone/VST3 builds,36/36 registered suites and core
linkage guards; orchestrator review confirms staged tempo is latched only on
accepted publication. Both handoffs are merged locally. Replay readiness/
validator correction remains with the worker; actual scoring has not run.

Fresh optional-backend verification passes core27/27 OFF,28/28 BTrack-only,
28/28 aubio-only and29/29 both, including linkage guards (BTrack is covered by
the36-suite product run). Replay `566032f` fixes nonjoining backend identity and
the timeout bound. Pre-measurement inspection then found stereo WAV cursor
advance and source/device-rate mapping errors; the fixture playback correction
is active under `docs/research/LIVE-JAM-FIXTURE-TIMELINE.md`. Original fixture
bytes/protocols remain preserved. No actual processor replay has run.

Replay `5c0d72e` is accepted/merged: fixture playback advances once per device
frame, applies source/device-rate conversion and maintains channel/chunk
coherence; nonjoining backend identity and timeout bounds are corrected.
First actual full replay is running in
`build-EVAL-LIVE-001-integration/actual-full-001` with the canonical synthetic
WAVs,54 cells and both supplemental scenarios. Results are not yet known.

First actual replay completed and is preserved in
`docs/research/live-jam-replay/actual-full-001/` with raw evidence, validator
verdict, manifests/log and hashes. All54 cells pass the callback RT gate. The
default experimental BTrack scenario joins at block753 (about8.03 s) and fires59
drum steps on the declared built-in120 BPM synthetic signal. Overall acceptance
fails:108 end-state checks because unsuccessful final LatestValue reads left
default fields, plus the injected scenario ran unpaced and never joined. Worker
capture/pacing/lifecycle-proof corrections are active before a new measured run.
Original raw results are not re-scored or rewritten.

Replay worker `35cd3f8` addresses actual001's snapshot/pacing failures with a
bounded prepared baseline, retention on unsuccessful latest-value reads, paced
injected first/second joins, actual engine stop and session-generation/released
payload checks. Orchestrator review accepts those fixes; the resync proof is
being tightened to require a rendered downbeat-phase change while already
playing rather than a generic new step after restarting. Actual002 remains
pending; no additional measurement has run.

Actual003 completes with all4090 checks passing,54 measured callback cells and
all RT/default-backend/injected-lifecycle gates true. Default BTrack joins the
synthetic signal at block752 (~8.02 s),59 steps. Injected first/restarted joins,
deferred bar stop187 blocks, immediate Stop1 block, resync phase2→1 and session
generation1→2/release pass. Zero-input internal-kit output is measured across
48,128 samples: mean block RMS0.09969, peak0.98615,8 steps; its allocator scope
is explicitly unmeasured. Raw/hashes are in
`docs/research/live-jam-replay/actual-full-003/`; failed001/002 remain unchanged.
Final independent audit and publication CI are pending.

Final independent read-only audit ACCEPTS code and actual003 measurement:
4051 hard checks,36 advisory checks and3 gates pass; all54 cells' owner
positions/counter families are valid, all link/archive/raw hashes re-match.
INT-LIVE-001 `016c0c8`, UI-LIVE-001 `8ac4517` and EVAL-LIVE-001 through
`f554884` plus orchestrator `99355bf`/`24af413` are accepted and integrated.
First-slice work is complete locally. Publication/GitHub Windows/NAM promotion
is next; G0/G1/G3 and broader physical/musical/release gates remain partial.

Published `23c4299` run37746525272 passes all four core configurations and NAM,
but Windows fails compiling upstream BTrack's GNU VLAs and `M_PI`. A Windows-
only generated overlay is source-pinned, preserves vendored/Linux source and
passes strict C++17 plus changed-pin/original-VLA negative controls. Native
original/overlay beat/tempo/cumulative outputs are bit-identical across36,000
rows. This repair does not alter clock thresholds or the measured Linux path;
Windows promotion is being retried.

Live wave publication completes: `74cdde4` run37748022994 passes all six jobs:
core27/28/28/29, NAM9/9, Windows Standalone/VST3 and8/8 drum/Jam UI suites.
Downloaded-log hashes are committed in
`docs/research/github-ci/run-37748022994.json`; archive SHA256
`b5084e46d4b0f52a79a3feb112004fd51f62ba28e0a619e17914b0f0a0e8b373`.
Independent review accepts the MSVC overlay;36,000 native comparison rows are
bit-identical and Linux implementation bytes remain unchanged. First live slice
is DONE/published; representative guitar, ASIO/device, worker allocation and
broader adaptive/release gates remain open.

**D11 — first audible live loop before full G1/G3 closure.** The user authorised
live processor and UI connection after independent repair/bridge review. The
default BTrack adapter is experimental. Synthetic processor replay verifies
wiring and callback behavior; it does not select the production tracker or
replace physical-device/real-guitar evidence.

### Completed build wave (base `677727c`, 2026-10-08)

The user authorised three implementation workers on
**`opencode-go/deepseek-v4.1-flash`**. Each owns an isolated branch and worktree:

| Task | Session | Ownership |
|---|---|---|
| RT-005 NAM activation allocation repair | `ses_ee6748237ffehxD8TO1at7Tb1N` | `../worktrees/RT-005-nam-activations`; generated NAM overlay/patches, repair checks and new evidence |
| TRACK-008 frozen tempo-stability candidate | `ses_ee67420b9ffexDdmhC9V6lUJKW` | `../worktrees/TRACK-008-tempo-stability`; new diagnostic candidate, frozen protocol and paired evidence |
| INT-DRUM-001 actual clock-to-drum bridge | `ses_ee673b711ffeFAFwgDVHAN3Z1O` | `../worktrees/INT-DRUM-001-clock-bridge`; DrumEngine audio-owner seam, bounded bridge and actual-JUCE checks |

RT-005 must retain original and LSTM-only positive controls and prove numerical
equivalence. TRACK-008 must commit its protocol and candidate freeze before
scoring, preserving all acquisition regressions and backend-specific windows.
INT-DRUM-001 must demonstrate injected next-bar join, clock-owned tempo updates,
stop and resync on the actual renderer. Shared build/CI, processor/editor wiring
and current-facing documents remain orchestrator-owned. Committed handoffs need
independent verification before integration. No new gate pass is claimed.

Current review state: TRACK-008 final `f621ee2` is accepted and integrated after
independent review and fresh plugin/replay verification (160 scores,24744
non-CPU fields,160 beat files,four method logs,eight derived outputs exact).
Its corrected method summary independently reproduces; historical evidence and
behavior freeze remain unchanged. Core24/24 both-enabled and22/22 OFF pass.
Receipt: `docs/research/tempo-stability-integration.json`.
INT-DRUM-001 `724d75f` passes the fresh driver (10 engine cases,
11 portable tests, 50 legacy cases), but review blocks merge on coincident
join/tempo ordering, beat-resync phase divergence and duplicate heap-probe
symbols. Its worker is correcting these and adding internal-renderer coverage.
RT-005 final `e83d619` is accepted and integrated after independent source review,
fresh9/9 required NAM suites and15-process replay (390 cases/120 NAM,16770 stable
fields exact). The hardened validator also passes on that independent replay.
All five models are measured-clean with the repair; three original/LSTM-only
positive controls retain their allocation findings. Receipt:
`docs/research/nam-activation-repair-integration.json`. G1 remains partial.

Published TRACK-008 run37726314737 at `1b05672` passed NAM and Windows but all
four core lanes failed on the research test's local binary path. The integration
correction keeps portable source/freeze/provenance authentication required and
makes the actual local artifact check explicit (`JAM_TEMPO_LOCAL_FREEZE=1`).
27 tests pass with that local check enabled; with frozen artifact paths simulated
absent,26 portable tests pass and the one local check is explicitly skipped.
Both registered research suites pass. Remote rerun is pending; failure receipt:
`docs/research/github-ci/run-37726314737.json`.

Run37727369967 at `e26eeb6` confirms the core portability fix: all core lanes
pass22/23/23/24 suites and Windows passes6/6 drums. NAM's seven numerical/patch
suites pass, but the two new evidence suites still require local probe/archive
paths. An explicit `--evidence-only` CI mode retains all recorded identity and
counter checks while default local validation remains strict.34 unit tests,
absent-build acceptance in evidence mode, strict rejection of absent builds,
strict local validation and both registered evidence suites pass locally.
Receipt: `docs/research/github-ci/run-37727369967.json`. Remote rerun pending.

Run37728344321 at `f7c0a11` passes all six jobs: core22/23/23/24, NAM9/9 and
Windows Standalone/VST3 with6/6 drums. This confirms both recorded-evidence
portability corrections. Receipt/log hashes:
`docs/research/github-ci/run-37728344321.json`.
INT-DRUM-001 final `467603e` is accepted after review and personal narrow-delta
verification of session reset and chronological staged tempo/resync handling.
The merged independent driver passes76 instrumented/75 normal JUCE cases and
20 portable tests/205 checks; core25/25 both-enabled and23/23 OFF pass. Shared
product/test linking and fail-closed registration are integrated. No live
processor wiring or G4/G1 pass is claimed.
The final-source Linux product/test build passes Standalone, VST3 and7/7
registered drum suites; the updated legacy foundation driver passes50 cases.
Receipt: `docs/research/drum-clock-bridge-integration.json`. The wave's
implementation workers are complete; final published-head CI is pending.

Final published-head run37729980845 at `b7e3be1` passes all six jobs: core
23/24/24/25, NAM9/9 and Windows Standalone/VST3 with7/7 drum suites. This
includes the injected-clock bridge and precedes the new live-Jam wave. Receipt:
`docs/research/github-ci/run-37729980845.json`.

### Completed development wave (base `bcf540a`, 2026-10-08)

The wave started on Haiku on OpenCode Go. The user's subsequent instruction
switched it immediately to **`opencode-go/deepseek-v4.1-flash`**. All three
Haiku continuations were interrupted successfully; continuations were launched
on DeepSeek in the same isolated worktrees, preserving commits and in-progress
files. All three continuations then returned a provider error: this Go model requires
**Global** regions in the workspace's Privacy settings. Their work is paused
pending that setting; no fallback model is authorised. The user subsequently
confirmed the model is enabled, and all three continuations were retried on the
same Go model. All three retries returned the same Global-region error before
execution. The user then authorised **Space Bunny while waiting**; the same
sessions have been continued with **`opencode-go/space-bunny`**, preserving work.
After RT-004 and DIAG-001 returned their corrections, the user switched back to
**`opencode-go/deepseek-v4.1-flash`** from now on. The remaining TRACK-007 Space
Bunny continuation was interrupted successfully and relaunched on DeepSeek Go.

| Task | Session | Ownership |
|---|---|---|
| TRACK-007 longer-window acquisition characterization | `ses_ee6b11506ffeO88DmgmBtgPPW8` | `../worktrees/TRACK-007-long-windows`; new generator/protocol and evidence only |
| RT-004 broader NAM architecture callback coverage | `ses_ee6b0ad29ffePmZt4GEancg5T3` | `../worktrees/RT-004-nam-architectures`; new runner/validators and evidence only |
| DIAG-001 portable diagnostics foundation (D10) | `ses_ee6b04532ffeuT9R0iCDjXGZP8` | `../worktrees/DIAG-001-core`; new diagnostics/trace interfaces and tests |

TRACK-007 must commit its fixed protocol before running new fixtures. RT-004
retains positive findings and explicit load failures; no architecture repairs
are included. DIAG-001 preserves event, horizon and measured availability as
distinct clocks. Shared build/CI/processor/editor surfaces remain orchestrator
owned; each handoff needs a clean commit, executed checks and independent review.
All three handoffs are now accepted and integrated. RT-004 retains the A2
activation finding, DIAG-001 remains a partial live task, and TRACK-007 preserves
its original evidence alongside reviewed corrections. No worker remains active.

### Resumption wave handoffs (2026-10-08)

Both Flash launch attempts failed immediately with `Insufficient Balance`.
The user explicitly approved **OpenAI Sol** for the replacement workers.

| Task | Worker session | Owned worktree / paths |
|---|---|---|
| TRACK-006 | `ses_ee6cc1552ffejirmt58pxD4co6` | `../worktrees/TRACK-006-variant-robustness`; fixed-variant paired tooling and new evidence |
| MOD-003 (D7) | `ses_ee6cbe495ffeIbNYJkk4me4r7b` | `../worktrees/MOD-003-jam-ui`; isolated Jam UI and preview |
| TEST-001 (D8) | `ses_ee6cbaba5ffep3Oe3caD334mQL` | `../worktrees/TEST-001-foundation`; new foundation tests and standalone runner |

Orchestrator owns CI-003 (`.github/workflows/`, `tools/ci/`) and integration.
All three workers have returned committed, clean handoffs; their accepted status
and integration evidence are recorded in the task table below. CI-003's first
remote run passed all core/NAM jobs; Windows stopped on generated CRLF bytes.
The explicit-LF correction preserves every expected hash and passes5/5 local NAM
suites. Remote rerun37715897283 at `0820bb5` passed all6 jobs: core18/19/19/20,
NAM5/5, Windows Standalone/VST3 and6/6 drum suites. Device/ASIO evidence remains open.

---

## Latest executed integration checks

- `677ce9f`: real pinned-JUCE Linux Standalone and VST3 built; all five drum
  suites passed. Artifacts/commands in `docs/research/LOCAL-LINUX-BUILD.md`.
- Enabled BTrack + aubio core: **11/11 ctest suites passed** after EVAL-004 and
  EVAL-003 integration, including generator acoustic checks.
- Default-OFF core: **9/9 passed**; no foreign tracker archives or symbols.
- Both adapters with `JAM_CORE_BUILD_TESTS=OFF` built with no test binaries or
  test registrations. The option now actually controls all test targets.
- After TRACK-003/EVAL-005 integration, research-tool tests are registered in
  CMake: enabled trackers **13/13 suites pass**, default-OFF **11/11 pass**.
  Both-enabled tests-OFF still registers **0 tests**. The two new suites verify
  scorer/aggregator contracts; the BeatNet suite runs no model inference.
- CI-002 integration (2026-10-08): actual tracker workflow checkout/build/test
  bodies run against committed `6e03b40` locally, passing **11/12/12/13 suites**
  for OFF/BTrack/aubio/both. Separate core workflow configure/build/boundary/test
  bodies pass **11/11**. All 19 shell bodies pass `bash -n`; structural validator
  passes. Toolchain installation, remote server CI and Windows remain unexecuted.
- Corrected corpus comparison: BTrack F=0.7099, acquisition 4/11, worst core BPM
  error 2.34%; aubio F=0.5357, acquisition 7/11, worst core BPM error 1.33%.
  Acquisition fails for both. No tracker selected and no G0/G1/G3 pass claimed.
- EVAL-006 integration: corrected handoff `e0e3bde`, **37 acoustic/integrity/
  regeneration tests pass**. All 19 repaired-manifest WAV references and four
  raw-run hashes independently verified. Both-enabled core now **14/14 passes**;
  the newly registered sustain suite also passes with trackers OFF (12 suites
  registered; prior full OFF run was 11/11). Tests-OFF still registers zero.
  All 19 workflow shell bodies pass syntax checks after adding the required
  sustain-suite guard. New fixture F=0.7273/0.5882 (BTrack/aubio); repaired-corpus
  acquisition=5/11 and 7/11, so both still fail. By-name CorpusDefect label remains
  conservative/stale for the repaired fixture; no silence gate pass inferred.
- TRACK-004 integration: accepted revised `98da13f` with integer-overflow and
  incomplete-availability corrections; **63 replay checks pass**. Independently
  reproduced all 19 fixture metrics, acquisition JSON and stored trace rows for
  both real trackers at 128/512 frames. Original WAV hashes and plugin hashes
  verified. Both-enabled core **15/15**, default-OFF **13/13**, tests-OFF zero
  registrations; all 19 workflow shell bodies pass syntax checks. At 128 frames
  four BTrack failed core fixtures have only numerical BPM disagreement in the
  longest positional run; palm-mute mixes that with phase-invalid evidence.
  The observed 123.046875 report equals lag42 exactly, but lag42 selection's
  internal cause remains unresolved. No adapter/scorer gate changed.
- EVAL-007 integration: accepted worker `514c154`, with CSV citation/escaping
  corrections. All six independent CLI reruns preserve scored results except
  timing and reproduce zero non-coverage diffs against prior artifacts. All 62
  declared WAV entries and both plugin hashes verified; CSV fields round-tripped.
  Enabled core **16/16**, OFF **14/14**, 19 workflow shell bodies valid. The
  unchanged robustness aggregator reproduces 2160 rows with only CPU/coverage
  string differences. Original sustained hash stays CorpusDefect; repaired
  sustain/tapping assessed; five derived-noise clips explicitly unassessed.
- RT-002 integration: corrected worker `6e89b1c` accepted with final fail-closed
  script/output/source-pin fixes. Independent real-processor probe reproduces all
  26 CSV cases except wall time: 18 dry/drum cases clean in the measured set;
  eight NAM cases allocate twice per model sample (calloc48 + malloc12). Real
  editor-absent scene delivery observed at 30.5 ms, with no-audio 150/400 ms
  controls distinguishing the ~257 ms fallback. All 15 CLI/output checks,
  failing-tee/source-pin injections and artifact hashes verified. This is bounded
  non-device evidence; model-specific callback allocation is an open G1 blocker.
- TRACK-005 integration: corrected worker `7b3a4d7` accepted; 67 C++ checks,
  8 Python tests and 22 CLI failure checks pass. All six diagnostic runs and both
  click sweeps/step sidecars independently reproduced; beat series unchanged.
  Added actual 512-frame metric records and a framing-mismatch rejection guard.
  All 38 input entries verified; current scorer agrees except coverage/timing.
  Variant acquisition original128=6/11, repaired128=7/11, original512=8/11;
  worst core BPM error 0.77%. Sparse original sustain low-tempo/acceleration
  proxy regression retained; no default or gate change, G3 open. Enabled core
  18/18, default-OFF16/16, 19 workflow shell bodies valid.
- RT-003 integration: corrected `401a6c1`,5/5 standalone suites pass; independent
  six-config/three-block output binaries are byte-identical. Baseline/repaired
  processor probes reproduce two/zero C allocations per model sample. Fixed the
  actual product include-root regression; fresh Standalone/VST3 builds and5/5
  JUCE drum suites pass. Further probe against fresh product archives is clean
  across all26 cases, including all8 NAM cases. Case matrix, heap frees and scene
  evidence validators strengthened; new Linux NAM workflow requires five suites;
  all22 workflow shell bodies pass syntax checks. Submodule remains pristine.
  This closes the measured example-LSTM allocation issue, not full G1 coverage.
- ANALYSIS-001 lifecycle foundation accepted at `5162e4d`, completed locally
  after Flash balance interruption.29 tests/1210 checks and a limited synthetic
  ThreadSanitizer run pass. Five benchmark-failure checks pass; real pinned
  plugins measured61–75× real time with no keep-up queue drops. Restart discards
  stale audio, output loss is counted per session, and frame-end is explicitly an
  input horizon with live availability unmeasured. Core20/20 ON,18/18 OFF; both
  Linux workflows require analyzer/benchmark suites and all22 shell bodies valid.
  Full ANALYSIS-001 stays partial until selection and processor-clock wiring.

### Returned handoffs under integration review (2026-10-07)

All three initial Flash handoffs were committed with clean worker trees. Their
review findings and correction status are:

- **EVAL-005 `acc6e7f`:** both real trackers produced derived-corpus runs, but
  `spec2pctWithin` coerces relative error to a boolean before thresholding,
  incorrectly failing small nonzero errors. Require numerical regression cases,
  stronger source-window/identity validation, both backends in reproduction,
  and regeneration against main's `0027baa` latency-mean correction. Whole-clip
  median BPM does not establish within-clip tempo-step following.
  Revised `816a955` is accepted and integrated: **29 tests pass**, including
  both real trackers; all 24 derived WAV hashes/sizes checked. Integration
  independently verified manifest/raw-result hashes and reproduced all **2160
  rows** and coverage exactly. The threshold correction changes 27 diagnostic
  rows to within-2%; no release gate or selection changes.
- **TRACK-003 `6a84ae1`:** accept the direction of the measured partial
  feasibility result (no inference), pending precise licence wording and scorer
  contract corrections. Repository CC-BY-4.0 terms and absence of a separate
  weights file do not establish blanket AGPL incompatibility or verified model
  redistribution rights. Declared provenance hashes do not authenticate a run.
  Require mode/availability validation, finite timestamps and explicit coverage.
  Revised `1ef3d5c` is accepted and integrated with two final scorer corrections:
  actual WAV-byte hashing and `time`-key availability math, plus nonnegative
  clip times. **51 tests pass on main.** No BeatNet inference was executed.
- **CI-002 `4dfbc96`:** genuine four-configuration builds replace the tripwire,
  pending enabled-symbol assertions, fail-closed `nm` checks, required Python/
  derived suites, and a Windows reference-materialization guard that tolerates
  empty gitlink placeholders. Correct stale Linux build and checkout claims.
  Revised `480f15f` accepted and integrated with conditional required-suite
  checks for RhythmRobustness and BeatNetResearch; current-main workflow bodies
  pass 11/12/12/13, and separate core workflow passes 11/11 locally.

These are review findings, not new tracker measurements or gate passes. Exact
continuation sessions remain in `HANDOFF.md`; no worker topics are duplicated.

## Model routing for delegated work

| Work class | Model |
|---|---|
| Development, evidence, tests and reviews | **`opencode-go/deepseek-v4.1-flash`** |
| Access history | Earlier Go requests returned Global-region rejection; latest continuation relaunched per user instruction |

Historical routing: Flash was initially requested, then Sol workers were
authorised after balance failures. Those workers completed the three packages
above. The latest user instruction replaces Sol subagents with Haiku on OpenCode
Go. Haiku audit `ses_ee6b95c28ffegXxn37dQR53e4e` completed successfully and its
concrete current-facing documentation findings were reviewed and corrected.

**Latest user instruction supersedes the queued switch:** stop Haiku and switch
now to **`opencode-go/deepseek-v4.1-flash`** (DeepSeek V4.1 Flash on OpenCode Go),
verified in the model catalog. All three interrupt responses returned
`interrupted: true`; continuations target DeepSeek Go. All three were
rejected before execution because the workspace region setting must be Global.
This is distinct from
the earlier direct DeepSeek route that reported insufficient balance. After all
three retries returned the same region rejection, the user authorised Space
Bunny while waiting. The active temporary route is **`opencode-go/space-bunny`**.
That temporary routing is now superseded: the latest user instruction restores
**`opencode-go/deepseek-v4.1-flash`** from now on. Completed Space Bunny handoffs
remain intact; only the remaining TRACK-007 worker was interrupted and switched.

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
| FND-002 baseline build record | PARTIAL:Linux/Windows builds/tests verified | main, `677ce9f` + `0820bb5` | orchestrator | G0 | both formats and6/6 drum suites on Linux/Windows; device/ASIO baseline outstanding |
| FND-003 RT reachability map | **DONE** | `wp/FND-003-rt-reach` → merged `931be23` | A — real-time | G0 | P0 `triggerAsyncUpdate` CONFIRMED reachable |
| — ADR-0003 build environment | **DONE** | `48f301c` | orchestrator | G0 | two-lane verification split |
| MOD-001 analysis ring tests | **DONE** | `wp/MOD-001-ring` → merged `89db28d` | B — intelligence | G2 | 19 tests / 95 236 checks; 2 header defects fixed in `efb820b` |
| CLOCK-001 musical clock | **DONE** | `wp/CLOCK-001-clock` → merged `b78c43c` | B — intelligence | G4 | 16 scenarios green; 2 policy gaps decided |
| — library-identity decision | **DONE** | `f04c055` | orchestrator | G5 | `LibraryIndex` positional, not string id |
| RT-SIGNAL-001 RT-safe signal primitive | **DONE** | `wp/RT-SIGNAL-001` → merged `63259a3` | A — real-time | G1 | 14 tests / 110 090 checks |
| MOD-002 drum transport adapter | **DONE** | `wp/MOD-002` → merged `86544f1` | B — intelligence | G2 | 13 tests / 129 checks; 3 extra probes |
| **RT-001 scene signal (F1)** | VERIFIED:bounded editor-absent delivery | orchestrator, `135b4b7` + RT-002 | A + orchestrator | G1 | real JUCE compilation; editor-absent Timer delivery measured30.5ms versus ~257ms no-audio fallback; other hosts open |
| EVAL-001 guitar rhythm corpus | **DONE** | `wp/EVAL-001` → merged `f5f5a11` | C — evidence | G3 | 19 fixtures, 21 MB; hashes independently verified |
| — BTrack vendored + licence seam | **DONE** | `a8da4f2` | orchestrator | G3 | 2 vendor defects found by smoke-build before delegating |
| TRACK-001 BTrack backend | **DONE** | `wp/TRACK-001` → merged `c3dad10` | B — intelligence | G3 | 10 tests / 38 940 checks; required an orchestrator vendor fix |
| EVAL-002 evaluation harness + metrics | **DONE** | `wp/EVAL-002` → merged `3e5bb7b` | B — evidence | G3 | 11 metrics; exposed 2 corpus defects |
| CI-001 continuous integration | **DONE** | `wp/CI-001` → merged `4376672` | C — evidence | G1 | 3 Gitea workflows; Windows job never run |
| EVAL-001 repair: true silence + core | **DONE** | `wp/EVAL-001` → merged `c68df60` | C — evidence | G3 | audio provably unchanged |
| EVAL-002R first real shootout | HISTORICAL: superseded by EVAL-004 | `wp/EVAL-002R` → `4bb5f97` | B — evidence | G3 | old artifacts retained; timestamp/gate claims corrected in EVAL-004 |
| TRACK-002 aubio backend | **DONE** | `wp/TRACK-002` → main; build wiring `eac59ba` | B — intelligence | G3 | 11 adapter tests; combined 8/8 ctest suites pass |
| EVAL-003 robustness fixtures | **DONE: scoped corpus** | `wp/EVAL-003` → `0a15eef` | C — evidence, Flash | G3 | 24 derived clips, 11.12 MiB; C++/Python checks integrated; comparison follows in EVAL-005 |
| EVAL-004 timing/gate audit + comparison | **DONE** | `wp/EVAL-004-timing` → `64b39ee` | B — evidence, Flash | G3 | combined 9/9 suites; both acquisition gates fail; aubio BPM passes, BTrack BPM fails |
| TRACK-003 BeatNet feasibility | INTEGRATED:PARTIAL; benchmark unavailable | `wp/TRACK-003-beatnet`, `1ef3d5c` → main + scorer corrections | C — research, Flash | G3 | pinned source/weight terms and measured dependency blockers; 51 scorer tests; no inference; redistribution review unresolved |
| CI-002 tracker CI repair | **DONE: definitions + local execution** | `wp/CI-002-trackers`, `480f15f` → main + research-suite guards | C — evidence, Flash | G1/G3 | current-main 11/12/12/13 suites; ON/OFF symbols and fail-closed nm; remote/Windows execution still unverified |
| EVAL-005 paired robustness curves | **DONE: scoped paired diagnostics** | `wp/EVAL-005-robustness`, `816a955` → main | B — evidence, Flash | G3 | 29 tests; 2160 rows exactly reproduced; all 24 derived WAV hashes/sizes checked; short-window/missing-data caveats retained |
| EVAL-006 sustained-corpus repair + tapping audit | **DONE: versioned synthetic repair** | `wp/EVAL-006-sustain`, `e0e3bde` → main + suite registration | C — evidence, Flash | G3 | 37 tests; 1.5 s measured persistence; historical hashes preserved; both real tracker pairs verified; stale name-based defect label reported |
| TRACK-004 acquisition/BPM diagnosis | **DONE: scoped causal diagnosis** | `wp/TRACK-004-acquisition`, `98da13f` → main + integration corrections | B — evidence, Flash | G3 | 63 replay checks; all four real-backend trace runs exactly reproduced; numerical vs invalid-phase clauses separated; lag-selection cause unresolved |
| RT-002 real processor runtime probe | **DONE: bounded runtime evidence** | `wp/RT-002-processor-probe`, `6e89b1c` → main + integration fixes | A — runtime evidence, Flash | G1 | 26 cases independently reproduced; example LSTM callback allocations measured; scene Timer/fallback differential; full G1 remains partial |
| EVAL-007 version-aware silence coverage | **DONE: scoped coverage correction** | `wp/EVAL-007-silence-coverage`, `514c154` → main + CSV citation corrections | C — evidence, Flash | G3 | exact known-defect hash; repaired/tapping assessed; structural noise unassessed; six reruns preserve non-coverage scores; all 62 input entries verified |
| TRACK-005 causal BPM-report variant | **DONE: diagnostic-only variant evidence** | `wp/TRACK-005-tempo-variant`, `7b3a4d7` → main + framing corrections | B — evidence, Flash | G3 | causal readiness-gated step evidence; six runs reproduced; gains and sparse regression retained; no production/default change |
| RT-003 LSTM callback allocation repair | **DONE: measured LSTM repair** | `wp/RT-003-nam-lstm`, `401a6c1` → main + product/verifier fixes | A — runtime repair, Flash | G1 | numerical and real processor evidence reproduced;5/5 standalone checks; fresh product builds/drum tests/probe pass; full G1 partial |
| ANALYSIS-001 injected worker lifecycle subset | **DONE: lifecycle foundation (D6); full task PARTIAL** | `wp/ANALYSIS-001-worker`, corrected `5162e4d` → main | C — core implementation, Flash + local review completion | G4 |29 tests/1210 checks, limited synthetic TSAN and throughput verified; production tracker/processor-clock wiring still blocked |
| TRACK-006 fixed variant paired robustness | **DONE: scoped paired diagnostics** | `wp/TRACK-006-variant-robustness`, `ce3c96e0` → main | B — evidence, Sol | G3 |72 scores and24 exact beat pairs independently reproduced;3240 paired rows; variant loses6 acquisitions/gains1, gap/noise regressions retained; no selection |
| RT-001 F2 MidiBuffer + bounded meter CAS | VERIFIED:engine scope | main, `677ce9f` | orchestrator | G1 | old 256 B buffer grows to 2115 B; new reservation has 0 observed heap calls across 16 cases; whole processor still unverified |
| CI execution evidence | **REMOTE GREEN: scoped builds/tests** | `0820bb5`, run37715897283 | C | G1 | all6 GitHub jobs pass; this is build/test coverage, not whole callback/device validation |
| TEST-001 foundation tests | **DONE: bounded foundation coverage (D8)** | `wp/TEST-001-foundation`, `0cfc252b` → main | C — verification, Sol | G1 |50 actual JUCE cases; root rebuild and6/6 suites pass; processor migration/concurrent edits remain uncovered |
| MOD-003 Jam UI shell | **DONE: isolated simulation shell (D7)** | `wp/MOD-003-jam-ui`, `34ab2cb3` → main | C — UI, Sol | G2 | fresh actual JUCE preview build and Xvfb verification pass; seven screenshots; live editor wiring pending |
| CI-003 GitHub verification | **DONE: RT-005/TRACK-008/INT-DRUM-001 remote green** | published `b7e3be1` | orchestrator | G1 | run37729980845 core23/24/24/25, NAM9/9, Windows Standalone/VST3 +7/7 drums; live wave integration pending |
| TRACK-007 longer-window acquisition characterization | **DONE: corrected scoped evidence; G3 open** | `wp/TRACK-007-long-windows`, `65cd77d` → main | B — evidence, Haiku/Space Bunny/DeepSeek Go | G3 |48 research tests; independent16-fixture render/48 scores,7503 non-CPU fields exact; raw and corrected evidence preserved; core22/20 suites pass |
| RT-004 NAM architecture callback coverage | **DONE: bounded historical evidence; A2 repair follows in RT-005** | `wp/RT-004-nam-architectures`, `337f4f3` → main | A — runtime evidence, Haiku + Space Bunny/Go | G1 |81 tests; independent260-case replay (80 NAM),6610 stable fields equal; original A2 PReLU allocation evidence retained |
| DIAG-001 portable diagnostics foundation | **FOUNDATION DONE (D10); full task PARTIAL** | `wp/DIAG-001-core`, `b5852a0` → main | C — core, Haiku + Space Bunny/Go | G4 | independent21/19 core suites,34 diagnostics cases,strict/TSan/ASan checks pass within receipt scope; live wiring/overhead pending |
| RT-005 NAM activation allocation repair | **DONE: measured activation repair; G1 partial** | `wp/RT-005-nam-activations`, `e83d619` → main | A — repair, DeepSeek Go | G1 | independent source review, fresh archive/probe +390 cases/16770 stable fields;9/9 required suites;5 repaired model runs measured-clean |
| TRACK-008 frozen tempo-stability candidate | **DONE: scoped diagnostic; G3 open** | `wp/TRACK-008-tempo-stability`, `f621ee2` → main | B — diagnostic candidate, DeepSeek Go | G3 | independent review/rebuild/replay accepted;160 scores/24744 fields; frozen behavior + historical raw retained; core24/22 suites pass |
| INT-DRUM-001 actual clock-to-drum bridge | **DONE: injected bridge; G2/G4 partial** | `wp/INT-DRUM-001-clock-bridge`, `467603e` → main | C — injected bridge, DeepSeek Go | G2/G4 | merged76 probe/75 normal JUCE cases,20 portable tests/205 checks; timing/lifecycle review accepted; production wiring pending |

---

## Historical scoping finding — grooves have no string ID

The provisional slug proposal below was rejected after the collision audit;
the later positional-identity decision is authoritative.

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
### Superseded early-wave blocker rows

Historical planning states only; the current task table above is authoritative.

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

Historical records below retain the evidence available at that wave. Current
status is the gate/task tables above. In particular the initial BTrack
libsamplerate removal was wrong and retracted in `VENDORED-PATCHES.md`:
`resampleOnsetDetectionFunction()` is called causally. Source was restored.
Feeding 48 kHz at the original hop biased BPM **low**, not fast. Old statements
that JUCE cannot build locally are being reassessed using a user-prefix build.

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
