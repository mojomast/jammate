# EVAL-007 silence-coverage artifact

Machine-readable evidence for the bounded silence-coverage correction described
in [`../SILENCE-COVERAGE.md`](../SILENCE-COVERAGE.md).

This directory was new for EVAL-007 and nothing under `testdata/`, `src/`,
`vendor/`, the corpus generators, the shared CMake files, the robustness
aggregator, the execution ledger or `HANDOFF.md`/`DEVPLAN.md` was modified.

## Contents

| path | what it is |
|---|---|
| `comparison.json` | per (corpus, backend) prior/new `results.json` hashes, the non-coverage diff count (all 0), the exact coverage changes, and the gate state |
| `comparison.md` | human-readable form of the above |
| `raw/original/<backend>/block128/` | CLI run over the 19-fixture EVAL-001 corpus |
| `raw/repaired/<backend>/block128/` | CLI run over the 19-fixture EVAL-006 repaired-sustain corpus |
| `raw/derived/<backend>/block128/` | CLI run over the 24-clip EVAL-003 derived corpus |
| `raw/<corpus>/<backend>/block128/run.txt` | exact command and exit code |
| `raw/<corpus>/<backend>/block128/results.json` | full per-fixture + aggregate metrics |
| `raw/<corpus>/<backend>/block128/summary.md` | Markdown summary (coverage / gates) |
| `raw/<corpus>/<backend>/block128/fixtures.csv` | combined per-fixture CSV |
| `raw/<corpus>/<backend>/block128/per_fixture/` | per-fixture JSON/CSV |

Total raw size ≈ 1.8 MiB (bounded ≤ 5 MiB).

## Run configuration

- Base commit `664041c`; CLI built from the task sources
  (`tools/rhythm-eval`, sha256 `46448822…`) with `-rdynamic`.
- Backends: the read-only EVAL-005 pin plugins
  `build-EVAL-005/main-core/librhythm-eval-{btrack,aubio}.so`
  (source `main ad7872f`), block 128, uncompensated.
- `rhythm-eval --corpus <c> --out ... --backend <b> --backend-lib <plugin> --block 128`.

## Assertions carried by `comparison.json`

- Every declared WAV sha256 in all three manifests matches the bytes on disk
  (62 entries re-hashed, 0 mismatches).
- Non-coverage scored metrics have **0** differences from the prior artefacts
  (`testdata/rhythm/repaired-sustain/raw/{original,repaired}` and
  `docs/research/robustness/raw`) on each respective input corpus.
- The only per-fixture differences are `falseBeatCoverage` /
  `falseBeatMetricInformative` / the additive identity and citation fields.
- SPEC 19 gate booleans reproduce EVAL-006; no tracker is selected; G3 stays open.
