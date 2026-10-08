# STYLE-DIRECTOR-002

## Goal

Deliver the JUCE-free adaptive style overlay and the deterministic Jam Director
core for the adaptive build wave: a bounded `StyleCatalog` over the actual
compiled drum library and a bounded `JamDirector` that consumes clock,
energy/onsets, settings and the explicit transport/audio echo, proposes a
`QueuedBarChange` + `JamIntent`, and advances musical state only after the caller
acknowledges a successful publication. Live join/stop lifecycle remains
`JamJoinPolicy`'s.

## Base

`984ad1d32c44157ad2d4128c47b86dc932535918` (branch `wp/STYLE-DIRECTOR-002`).

## Files changed

New files only; no shared/build/CI/ledger/governing/live-pipeline/UI/engine file
was edited.

- `src/jam/StyleCatalog.h`, `src/jam/StyleCatalog.cpp`
- `src/jam/JamDirector.h`, `src/jam/JamDirector.cpp`
- `tests/jam/StyleCatalogTests.cpp`, `tests/jam/JamDirectorTests.cpp`
- `tools/style-catalog/style_catalog_provenance.py` (verifier/regenerator)
- `tools/style-catalog/build_style_catalog.py` (one-shot authoring generator)
- `tools/style-catalog/README.md`
- `tools/style-catalog/generated/StyleCatalogLibraryFixture.h` (generated)
- `tools/style-catalog/library_provenance.json` (generated)
- `tools/style-catalog/tests/test_style_catalog_provenance.py`
- `docs/research/style-director/STYLE-CATALOG.md`
- `docs/research/style-director/JAM-DIRECTOR.md`
- `task-notes/STYLE-DIRECTOR-002.md` (this file)

## Contract implemented

### StyleCatalog

- Six styles in the fixed UI order: `Rock=0`, `HardRockMetal=1`, `Blues=2`,
  `Funk=3`, `Pop=4`, `Shuffle=5` (frozen by a test).
- Each descriptor: BPM min/ideal/max, supported meters (4/4), low/medium/high
  groove lists, short/long/transition fill lists, humanization defaults, swing
  range + default, minimum repetition distance.
- Every reference is `(index + actual genre/name/fill/meter/specHash)`, never a
  name. 123 references over 106 distinct indices.
- `StyleCatalog::validate(ILibraryProbe)` checks every reference against the
  actual library and reports the first issues.
- `StyleCatalog::libraryFingerprint()` freezes the sha256 of
  `src/DrumLibrary.cpp`.

### JamDirector

- Bounded, JUCE-free, no allocation, fixed arrays, seeded SplitMix64.
- States: `Idle/Listening/ReadyToJoin/Playing/Holdover/Reacquiring/Stopping`.
  Proposals only while `Playing`, clock Locked and
  `lifecycleAllowsPerformance`.
- Emits one `QueuedBarChange` + `JamIntent`; retries the same change until
  `acknowledgePublication()` reports the bridge result; commits on `true`,
  retries on `false`.
- `cancelPending()` forgets an unacknowledged proposal (auto on Stop, Lost,
  discontinuity, session-generation change and lifecycle disallow) without
  touching live queues. `notifyStopRequested()` is the only stop action and it
  never stops the transport.
- Settings: style/intensity/complexity/fill amount + edge-latched request fill
  and request break.
- Musical policy: gradual intensity and complexity envelopes, hysteresis tiers,
  sustained-energy promotion to High, phrase-boundary groove rotation, fill
  probability with phrase/onset bonuses, hard fill suppression below the
  confidence threshold and scaled suppression between thresholds, minimum fill
  gap, anti-repeat window.
- Idempotent per audio cursor: a repeated non-zero `audioCursor` does not advance
  the envelope, phrase counter or RNG and does not re-decide a committed bar; it
  still re-emits a pending proposal.
- `report()` exposes state, settings, tier, committed/pending groove+fill,
  pending change, envelopes, bar count and counters.

## Tests

Portable core build (JUCE-free):

