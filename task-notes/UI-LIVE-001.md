# UI-LIVE-001 — Actual Jam screen, controls and telemetry (first audible wave)

## Goal
Wire the real Jam screen into the app editor against the frozen
`jam::IJamLiveControl` facade: visible Jam button, lazily built overlay, live
telemetry, supported controls, and explicit unavailable/disabled presentation
for everything the frozen command set does not implement.

## Base
`88893e2` on `wp/UI-LIVE-001-jam-screen`.

## Files changed / added
- `src/PluginEditor.h` — forward decls + `JamLivePresenter`/`JamOverlay`/
  `jamButton` members (presenter declared before overlay).
- `src/PluginEditor.cpp` — includes, lazy overlay wiring, 30 Hz poll while
  visible, Escape handling, destructor callback teardown, top-bar layout +
  CPU/divider anchors.
- `src/ui/JamOverlay.h/.cpp` — zero/unavailable defaults, production control
  set, availability text, command feedback, explicit preview opt-in, keyPressed.
- `src/ui/JamLivePresenter.h/.cpp` (new) — facade↔view mapping, whole-state
  reader cache, visible reject/drop feedback.
- `tests/JamLiveUiTests.cpp` (new) — mock-facade mapping + JUCE control tests.
- `tools/live-jam-ui/CMakeLists.txt`, `main.cpp`, `verification.log` (new) —
  isolated harness (tests + deterministic test-fixture snapshots).
- `docs/research/LIVE-JAM-UI.md` (new) — design, semantics, build recipe.
- `docs/screenshots/jam-live-*-testfixture.png` (new, 3 fixtures).
- This note.

## Contract implemented
Message-thread-only presenter over the frozen facade. One coherent UI read per
30 Hz tick; false/unchanged keeps the previous whole state. Intent mapping:
Start/Stop (Stop once the intent is echoed), TapTempo, ResyncNextBeat/Bar,
HalfTime, DoubleTime, FreezeTempo, ResumeFollow, StopAtNextBar, Reset, SetMode.
Unsupported controls (style/intensity/complexity/fill/tightness/fill/break) are
disabled and rejected visibly. No worker joins, renderer setters, raw BPM setter
or plain engine getters. Start is presented as intent; only the audio-owner echo
shows `PLAYING (AUDIO ECHO)`.

## Tests
`tools/live-jam-ui` harness: 9 cases, 0 failed checks, 0 failed cases (see
`tools/live-jam-ui/verification.log`). `PluginEditor.cpp` validated with a real
`-fsyntax-only` compile (exit 0, 0 errors) using extracted product flags + the
current frozen header. The MOD-003 preview still compiles.

## Evidence
`docs/research/LIVE-JAM-UI.md` has the exact build/run commands and semantics;
`tools/live-jam-ui/verification.log` is the final harness output; three
`docs/screenshots/jam-live-*-testfixture.png` render the real presenter + mock
facade (labelled `test fixture`).

## Known limitations
- Root product link is impossible in this worktree until INT-LIVE-001 defines
  the frozen facade methods; the orchestrator must add the new `.cpp` files to
  CMake. No audible/pipeline/device claim is made.
- Legacy `tools/jam-ui-preview` runtime assertions assume removed fake defaults
  and the old demo controls; it compiles and is superseded by `tools/live-jam-ui`.
- Windows/full-product builds not run here.

## Commit
The commit containing this note is the implementation/evidence commit; its exact
SHA is returned to the orchestrator after committing.
