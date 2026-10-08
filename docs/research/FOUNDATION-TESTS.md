# TEST-001 foundation regression expansion

## Scope and authorization

Executed under the user's explicit D8 permission to run this plugin-lane task
now that actual JUCE builds work. Base: `45fa333ed8dc4b81ce392b42b6b37ba678f10176`,
branch `wp/TEST-001-foundation`. `DEVPLAN.md` TEST-001 and
`LOCAL-LINUX-BUILD.md` informed the work. The only additions are the new test
translation unit, the standalone runner, this evidence document, and the task
note. Production behavior and shared build/task files are untouched.

## Meaningful gaps covered

All 12 new cases use the `foundation_` prefix in `tests/DrumFoundationTests.cpp`.

| Case suffix | Regression protected |
| --- | --- |
| `parser_legacy_invalid_numeric_contract` | Exact whole-grid results for `abc`, leading minus, numeric prefix with junk, suffix-only items, reversed range, invalid stride, case-sensitive/unknown voice codes, and overwrite across repeated voice tokens. Existing tests only report some of these quirks. |
| `duplicate_labels_preserve_positional_payloads` | Both ROCK / Shuffle rock factory entries retain distinct rhythm/BPM payloads; applying and saving the second entry does not collapse it to the first by label. No numeric library index is pinned. |
| `meter_fallbacks_and_fractional_grid_contract` | Half-note and sixteenth-note meters, ceiling, accepted nonstandard denominator rounding, constructor defaults, and the distinction between raw `stepsForMeter` fallback and `setMeter` numerator clamping/denominator fallback. |
| `codec_silent_used_and_absent_bar_are_distinct` | Nonempty all-zero or corrupt payload remains a used silent bar; empty payload clears usage and preserves the assigned meter. |
| `codec_reload_meter_first_and_clear_hidden_tail` | Reload from a long meter into a one-step meter uses voice-major ordering, ignores extras, clears every hidden tail cell, and does not resurrect old notes when expanded again. |
| `generator_interleaved_calls_replay_complete_saved_bar` | Intervening generation with other parameters cannot alter replay of zero/max seed in odd meter across four additional roles; full grid and saved-bar replay agree. Existing immediate repeated-seed coverage is retained. |
| `transport_stop_flushes_once_and_restart_starts_bar_zero` | Idle, start, step advancement, stop UI state, nine GM note-offs at offset zero exactly once, and restart after partial playback. |
| `transport_meter_lengths_and_timeline_wrap` | Intermediate bar/step positions across 1/16, 3/8, 1/16, 1/16, then four-bar timeline wrap. |
| `transport_count_in_is_legacy_fixed_four_four` | A one-step opening bar still receives the legacy sixteen-step count-in; MIDI/UI transport begins afterward. |
| `transport_bpm_mid_interval_preserves_pending_deadline` | Mid-interval BPM edit preserves the pending deadline and uses the edited BPM for the interval scheduled at that deadline. |
| `transport_bpm_change_at_bar_boundary_uses_new_next_interval` | Edit exactly before the first hit of bar two retains that hit's deadline and changes the next interval. |
| `transport_block_partition_preserves_unswung_midi_timing` | Equal absolute MIDI times and final UI position with 128-frame and 333-frame partitions across a bar boundary. |

The transport fixture is a real JUCE `AudioPluginInstance` MIDI sink, with
humanization disabled, 8 kHz sample rate, 120 BPM, and zero swing. Thus a
sixteenth is exactly 1,000 samples. It calls the actual engine `process`, never
its private scheduling methods. Startup's eight-sample delay is explicitly
observed. Expected times are independent fixed musical examples, not another
implementation of the scheduling loop.

## Explicit compatibility contracts and limits

- There is **no strict production parser**. Invalid numeric text becoming a
  step-zero hit, `-4` becoming range 0..4, numeric prefixes being accepted,
  suffix-only accents/ghosts, and invalid strides becoming one are now asserted
  compatibility outcomes. A future strict parser needs an approved/versioned
  migration rather than silently tightening these assertions. Extreme integer
  overflow/pathological unbounded ranges are outside these bounded fixtures.
