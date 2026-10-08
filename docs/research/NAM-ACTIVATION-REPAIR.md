# NAM-ACTIVATION-REPAIR — removing the per-sample PReLU/blending allocations

**Task:** RT-005 (actual NAM PReLU/blending activation allocation repair)
**Worktree:** `/home/mojo/projects/worktrees/RT-005-nam-activations`
**Branch:** `wp/RT-005-nam-activations` · **base:** `677727c`
**NAM pin:** `third_party/NeuralAmpModelerCore` @ `1f42f88535884450104b8711d7595019afa0495b` (claimed v0.5.4)
**Related:** `docs/research/NAM-ARCHITECTURE-PROBE.md` (RT-004), `docs/research/NAM-LSTM-RT-REPAIR.md` (RT-003), `docs/research/PROCESSOR-RUNTIME-PROBE.md` (RT-002), SPEC.md §7.1/§18.1.
**Tools:** extended `tools/nam-rt-repair/**`; new read-only `tools/nam-activation-repair/**`.
**Artifacts:** `docs/research/nam-activation-repair/**`.

This document reports an **executed** repair of the per-sample activation
allocation that RT-004 measured on the real
`GuitarCompanionProcessor::processBlock` for `wavenet_a2_max.nam`, an A2-style
WaveNet with a PReLU/blended condition stack. It patches the pinned upstream
NAM activation code, rebuilds the production `nam_core`, and re-measures the same
callback for three archives (pinned upstream, RT-003 LSTM-only, RT-005 new).

## 1. What RT-004 measured

RT-004 measured `wavenet_a2_max.nam` on the RT-003 repaired archive and on the
pinned upstream control. Both showed **491520 warm C++ heap allocations** = 2.0
per host sample at 48 kHz (1.0 at the resampled 96 kHz), matched frees, zero
locks. Resolved call sites:

- `nam::activations::ActivationPReLU::apply(Eigen::Matrix<float,-1,-1,...>&)`
- `void nam::gating_activations::BlendingActivation::apply<...>(...)`

The model's `condition_dsp` has one `blended` layer with a per-channel PReLU
primary activation and a `LeakyHardtanh` blend activation, plus a `gated` layer
with a PReLU primary activation. RT-004 recorded the finding but did not repair
it; G1 stayed partial.

## 2. Root cause

At the pin, `NAM/activations.h`:

```cpp
void apply(Eigen::MatrixXf& matrix) override
{
  ...
  // Prepare the slopes for the current matrix size
  std::vector<float> slopes_for_channels = negative_slopes;   // <-- heap copy
  ...
  matrix(channel, time_step) = leaky_relu(matrix(channel, time_step), slopes_for_channels[channel]);
}
```

`ActivationPReLU::apply(Eigen::MatrixXf&)` copied the preallocated
`negative_slopes` vector into a fresh `std::vector<float>` **on every call**,
i.e. once per model sample per PReLU layer. The vector copy itself was
read-only: the loop only ever indexes it.

A direct harness (pinned headers, global `operator new`/`delete` counters) shows
the exact accounting:

- `ActivationPReLU::apply(Eigen::MatrixXf&)` → **1** `new` per call;
- `ActivationPReLU::apply(float*, long)` → 0;
- `GatingActivation::apply` / `BlendingActivation::apply` with **non-PReLU**
  activations → 0;
- `BlendingActivation::apply` with a PReLU primary → 1 `new` per sample, which
  is exactly the PReLU call inside it.

The two `blended`/`gated` PReLU layers per sample give the measured 2.0/sample.
The RT-004 `BlendingActivation::apply` call site is the **inlined
`ActivationPReLU::apply`** inside the blending template instantiation; the
blending channel-combination expression in this compiler/Eigen build did not
itself allocate (verified directly for the exact `Block`/`Block-of-Block`
destination types).

## 3. The repair

`patches/nam/activation-rt-alloc.patch` makes **three logical edits across two
files** (one in `activations.h`, two in `gating_activations.h`):

1. **`ActivationPReLU::apply(Eigen::MatrixXf&)`** — read the preallocated
   `negative_slopes` member directly instead of copying it into a local vector.
   The copy was read-only, so the result is bit-for-bit identical.
2. **`GatingActivation::apply`** and 3. **`BlendingActivation::apply`** (the
   non-`NAM_USE_INLINE_GEMM` branch) — replace the Eigen array combine
   expressions with explicit preallocated element-wise loops, identical to the
   existing `NAM_USE_INLINE_GEMM` branch. This makes the non-inline path
   explicitly preallocated and removes any dependence on Eigen evaluation
   temporaries, while preserving the per-element arithmetic.

The floating-point operation order is unchanged. The patch is zero-context
(`-U0`) so it contains no blank `" "` context lines and passes
`git diff --check`.

### Overlay integration

`cmake/nam-rt/NamRtPatch.cmake` is extended (not replaced): it still
SHA256-verifies the pinned LSTM inputs and the RT-003 `lstm-rt-alloc.patch`
first, then verifies the pinned `activations.h`/`gating_activations.h` and the
RT-005 activation patch, applies all edits to the same generated copy, and
SHA256-verifies all four generated files. Both tracked patches and the whole
copied NAM tree remain `CMAKE_CONFIGURE_DEPENDS`. The RT-003 LSTM overlay bytes
and their pins are unchanged.

