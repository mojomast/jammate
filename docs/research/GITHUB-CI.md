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

Remote run results will be appended after execution. A green Windows build
would establish MSVC/format compatibility, not ASIO or physical-audio behavior.
