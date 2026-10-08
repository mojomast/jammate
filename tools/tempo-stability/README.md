# tools/tempo-stability — TRACK-008 post-readiness tempo-stability candidate

Diagnostic-only, JUCE-free decorator over the pinned `jam::BTrackBackend` that
changes exactly one observation field (`bpmCandidate`) to test whether a
**confirmation gate** removes the early post-readiness derived-BPM excursions
identified in the corrected TRACK-007 evidence.

- `TempoStable.{h,cpp}` — the decorator: median-of-4 core plus a confirmation gate
  (three agreeing full-ring updates within 2 %, the scorer's frozen BPM agreement
  band). Base BPM is forwarded until confirmed; any ring reset drops confirmation.
- `MethodLog.{h,cpp}` — per-block auditable CSV for the confirmation timeline.
- `TempoStablePlugin.cpp` — dlopen plugin, id `btrack-tempo-stable`, env
  `JAM_TEMPO_STABILITY_LOG_DIR`.
- `build.sh` — builds the plugin and the C++ method tests against the pinned
  EVAL-005 main-core archives. No CMake file is touched.
- `freeze.py` — writes `docs/research/tempo-stability/FREEZE.json` (candidate
  source + binary + embedded-dependency pins). Must be committed before scoring.
- `run_stability.py` — the predeclared paired evaluation over the preserved short
  (TRACK-006/EVAL-003 derived-24) and long (TRACK-007 16) matrices for default
  BTrack, the old fixed variant, the new candidate and aubio.
- `summarize.py` — regenerates `docs/research/tempo-stability/tables.md`.
- `tests/` — deterministic C++ method tests and independent Python spec/evidence
  tests.

Method, matrix, bands, missingness and failure criteria are frozen in
`docs/research/tempo-stability/PROTOCOL.md`. This is not a production candidate;
G3 stays OPEN.

```bash
PATH=/tmp/opencode/venv/bin:$PATH bash tools/tempo-stability/build.sh
/home/mojo/projects/build-TRACK-008-worker/TempoStableTests
python3 tools/tempo-stability/tests/test_stability_method.py
python3 tools/tempo-stability/freeze.py          # commit FREEZE.json first
python3 tools/tempo-stability/run_stability.py
python3 tools/tempo-stability/summarize.py
python3 tools/tempo-stability/tests/test_stability_evidence.py
```
