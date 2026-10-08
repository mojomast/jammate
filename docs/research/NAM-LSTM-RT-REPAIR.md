# NAM-LSTM-RT-REPAIR — removing the two per-sample LSTM allocations

**Task:** RT-003 (minimal real LSTM allocation repair)
**Worktree:** `/home/mojo/projects/worktrees/RT-003-nam-lstm`
**Branch:** `wp/RT-003-nam-lstm` · **base:** `af8b77a`
**NAM pin:** `third_party/NeuralAmpModelerCore` @ `1f42f88535884450104b8711d7595019afa0495b` (claimed v0.5.4)
**Related:** `docs/research/PROCESSOR-RUNTIME-PROBE.md` (RT-002), SPEC.md §7.1, §18.1.

This document reports an **executed** repair of the callback allocation that
RT-002 measured on the real `GuitarCompanionProcessor::processBlock`. It does
not restate the audit; it patches the pinned upstream LSTM, builds it, and
re-measures the same callback.

## 1. What RT-002 measured

`docs/research/PROCESSOR-RUNTIME-PROBE.md` §3 recorded, for the pinned example
`lstm.nam` loaded through the processor's own `loadModelAsync`:

- **2 C-heap allocations per model sample**, `calloc`/`malloc` immediately
  followed by `free`, at 48 kHz and (resampled) 96 kHz;
- resolved call sites `nam::lstm::LSTMCell::process_` and
  `nam::lstm::LSTM::_process_sample` / `LSTM::process`.

Those are the only two sites. This is the concrete form of audit finding
**F6 P1** for this architecture/model.

## 2. Root cause

`third_party/NeuralAmpModelerCore/NAM/lstm.{cpp,h}` at the pin:

1. `LSTMCell::process_` computes the gates with
   `this->_ifgo = this->_w * this->_xh + this->_b;`.
   For this mixed matrix-vector-plus-sum expression Eigen cannot write the
   product straight into `_ifgo`; it materialises the product in a heap
   temporary (`calloc(48)` for the example's `hidden_size == 3` →
   `4 * 3 * sizeof(float)`).
2. `LSTMCell::get_hidden_state()` returns `Eigen::VectorXf` **by value**:
   `return this->_xh(lastN(hidden_size));`. Every call copies the hidden block
   onto the heap (`malloc(12)` = `3 * sizeof(float)`). It is called once per
   inter-layer hop and once for the head. For the single-layer example that is
   one allocation per sample.

Both are real C-allocator calls, not C++ `new`; both occur inside the armed
`processBlock` region (RT-002 `symbols.txt`).

## 3. The repair

`patches/nam/lstm-rt-alloc.patch` makes **five logical edits across two files**
(three in `lstm.cpp`, two in `lstm.h`); together they remove the **two**
per-sample allocations:

- `_ifgo.noalias() = _w * _xh; _ifgo += _b;` — writes the product directly into
  the preallocated gate vector and adds the bias in place. The operation order
  (per-element dot product, then bias) is unchanged.
- `LSTMCell::process_` declaration and definition take
  `const Eigen::Ref<const Eigen::VectorXf>&`, so the layer-hop call and the
  `_input` call bind without a copy.
- `get_hidden_state()` returns `Eigen::Ref<const Eigen::VectorXf>`, a non-owning
  view of the existing `_xh` block. No copy.
- The head line uses `Eigen::Ref<const Eigen::VectorXf>` instead of a `VectorXf`
  temporary.

### Prebuilt SharedCode compatibility

The processor-visible NAM interface is `NAM/dsp.h`, which is **unchanged**; the
`DSP` base layout and virtual interface are therefore unchanged. `nm` on the
read-only `libGuitar Companion_SharedCode.a` shows **no** LSTM or
`get_hidden_state` symbols, so that archive does not consume the changed
internal API. That is evidence for **this specific read-only archive**, not a
general ABI guarantee: the repair does change the public signature of
`LSTMCell::process_`/`get_hidden_state`, so any *other* external NAM consumer of
`NAM/lstm.h` would need rebuilding. In this tree the only consumers of
`lstm.h` are `NAM/lstm.cpp` itself and upstream's own `tools/test/test_lstm.cpp`,
and every NAM translation unit for the patched target is compiled from the same
generated copy.

