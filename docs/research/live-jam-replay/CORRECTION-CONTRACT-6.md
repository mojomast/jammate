# EVAL-LIVE-001 sixth correction contract — real resync phase proof

**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` · **Superseded tools:** `35cd3f8`
**Amendment:** `tools/live-jam-replay/protocol-amendment-6.json`
**Preserved:** actual-full-001 (read-only) and amendment 5 + its receipts (immutable history)

Committed before the corrected resync proof is reevaluated. Additive.

## The false positive

The injected scenario submitted `ResyncNextBar` after `StopNow` and then started
a third join; any new step plus a last-step change set `resync_effect_observed`,
so an **ordinary join passed even if Resync was ignored**.

## The meaningful phase proof

After the second-join `StopNow` proof:

1. Start a **third actual join** (paced); wait for engine `injectedPlaying` plus
   new steps, with **no resync yet**.
2. Advance paced callbacks until the baseline `injectedNextStep()` is in `[2,14]`
   (not the downbeat `1`) and the engine is playing. Record baseline
   `injectedNextStep()`, `injectedLastStepSample()`, `injectedCommandCount()` and
   `injectedSamplePosition()` (submit cursor).
3. Submit `ResyncNextBar` **alone** (no concurrent Start).
4. Advance paced callbacks (bounded 2 s) until `injectedCommandCount()` advances
   and `injectedStepsFired()` advances while playing.
5. Assert `injectedNextStep()==1` (step 0 fired),
   `injectedLastStepSample()` in `[submitCursor, audioOwnerAfter)`, and a
   positive command-count delta.

`requestResyncNextBar` sends a `ResyncBar` at the current worker cursor; the
engine rephases so step 0 fires at the next serviced callback. The baseline
phase in `[2,14]` proves a normal next step would not be 0, so the observed
step-0 event is attributable to the resync, not to an ordinary join.

Recorded fields: `resync_phase_before`, `resync_phase_after`, `resync_step_sample`,
`resync_submit_cursor`, `resync_observed_end`, `resync_owner_command_delta`,
`resync_effect_observed`.

## Gate

The injected join gate requires `resync_phase_before in [2,14]`,
`resync_phase_after == 1`, `submit_cursor <= step_sample < observed_end`,
`owner_command_delta >= 1` and `resync_effect_observed true`. Missing fields fail
even if `resync_effect_observed` is true. The default-long gate remains identity
plus actual audio-owner advancement and does **not** require injected-specific
fields; the paced/wall checks apply to the injected scenario only.

Semantics unchanged (`structural AND rt_gate AND join_gate`; default no-lock
diagnostic). A truthful actual-002 run is required; failures are never hidden.

## Scope

Only `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md`. No processor/editor/engine/core/shared CMake/UI or
ledger source. No agents. Link only; no measurement from the worker.
