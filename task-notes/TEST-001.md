# TEST-001 — foundation regression expansion

## Authorization / ownership

User explicitly granted D8 permission to execute this plugin-lane task with
working actual JUCE builds. Worktree:
`/home/mojo/projects/worktrees/TEST-001-foundation`, branch
`wp/TEST-001-foundation`, base `45fa333ed8dc4b81ce392b42b6b37ba678f10176`.
Session moved into this worktree. No subagents used.

Owned additions only:

- `tests/DrumFoundationTests.cpp`
- `tools/foundation-tests/run.py`
- `docs/research/FOUNDATION-TESTS.md`
- `task-notes/TEST-001.md`

## Implementation / validation

Added 12 meaningful foundation cases after reviewing all existing drum suites
and TEST-001. Exact lenient parsing, distinct positional duplicate payloads,
meter fallback/rounding, saved silent/absent bars, hidden-tail clearing on meter
reload, interleaved generator replay, and MIDI-observed transport/BPM boundaries
are covered. Full detail and exact CMake registration recommendations are in
`docs/research/FOUNDATION-TESTS.md`.

Command, executed from the worktree:

```sh
PATH=/tmp/opencode/venv/bin:$PATH \
TMPDIR=/home/mojo/projects/guitars-build-resume/tmp \
python3 tools/foundation-tests/run.py
```

Final result: **50 cases, 0 failed cases, 0 failed checks** (38 existing,
12 new). Fresh drum/tests compile against main read-only production source at
`8f18e03c3f4cdb7a7008b127896a16fa611939a3`; existing JUCE object files and asset
archive reused from `/home/mojo/projects/build-RT-003-integration/product`.
No full product rebuild. Artifacts, full output, input hashes and exact commands:
`/home/mojo/projects/guitars-build-resume/foundation-tests/{build.log,results.log,manifest.json}`.
Initial 49-case run also passed before adding the fixed-4/4 count-in regression
and stronger intermediate mixed-meter position checks.

## Limits / handoff

Legacy parsing is now an explicit compatibility contract, not strict parsing.
Legacy BPM edits preserve already scheduled deadlines; no clock-safe/bar-queued
semantics are claimed. Duplicate factory rows are positional; favorites remain
first-label-match UI behavior outside this unit seam. Complete processor state
load/migration is not tested by reproducing its loader. The engine-level empty
bar and meter preservation seam is tested. Hardware/ASIO, arbitrary VST guests,
and concurrent state edits are outside the evidence.

The orchestrator should register `DrumFoundationTests.cpp` on the existing test
target and `drums.foundation` with the `foundation_` filter after review, then
run all six drum CTest suites. No production/shared CMake/ledger/HANDOFF/DEVPLAN
changes were made. Commit SHA is supplied in the agent handoff, avoiding a
self-referential commit hash in this file.