Consequently the root product `CMakeLists.txt` and the standalone
`tools/nam-rt-repair` build compile the **same repaired bytes**. In this
environment the root product `nam_core` and the standalone `nam_core` produced a
**byte-identical static archive**
(`ad0ffbb37434a860a5084831ef7514f7e0146318cf98a743c14e2d24b68b5579`), and the
generated activation files match:

| generated file | sha256 |
|---|---|
| `NAM/activations.h` | `7a43dc54bc47cace5fff7cdb5d3d132e00e2c8d9c49fecc4b113c8d550e4e283` |
| `NAM/gating_activations.h` | `a0640cb69618f9cb43d40499aae697eb2b3bf068b44b93d3905cda6a6f6fcb42` |
| `NAM/lstm.cpp` (RT-003, unchanged) | `82f25497f92022edee44a62bb036bdb37490de669a766fbf0ea51ebb955151f0` |
| `NAM/lstm.h` (RT-003, unchanged) | `4975a3060d7a8108c1ba27e0b15cc57ad2f0f727448459c238093368f2641bc9` |

## 4. Numerical equivalence

`tools/nam-rt-repair` gains a second differential test (`src/NamActivationDiffTest.cpp`)
compiled twice — pinned upstream vs generated patched overlay — and run in two
independent processes. The shared source exercises four sections over a
deterministic workload and counts global C++ `operator new`/`delete` calls
inside each measured region:

| section | what it covers | floats | upstream `new` | patched `new` |
|---|---|---|---|---|
| `prelu` | `ActivationPReLU::apply(MatrixXf&)` and `apply(float*, long)`: 12 slope vectors (single/channelwise, 0, negative, `1e-8`, `1e8`), 6 time lengths, edge + pseudo-random values | 5 580 | 72 | **0** |
| `gating` | `GatingActivation::apply`: 8 activation pairings (incl. PReLU), channels 1/2/3/4/8, samples 1/2/3/5/17/64 | 13 248 | 1 840 | **0** |
| `blending` | `BlendingActivation::apply`: paired PReLU + `LeakyHardtanh`/`ReLU`/`Sigmoid` and other combinations | 13 248 | 1 840 | **0** |
| `model` | the real pinned `wavenet_a2_max.nam` loaded through `get_dsp`, cold + 64 warm blocks | 8 320 | 16 640 | **0** |

Result: the two raw binaries are **byte-identical** in every section
(`max_abs_diff = 0`, `max_rel_diff = 0`, `raw_bytes_identical = true`), while the
upstream process allocates in every PReLU-bearing section and the patched
process allocates zero. `compare_activation_diff.py` accepts a within-budget
pair (`|a-b| <= 1e-5 + 1e-5*max(|a|,|b|)`) but is byte-identity-strict here.
`test_compare_activation_diff.py` drives the comparator with **17** adversarial
fixtures (missing/duplicate sections, offset gap, tampered FNV/sum, non-finite
values, `all_finite=false`, metadata/count mismatch, patched allocation/free,
missing positive control, wrong flag/kind, out-of-budget value).

The LSTM differential test, comparator tests and probe-evidence check from
RT-003 are retained unchanged and still pass.

## 5. Actual-processor three-archive comparison

`tools/nam-activation-repair` runs the existing, **unchanged** processor probe
once per (archive, model) under `env -i` with a bounded timeout, reusing the
read-only shared/JUCE/assets archives. `lstm_only` and `new` share the same
read-only shared archive (`405ee591…`) and differ only in `libnam_core.a`.
`original` is the RT-004 historical control (`46edfe5d…`).

| model | original warm alloc | lstm_only warm alloc | new warm alloc |
|---|---|---|---|
| `lstm_control` | 491520 | 0 | **0** |
| `a1_wavenet_standard` | 0 | 0 | **0** |
| `a2_wavenet_max` | 491520 | 491520 | **0** |
| `slimmable_wavenet` | 0 | 0 | **0** |
| `a2_slimmable_container` | 0 | 0 | **0** |

Every `new` NAM case (8 per model: 48/96 kHz × 128/512 × `nam`/`nam+drums`) has
**zero warm and cold allocations, frees and lock/trylock/cond/unlock
operations**, and every dry/built-in case is zero in all three variants. The
`lstm_only` variant retains the a2 finding (491520 = 2.0/sample at 48 kHz),
confirming the RT-003 LSTM repair did not cover the activation path. Warm free
totals equal the warm allocation totals in the positive controls; the repaired
cases have zero of both. Mean warm `out_rms` is **identical** across all three
archives for every model and case, so the repair does not change the processor
output level.

