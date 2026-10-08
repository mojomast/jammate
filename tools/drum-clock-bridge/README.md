# tools/drum-clock-bridge

Build and run the INT-DRUM-001 clock-bridge tests.

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-INT-DRUM-001-worker/tmp   # /tmp is a full tmpfs
cd /home/mojo/projects/worktrees/INT-DRUM-001-clock-bridge
python3 tools/drum-clock-bridge/run.py
```

`run.py` is a self-contained driver in the style of `tools/foundation-tests/run.py`:

1. reads the product build's compile/link recipe from
   `ninja -t commands GuitarCompanionTests` (read-only);
2. fresh-compiles, into ONE combined binary, the new integration suite
   (`tests/DrumClockBridgeTests.cpp`), the shared heap probe
   (`tests/DrumHeapProbe.cpp`), all six existing drum suites (so the new and old
   probe users share one symbol definition), the changed `src/DrumEngine.cpp`,
   `src/DrumLibrary.cpp`, `src/DrumGenerator.cpp` and the new
   `src/jam/DrumClockBridge.cpp`;
3. links against the reused read-only JUCE module objects and
   `libGuitarCompanionAssets.a` from `--product-build`;
4. runs the combined binary (new integration + standalone regressions);
5. compiles and runs the portable JUCE-free bridge suite with the jam-core
   harness and the bare compiler.

Useful flags: `--source`, `--product-build`, `--output`, `--no-portable`.

Outputs: `build.log`, `*-results.log`, `manifest.json` (source HEAD, fresh-source
hashes, reused-input hashes, exact commands, binary hashes) under `--output`
(default `/home/mojo/projects/build-INT-DRUM-001-worker`).

This driver does not build the plugin, does not write to the product build, and
does not register the new tests in the root build. The orchestrator owns that
registration and the final integrated build.
