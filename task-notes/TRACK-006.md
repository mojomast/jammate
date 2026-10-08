# TRACK-006 — unchanged variant paired robustness

## Base, ownership and executed scope

Resumed with authorized OpenAI Sol after the prior Flash provider balance stop.
Base `main` `45fa333`; branch `wp/TRACK-006-variant-robustness`, explicit workdir
`/home/mojo/projects/worktrees/TRACK-006-variant-robustness`. Session moved here.

Owned work: new `tools/tempo-variant/run_paired.py`, new
`tools/tempo-variant/tests/test_paired.py`,
`docs/research/tempo-variant-robustness/**`,
`docs/research/TEMPO-VARIANT-ROBUSTNESS.md`, this note. Fixed variant, adapters,
gates, historical evidence and corpus remain unchanged. No processor/root
CMake/ledger/HANDOFF/DEVPLAN edits, builds, downloads or subagents.

Read SPEC, ledger, TRACK-005 and EVAL-005/EVAL-007 notes/reports/pins, and the
existing robustness extraction tool. Reused the pinned read-only EVAL-005
default plugins, TRACK-005 integration variant plugin, EVAL-007 integration
current CLI and TRACK-004 integration diagnostic. Actual binary/source hashes
verified; adapter sources match the EVAL-005 read-only build export. Exact pins
and executed argv are in the new `provenance.json`.

## Results

- Executed **3 × 24** fresh current CLI scores and three auxiliary beat-series
  runs, at **128 frames, uncompensated**, over exactly the unchanged manifest:
  **22 perturbations + 2 matched parent windows**. All 24 WAV hashes/sizes/
  framing checked; same-parent and exact source truncation enforced. Derived
  manifest matches its EVAL-005 pin; both original parents re-hashed and baseline
  PCM compared byte-for-byte to the declared original source slices.
- Default BTrack/aubio reproduce all 24 EVAL-007 historical per-fixture fields
  except CPU time. Current structural-noise coverage retained.
- **24/24 exact byte-identical default/variant beat series**; current variant
  method-log event and availability tuples match the diagnostic series exactly.
- **3240 paired metric rows**, no hard errors or duplicates. Measured/missing/
  noise-unassessed: BTrack 868/202/10; variant 853/217/10; aubio 890/180/10.
- Acquisition counts per clip: BTrack **6/24**, variant **1/24**, aubio **15/24**.
  Variant loses six, gains one (40 ms offset). No core denominator or release
  pass claim. Whole-clip steady BPM within 2%: 7/21, 12/21, 20/21 respectively.
- **Actual regressions:** gap error 2.3438% → **34.3751%**, variant locked BPM
  82.68734; 1.4860625-second emitted interval is accepted inside the frozen bound.
  Clean 0 dB noise error 0.0381% → **11.7945%**, acquisition lost. Funk noise
  10/0 dB error 1.8243% → **2.8568%**, acquisition lost. Clipping 0.25/0.125
  and funk baseline lose acquisition despite improving whole-clip BPM error.
- 23 clips become ready; −60 dB emits no beats and never becomes ready. Every
  fallback verified equal to base BPM; missing intervals empty. No invalid/gap
  reset appears in these runs. Stale retention explicitly counted. Raw log is
  losslessly compressed and hash authenticated.

## Validation and commands

Environment for every command:
`PATH=/tmp/opencode/venv/bin:$PATH`,
`TMPDIR=/home/mojo/projects/guitars-build-resume/tmp` (system `/tmp` full).

```bash
python3 tools/tempo-variant/run_paired.py
python3 tools/tempo-variant/tests/test_paired.py
python3 -m unittest discover -s tools/tempo-variant/tests -p 'test_*.py'
python3 tools/rhythm-eval/tools/test_run_robustness.py
/home/mojo/projects/build-TRACK-005-integration/TempoVariantTests
bash tools/tempo-variant/check_failclosed.sh /home/mojo/projects/build-TRACK-005-integration /home/mojo/projects/build-EVAL-005/main-core
```

Results: runner **PASS**, 9 new meaningful fail-closed/audit tests **OK**,
17 total variant Python tests **OK**, 29 robustness tests **OK**, 67 C++ checks /
0 failures; 22 fail-closed CLI checks / 0 failures. Existing framing-negative
tests print expected rejection messages.

The initial run's validation caught that the current CLI reuses one instance
with resets, unlike the historical diagnostic's per-fixture instances. Corrected
the new tool to validate expected block counts/reset boundaries and split the
one raw log. Preliminary generated trees were moved to approved project scratch;
final evidence was generated fresh after the validation improvements, including
the independent parent-window PCM check.
No result was inferred from the failed validation. Raw CLI results, logs and
hashes are retained in the final evidence tree (~3.91 MiB before receipts).

## Limitations and next action

Synthetic short source windows, correlated perturbations, no core release gate,
no clock/audition/live hardware. Noise silence remains inherited/unassessed;
tempo-step nominal BPM and most silence acceleration are missing. The measured
gap acceleration proxy remains 0 despite the low-BPM regression. Syncopation
proxies remain absent on derived names. Same framing does not equalize internal
resampling; logging makes CPU comparisons resource diagnostics. Historical
original sustained-chord sub-octave/acceleration regression is not retested here.

G3 **OPEN**. Next: separately predeclare a longer paired sparse/gap/noise
characterization and any proposed guard as a new named/frozen variant; preserve
this run and regular-material gains. No backend selection or production wiring.

Implementation/evidence commit: recorded in the final worker response (a commit
cannot embed its own SHA); `git log -1` identifies the final handoff commit.
