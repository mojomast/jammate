# EVAL-LIVE-001 second correction contract (additive, frozen before reevaluation)

**Task:** EVAL-LIVE-001 · **Base:** `88893e24be328f131b5df673078ff934a46ed5ab`
**Superseded tools:** `a3ecf1f` (BLOCKed by the second independent review)
**Previous contract:** `a293c20` / `CORRECTION-CONTRACT.md`
**Amendment:** `tools/live-jam-replay/protocol-amendment-2.json`

Committed **before** any second-corrected tool is reevaluated. Additive: the
original `predeclared.json`, `protocol.sha256` and every pre-existing artifact
under `docs/research/live-jam-replay/` remain byte-for-byte unchanged.

## N1 — readiness bootstrap (critical)

`readJamLiveState` before any `prepareToPlay` reads an unpublished latest-value
slot, so `backendKind=unknown`/`usable=false` was persisted and a good product
was reported `awaiting-backend`. The harness now calls `prepareToPlay` first
(proper cold publish), then polls `readJamLiveState` off-callback for up to
~2 s (1 ms sleep, bounded attempts) for a coherent prepared tag, then
`releaseResources()` before the cells. The bootstrap is outside the armed region
and excluded from counters. The default actual backend must be exactly
`experimentalBTrack`; no arbitrary injected identity. Instrument/support/facade
self-check failure fails closed before any measurement (build and runner).

## N2 — findings set

One finding per phase when ANY allocator/free/lock family occurred (authoritative
C++/C + delete/cfree + lock/trylock/unlock/cond), so free-only and lock-only runs
are consistent between harness and validator. Overflow cannot be clean.
Correctly reported findings are a real RT gate failure (`measured-findings`),
reported as such, never as an invalid-identity error.

## N3 — scenario gates

Full scope requires both preregistered scenarios. `default_clean_long` must have
run with the actual `experimentalBTrack` backend and real audio-owner
advancement. `injected_join_stop_resync` must have run with the `injectedTest`
backend and actual processor/clock/renderer evidence. If the injection seam is
absent, full scope is callback-coverage-only and `join_proof_unavailable` is a
gate FAIL with a known reason code — never a hard pass. Smoke may omit scenarios
and is clearly partial; it never claims first-audible verified.

## N4 — override allowlist

Exactly three changed pipeline compile seams may be overridden, pinned to exact
hashes:

| path | sha256 |
|---|---|
| `src/jam/DrumClockBridge.h` | `c115eb8b24f0918f7a375f6507d094cf94d5a0fc99019bba6cf5b3b4459d1025` |
| `src/PluginProcessor.h` | `3e1958e3eccc3892304f8a42c28a1c7cdf711c226e7ec91082211b0d80059930` |
| `src/PluginProcessor.cpp` | `21e3d7ad2cb8a0d5f8fb4f068ee82ac3332a0bf8a4cdaaede062bca146e76e66` |

Unknown paths → `override_unknown_path`; immutable headers → `override_immutable`
(and must always match their original pins); duplicates → `override_duplicate`;
non-preregistered hash → `override_hash_not_preregistered`; file mismatch →
`override_mismatch`. Paths are canonical. The orchestrator's
`docs/research/live-jam-replay-source-pins.json` (schema
`source-pin-overrides/1.0`) is the conforming input; `live-jam-bridge-pin-amendment.json`
is a chronology document, not an override input.

## N5 — link metadata required

Complete real link metadata is required. If the metadata tool is unavailable,
fail closed with `missing_link_metadata_tool`; never guess an incomplete static
list. GNU/Linux tool scope only.

## N6 — reported cursor start

Record the first snapshot cursor before the `haveReported` flag, via a tested
helper. A genuinely cold zero is valid; a forged zero is not.

## N7 — timeout partial

A truthful timeout may report `measured_partial=false` with counters unmeasured
(not zero); `measured_partial=true` requires a preserved parsed cells file with
its exact hash. Logs are preserved with an exact hash; the deadline is ≤300 s. A
timeout is a whole-goal failure and never a gate pass.

## N8 — audio-owner gate

Every measured cell must have `audio_owner_measured=true` with real advancement:
`audio_owner_start == block`, `audio_owner_end == (warm_blocks+1)*block`,
`audio_owner_end > audio_owner_start`, `audio_owner_delta_ok`, no
mismatches/backwards. Missing/false/forged-zero fails.
`identity.source.immutable_pins_ok` must be true.

## Gates

`structural` (schema/scope/matrix/counters/identity/fixtures/overrides) AND
`rt_gate` (no callback findings) AND `join_gate` (full scope requires the
injected join scenario). The CLI passes only when all three pass.

## Scope

Only new/changed files under `tools/live-jam-replay/**`,
`tests/LiveJamProcessorTests.cpp`, `docs/research/LIVE-JAM-REPLAY*.md`,
`docs/research/live-jam-replay/**` and `task-notes/EVAL-LIVE-001.md`. No
processor, editor, `JamLiveInterface`, engine, core, shared CMake/CI or ledger
source. No agents. No actual run.
