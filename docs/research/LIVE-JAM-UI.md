# UI-LIVE-001 — Actual Jam screen, controls and telemetry

Base: `88893e2` (`wp/UI-LIVE-001-jam-screen`). This worker owns `PluginEditor.h/.cpp`,
`ui/JamOverlay.h/.cpp`, the new `ui/JamLivePresenter.h/.cpp`, the new UI tests and
harness, and this document plus the task note. It does **not** define the frozen
facade (INT-LIVE-001 owns `PluginProcessor.cpp`), touch the renderer, the drum
engine, the shared CMake/CI, or the ledgers.

## What changed

- `src/ui/JamLivePresenter.h/.cpp` (new): the message-thread mapper between the
  frozen `jam::IJamLiveControl` facade and the JUCE view. One UI reader cache,
  no worker lifecycle, no renderer setters, no plain `DrumEngine` getters.
- `src/ui/JamOverlay.h/.cpp`: the MOD-003 view is reused. Defaults are now
  zero/not-prepared/unavailable, production controls show one `Rock` style and
  `Fixed/Follow/Loose` modes, unsupported controls are disabled with explicit
  availability text, and the presenter maps the real command set. The isolated
  preview can opt in to the demo control set with `setSimulatedPreview(true)`.
- `src/PluginEditor.h/.cpp`: a visible `Jam` button next to `Drums`/`Song`/
  `Audio`, a lazily built full-screen overlay, the presenter created on first
  open, a 30 Hz coherent poll while the screen is visible, Escape dismissal and
  callback teardown on editor destruction.

## Threading and lifecycle

- UI submits commands and reads state only from the message thread. The
  presenter never starts or joins a worker and never touches the audio renderer.
- `JamLivePresenter::poll()` performs exactly one `readJamLiveState()` attempt
  per timer tick. `true` replaces the cached `jam::JamLiveState`; `false` keeps
  the previous **whole** snapshot. There is no retry and no blocking loop.
- `RigContent` owns both objects and declares the presenter before the overlay,
  so the overlay is destroyed first; the destructor also clears `onIntent` /
  `onClose`. Closing the editor therefore neither stops workers nor resets any
  pipeline queue and leaves no dangling UI callback.
- Recreating an editor creates a fresh presenter whose cache is cold until the
  worker publishes the next coherent snapshot. This is acceptable: the worker
  publishes telemetry even while stopped, so the new editor warms up on the next
  tick. `resetCache()` only resets the per-editor presentation cache; it makes no
  facade call.

### Proposed read semantics (consistent with the frozen facade)

`readJamLiveState(out) == false` leaves `out` unchanged. The presenter only
copies a **fully coherent** snapshot on `true`; on `false` it keeps the previous
whole state until the next tick. No partial merge, no fabricated sub-block
timing. A new editor cannot know the worker's last value before its first
coherent read; it shows the zero/unavailable default for at most one tick.

## Intent → frozen command mapping

| UI control | `jam::JamLiveCommandType` |
|---|---|
| Start/Stop | `Start`, or `Stop` once `requestedRunning`/`drumsPlaying` is echoed |
| Tap | `TapTempo` |
| Resync | `ResyncNextBeat` |
| Resync Bar | `ResyncNextBar` |
| Half / Double | `HalfTime` / `DoubleTime` |
| Freeze / Resume | `FreezeTempo` / `ResumeFollow` |
| Stop Next Bar | `StopAtNextBar` |
| Reset | `Reset` |
| Mode (Fixed/Follow/Loose) | `SetMode` with numeric `TempoMode` (0/1/2) |

There is no raw tracker-BPM setter and no hidden BPM knob: the MusicalClock owns
tempo. `submitJamCommand() == false` is shown as a red, explicit dropped/rejected
message and never updates the cached state as if applied. Style, intensity,
complexity, fill amount, follow tightness, fill and break are rejected visibly —
they are not silently queued.

## Presentation

- Default: `NOT CONNECTED` / `NOT PREPARED`, `--` BPM and confidence, zero input
  peak, `unavailable` backend. No `118.4` / `120` / `82%` production defaults.
- Start/Stop is intent: the status line distinguishes `PREPARED - STOPPED`,
  `ARMED - LISTENING`, `ARMED - WAITING FOR THE CLOCK`, and `PLAYING (AUDIO ECHO)`
  so a scheduled command is never presented as sound.
