# CI-003 — GitHub verification

JamMate is published at [`mojomast/jammate`](https://github.com/mojomast/jammate).
`.github/workflows/verify.yml` adds the native GitHub runner lane alongside the
historical Gitea definitions. Workflow generation and local validation do not
constitute remote execution evidence.

## Required jobs

- **Core matrix:** dependency-free, BTrack-only, aubio-only and both enabled.
  No submodules are materialized. All registered suites execute; 18 base suites
  are required, plus each enabled tracker suite. `nm` must succeed and produce
  actual symbol lines; the core archive/tests cannot contain tracker symbols,
  each enabled tracker binary must contain its own real symbols, and the fully
  disabled Ninja graph cannot compile tracker sources.
- **NAM repair:** fetch only the pinned NAM dependency tree, compile original
  and overlay implementations, and require all five differential/verifier suites.
  This rechecks numerical evidence and committed callback artifacts; it does
  not rerun the actual processor callback probe.
- **Windows:** fetch only JUCE and NAM product dependencies, configure MSVC,
  build Standalone, VST3 and the actual drum test executable, require all five
  existing drum suites, and execute them. This uses WASAPI/DirectSound, with the
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
`nm`, and directory-name false positives. Actual current-main OFF/ON artifacts
pass registration/symbol checks at 18/20 suites; the NAM and Linux drum build
artifacts pass at five suites each. All 33 Bash bodies across both forge
definitions parse. SHA-verified actionlint **1.7.12** accepts the GitHub workflow;
its optional external shellcheck/pyflakes integration was disabled.

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