Only the `nam_core` target was built from the overlay here (see §4.4); the full
product (JUCE/Standalone/VST3) was **not** rebuilt in this task.

### Why a generated overlay, not an edit

`third_party/NeuralAmpModelerCore` is a tracked gitlink/submodule and must stay
byte-identical. `cmake/nam-rt/NamRtPatch.cmake` runs at configure time:

1. SHA256-verify the tracked patch file itself (resolved from the module's own
   directory); editing the patch without re-pinning aborts configure even when
   the pinned inputs are clean.
2. SHA256-verify the pinned `NAM/lstm.cpp`/`lstm.h`; mismatch aborts configure.
3. Remove and re-`file(COPY)` the entire pinned `NAM/` tree to
   `<build>/nam-rt/generated/NAM` (~430 KiB of sources), so deleted upstream
   files do not linger as stale copies.
4. Apply the exact literal replacements (each old string must be present).
5. SHA256-verify the generated files against the declared patched bytes.
6. Register every file in the copied tree as `CMAKE_CONFIGURE_DEPENDS`, so a
   change/addition/deletion in any other NAM source or header also refreshes the
   overlay.

So the submodule bytes are never touched, the whole NAM target for the patched
build compiles from one consistent (patched) header set, Eigen/nlohmann remain
the original pinned headers, and a stale/forked submodule, a deleted upstream
file, or an edited patch fails closed or refreshes instead of silently building
something else.

The root `CMakeLists.txt` NAM section now lists the 13 sources from
`${NAM_RT_NAM_DIR}` and prepends `${NAM_RT_INCLUDE_ROOT}`, keeping the
Dependencies include paths on the original pinned tree. `patches/nam/**` plus
the module are the only NAM-build inputs added; no other build section changed.

## 4. Executed evidence

Environment: Debian 13, Linux 6.12 x86_64, GCC 14.2, Release, no audio device,
headless JUCE. Exact `NAM_SAMPLE_FLOAT` / `NAM_ENABLE_A2_FAST` / `-O3 -DNDEBUG
-std=gnu++20 -fPIC` production flags were extracted from the build's
`build.ninja`.

### 4.1 Differential numerical test (independent processes)

`tools/nam-rt-repair` builds two libraries with identical flags — pinned
upstream (`nam_core_original`) and the generated patch (`nam_core`) — then runs
the same deterministic workload in one process per library:
`src/NamLstmDiffTest.cpp` runs 6 configurations (single/multi-layer,
1/2/3-channel, hidden 3/5/7/11/16, and a zero-layer passthrough) over one cold
block + 64 warm blocks cycling **ramp, sine, uniform noise, impulses, silence**,
at block sizes **64, 128 and 512**. The workload is
`sum(out_channels) * block * (warm+1)` floats: **41 600 / 83 200 / 332 800**
total across the ten output channels at block 64 / 128 / 512.

`compare_diff.py` is fail-closed. It requires identical `seed`/`warm_blocks`/
`block` metadata, exactly the six named configs with no missing/empty/duplicate,
contiguous non-overlapping offsets/counts covering the whole binary with
`count == out_channels*block*(warm+1)`, every sample finite in both binaries,
and the declared per-config FNV-1a and raw float summaries **recomputed from the
binary** (so a tampered summary fails). Samples are accepted within
`|a-b| <= 1e-5 + 1e-5*max(|a|,|b|)`.

Result: **bit-exact in the raw-byte sense** — the two output binaries are
byte-identical at every block size (`raw_bytes_identical = true`, which is how
`bit_exact` is defined; float equality alone cannot distinguish signed zero).
`max_abs_diff = 0.0`, `max_rel_diff = 0.0`, and the recomputed `fnv1a` matches
for every config. A within-budget but not byte-identical pair still passes with
`bit_exact=false` (exercised by `test_compare_diff.py`). `4.1` is stronger than
the budget; the budget is the acceptance criterion because bit-exactness across
arbitrary compilers is not claimed. Reproducer:
`differential/combined-diff.json`.

`test_compare_diff.py` (ctest `nam_rt_compare_unit`, 19 cases) drives the real
comparator with scripted fixtures: NaN / ±Inf, empty/missing/duplicate configs,
truncated tail, offset gap/overlap, negative and wrong counts, metadata
mismatch, tampered FNV/sum, `all_finite=false`, out-of-budget, within-budget
non-byte-identical, and signed-zero byte-identity.

