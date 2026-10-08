# UI-LIVE-001 — Actual Jam screen, controls and telemetry (corrected)

## Goal
Wire the real Jam screen into the app editor against the frozen
`jam::IJamLiveControl` facade: visible Jam button, lazily built overlay, live
telemetry, supported controls, explicit unavailable/disabled presentation for
unimplemented controls, and primary controls fully visible on the real 1100x700
canvas.

## Base
`88893e2` on `wp/UI-LIVE-001-jam-screen`.

## Files changed / added
- `src/PluginEditor.h/.cpp` — Jam button, lazy overlay + presenter wiring, 30 Hz
  visible-only poll, Escape, callback teardown, top-bar layout.
- `src/ui/JamOverlay.h/.cpp` — zero/unavailable defaults, compact 1100x700
  layout (action strip before advanced controls), exact status line, `--`
  unknown beat, preview opt-in, `close()` fires once, keyPressed.
- `src/ui/JamLivePresenter.h/.cpp` (new) — facade↔view mapping, whole-state
  reader cache, accepted-intent latch for fast double-click, status strings.
- `tests/JamLiveUiTests.cpp` (new) — 14 mock-facade mapping/control/real-canvas
  cases.
- `tools/live-jam-ui/**` (new) — harness (tests + async click self-check +
  1100x700 cold/armed/playing/demo snapshots) and `verification.log`.
- `tools/jam-ui-preview/main.cpp` + `README.md` +
  `verification-live-adapted.log` — explicit demo opt-in so the MOD-003
  self-checks pass without fake production defaults; original
  `verification.log` preserved as historical base proof.
- `docs/research/LIVE-JAM-UI.md`, `docs/screenshots/jam-live-*-testfixture.png`
  (4), this note.

## Contract implemented
Message-thread-only presenter over the frozen facade. One coherent UI read per
30 Hz tick; false/unchanged keeps the previous whole state. Intent mapping:
Start/Stop, TapTempo, ResyncNextBeat/Bar, HalfTime, DoubleTime, FreezeTempo,
ResumeFollow, StopAtNextBar, Reset, SetMode. Unsupported controls are disabled
and rejected visibly. No worker joins, renderer setters, raw BPM setter or plain
engine getters.

The label and action share one `effectiveDesired` bit: a pending local request
overrides the engine echo until a fresh coherent state acknowledges it, otherwise
the live request/play echo decides. A recreated editor with the engine running
offers Stop first; a same-generation engine stop syncs back to Start; a queued
Stop overrides the old play echo for a deliberate restart while the status still
shows `STOP QUEUED`; rejected commands retain the prior desired bit; prepared
release / generation change / worker failure / unavailable backend clear the
pending bit and are reported truthfully. Status separates accepted intent
(`START/STOP QUEUED`, `ARMED`) from the audio echo (`PLAYING (AUDIO ECHO)`).
The frozen facade has no command-ack sequence, so an unobservable coalesced
Start/auto-stop cannot be deterministically acknowledged (documented).

## Tests
`tools/live-jam-ui`: 20 cases, 0 failed checks/cases, plus the async
`triggerClick` self-check (enabled fires, disabled ignored) — see
`tools/live-jam-ui/verification.log`. Legacy preview self-checks pass
(`intents=21`, exit 0) — `tools/jam-ui-preview/verification-live-adapted.log`
(re-run only if overlay widgets change; this round changed the presenter and a
`PillButton` fitted-text detail). `PluginEditor.cpp` real `-fsyntax-only`
compile: exit 0, 0 errors.

## Evidence
`docs/research/LIVE-JAM-UI.md`; harness and preview receipts; four 1100x700
`test fixture` snapshots rendered through the real presenter + mock facade.

## Known limitations
- Root product link waits on INT-LIVE-001 defining the frozen facade; the
  orchestrator must add the new `.cpp` files to CMake. No audible/pipeline/device
  claim.
- The `triggerClick` pump check is harness-only (stopping the JUCE dispatch loop
  poisons `callAsync` for the rest of a shared test binary).
- Windows/full-product builds not run here.

## Commit
The commit containing this note is the corrected implementation/evidence
commit; its exact SHA is returned to the orchestrator.
