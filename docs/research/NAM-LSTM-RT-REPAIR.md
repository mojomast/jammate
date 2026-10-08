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

`patches/nam/lstm-rt-alloc.patch` (three edits, both files):

- `_ifgo.noalias() = _w * _xh; _ifgo += _b;` — writes the product directly into
  the preallocated gate vector and adds the bias in place. The operation order
  (per-element dot product, then bias) is unchanged, so the floating-point
  result is unchanged.
- `get_hidden_state()` returns `Eigen::Ref<const Eigen::VectorXf>`, a
  non-owning view of the existing `_xh` block. No copy.
- `LSTMCell::process_` takes `const Eigen::Ref<const Eigen::VectorXf>&`, so the
  layer-hop call and the `_input` call bind without a copy. The head line uses
  `Eigen::Ref<const Eigen::VectorXf>` instead of a `VectorXf` temporary.

### Why this is ABI-safe for the prebuilt product

The repair lives entirely inside `nam::lstm::{LSTMCell,LSTM}` translation
units. The processor-visible interface is `NAM/dsp.h` (unchanged); `nm` on the
read-only `libGuitar Companion_SharedCode.a` shows **no** LSTM or
`get_hidden_state` symbols, so the prebuilt archive and the rebuilt `nam_core`
cannot disagree. `get_hidden_state` is header-inline and is included only by
`NAM/lstm.cpp` (verified: the only other reference in the whole NAM checkout is
its own `tools/test/test_lstm.cpp`). Because the changed header is internal and
the only consumer is rebuilt from the same generated copy, no whole-product
rebuild is required to trust the link; the probe below confirms it.

### Why a generated overlay, not an edit

`third_party/NeuralAmpModelerCore` is a tracked gitlink/submodule and must stay
byte-identical. `cmake/nam-rt/NamRtPatch.cmake` is applied at configure time:

1. SHA256-verify the pinned `NAM/lstm.cpp`/`lstm.h`; mismatch aborts configure.
2. `file(COPY)` the entire pinned `NAM/` tree to
   `<build>/nam-rt/generated/NAM` (~430 KiB of sources).
3. Apply the exact literal replacements (each old string must be present).
4. SHA256-verify the generated files against the declared patched bytes.

So the submodule bytes are never touched, the whole NAM target for the patched
build compiles from one consistent (patched) header set, Eigen/nlohmann remain
the original pinned headers, and a stale/forked submodule or an edited patch
text fails closed instead of silently building something else.

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
at block sizes **64, 128 and 512**. `compare_diff.py` checks every one of the
83 200 output samples per block with a predeclared budget
`|a-b| <= 1e-5 + 1e-5*max(|a|,|b|)`.

Result: **bit-exact**. `max_abs_diff = 0.0`, `max_rel_diff = 0.0`,
`fnv1a` equal for every config at every block size. (This is stronger than the
budget; the budget is stated because bit-exactness across arbitrary compilers
is not claimed.) A reproducer is in `differential/combined-diff.json`.

### 4.2 Patch provenance / idempotence / stale-hash checks

`tools/nam-rt-repair/check_patch.sh` (ctest `nam_rt_patch_checks`) verifies:

- `patch -p1` of the tracked patch onto the pinned sources reproduces the
  generated overlay **byte-for-byte**;
- two fresh configures produce identical generated bytes and equal the built
  overlay;
- a one-line mutation of the pinned `lstm.cpp` makes configure **fail closed**
  with `SHA256 mismatch`.

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

### 4.4 Root CMake integration

Configuring the actual product from this worktree (read-only submodule
references for the local check) generated the overlay at
`<build>/nam-rt/generated/NAM` and built the real `nam_core` target. The
generated `lstm.cpp`/`lstm.h` SHA256 equal the standalone build's overlay
exactly (`82f25497…` / `4975a306…`).

## 5. Provenance, licensing and integrity

- Upstream notice: NAM Core is MIT (`Copyright (c) 2023 Steven Atkinson`); the
  generated copy is a derivative of the same-pinned MIT source. The MIT notice
  in `THIRD_PARTY.md` still travels with the copy; this repair adds no new
  third-party dependency and changes no licence. See the updated NAM row in
  `docs/research/DEPENDENCIES.md`.
- `third_party/NeuralAmpModelerCore` bytes were not modified. Originals
  `lstm.cpp` `c544c217…`, `lstm.h` `e66c90b9…`; generated `82f25497…`,
  `4975a306…`; patch `bc78b063…`; module `463d72bc…`.

## 6. Reproduce

```sh
cd /home/mojo/projects/worktrees/RT-003-nam-lstm
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-RT-003/tmp

# 1. differential + patch checks + committed probe-evidence assertions
cmake -S tools/nam-rt-repair -B /home/mojo/projects/build-RT-003/nam -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
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
aborts; patch/overlay mismatch ⇒ `nam_rt_patch_checks` fails; probe-evidence
mismatch ⇒ `nam_rt_probe_repair` fails.

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
- The differential result is bit-exact **for this compiler/flag set**; the
  accepted criterion is the predeclared `1e-5` abs/relative reordering budget.
  No cross-compiler bit-exactness is claimed.
- Non-device, single-threaded probe: not latency/dropout/device-timing or
  Windows/ASIO evidence. Hosted VST3 remains unmeasured. **G1 remains partial.**
- No production capture was used or distributed; the pinned example
  `example_models/lstm.nam` is read-only input.

## 8. Artifacts

| path | content |
|---|---|
| `differential/combined-diff.json`, `diff-block-{64,128,512}.json` | original-vs-patched numerics |
| `probe/` | patched `libnam_core.a` probe run (0 allocations) |
| `probe-baseline/` | pinned-upstream control (RT-002 positive baseline) |
| `probe-repair-verification.json` | machine-verified patched-vs-control verdict |
| `manifest.sha256` | hashes of this directory's files |

Total artifact size is ~160 KiB (limit 5 MiB). See `task-notes/RT-003.md` for
the task-note summary and hashes.