### 4.2 Patch provenance / idempotence / dependency / stale-hash checks

`tools/nam-rt-repair/check_patch.sh` (ctest `nam_rt_patch_checks`) verifies:

- the tracked zero-context patch applied with `patch -p1` onto the pinned
  sources reproduces the generated overlay **byte-for-byte**;
- **same-build** reconfigure (repeated configure of one build dir, which
  re-seeds the existing generated copy) is idempotent and equals the built
  overlay;
- editing a harmless other NAM header in a scratch clone refreshes the generated
  overlay, and deleting that file removes the generated stale copy (whole-tree
  dependency tracking);
- a one-line mutation of the pinned `lstm.cpp` makes configure **fail closed**
  with an input-SHA mismatch;
- a mutated tracked patch makes configure **fail closed** with a patch-SHA
  mismatch while the pinned sources are clean (independent of the input hashes).

### 4.3 Real-processor callback probe (unchanged RT-002 tools)

Directories: `probe/` = patched `libnam_core.a`; `probe-baseline/` = pinned
upstream `libnam_core.a`. In both, the probe binary is the **unchanged**
`tools/processor-probe` linked against the **read-only** prebuilt
`libGuitar Companion_SharedCode.a` + `libGuitarCompanionAssets.a`; only
`libnam_core.a` differs. Both runs use 256 warm blocks.

| NAM case | baseline alloc/free (warm) | patched alloc/free (warm) |
|---|---|---|
| 48000 / 128 | 65536 / 65536 | **0 / 0** |
| 48000 / 512 | 262144 / 262144 | **0 / 0** |
| 96000 / 128 (resampled) | 32768 / 32768 | **0 / 0** |
| 96000 / 512 (resampled) | 131072 / 131072 | **0 / 0** |

Cold is `0/0` in every patched case; `nam+drums` is identical. The baseline
reproduces RT-002's `2.0` allocations per model sample at 48 kHz. C++
`new`/`delete` was zero in both runs; all lock/trylock/cond/unlock counts are
zero in the patched run. `probe-repair-verification.json` (also ctest
`nam_rt_probe_repair`) asserts all of this.

No regressions in the same run: the 18 dry/built-in cases remain
zero-allocation/lock; built-in drum rendering is unchanged (256/256 active
blocks, identical voice events, `out_rms` equal to the baseline to 1e-6); the
editor-absent scene/timer differential and both no-audio controls pass exactly
as in RT-002 (`scene_restore ≈ 34 ms`, short window not restored, long window
fallback ≈ 259 ms).

### 4.4 Root CMake integration (partial: `nam_core` target only)

Configuring the actual product from this worktree (read-only submodule
references for the local check) generated the overlay at
`<build>/nam-rt/generated/NAM` and built the real `nam_core` target; its
generated `lstm.cpp`/`lstm.h` SHA256 equal the standalone build's overlay
(`82f25497…` / `4975a306…`). The full JUCE/Standalone/VST3 product was **not**
rebuilt from the overlay here; a full-product build remains a separate future
verification step.

## 5. Provenance, licensing and integrity

- Upstream notice: NAM Core is MIT (`Copyright (c) 2023 Steven Atkinson`); the
  generated copy is a derivative of the same-pinned MIT source. The MIT notice
  in `THIRD_PARTY.md` still travels with the copy; this repair adds no new
  third-party dependency and changes no licence. See the updated NAM row in
  `docs/research/DEPENDENCIES.md`.
- `third_party/NeuralAmpModelerCore` bytes were not modified. Originals
  `lstm.cpp` `c544c217…`, `lstm.h` `e66c90b9…`; generated `82f25497…`,
  `4975a306…`; patch `df0c2ac8…`; module `a8cad69b…`.

## 6. Reproduce

