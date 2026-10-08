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
2. fresh-compiles the new integration suite (`tests/DrumClockBridgeTests.cpp`),
   the shared heap probe (`tests/DrumHeapProbe.cpp`), all six existing drum
   suites, the changed `src/DrumEngine.cpp`, `src/DrumLibrary.cpp`,
   `src/DrumGenerator.cpp` and the new `src/jam/DrumClockBridge.cpp` into **one
   combined binary, twice**:
   - `probe/` — with `-DDRUM_MIDI_HEAP_PROBE=1` and the `--wrap` linker flags
     (allocation checks active), and
   - `no-probe/` — without the macro and without the wrap flags (allocation
     checks explicitly skipped), proving every test compiles and runs on a
     default/Windows-style configuration;
3. links against the reused read-only JUCE module objects and
   `libGuitarCompanionAssets.a` from `--product-build`;
4. runs both combined binaries;
5. compiles and runs the portable JUCE-free bridge suite (`portable/`) with the
   jam-core harness and the bare compiler.

Useful flags: `--source`, `--product-build`, `--output`, `--no-portable`.

Outputs are kept in per-mode subdirectories (`probe/`, `no-probe/`,
`portable/`) with each `build.log` and `*-results.log`; the top-level
`manifest.json` records the source HEAD, fresh-source hashes, reused-input
hashes, exact commands and binary hashes.

This driver does not build the plugin, does not write to the product build, and
does not register the new tests in the root build. The orchestrator owns that
registration (see the task note integration section) and the final integrated
build.
