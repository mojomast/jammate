# tools/live-jam-replay — EVAL-LIVE-001 actual-processor replay harness

Independent, fail-closed replay of the **real** `GuitarCompanionProcessor`
callback driving the frozen `submitJamCommand` / `readJamLiveState` facade, plus
a standalone evidence validator and a predeclared conditions matrix.

No processor, editor, engine or frozen-interface source is modified, and **no
stub processor is ever substituted** for actual callback evidence. The harness
is linked and run only against a real product archive that defines the facade;
otherwise the runner reports `awaiting-product` and never invokes a binary.

## Layout

```
tools/live-jam-replay/
  predeclared.json               frozen conditions matrix, metrics, truth, fail-closed rules
  replay_lib.py                  shared preflight/hash/flags/symbol helpers (read-only)
  build_replay.py                self-tests + smoke compile + gated full link
  run_replay.py                  fresh-output runner; awaiting receipts; merge + validate
  validate_evidence.py           standalone fail-closed evidence validator
  test_validate_evidence.py      41 adversarial validator unit tests
  make_synthetic_evidence.py     deterministic synthetic evidence + fixture WAVs
  src/ReplaySupport.h            JSON serializer + 16-bit WAV reader (shared)
  src/LiveJamReplay.cpp          actual-processor replay harness
  src/InstrumentSelfCheck.cpp    instrumentation self-check (no processor)
  src/SupportSelfTest.cpp        JSON/WAV logic self-test (no processor)
  src/RtProbeInstrumentation.*   copied RT-002 instrumentation (unmodified)
```

## Build

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp

python3 tools/live-jam-replay/build_replay.py \
  --source <merged-pipeline-source> \
  --product-build <freshly-built-product> \
  --out <build-dir>
```

Always builds and runs the instrumentation self-check and the support self-test,
and smoke-compiles the harness and facade tests. Links and runs the full harness
only when the preflight passes; otherwise exits 3 (`harness ready, awaiting
actual product`). It reuses the product's shared-code/NAM/jam archives and JUCE
objects — no full rebuild of shared dependencies.

## Run

```sh
python3 tools/live-jam-replay/run_replay.py \
  --source <merged-pipeline-source> \
  --product-build <freshly-built-product> \
  --fixtures-dir <generated-fixtures> \
  --scope full \
  --out <fresh-evidence-dir>
```

Refuses a non-empty `--out` (preserves old results). Enforces a preregistered
bounded timeout (`--timeout-s`, default 300); a timeout yields `status=timed-out`
with `invoked_binary=true` and partial evidence preserved. Writes `evidence.json`
and a validator verdict, or an `awaiting-product` / `awaiting-backend` receipt.
`--scope smoke|full|diagnostic` selects exactly the preregistered smoke IDs, the
full 54-cell matrix, or a declared diagnostic subset (`--scope-reason` required).

For the pipeline's changed headers, pass a preregistered override file:

```sh
python3 tools/live-jam-replay/run_replay.py ... \
  --source-pin-overrides <source-pin-overrides.json>
```

## Validate

```sh
python3 tools/live-jam-replay/validate_evidence.py \
  --evidence <evidence.json> [--predeclared predeclared.json] \
  [--source <src>] [--fixtures-dir <fixtures>] \
  [--allow-synthetic-selftest] \
  [--json report.json] [--summary-md summary.md]
```

Portable by default (recorded evidence only). Synthetic evidence is rejected
unless `--allow-synthetic-selftest` is given, in which case it is labelled
unmistakably as a synthetic self-test. Exits non-zero on any hard-check failure.

## Fixtures

```sh
python3 tools/live-jam-replay/make_synthetic_evidence.py --out <dir>
```

Generates deterministic synthetic `strum_120` / `click_120` / `noise` /
`silence` WAVs and a manifest (sha256 + sample-exact checksum). They are not
guitar recordings and are generated outside git.

See `docs/research/LIVE-JAM-REPLAY.md` for the full method, semantics and
limitations.
