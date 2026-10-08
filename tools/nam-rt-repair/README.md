# tools/nam-rt-repair — RT-003 NAM LSTM allocation repair

Standalone build + tests for the RT-003 repair of the two per-model-sample
LSTM allocations that RT-002 measured on the real processor callback. It builds
two NAM static libraries with identical production flags — pinned upstream
(`libnam_core_original.a`) and the configure-time generated patch
(`libnam_core.a`) — and runs the tests below. It does not build JUCE.

See `docs/research/NAM-LSTM-RT-REPAIR.md` for the full report and the exact
real-processor probe commands.

## Layout

```
tools/nam-rt-repair/
  CMakeLists.txt                  two NAM libs + tests
  src/NamLstmDiffTest.cpp         deterministic multi-config workload
  run_diff_test.sh                run both exes at blocks 64/128/512 + compare
  compare_diff.py                 fail-closed per-sample abs/rel budget check
  test_compare_diff.py            comparator acceptance unit tests (unsafe fixtures)
  src/NamActivationDiffTest.cpp   RT-005 PReLU/gating/blending + real-model workload
  run_activation_diff_test.sh     run both activation exes + compare
  compare_activation_diff.py      equivalence + zero-allocation comparator
  test_compare_activation_diff.py activation comparator unit tests
  check_patch.sh                  patch↔overlay (both patches), same-build idempotence,
                                  dependency refresh, stale removal, input/patch SHA
                                  fail-closed for LSTM and activation inputs
  check_probe_repair.py           assert committed patched-vs-control probe evidence
```

Linux / GNU `patch` and a POSIX shell are required; Python 3 is required for the
differential comparator and the probe-evidence check. Windows was not verified.

## Build and test

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
cmake -S tools/nam-rt-repair -B /home/mojo/projects/build-RT-003/nam \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DNAM_CORE_DIR=/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore
cmake --build /home/mojo/projects/build-RT-003/nam -j
ctest --test-dir /home/mojo/projects/build-RT-003/nam --output-on-failure
```

Options:

- `-DNAM_CORE_DIR=<path>` — pinned NAM checkout. Defaults to this worktree's
  submodule; if that is not populated configure fails and asks for the path
  explicitly (no host-specific guess).

## Tests

- `nam_rt_diff` — pinned-upstream vs patched, 6 configurations (multi-layer,
  multi-channel, zero-layer), ramp/sine/noise/impulses/silence, blocks
  64/128/512; metadata/structure/finiteness/raw-summary checks and the
  predeclared `abs/rel <= 1e-5` budget; `bit_exact` is raw-byte identity.
- `nam_rt_compare_unit` — 19 scripted comparator fixtures (NaN/±Inf, malformed
  structure, tampered summaries, within vs out of budget, signed zero).
- `nam_rt_patch_checks` — `patch -p1` (both tracked patches) reproduces the
  generated overlay; same-build reconfigure idempotent; other-header refresh and
  stale-file removal; mutated pinned `lstm.cpp` and `activations.h`, and mutated
  tracked LSTM and activation patches all fail configure closed.
- `nam_rt_probe_repair` — asserts the committed real-processor probe artifacts:
  patched NAM callback is 0 alloc/free/lock, the pinned-upstream control
  reproduces RT-002's 2.0 allocations/model sample, drum render and scene
  controls unchanged.
- `nam_rt_activation_diff` — RT-005 activation differential: PReLU coefficient/
  shape/edge cases, gating and blending activation pairings, and the real
  `wavenet_a2_max.nam` model, pinned vs patched. Byte-identical output; the
  patched process performs zero C++ allocations in every measured section while
  the pinned process allocates in each PReLU-bearing section.
- `nam_rt_activation_compare_unit` — 17 adversarial fixtures for the activation
  comparator (missing/duplicate sections, gaps, tampered summaries, non-finite
  values, patched allocations, missing positive control, wrong kind/flag,
  out-of-budget, metadata and count mismatches).

The overlay itself lives in `cmake/nam-rt/NamRtPatch.cmake` and is also applied
to the real `nam_core` target by the root `CMakeLists.txt`. RT-003's LSTM
overlay hashes are unchanged; RT-005 adds the activation patch on top.

The RT-005 actual-processor three-archive comparison lives in a separate,
read-only tool: `tools/nam-activation-repair/` (see
`docs/research/NAM-ACTIVATION-REPAIR.md`).