- Telemetry: candidate BPM, clock BPM, confidence, input peak, lock state,
  beat/beats-per-bar, tempo-frozen, backend/failure, receipt lag and the four
  drop counters plus discontinuities (in diagnostics).
- Production controls: `Rock` only, `Fixed/Follow/Loose`, disabled intensity/
  complexity/fill/tightness, disabled `FILL`/`BREAK`, with the availability text
  "Intensity, complexity, fills and tightness are disabled: not implemented in
  this build."
- Accessibility: every control has a name/title/description; status is readable
  as text (not colour alone); Escape closes; Tab traversal is left to JUCE.

## Top bar

The screen cluster is now `Jam | Song | Drums | Audio | Tone 3000 Store`, with the
meters to its left. Button widths and the centred preset group were tightened so
the fixed 1100×700 design canvas still fits cluster + meters + preset group with
no overlap. The CPU label and divider anchor to `jamButton`. The editor's
`setResizeLimits` (half the design canvas) is unchanged; the fixed canvas is
scaled by the editor transform, so the overlay is a full-canvas modal layer.

## Verification

Exact recipe (Linux, no root; scratch ≤ 0.6 GiB, `-j 2`):

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
export PKG_CONFIG_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/home/mojo/projects/guitars-build-resume/sysroot/usr/share/pkgconfig
export CPATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/include:/home/mojo/projects/guitars-build-resume/sysroot/usr/include/freetype2
export LIBRARY_PATH=/home/mojo/projects/guitars-build-resume/sysroot/usr/lib/x86_64-linux-gnu
cmake -S tools/live-jam-ui -B /home/mojo/projects/build-UI-LIVE-001-worker -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DJUCE_SOURCE_DIR=/home/mojo/projects/guitars/third_party/JUCE \
  -DJUCE_REUSE_BUILD=/home/mojo/projects/build-RT-003-integration/product
cmake --build /home/mojo/projects/build-UI-LIVE-001-worker -j 2
xvfb-run -a /home/mojo/projects/build-UI-LIVE-001-worker/LiveJamUiHarness
xvfb-run -a /home/mojo/projects/build-UI-LIVE-001-worker/LiveJamUiHarness --snapshot docs/screenshots
```

`tests/JamLiveUiTests.cpp` runs 9 cases / 18+ checks against a mock
`IJamLiveControl`: cold default, coherent mapping, failed-read whole-state
retention, pure intent→command mapping, visible rejection, Start/Stop intent
echo, production control availability/accessibility, preview opt-in, and
UI-click→presenter integration. Full output: `tools/live-jam-ui/verification.log`.

The production editor TU was validated with a real `-fsyntax-only` compile using
the flags extracted from the reusable product build (`ninja -t commands`), the
current frozen header, and the worktree sources:

```sh
c++ <extracted flags> -I<worktree>/src -fsyntax-only <worktree>/src/PluginEditor.cpp
# exit 0, 0 errors
```

The MOD-003 preview (`tools/jam-ui-preview`) still compiles against the reused
JUCE objects (API and names unchanged); it is not the supported demo path.

Snapshots are rendered by the harness with the real presenter + a deterministic
mock facade and are explicitly labelled `test fixture` (no physical input):
`jam-live-testfixture.png`, `jam-live-cold-testfixture.png`,
`jam-live-demo-testfixture.png`.

## Known limitations / integration needs

- The root product cannot link in this worktree until INT-LIVE-001 defines
  `submitJamCommand` / `readJamLiveState` in `PluginProcessor.cpp`. Wiring is
  validated by the syntax-only editor compile and the mock harness.
- The orchestrator must add `src/ui/JamOverlay.cpp` and
  `src/ui/JamLivePresenter.cpp` to the product target, and
  `tests/JamLiveUiTests.cpp` to a test target that also links `JamOverlay.cpp` /
  `JamLivePresenter.cpp` and initialises JUCE for the component cases.
- No audible pipeline, device/ASIO deadline or real-guitar lock claim is made
  here; that evidence belongs to INT-LIVE-001 / EVAL-LIVE-001.
- The legacy MOD-003 preview's runtime assertions assume the removed fake
  defaults (`118.4`/`120`, demo mode ids) and the old enabled demo controls. It
  compiles but is superseded by `tools/live-jam-ui`; its owner can call
  `setSimulatedPreview(true)` and seed demo state to restore it.
- Windows and the full-product build were not run in this worktree.
