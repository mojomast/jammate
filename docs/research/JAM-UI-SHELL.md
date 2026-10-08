# MOD-003: isolated Jam UI shell

Implemented under explicit D7 permission to prototype fake-data UI before full
G1/G2 acceptance. Base: `45fa333`, branch `wp/MOD-003-jam-ui`.

## Component contract

`src/ui/JamOverlay.h/.cpp` provides a JUCE `Component` with:

- Style and Jam Mode selectors; Intensity, Complexity, Fill Amount and Follow
  Tightness sliders.
- Start/Stop Demo, Tap, Resync, Half, Double, Fill, Break and Stop Next Bar.
- Candidate and clock BPM, confidence, Acquiring/Locked/Holdover/Lost status,
  simulated input meter, current bar/beat and next planned intent.
- An advanced diagnostics disclosure and explicit simulation identification.

`JamViewState` is a value snapshot. `setViewState()` silently updates controls;
it never sends commands. `onIntent(const JamUiIntent&)` reports a typed action
and numeric value. Selector values are one-based IDs; slider values are 0–100.
The owner must publish the resulting state after handling an intent. All calls
and callback handling occur on the JUCE message thread. This shell owns neither
an audio device nor an engine, tracker, worker, queue or transport timer.

The component uses `src/LookAndFeel.h` palette and `RigLookAndFeel`, following
the nearby overlays' painted labels and panel style. In the product, the existing
font functions provide embedded typefaces; the standalone preview supplies a
system-font bridge (DejaVu Sans). No production registration is required to
build the preview.

## Preview behavior

`tools/jam-ui-preview/main.cpp` owns the only simulator. Its 30 Hz message-thread
timer animates candidate BPM, confidence, input level, bar/beat and lock state.
Tap measures a preview-only interval; Half/Double change the mock clock; Resync
resets mock beat phase; Fill/Break/Stop queue a mock bar-boundary action. No sound
is generated. The header explicitly says **SIMULATION / no live guitar input or
audio output**; the primary button says START DEMO or STOP DEMO.

The prototype diagnostics identify the absence of an audio device. SPEC §17's
actual backend/device/sample-rate/buffer/latency/load/xrun information requires
future device integration; this shell does not invent hardware measurements.
Swing, kit/source and Guitar/Drums balance from the broader SPEC §16 production
screen remain UI-001 scope, beyond MOD-003's requested controls.

## Layout and evidence

Supported preview window minimum: 580 × 480. Panels grow horizontally; below
1000 px content width, performance actions wrap into a primary button plus
four- and three-button rows. A vertical viewport keeps all controls reachable
at short heights. Horizontal compression below the supported width is not a
validated layout. The diagnostics disclosure increases scrollable content.

The executed verification checks sibling control non-overlap, content containment
and minimum control dimensions at 580, 800, 1000, 1016 and 1280 × 600, plus the
screenshotted layouts. It exercises all selectors/sliders/actions, silent state
refresh, interval tap BPM, running timer telemetry, queued bar-boundary stop,
immediate stop, and publication of all four lock states. The final run passed
with 21 intents. This is a GUI smoke/contract check, not rhythm-engine testing.

Evidence in `docs/screenshots/`:

| File | Capture |
|---|---|
| `jam-shell-wide.png` | 1060 × 720, started simulation |
| `jam-shell-minimum.png` | 580 × 480, minimum preview window |
| `jam-shell-compact.png` | 640 × 540, status/control view |
| `jam-shell-compact-actions.png` | 640 × 540, scrolled performance actions |
| `jam-shell-diagnostics.png` | 1060 × 860, expanded disclosure |
| `jam-shell-stopped.png` | 1060 × 860, mock next-bar stop completed |
| `jam-shell-xvfb.png` | 1600 × 1200, actual X11 desktop/window capture |

The first six are JUCE component snapshots taken by the running preview under
Xvfb. The seventh is an independent FFmpeg X11 capture. Screenshots were visually
reviewed for legibility, complete action labels, spacing and simulation status.
`tools/jam-ui-preview/verification.log` records the final executed assertions.
Telemetry and tap-derived values vary slightly with scheduling between runs.

## Exact build/run recipe (Linux, no root)

Run from `/home/mojo/projects/worktrees/MOD-003-jam-ui`:

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
export PKG_CONFIG_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/home/mojo/projects/guitars-build-resume/sysroot/usr/share/pkgconfig
export CPATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/include:/home/mojo/projects/guitars-build-resume/sysroot/usr/include/freetype2
export LIBRARY_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu
cmake -S tools/jam-ui-preview -B /home/mojo/projects/guitars-build-resume/jam-ui-preview \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DJUCE_SOURCE_DIR=/home/mojo/projects/guitars/third_party/JUCE \
  -DJUCE_REUSE_BUILD=/home/mojo/projects/build-RT-003-integration/product
cmake --build /home/mojo/projects/guitars-build-resume/jam-ui-preview -j 2
Xvfb :93 -screen 0 1600x1200x24 -nolisten tcp
```

Keep Xvfb running in a separate terminal. Then:

```sh
set -o pipefail
DISPLAY=:93 /home/mojo/projects/guitars-build-resume/jam-ui-preview/JamUiPreview \
  --verify /home/mojo/projects/worktrees/MOD-003-jam-ui/docs/screenshots \
  | tee tools/jam-ui-preview/verification.log
```

For the independently captured desktop (also executed):

```sh
DISPLAY=:93 /home/mojo/projects/guitars-build-resume/jam-ui-preview/JamUiPreview &
preview_pid=$!
trap 'kill "$preview_pid" 2>/dev/null || true' EXIT
sleep 1
ffmpeg -hide_banner -loglevel error -f x11grab -video_size 1600x1200 \
  -i :93 -frames:v 1 -y docs/screenshots/jam-shell-xvfb.png
```

The preview-only CMake reads existing Release JUCE module objects for core,
events, data structures, graphics, GUI basics and GUI extra. GUI extra is needed
because the reused GUI basics objects reference its Linux XEmbed functions.
Only the two new translation units are compiled, then linked with these objects
and system font libraries. No product archive, audio module, NAM object or
application source is linked, and the reuse directory is not written to.
Use the same JUCE checkout/configuration as the reusable objects; this is a
deliberately local resource-saving build recipe, not a portable packaged build.
See `LOCAL-LINUX-BUILD.md` for the extracted sysroot. `/tmp` is full, so scratch
and the small preview build live on disk under `guitars-build-resume`.

Initial configure exposed stale `/usr/include/freetype2` paths in extracted
pkg-config metadata; the final recipe uses CPATH instead of importing those
include paths. Initial link required adding the reusable GUI-extra object.
Both were resolved. Final fresh-source compilation, linking and verification
completed successfully. No Windows, physical-device, audible or full-product
validation was performed for this isolated shell.
