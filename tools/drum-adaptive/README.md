# tools/drum-adaptive

Build and run the DRUM-ADAPT-002 adaptive drum-engine tests.

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-DRUM-ADAPT-002-worker/tmp   # /tmp is a full tmpfs
cd /home/mojo/projects/worktrees/DRUM-ADAPT-002
python3 tools/drum-adaptive/run.py
```

`run.py` is a self-contained driver in the style of `tools/drum-clock-bridge/run.py`:

1. reads the product build's compile/link recipe from
   `ninja -t commands GuitarCompanionTests` (read-only);
2. fresh-compiles the new adaptive actual-engine suite
   (`tests/DrumAdaptiveTests.cpp`), the existing clock-bridge suite
   (`tests/DrumClockBridgeTests.cpp`), the shared heap probe
   (`tests/DrumHeapProbe.cpp`), all six existing drum suites, the changed
   `src/DrumEngine.cpp`, `src/DrumLibrary.cpp`, `src/DrumGenerator.cpp` and the
   changed `src/jam/DrumClockBridge.cpp` into **one combined binary, twice**:
   - `probe/` — with `-DDRUM_MIDI_HEAP_PROBE=1` and the `--wrap` linker flags
     (allocation checks active), and
   - `no-probe/` — without the macro and without the wrap flags (allocation
     checks explicitly skipped), proving every test compiles and runs on a
     default/Windows-style configuration;
3. links against the reused read-only JUCE module objects and
   `libGuitarCompanionAssets.a` from `--product-build`;
4. runs both combined binaries;
5. compiles and runs the portable JUCE-free bridge suites
   (`tests/jam/DrumClockCommandTests.cpp` + `tests/jam/DrumAdaptiveBridgeTests.cpp`)
   with the jam-core harness and the bare compiler.

Useful flags: `--source`, `--product-build`, `--output`, `--no-portable`.

Outputs are kept in per-mode subdirectories (`probe/`, `no-probe/`,
`portable/`) with each `build.log` and `*-results.log`; the top-level
`manifest.json` records the source HEAD, the **dirty status** (`gitClean` /
`gitStatus`), fresh-source hashes, hashes of **all consumed project headers and
the driver itself** (`projectInputs`), reused-input hashes, exact commands and
binary hashes. A dirty worktree is reported explicitly (and on stderr) rather
than silently; a final evidence receipt must show `gitClean: true`.

Run the final receipt into a **new** `--output` directory so earlier logs and
receipts are preserved rather than overwritten:

```sh
python3 tools/drum-adaptive/run.py \
  --output /home/mojo/projects/build-DRUM-ADAPT-002-worker/receipt-<head>
```

This driver does not build the plugin, does not write to the product build, and
does not register the new tests in the root build. The orchestrator owns that
registration (see the task note integration section) and the final integrated
build.