```sh
cmake -S jam-core -B /home/mojo/projects/build-STYLE-DIRECTOR-002 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/mojo/projects/build-STYLE-DIRECTOR-002 --target jamTests -j 2
ctest --test-dir /home/mojo/projects/build-STYLE-DIRECTOR-002 -R 'jam\.(stylecatalog|jamdirector)' --output-on-failure
```

Results at handoff (after the independent-review fixes):

- `jam.stylecatalog`: 12 tests / 285 checks, 0 failed.
- `jam.jamdirector`: 44 tests / 1002 checks, 0 failed.
- Whole portable binary `jamTests`: 318 tests / 214037 checks, 0 failed.
- `python3 -B -m unittest discover tools/style-catalog/tests`: 8 tests OK.
- `python3 tools/style-catalog/style_catalog_provenance.py --check`: OK,
  123 refs checked, 106 distinct indices.

Coverage highlights: exact-reference validation against the frozen actual
library; duplicate-name traps (`FUNK/Linear funk` 63 vs 187, `ROCK/Shuffle rock`
9 vs 535); spec-hash vectors; tier/fill population; corruption and missing-index
detection; UI style order; idle/no-lock behaviour; ReadyToJoin -> Playing on
echo; rejected-publication retry without commit; repeated-cursor idempotency;
Stop/Lost/discontinuity/lifecycle forget pending; session reset; settings
sanitization; gradual envelopes; sustained-energy tier gate; explicit fill/break;
low-confidence fill suppression and moderate-confidence non-suppression;
anti-repeat; phrase counting; deterministic seeds; style switch; `publishPending`.

Independent-review repairs (all in owned director/tests/docs):

- **F1** One authoritative bar source: the clock phase advances bars; the
  transport only confirms and re-anchors on skip/backward. Regressions cover a
  lagged transport increment, transport-only increments, skip/backward and a
  flagged resync discontinuity.
- **F2** `enforcePendingSafety` runs on every tick (including a repeated audio
  cursor) and cancels a pending proposal on lifecycle-disallow, non-Locked clock
  or a fill whose confidence fell below `fillConfidenceThreshold`; the idempotent
  fast path now runs the state machine and safety checks before holding
  envelope/phrase/RNG. `publishPending` refuses outside `Playing`. Regressions:
  failed full-queue fill -> Holdover on the same and a different cursor,
  low-confidence cancellation, lifecycle-disallow on a duplicate cursor,
  `publishPending` refusal.
- **F3** `Stopping` is documented terminal; `notifyStopCompleted()` is the
  documented exit on the actual stopped echo (parent-reset path also works).
  Regressions: transient echo does not revive Playing; clean restart re-proposes;
  `reset()` from Stopping returns to Idle.
- **F4** The per-style `minRepetitionDistanceBars` is consumed (global config is
  only a fallback when the style value is <= 0); `effectiveRepetitionWindowBars()`
  is exposed and tested. BPM band and swing range are documented as advisory
  hints, not clamps; the clock remains the sole tempo authority.
- **F5** `report().intent` now carries `requestCrash` and `sectionIndex`, and
  `DirectorReport` exposes `pendingBreak`/`pendingCrash`/`repetitionWindowBars`.
  Added tier-demotion, minimum-fill-gap and reset/cancel coverage. Docs note that
  the `QueuedBarChange` payload does not render break/crash.

## Evidence

- Library fingerprint: `68008b4b139750e337a6166f6881e5724eee577b196efd8f467bf9562153d636`
  (`sha256` of `src/DrumLibrary.cpp`).
- Generated fixture: `tools/style-catalog/generated/StyleCatalogLibraryFixture.h`
  (`sha256 443bfd0b96139688c671bdd268def466aa377c72c5f218b52f81977f35f33d24`).
- Manifest: `tools/style-catalog/library_provenance.json`
  (`sha256 9a366d9c3c5a96e731ce1cbb133397790a2e0b0fc3ac8609ef2145190f2a4975`).
- `src/jam/StyleCatalog.cpp`
  `sha256 400a277b356779de04996c15a1281d27cb83140d2eb97ffe55a8a2530678ac5d`
  (unchanged across the fix).
