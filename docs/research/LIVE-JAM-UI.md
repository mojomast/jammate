# UI-LIVE-001 — Actual Jam screen, controls and telemetry

Base: `88893e2` (`wp/UI-LIVE-001-jam-screen`). This worker owns `PluginEditor.h/.cpp`,
`ui/JamOverlay.h/.cpp`, the new `ui/JamLivePresenter.h/.cpp`, the new UI tests and
harness, `tools/jam-ui-preview/main.cpp` + `README.md` +
`verification-live-adapted.log`, and this document plus the task note. It does
**not** define the frozen facade (INT-LIVE-001 owns `PluginProcessor.cpp`), touch
the renderer, the drum engine, the shared CMake/CI, the ledgers, or the frozen
interface/contract.

## What changed

- `src/ui/JamLivePresenter.h/.cpp` (new): the message-thread mapper between the
  frozen `jam::IJamLiveControl` facade and the JUCE view. One UI reader cache,
  an accepted-intent latch, no worker lifecycle, no renderer setters, no plain
  `DrumEngine` getters.
- `src/ui/JamOverlay.h/.cpp`: the MOD-003 view is reused. Defaults are now
  zero/not-prepared/unavailable; production shows one `Rock` style and
  `Fixed/Follow/Loose` modes, disables unsupported controls with explicit
  availability text, and the compact real-canvas layout keeps every primary
  control visible without scrolling. The isolated preview opts in to the demo
  control set with `setSimulatedPreview(true)`.
- `src/PluginEditor.h/.cpp`: a visible `Jam` button next to `Drums`/`Song`/
  `Audio`, a lazily built full-screen overlay, the presenter created on first
  open, a 30 Hz coherent poll only while the screen is visible, Escape
  dismissal, Tab traversal preserved, and callback teardown on editor
  destruction.

## Threading and lifecycle

- UI submits commands and reads state only from the message thread. The
  presenter never starts or joins a worker and never touches the audio renderer.
- `JamLivePresenter::poll()` performs exactly one `readJamLiveState()` attempt
  per timer tick. `true` replaces the cached `jam::JamLiveState`; `false` keeps
  the previous **whole** snapshot. No retry, no blocking loop.
- `RigContent` owns both objects and declares the presenter before the overlay,
  so the overlay is destroyed first; the destructor also clears `onIntent` /
  `onClose`. Closing the editor neither stops workers nor resets any pipeline
  queue and leaves no dangling UI callback.
- Recreating an editor creates a fresh presenter whose cache is cold until the
  worker publishes the next coherent snapshot. The worker publishes telemetry
  even while stopped, so the new editor warms up on the next tick.
  `resetCache()` only resets the per-editor presentation cache; it makes no
  facade call.

### Proposed read semantics (consistent with the frozen facade)

`readJamLiveState(out) == false` leaves `out` unchanged. The presenter copies a
snapshot only on `true`; on `false` it keeps the previous whole state until the
next tick. No partial merge, no fabricated sub-block timing. A new editor shows
the zero/unavailable default for at most one tick.

## Intent → frozen command mapping

| UI control | `jam::JamLiveCommandType` |
|---|---|
| Start/Stop | `Start`, or `Stop` from the accepted-intent latch (see below) |
| Tap | `TapTempo` |
| Resync / Resync Bar | `ResyncNextBeat` / `ResyncNextBar` |
| Half / Double | `HalfTime` / `DoubleTime` |
| Freeze / Resume | `FreezeTempo` / `ResumeFollow` |
| Stop Next Bar | `StopAtNextBar` |
| Reset | `Reset` |
| Mode (Fixed/Follow/Loose) | `SetMode` with numeric `TempoMode` (0/1/2) |

There is no raw tracker-BPM setter and no hidden BPM knob: the MusicalClock owns
tempo. `submitJamCommand() == false` is shown as a red, explicit dropped/rejected
message and never updates the cached state as if applied. Style, intensity,
complexity, fill amount, follow tightness, fill and break are rejected visibly —
never silently queued.

### Start/Stop intent latch (fast double-click)

The button toggles off a **local accepted-intent latch**, not the audio echo, so
a fast second click after an accepted Start schedules a `Stop` before the worker
echoes anything (a pending join can be cancelled). Rules:

- The latch is set only when `submitJamCommand` accepted the Start/Stop.
- A rejected command never sets the latch, so a failed Start is not shown as
  accepted.
- The latch is released only on a device generation change or a prepared
  release, never on a stale/raced read with the same generation.
- `view.running` (button selection) reflects the latch; the status line uses the
  audio-owner echo, so a scheduled command is never presented as sound.
- `StopAtNextBar` stays a distinct bar-bounded command; the presenter mapping
  enum is unchanged. The actual pipeline contract (Stop = next serviced callback
  cancels a pending join) is the orchestrator/INT-LIVE-001 decision and needs no
  presenter code change.

## Presentation

- Default: `NOT CONNECTED` / `NOT PREPARED`, `--` BPM, `--` confidence, `--`
  beat (no invented bar), zero input peak, `unavailable` backend. No `118.4` /
  `120` / `82%` production defaults.
