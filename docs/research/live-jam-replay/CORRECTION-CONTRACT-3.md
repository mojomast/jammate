# EVAL-LIVE-001 third (narrow) correction contract

**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` · **Superseded tools:** `852ddb6`
**Amendment:** `tools/live-jam-replay/protocol-amendment-3.json`

Committed before the narrowly-corrected tools are reevaluated. Additive: all
prior protocol/pin/receipt/contract files are preserved byte-for-byte; new pins
and receipts are written to new files.

## The one outcome-recording bug

`runDefaultCleanLong` assigned `backend_kind` only inside
`!joinObserved && drumsPlaying`. A legitimate 16 s default run that never
acquires a lock therefore wrote an empty `backend_kind` and failed the identity
check falsely.

Fix: capture the actual default backend from the **first coherent prepared
state** after `prepareToPlay` (and each successful read), independent of join.
Label `experimentalBTrack` only when the field actually is that; preserve
unavailable/unknown/failure as recorded (never blind-copy the bootstrap signal).
Track first vs last observed; a change of the actual backend tag within one
prepared session is a fail-closed identity discrepancy, and a wrong identity is
never overwritten by an eventual experimental value.

`joinObserved`, `steps_fired` and audio metrics remain **separate measured
outcomes**. A default run that does not acquire a lock in 16 s is a legitimate
diagnostic quality outcome, recorded exactly and not tuned after the run. The
first-slice join proof is the injected scenario; no ≥95% claim is made.

## The timeout input gap

`run_replay --timeout-s` must reject non-finite, ≤0 or >300 before invoking any
subprocess (exit 64, consistent with the preregistered cap). The default 300 is
unchanged; a user override can never exceed the frozen bound.

## Scope

Only `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md`. No processor/editor/engine/core/shared CMake/UI
or ledger source. No agents. Link only; no actual run.
