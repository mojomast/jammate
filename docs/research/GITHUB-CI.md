# CI-003 — GitHub verification

JamMate is published at [`mojomast/jammate`](https://github.com/mojomast/jammate).
`.github/workflows/verify.yml` adds the native GitHub runner lane alongside the
historical Gitea definitions. Workflow generation and local validation do not
constitute remote execution evidence.

## Required jobs

- **Core matrix:** dependency-free, BTrack-only, aubio-only and both enabled.
  No submodules are materialized. All registered suites execute; 23 base suites
  are required, plus each enabled tracker suite. `nm` must succeed and produce
  actual symbol lines; the core archive/tests cannot contain tracker symbols,
  each enabled tracker binary must contain its own real symbols, and the fully
  disabled Ninja graph cannot compile tracker sources.
- **NAM repair:** fetch only the pinned NAM dependency tree, compile original
  and overlay implementations, and require all nine differential/verifier suites
  for the LSTM and activation repairs.
  This rechecks numerical evidence and committed callback artifacts; it does
  not rerun the actual processor callback probe.
- **Windows:** fetch only JUCE and NAM product dependencies, configure MSVC,
  build Standalone, VST3 and the actual drum test executable, require all seven
  drum suites (including the injected-clock bridge), and execute them. This uses WASAPI/DirectSound, with the
  embedded browser disabled. ASIO/device/latency execution is separate.

Actions are pinned to immutable commits; checkout does not persist credentials.
Failure logs are uploaded. No job uses `continue-on-error`; independent core
configurations run even if another matrix entry fails.

## Local verification (2026-10-08)

```sh
python3 -B tools/ci/test_registration.py
python3 tools/ci/verify_registration.py --lane core --build <core-off>
python3 tools/ci/verify_registration.py --lane core --build <core-both> --btrack ON --aubio ON
python3 tools/ci/verify_registration.py --lane nam --build <nam-repair>
python3 tools/ci/verify_registration.py --lane drums --build <linux-product>
actionlint -shellcheck= -pyflakes= .github/workflows/verify.yml
```

Seven guard acceptance tests pass, including every required suite's removal,
enabled/disabled tracker contradictions, duplicate registration, failed/empty
`nm`, and directory-name false positives. Initial CI-003 OFF/ON artifacts
passed registration/symbol checks at 18/20 suites; the NAM and Linux drum build
artifacts pass at five suites each. All 33 Bash bodies across both forge
definitions parse. SHA-verified actionlint **1.7.12** accepts the GitHub workflow;
its optional external shellcheck/pyflakes integration was disabled.

TRACK-008 / RT-005 integration passes current core **22/22 OFF, 24/24 both**
and **9/9 NAM suites** with the required-suite guards. The independent local
processor replay covers15 processes/390 cases, preserving16770 stable CSV fields.
The activation validator is exercised on the preserved evidence in CI; local
probe binaries are not invoked by the remote job. Receipts:
[`tempo-stability-integration.json`](tempo-stability-integration.json) and
[`nam-activation-repair-integration.json`](nam-activation-repair-integration.json).

INT-DRUM-001 final integration passes core **23/23 OFF,25/25 both**, and the
final-source Linux Standalone/VST3 build with **7/7 registered drum suites**.
The separate instrumented/normal actual-JUCE driver passes76/75 cases, plus
20 portable tests/205 checks. Both forge guards require the bridge suites.
Final published-head remote CI is pending. Receipt:
[`drum-clock-bridge-integration.json`](drum-clock-bridge-integration.json).

Fresh BTrack-only and aubio-only Release builds also each pass **19/19 suites**
and their real linkage/registration guards. Logs are retained locally under
`/home/mojo/projects/build-CI-003-integration/`: `btrack-{0,1,2,3}.log` and
`aubio-{0,1,2,3}.log` (configure/build/guard/ctest respectively).

The first remote run at `8f18e03` completed:
[37715362637](https://github.com/mojomast/jammate/actions/runs/37715362637).
All four core jobs and the NAM repair job passed. Windows configured JUCE's
helper, then failed the generated LSTM SHA check before product compilation.
The reported hash `2ec0d534…` is exactly the expected generated file converted
from LF to CRLF, independently verified locally. The source/patch input hashes
passed; the generator must explicitly retain LF output on Windows. The job log
is retained in `/home/mojo/projects/build-CI-003-integration/windows-first-run.log`.
No Windows product pass is claimed. A later green Windows build would establish
MSVC/format compatibility, not ASIO or physical-audio behavior.

The generator now writes through CMake `file(CONFIGURE ... NEWLINE_STYLE UNIX)`
for both patched files. Expected input/output hashes remain unchanged. Fresh
local NAM rebuild passes **5/5 suites**, including numerical byte equality,
idempotence and stale-hash rejection. Generated `lstm.cpp`/`.h` retain their
original `82f25497…` / `4975a306…` pins. Logs: `lf-overlay-{0,1}.log` in the local
CI integration scratch directory.

## First complete remote pass

[Run37715897283](https://github.com/mojomast/jammate/actions/runs/37715897283) at
`0820bb59226a27994bfc9ba4e2003563fd136375` passed all six jobs:

| Lane | Result |
|---|---|
| Core OFF / BTrack-only / aubio-only / both |18/18,19/19,19/19,20/20 suites |
| NAM repair |5/5 suites |
| Windows2022 / MSVC | Standalone and VST3 build;6/6 drum suites |

The generated-byte pins pass on Windows with the explicit-LF correction.
The tested Windows build uses WASAPI/DirectSound and disables the embedded
browser. It does not execute physical audio, ASIO, latency or processor-probe
instrumentation. Remote NAM validation rechecks committed callback artifacts.
Evidence receipt and downloaded-log hashes:
[`github-ci/run-37715897283.json`](github-ci/run-37715897283.json).
This receipt authenticates the stated tested SHA only.

## Published merged-head pass

[Run37716882463](https://github.com/mojomast/jammate/actions/runs/37716882463)
at `bcf540ae9509315d143595f5887060444aa9bae6` passed all six jobs, including
the merged UI, tracker evidence and foundation-test head:

- Core OFF / BTrack-only / aubio-only / both: **18/18, 19/19, 19/19, 20/20** suites.
- NAM repair: **5/5** suites.
- Windows2022 / MSVC: **Standalone and VST3 built; 6/6 drum suites passed**.

Receipt and downloaded-log hashes:
[`github-ci/run-37716882463.json`](github-ci/run-37716882463.json).
The same device, ASIO and callback-timing limits apply. The simulated Jam UI
preview was verified locally; this run does not build that preview target.

## RT-005 / TRACK-008 published-head pass

[Run37728344321](https://github.com/mojomast/jammate/actions/runs/37728344321)
at `f7c0a113a12cd6c8cd8c95c0ee98836bdc0a29a5` passes all six jobs:
core **22/22 OFF,23/23 BTrack-only,23/23 aubio-only,24/24 both**, NAM **9/9**,
Windows2022/MSVC **Standalone + VST3 and6/6 drums**. Both recorded-evidence
portability corrections are confirmed remotely. This SHA precedes drum-bridge
integration. Receipt:
[`github-ci/run-37728344321.json`](github-ci/run-37728344321.json).

## RT-005 recorded-evidence portability correction

[Run37727369967](https://github.com/mojomast/jammate/actions/runs/37727369967)
at `e26eeb64991676836b3e36b4586287bab76e2671` passed all core lanes
**22/23/23/24** and Windows **6/6 drums**. NAM's seven numerical/patch suites
passed; the two newly registered evidence suites failed on required local
probe/archive paths. They now use explicit `--evidence-only` validation of
recorded identities and all counters. Default local artifact validation remains
strict, and present mismatched files fail in either mode.34 validator tests and
both registered evidence suites pass with the correction; remote rerun pending.
Receipt: [`github-ci/run-37727369967.json`](github-ci/run-37727369967.json).

## TRACK-008 initial CI failure and local correction

[Run37726314737](https://github.com/mojomast/jammate/actions/runs/37726314737)
at `1b05672b7540f7ec9f217276641a6c6a129cafb2` passed NAM and Windows but failed
all four core lanes: `jam.TempoStableResearch` attempted to open a frozen plugin
in an orchestrator-local scratch path. The corrected portable suite authenticates
source/freeze/provenance records; the actual binary/archive check is an explicit
local opt-in.27 tests pass with local validation;26 pass with local paths
simulated unavailable and the one local check explicitly skipped. Candidate
behavior and historical evidence remain unchanged. Remote rerun is pending.
Failure and correction-check log hashes:
[`github-ci/run-37726314737.json`](github-ci/run-37726314737.json).

## Final RT-004 / DIAG-001 / TRACK-007 integrated-head pass

[Run37721981804](https://github.com/mojomast/jammate/actions/runs/37721981804)
at `677727cb770b03abb63d39fe05862e525ebce226` passed all six jobs:
core **20/20 OFF, 21/21 BTrack-only, 21/21 aubio-only, 22/22 both**, NAM **5/5**,
and Windows2022/MSVC **Standalone + VST3 with 6/6 drum suites**. This includes
the newly required 48-test tempo characterization research suite in shallow
checkouts. Receipt/log hashes:
[`github-ci/run-37721981804.json`](github-ci/run-37721981804.json).
Physical-device, ASIO and callback-deadline gates remain open.

## RT-004 / DIAG-001 integrated-head pass

[Run37720364067](https://github.com/mojomast/jammate/actions/runs/37720364067)
at `834823320ce1fb700e94e9e50ed18e043d297578` passed all six jobs:
core **19/19 OFF, 20/20 BTrack-only, 20/20 aubio-only, 21/21 both**, NAM **5/5**,
and Windows2022/MSVC **Standalone + VST3 with 6/6 drum suites**.
The portable diagnostics suite is required by both forge guards.

Receipt and downloaded-log hashes:
[`github-ci/run-37720364067.json`](github-ci/run-37720364067.json).
The broader actual-processor replay and scoped diagnostics sanitizer checks
were executed locally, not by these remote jobs. Device/ASIO/deadline gates
remain open.