- Exact status strings distinguish intent from echo: `PREPARED - STOPPED`,
  `ARMED - LISTENING`, `ARMED - WAITING FOR THE CLOCK`,
  `START QUEUED - WAITING FOR THE ENGINE`, `STOP QUEUED - WAITING FOR THE
  ENGINE`, and `PLAYING (AUDIO ECHO)`.
- Telemetry: candidate BPM, clock BPM, confidence, input peak, lock state,
  beat/beats-per-bar, tempo-frozen, backend/failure, receipt lag and the four
  drop counters plus discontinuities (diagnostics).
- Production controls: `Rock` only, `Fixed/Follow/Loose`, disabled intensity/
  complexity/fill/tightness, disabled `FILL`/`BREAK`, with the availability text
  "Intensity, complexity, fills and tightness are disabled: not implemented in
  this build."
- Accessibility: every control has a name/title/description; status is readable
  as text (not colour alone); Escape closes (once per open screen); Tab falls
  through to the JUCE focus traverser.

## Real-canvas layout (1100x700)

Compact fixed sections — header 74, status 132, telemetry 126, then the action
strip (Start/Stop + Tap/Resync beat+bar/Half/Double/Freeze/Resume/Stop next
bar/Reset) at y≈370..470, then the advanced controls at y≈478..688. Every
primary control is fully inside the 1100x700 viewport before scrolling; only the
dev diagnostics toggle can fall below the fold. The tests assert this by
translating each control through content→viewport with
`Component::getLocalArea` and checking containment, non-zero size and
non-overlap. The editor's `setResizeLimits` is unchanged; the fixed design canvas
is scaled by the editor transform, so the budget is identical at the minimum
editor size.

## Top bar

The screen cluster is `Jam | Song | Drums | Audio | Tone 3000 Store`, with the
meters to its left. Widths and the centred preset group were tightened so the
fixed 1100x700 canvas still fits cluster + meters + preset group with no overlap.
The CPU label and divider anchor to `jamButton`. The long store/preset text is
drawn with fitted text by the existing LookAndFeel; no RigContent screenshot is
faked here because it needs a real processor instance.

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
  -DJUCE_REUSE_BUILD=/home/mojo/projects/build-INT-DRUM-001-integration/product
cmake --build /home/mojo/projects/build-UI-LIVE-001-worker -j 2
xvfb-run -a /home/mojo/projects/build-UI-LIVE-001-worker/LiveJamUiHarness
xvfb-run -a /home/mojo/projects/build-UI-LIVE-001-worker/LiveJamUiHarness --snapshot docs/screenshots
```

`tests/JamLiveUiTests.cpp` runs 14 cases against a mock `IJamLiveControl`:
cold default, coherent mapping, failed-read whole-state retention, exact status
strings, pure intent→command mapping, visible rejection, fast double/triple
click, rejected-Start-not-pretended, generation/prepared latch release,
real-canvas primary-control visibility/non-overlap, production
availability/accessibility, preview opt-in, Escape/Tab, and click→presenter
mapping. The isolated harness additionally runs a real posted-click
(`triggerClick` + message pump) self-check: an enabled button fires, a disabled
button is ignored. Full output: `tools/live-jam-ui/verification.log`.

The production editor TU was validated with a real `-fsyntax-only` compile using
the flags extracted from the reusable product build (`ninja -t commands`), the
current frozen header, and the worktree sources:

```sh
c++ <extracted flags> -I<worktree>/src -fsyntax-only <worktree>/src/PluginEditor.cpp
# exit 0, 0 errors
```

Snapshots are rendered by the harness with the real presenter + a deterministic
mock facade at the real 1100x700 canvas and are explicitly labelled `test
fixture` (no physical input): `jam-live-cold-testfixture.png`,
`jam-live-armed-testfixture.png`, `jam-live-playing-testfixture.png`,
`jam-live-demo-testfixture.png`.

### Legacy MOD-003 preview

`tools/jam-ui-preview` is extended so its self-checks still pass against the new
overlay without restoring fake production defaults: `main.cpp` calls
`setSimulatedPreview(true)` and seeds explicit demo telemetry. `README.md`
documents the split. `verification.log` is retained as the **historical MOD-003
base proof** (not a current claim); `verification-live-adapted.log` is the
current-source receipt (`intents=21`, exit 0).

## Known limitations / integration needs

- The root product cannot link in this worktree until INT-LIVE-001 defines
  `submitJamCommand` / `readJamLiveState` in `PluginProcessor.cpp`. Wiring is
  validated by the syntax-only editor compile and the mock harness.
- The orchestrator must add `src/ui/JamOverlay.cpp` and
  `src/ui/JamLivePresenter.cpp` to the product target, and
  `tests/JamLiveUiTests.cpp` to a test target that also links those two sources
  and initialises JUCE for the component cases (shared `TestMain` init/fonts are
  orchestrator-owned and not edited here).
- No audible pipeline, device/ASIO deadline or real-guitar lock claim is made
  here; that evidence belongs to INT-LIVE-001 / EVAL-LIVE-001.
- The `triggerClick` + message-pump click check lives only in
  `tools/live-jam-ui` because stopping the JUCE dispatch loop poisons
  `callAsync` for the rest of a shared test binary; the shared tests cover the
  disabled/enabled states directly.
- Windows and the full-product build were not run in this worktree.
