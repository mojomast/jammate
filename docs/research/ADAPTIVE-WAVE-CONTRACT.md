# Adaptive build wave contract

Base: published `c8f87a81b257529e76258615c6b1d981a048fbcf`, 2026-10-08.
The user authorised Flash workers to build the remaining work. This wave advances
software foundations and live adaptation; representative guitar and physical
device acceptance require measured evidence and remain separate gates.

## Ownership

- STYLE-DIRECTOR-002: new JUCE-free `StyleCatalog.*`, `JamDirector.*`, their
  portable tests, catalogue provenance and task note.
- DRUM-ADAPT-002: `DrumEngine.*`, `DrumClockBridge.*`, actual-engine and portable
  adaptation tests, standalone test driver, task note.
- EVAL-GUITAR-009: new `tools/guitar-lock-eval/`, protocol, tests, task note.
- DEVICE-002: new `tools/device-validation/`, physical-play protocol, tests,
  task note.
- Orchestrator: build/CI registration, live session, processor/editor/UI,
  integration, documentation, ledger and publication.

Workers use isolated worktrees and commit handoffs; they do not edit shared
build definitions, earlier evidence, or governing documents. Catalogue references
use the existing library's positional indices, with identity/spec provenance.

## Adaptive seam

The existing `QueuedBarChange` in `IDrumTransport.h` is the musical payload:
groove/fill indices, intensity, swing and humanization. No detector tempo is
included. `DrumClockBridge::requestBarChange(const QueuedBarChange&) noexcept`
publishes a bounded command for the next strictly future bar and returns false
on rejection. Only a successful publication commits a director decision.
`requestFillAtNextBar(LibraryIndex)` may be a convenience wrapper. Worker/API
state must not claim a musical change when the queue rejects it.

The engine prepares a bounded immutable bank of library patterns off the audio
callback. Normal changes are exact-sample bar events; a fill is one bar, then
returns to the selected groove. Prepared bank validity includes index, fill kind
and supported meter. Clear/Stop cancel pending changes and fills; stopping
releases injected ownership. Existing manual/song transport stays compatible.
No callback parsing, heap allocation, locks, messages or worker joins.

`StyleCatalog` is JUCE-free and overlays at least Rock, Hard Rock/Metal, Blues,
Funk, Pop and Shuffle. It exposes fixed bounded descriptors with groove tiers,
fills, supported meter/BPM, repetition and humanization defaults. Every shipped
reference is checked against the actual compiled library, not guessed by name.

`JamDirector` consumes clock, input energy/onsets, settings and explicit transport
position/audio echo. It emits a proposed `QueuedBarChange` and `JamIntent`;
the caller acknowledges successful publication before selection/repetition state
advances. Decisions use deterministic seeded selection and phrase boundaries,
low-confidence fill suppression, gradual intensity and complexity tiers. Existing
live join/stop lifecycle remains the authority until integration tests support any
replacement. Expose an inspectable state machine and reset at session boundaries.

## Evidence boundaries

Guitar evaluation must distinguish real, synthetic and derived input; require
provenance, hashes, annotation and event/horizon/receipt timing. Useful lock means
correct tempo AND usable phase within the specified two-bar window, rather than
merely a `Locked` label. Missing/invalid/empty data cannot pass the >=95% gate.
Any tuning is a diagnostic candidate, never a silent production promotion.

Device tools must retain raw observations, source/device/config identity and
explicit measured/unmeasured fields. Callback measurements do not establish
physical monitoring latency. An empty template cannot pass hardware/play/ASIO
gates; synthetic self-tests prove the validator only.