```sh
cd /home/mojo/projects/worktrees/RT-003-nam-lstm
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-RT-003/tmp

# 1. differential + patch checks + committed probe-evidence assertions
#    (pass -DNAM_CORE_DIR explicitly; the worktree submodule may be empty)
cmake -S tools/nam-rt-repair -B /home/mojo/projects/build-RT-003/nam -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNAM_CORE_DIR=/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore
cmake --build /home/mojo/projects/build-RT-003/nam -j
ctest --test-dir /home/mojo/projects/build-RT-003/nam --output-on-failure

# 2. real-processor probe: substitute the patched libnam_core.a into the
#    unchanged RT-002 probe. Symlink the read-only SharedCode/assets and copy
#    build.ninja for flags.
PLUGIN=/home/mojo/projects/build-RT-003/probe-plugin
RO=/home/mojo/projects/guitars-build-resume/plugin
mkdir -p "$PLUGIN/GuitarCompanion_artefacts/Release"
ln -s "$RO/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a" \
      "$PLUGIN/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a"
ln -s "$RO/libGuitarCompanionAssets.a" "$PLUGIN/libGuitarCompanionAssets.a"
cp "$RO/build.ninja" "$PLUGIN/build.ninja"
cp /home/mojo/projects/build-RT-003/nam/libnam_core.a "$PLUGIN/libnam_core.a"

export PLUGIN_BUILD_DIR="$PLUGIN"
export RT002_OUT=/home/mojo/projects/build-RT-003/probe-build
export RT002_MAIN_REPO=/home/mojo/projects/guitars
export RT002_SYSROOT_LIB=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu
tools/processor-probe/build_probe.sh          # unchanged probe
RT002_SCRATCH=/home/mojo/projects/build-RT-003/probe-run \
RT002_ARTIFACTS="$PWD/docs/research/nam-rt-repair/probe" \
  tools/processor-probe/run_probe.sh          # unchanged runner
```

Failure checks were exercised by ctest: mutated pinned source ⇒ configure
aborts (input SHA); mutated tracked patch with clean sources ⇒ configure aborts
(patch SHA); patch/overlay mismatch, same-build non-idempotence or untracked
other-header change ⇒ `nam_rt_patch_checks` fails; probe-evidence mismatch ⇒
`nam_rt_probe_repair` fails; comparator unsafe fixtures ⇒ `nam_rt_compare_unit`
fails.

## 7. Honest limitations

- **Not a whole-program proof.** This removes the two sites RT-002 measured for
  this architecture/model. Other NAM architectures (WaveNet/A2/ConvNet/
  Slimmable) were not measured and are not claimed allocation-free.
- The real-callback probe's NAM matrix is **48 kHz and 96 kHz (resampled) at
  128/512 frames**; the unchanged probe does not run the (48 kHz) example model
  at 44.1 kHz, and the dry/built-in matrix (44.1/48/96 × 64/128/512) is what
  covers those combinations. The direct differential test covers block sizes
  64/128/512; `LSTM::process` is sample-rate independent (only
  `GetPrewarmSamples` reads the rate), so a rate axis is not numerically
  meaningful for this model.
- The differential result is byte-identical **for this compiler/flag set**; the
  accepted criterion is the predeclared `1e-5` abs/relative reordering budget
  (a within-budget, non-byte-identical pair is accepted with `bit_exact=false`).
  No cross-compiler bit-exactness is claimed.
- `check_patch.sh` and the probe reproduce steps rely on GNU `patch` and a
  POSIX shell; this work is verified on Linux only. **Windows verification was
  not done** and no portable-cross-platform claim is made.
- Non-device, single-threaded probe: not latency/dropout/device-timing or
  Windows/ASIO evidence. Hosted VST3 remains unmeasured. **G1 remains partial.**
- No production capture was used or distributed; the pinned example
  `example_models/lstm.nam` is read-only input.

## 8. Artifacts

| path | content |
|---|---|
| `differential/combined-diff.json`, `diff-block-{64,128,512}.json` | original-vs-patched numerics (metadata, finite, raw-byte identity, budget) |
| `probe/` | patched `libnam_core.a` probe run (0 allocations) |
| `probe-baseline/` | pinned-upstream control (RT-002 positive baseline) |
| `probe-repair-verification.json` | machine-verified patched-vs-control verdict |
| `manifest.sha256` | hashes of this directory's files |

The `tools/nam-rt-repair/test_compare_diff.py` comparator unit tests are source,
not artifacts. Total artifact size is ~180 KiB (limit 5 MiB). See
`task-notes/RT-003.md` for the task-note summary and hashes.