- `src/jam/JamDirector.cpp` — **historical receipt at the implementation commit
  `16db474`** (superseded by the fix commit below):
  `sha256 cb3cf5b20ba310cc8ff91465db65e058ef5e0fbf4d49d05695d4676c3ce832ed`.
- `src/jam/JamDirector.cpp` — **corrected artifact at the fix commit `64a8de9`**
  (current shipped bytes, matches the working tree):
  `sha256 13433f3f4a0dd081f389aeeb590d8ecca48120f4f47d31ae9ffddff6fd690768`.
- Headers changed by the fix commit `64a8de9` (current shipped bytes):
  `src/jam/JamDirector.h`
  `sha256 1820b63983a168fefc7ae8968da0f5e7e9fd8b59280d79339b8cd5f8a63f8e75`;
  `src/jam/StyleCatalog.h`
  `sha256 65a43e849b67f5399d186d7e90b3c2ecf3acd86972c75c90d48e574991928e38`.

All other Evidence artifacts (library fingerprint, fixture, manifest,
`StyleCatalog.cpp`) are byte-identical across `16db474` and `64a8de9`.

## Known limitations

- `JamDirector` is not wired into `LiveJamSession`/`DrumClockBridge`; that is the
  orchestrator's integration task. The adaptive seam names
  `DrumClockBridge::requestBarChange(const QueuedBarChange&)`, which did not
  exist in the bridge at handoff time, so the director exposes the proposal and
  an acknowledge/publish helper instead.
- Only 4/4 is modelled; odd-meter library patterns are deliberately unreferenced.
- The catalog is a curated 106-index subset, not an exhaustive classification.
- A library edit requires re-running `--emit` and re-reviewing the selection;
  `--check` fails until then.
- No CMake edit was made (ownership), so the style-catalog Python tests are not
  auto-registered with ctest; the orchestrator can add
  `python3 -B -m unittest discover tools/style-catalog/tests` as a test, and
  `style_catalog_provenance.py --check` as a gate.

## Integration notes

- Fixed style order matches the appended `SetStyle 0..5` command and the UI.
- Set `DirectorInputs.sessionGeneration` from the live session generation,
  `audioCursor` from the audio cursor, `lifecycleAllowsPerformance` from
  `JamJoinPolicy::requestedRunning() && !stopPending()`, `discontinuity` from the
  session's discontinuity flag, and `playbackEchoPlaying` from the drum echo.
- On publication, call `bridge.requestBarChange(decision.barChange)` (bool) and
  pass the result to `acknowledgePublication(accepted)`; or use
  `publishPending([&](const QueuedBarChange& c){ return bridge.requestBarChange(c); })`.
  The helper refuses to publish unless the director is safely `Playing`.
- The director's bar count advances from the clock phase only. Pass the explicit
  transport position for confirmation/re-anchor; do not expect it to add bars.
- Call `cancelPending()` on user Reset; Stop/Lost/Holdover/low-confidence/
  discontinuity are handled internally.
- On `Stopping`, after the actual stopped echo (`!playing && !policy.stopPending`)
  call `notifyStopCompleted()` to return the director to `Idle` (or
  `reset(settings)`), then restart normally.
- Call `director.reset(settings)` at every session boundary and `report()` for UI
  telemetry (settings, performance intensity, committed/pending groove+fill,
  break/crash, repetition window).
- Recommendation (parent owns `JamConfig.h`): promote the named director policy
  defaults (tier edges, phrase length, min fill gap, neutral fill amount, onset
  bonus, complexity gains) into `DirectorConfig`.

## Commit

Implementation commit (contains `src/jam/StyleCatalog.*`,
`src/jam/JamDirector.*`, the two portable test suites, `tools/style-catalog/`
and `docs/research/style-director/`):
`16db474cf828f096d06b9755a7323548182a2f50`.

Independent-review fix commit (F1-F5):
`64a8de948df4d67b14c9660accfe9a7ab61a0b52`.

The tip of `wp/STYLE-DIRECTOR-002` is the immediately following documentation
commit that records this line; the worktree is clean.