- Factory browser rows use positional `f:<index>` IDs: see
  `DrumOverlay.cpp::rebuildList` and `selectEntry`. Favorites instead resolve
  `(genre,name)` to the **first** match. The test exercises the available library
  and engine payload seams; it does not instantiate the overlay or claim to test
  drag-ID parsing, favorite resolution, or a stable persisted groove UUID.
  No groove-identity ADR was present in the available `docs/adr` tree; the
  regression follows the actual source semantics requested by the assignment.
- The legacy engine has no pending bar-quantized BPM command. Already scheduled
  sample countdowns remain intact; BPM is read when the next step interval is
  scheduled. The boundary test is a deterministic caller-timed edit, not evidence
  of a thread-safe Musical Clock integration. Swing, humanized timing, concurrent
  edits, hardware latency and arbitrary hosted plugin behavior are not measured.
- Missing saved bar fields become empty strings in the processor and can use
  the tested engine seam. Complete APVTS state migration/default handling,
  stale metadata reset, and absent top-level drum fields remain processor-level
  coverage gaps. No copied `ValueTree` loader pretending to exercise the
  processor was added. `clearBar` deliberately preserves meter; full-song reset
  belongs to the processor.

## Standalone build/run

From this worktree:

```sh
PATH=/tmp/opencode/venv/bin:$PATH \
TMPDIR=/home/mojo/projects/guitars-build-resume/tmp \
python3 tools/foundation-tests/run.py
```

The runner is Linux/Ninja-specific and uses the already configured
`GuitarCompanionTests` compiler/linker metadata read-only. Defaults:

- production/test source: `/home/mojo/projects/guitars`;
- prebuilt inputs: `/home/mojo/projects/build-RT-003-integration/product`;
- output: `/home/mojo/projects/guitars-build-resume/foundation-tests`.

Override with `--source`, `--product-build`, and `--output`; the alternate
existing `/home/mojo/projects/guitars-build-resume/plugin` build is also a suitable
configured metadata/input layout. The runner compiles all existing drum tests,
the new foundation file from its own checkout, and all three drum production
sources fresh. It reuses actual configured JUCE module object files and
`libGuitarCompanionAssets.a`, retaining the MIDI heap-wrap probe and system
libraries. It never invokes a product rebuild, compiles JUCE/NAM, or creates
fake JUCE/BinaryData implementations. Logs and SHA-256 input/command manifests
live in the output directory, outside git. Missing prebuilt inputs fail closed.

## Results

Final run against production checkout HEAD
`8f18e03c3f4cdb7a7008b127896a16fa611939a3`:

- **50 cases passed: 38 existing + 12 new; zero failed cases/checks.**
- All five existing suite prefixes plus every new foundation case ran in one
  actual JUCE executable. This includes the existing allocation-capacity probe.
- Build log contains no diagnostics; input hashes, exact compile/link commands,
  and binary hash are recorded in `manifest.json`.
- `results.log` contains full case-by-case output. No failing regression was
  suppressed or converted into an informational-only result.
- Existing duplicate-library report still reports 16 duplicate label pairs;
  the new positional regression demonstrates why automatic deduplication would
  be a behavior change.

## Exact registration recommendation for orchestrator

The root already gates `add_subdirectory(tests)` behind
`GUITAR_COMPANION_BUILD_TESTS`. After review, add the following to
`tests/CMakeLists.txt` (the owner has not edited shared CMake):

```cmake
target_sources(GuitarCompanionTests PRIVATE DrumFoundationTests.cpp)
add_test(NAME drums.foundation COMMAND GuitarCompanionTests foundation_)
```

Then build `GuitarCompanionTests` in the existing actual JUCE test configuration
and run `ctest --test-dir <build> -R '^drums\.' --output-on-failure`. This gives
six registered drum suites. The new file requires no additional definitions,
libraries, include paths, or production seams beyond the existing test target.
