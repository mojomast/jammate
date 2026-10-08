# tools/nam-activation-repair — RT-005 activation-allocation processor probe

Bounded, read-only comparison of **three NAM archives** through the actual
`GuitarCompanionProcessor::processBlock`:

| variant | NAM archive | overlay |
|---|---|---|
| `original` | pinned upstream `libnam_core.a` (`af023ffc…`) | none |
| `lstm_only` | RT-003 integration product `libnam_core.a` (`dbd11fb2…`) | LSTM patch only |
| `new` | RT-005 production `libnam_core.a` (`ad0ffbb3…`) | LSTM + activation patches |

The `lstm_only` and `new` probes are linked against the **same** read-only
shared/assets archives and the same `build.ninja`, so they differ only in
`libnam_core.a`. `original` is the historical RT-004 control. The runner executes
each variant's **prebuilt static probe binary**: the NAM archive is linked in at
build time and there is **no runtime archive substitution** — the worker proof is
the pinned binary/archive identities plus the recorded runs. The orchestrator
independently rebuilt the archive and the linked probe and obtained the same
hashes (`ad0ffbb3…` / `faa79a4f…`).

`predeclared.json`'s `source_pin.revision` (`677ce9f`) is the processor source
revision the probe archives were compiled from; those `src/` bytes are identical
to the RT-005 base `677727c`. It is not the RT-005 worktree HEAD, and it (like the
other predeclared pins) is kept immutable.

The tooling does not rebuild the shared product, JUCE or assets, and does not
edit the probe source (`tools/processor-probe/**` is read-only). Model files are
read in place.

## Layout

```
tools/nam-activation-repair/
  predeclared.json            task, pinned identities, protocol, expected matrix
  run_activation_probe.sh     preflight + one clean process per (variant, model)
  check_activation_probe.py   fail-closed validator and 3-archive comparison
  test_check_activation_probe.py  evidence + adversarial validator tests
  README.md
```

Artifacts live in `docs/research/nam-activation-repair/` (`runs/`,
`summary.md`, `activation-probe-verification.json`, `manifest.sha256`). The full
report is `docs/research/NAM-ACTIVATION-REPAIR.md`.

## Building the `new` probe

The shared product is reused read-only. Build the repaired production
`nam_core`, then substitute it into the existing probe recipe (the same one
RT-003 used):

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
base=/home/mojo/projects/guitars-build-resume
export TMPDIR="$base/tmp"
export CMAKE_BUILD_PARALLEL_LEVEL=2
export PKG_CONFIG_PATH="$base/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$base/sysroot/usr/share/pkgconfig"
export CPATH="$base/sysroot/usr/include:$base/sysroot/usr/include/freetype2"
export LIBRARY_PATH="$base/sysroot/usr/lib/x86_64-linux-gnu"
# worktree third_party submodules are empty; reference the pinned checkout
# read-only for the local configure (see docs/research/NAM-ACTIVATION-REPAIR.md).
cmake -S <worktree> -B /home/mojo/projects/build-RT-005-worker/product -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGUITAR_COMPANION_BUILD_TESTS=OFF \
  -DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF -DCMAKE_PREFIX_PATH="$base/sysroot/usr"
cmake --build /home/mojo/projects/build-RT-005-worker/product --target nam_core -j2

# probe scratch: read-only shared/assets + the new archive
P=/home/mojo/projects/build-RT-005-worker/newprobe/plugin
mkdir -p "$P/GuitarCompanion_artefacts/Release"
ln -s /home/mojo/projects/build-RT-003-integration/product/GuitarCompanion_artefacts/Release/"libGuitar Companion_SharedCode.a" \
      "$P/GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a"
ln -s /home/mojo/projects/build-RT-003-integration/product/libGuitarCompanionAssets.a "$P/libGuitarCompanionAssets.a"
cp /home/mojo/projects/build-RT-005-worker/product/libnam_core.a "$P/libnam_core.a"
cp /home/mojo/projects/build-RT-003-integration/product/build.ninja "$P/build.ninja"
PLUGIN_BUILD_DIR="$P" RT002_OUT=/home/mojo/projects/build-RT-005-worker/newprobe/build \
  RT002_MAIN_REPO=/home/mojo/projects/guitars \
  RT002_SYSROOT_LIB="$base/sysroot/usr/lib/x86_64-linux-gnu" \
  <worktree>/tools/processor-probe/build_probe.sh
```

The `new` binary and archive hashes are pinned in `predeclared.json`; the runner
preflights them and refuses to measure on any deviation.

## Run and validate

```sh
tools/nam-activation-repair/run_activation_probe.sh        # ~14 s, 15 processes
python3 tools/nam-activation-repair/check_activation_probe.py \
  --runs docs/research/nam-activation-repair/runs \
  --predeclared tools/nam-activation-repair/predeclared.json \
  --models-dir /home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models \
  --out-json docs/research/nam-activation-repair/activation-probe-verification.json \
  --summary-md docs/research/nam-activation-repair/summary.md
python3 -m unittest tools/nam-activation-repair/test_check_activation_probe.py
```

The runner accepts `NAM_ACT_ONLY=<variant>`,
`NAM_ACT_BIN_<VARIANT>`, `NAM_ACT_ARCHIVE_<VARIANT>` and
`NAM_ACT_MODELS_DIR` overrides; the protocol budget (`warm_blocks=128`,
`timeout_s=600`) is read from `predeclared.json` and never from a run's own
status.

Exit status of the validator: 0 all checks pass, 3 any run invalid or a
zero-expected case is positive.
