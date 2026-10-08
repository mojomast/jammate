# Jam UI preview (MOD-003 shell, UI-LIVE-001 adapted)

Isolated standalone harness for the reusable `src/ui/JamOverlay.h/.cpp`. It
compiles only `main.cpp` + `JamOverlay.cpp` and links read-only Release JUCE
module objects from an existing product build. It never links the product
archive, the processor or an audio device.

## Receipts

- `verification.log` — **historical MOD-003 base proof**, produced when the Jam
  overlay still shipped fake demo defaults. Retained unchanged for history; it
  is **not** a current-source claim.
- `verification-live-adapted.log` — **current-source receipt** for the
  UI-LIVE-001 corrected overlay. Supersedes `verification.log` for current
  claims. It records the exact build/run commands and the passing output
  (`intents=21`, exit 0).

## UI-LIVE-001 change

Production defaults are now zero/not-prepared/unavailable (no `118.4` / `120` /
`82%`), and the production control set is one `Rock` style with
`Fixed/Follow/Loose` modes and disabled unsupported controls. To keep the
historical self-checks meaningful, this harness now **explicitly opts in** to the
demo presentation:

- `Simulator` calls `view.setSimulatedPreview(true)` before publishing.
- It seeds explicit demo telemetry (`candidateBpm 118.4`, `clockBpm 120.0`,
  `confidence 0.82`, amounts `65/40/30/70`, demo style/mode ids) so the
  `HALF -> 60`, `DOUBLE -> 120`, style `2`, mode `4` and amount intents still
  hold.

No fake production defaults were restored in the overlay; the opt-in lives only
in this dev wrapper.

## Build / run (Linux, no root)

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
export PKG_CONFIG_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/home/mojo/projects/guitars-build-resume/sysroot/usr/share/pkgconfig
export CPATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/include:/home/mojo/projects/guitars-build-resume/sysroot/usr/include/freetype2
export LIBRARY_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu
cmake -S tools/jam-ui-preview -B /home/mojo/projects/build-UI-LIVE-001-preview -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DJUCE_SOURCE_DIR=/home/mojo/projects/guitars/third_party/JUCE \
  -DJUCE_REUSE_BUILD=/home/mojo/projects/build-RT-003-integration/product
cmake --build /home/mojo/projects/build-UI-LIVE-001-preview -j 2
xvfb-run -a /home/mojo/projects/build-UI-LIVE-001-preview/JamUiPreview \
  --verify /home/mojo/projects/build-UI-LIVE-001-preview/verify-live
```

The current production-facing harness for the live screen is
`tools/live-jam-ui` (real presenter + mock facade + 1100x700 test-fixture
snapshots). This preview remains the MOD-003 shell regression wrapper.