`check_activation_probe.py` validates every run fail-closed (exit status, exact
case matrix, protocol budget, model/binary/archive identity and on-disk hashes,
self-check, dry-all-zero, finite values, capture overflow) and asserts the exact
expected allocation matrix above. `test_check_activation_probe.py` adds 10
evidence/adversarial tests (zero-expected positive, missing positive control,
missing run, non-zero exit, dry non-zero, `out_rms` mismatch, capture overflow
in a clean run, wrong identity). The committed artifacts are under
`docs/research/nam-activation-repair/` with a `manifest.sha256`.

## 6. Preserved RT-003 evidence

- `patches/nam/lstm-rt-alloc.patch` and its SHA pin are unchanged.
- The module still verifies the pinned LSTM inputs, the LSTM patch, and the
  generated LSTM bytes with the original hashes.
- All RT-003 standalone tests are retained: `nam_rt_diff`, `nam_rt_compare_unit`,
  `nam_rt_probe_repair`, `nam_rt_probe_verifier_unit`, and the patch checks now
  cover both patches. The RT-003 real-processor probe artifacts under
  `docs/research/nam-rt-repair/**` are untouched.
- `third_party/NeuralAmpModelerCore` bytes were not modified.

## 7. Provenance and licensing

- Upstream NAM Core is MIT (`Copyright (c) 2023 Steven Atkinson`); the generated
  copy is a derivative of the same-pinned MIT source. This repair adds no new
  third-party dependency and changes no licence.
- Pinned upstream: `activations.h` `83531762249acd73e97ac7247a5ebb482b0ba127bf261a8a74ee19576b983576`,
  `gating_activations.h` `e004bda49503acc21ab42a66bab847dec2d6d55461807b0de707cd1ad1bdb060`.
- Activation patch `dfa247d59387e7f6c042bbf91c265debd90c66d503a5570d8f8b3a46711681e5`;
  module `309568accdafaa7f10c9ffb23ef469f70fbd196f5ab72bd20117cee0928e7d40`.
- New production `libnam_core.a` `ad0ffbb37434a860a5084831ef7514f7e0146318cf98a743c14e2d24b68b5579`;
  new probe binary `faa79a4feee138836b9fce8d77cfbbce6557603ccce7090a195cfc0cb5c7e379`.
- RT-003 archives: LSTM-only `libnam_core.a` `dbd11fb2c63b69e00b78a559cff0cb1fbe14ece214dba73326b4232014498507`,
  probe `f950d6d95ff2f02d3deaab25e807b15c8fe0ac208a59f07f0e961729e3761715`.
- Pinned upstream archive `af023ffcafd198e7a09107b78f2c540d4d3dbdce3c717e9ea3f215097e412d1b`,
  probe `de089230f5c718641e5620376cc8b7a8e8932b527464042a9211f8f32aa302e7`.

## 8. Reproduce

```sh
cd /home/mojo/projects/worktrees/RT-005-nam-activations
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp

# 1. standalone differential + comparator units + patch checks + RT-003 evidence
cmake -S tools/nam-rt-repair -B /home/mojo/projects/build-RT-005-worker/nam -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNAM_CORE_DIR=/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore
cmake --build /home/mojo/projects/build-RT-005-worker/nam -j2
ctest --test-dir /home/mojo/projects/build-RT-005-worker/nam --output-on-failure
# -> 7/7 tests pass

# 2. actual-processor three-archive comparison (~14 s, 15 processes)
tools/nam-activation-repair/run_activation_probe.sh
python3 tools/nam-activation-repair/check_activation_probe.py \
  --runs docs/research/nam-activation-repair/runs \
  --predeclared tools/nam-activation-repair/predeclared.json \
  --models-dir /home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models \
  --out-json docs/research/nam-activation-repair/activation-probe-verification.json \
  --summary-md docs/research/nam-activation-repair/summary.md
python3 -m unittest tools/nam-activation-repair/test_check_activation_probe.py
```

Building the `new` probe and the repaired production `nam_core` is documented in
`tools/nam-activation-repair/README.md`.

## 9. Honest limitations

- **Not a whole-program proof.** This removes the activation allocation RT-004
  measured for `wavenet_a2_max.nam` and keeps the previously clean architectures
  clean. It does not claim every NAM architecture or model is allocation-free.
- Only the example models in the RT-004 predeclared set were measured; four
  example models outside the 5-type cap remain unmeasured. `SlimmableContainer`
  runs its default submodel only.
- The `original` control uses the read-only baseline shared archive
  (`46edfe5d…`) while `lstm_only`/`new` use the RT-003 integration product shared
  archive (`405ee591…`); the `lstm_only` vs `new` contrast is a strict
  single-archive substitution, the `original` row is historical RT-004 context.
- Capture overflow in the positive runs (original/lstm_only a2 and lstm) is
  expected and recorded; the repaired runs have no overflow. A capture overflow
  can never be reported as clean.
- The differential is byte-identical for this compiler/Eigen/flag set; the
  predeclared acceptance criterion is the `1e-5` abs/relative budget, not
  cross-compiler bit-exactness. GNU `patch`/POSIX shell and Linux only; Windows
  and hosted-VST3 are not measured.
- Non-device, single-threaded probe: no latency/dropout/device or Windows/ASIO
  evidence. **G1 remains partial.**
