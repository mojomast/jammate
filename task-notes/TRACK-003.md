# TRACK-003 — BeatNet feasibility research

## Outcome

**PARTIAL — infeasible now, no benchmark produced.** The pinned official BeatNet
cannot run in this environment (Python 3.13.5 only, no GPU, ≤ 500 MB download
budget) without provisioning a supported interpreter or patching unmaintained
third-party source. **No BeatNet accuracy, acquisition, latency, CPU or startup
number exists**, and none is invented. G3 remains **OPEN** and untouched.

Main artifact: `docs/research/BEATNET-FEASIBILITY.md`.
Machine-readable evidence: `tools/beatnet-eval/provenance.json`.

## Scope and base

- Base commit `64b39ee` (`merge(EVAL-004): preserve tracker event time, separate
  causal availability and compare both candidates`), worktree
  `/home/mojo/projects/worktrees/TRACK-003-beatnet`, branch
  `wp/TRACK-003-beatnet`.
- Owned new paths only: `docs/research/BEATNET-FEASIBILITY.md`,
  `tools/beatnet-eval/**`, `task-notes/TRACK-003.md`.
- Read `SPEC.md` §2.2 / §12.1 / §25.5, `DEVPLAN.md` §11 `TRACK-003`,
  `docs/research/DEPENDENCIES.md` §6, `task-notes/EVAL-004.md` and
  `docs/research/tracker-comparison/comparison.md` for the current G3 evidence.
- No change to `src/`, trackers, harness, CMake, corpus, packaging or `.gitmodules`.
  No model weights or audio committed.

## What was found (headline)

1. **Pinned source/licence.** SPEC-cited fork `hashimkarim/beatnet` @
   `be864a4b0f126aa90aeabdeedf23f48865e09512`; upstream `mjhydri/BeatNet` @
   `81cedd4beeb7235262db80969a0c9ce9a48a0ed4` (`setup.py` 1.2.0). Both
   CC-BY-4.0; `LICENSE` sha256
   `7e7170e3cebf88a9f60c7b8421418323c09304da1af4d5e90f4da1dc1c8a2661`, identical
   in both. That repository `LICENSE` is the **only stated term found**; the
   weights are bundled with no separate weights-licence/terms file, so whether it
   covers the weights and how redistribution is treated is **unresolved** and
   must be reviewed under `SPEC §25.5`. Fact (not a compatibility conclusion):
   CC-BY-4.0 grants no patent rights and CC advises against CC licences for
   software; no blanket AGPLv3 compatibility determination is made. PyPI
   `BeatNet` 1.1.3 has an empty licence field; wheel sha256 `1ecfa17b…`, sdist
   sha256 `bb76dd00…`.
2. **Weights.** Three `model_{1,2,3}_weights.pt`, each 1 612 179 B, GTZAN /
   Ballroom / Rock Corpus, with exact git blob SHA-1 and content sha256 in
   `provenance.json`, verified at the pinned upstream commit URL (not `/main`);
   downloaded to scratch to hash, not committed.
3. **Causal vs offline.** `stream`/`realtime` are hop-by-hop PF; `online` is the
   causal PF algorithm but reads the whole file first, so it has **no per-event
   causal availability**; `offline` is non-causal madmom DBN. The causal path
   still hard-imports madmom (`log_spect.py`, `particle_filtering_cascade.py`,
   and a top-level `BeatNet.py` import). Documented stream lookahead ≈ 0.084 s
   plus a 5-hop warmup; `realtime`'s 64 ms window extends ~64 ms beyond the
   current hop, so it is causal only after the window completes (not
   zero-lookahead); 22 050 Hz, 20 ms hop, 64 ms window, PF 50 fps.
4. **Measured blockers (not guessed).**
   - `madmom==0.16.1` isolated build fails (`No module named 'Cython'`); the
     required `numpy<1.24` cannot build on 3.13 (`pkgutil.ImpImporter`); it
     builds with a modern stack + `--no-build-isolation` but then fails to
     import (`collections.MutableSequence`; then `numpy.float` after a shim).
   - PyPI `BeatNet==1.1.3` pins `numba==0.54.1`, Python ≤ 3.9 only.
   - Default PyPI `torch` wheel is 554.6 MB > 500 MB budget; CPU wheel
     `torch-2.6.0+cpu` is 178 571 455 B. Review venv without torch already 277 MB.
   - Only Python 3.13.5 present; no GPU.
5. **Verdict.** Infeasible without a supported interpreter or dependency patch.

## Deliverables

```
docs/research/BEATNET-FEASIBILITY.md     # findings, SHA/citations, blockers, budget, runbook
tools/beatnet-eval/README.md             # offline runbook
tools/beatnet-eval/provenance.json       # pinned SHAs, licence status, weight hashes, measured blockers
tools/beatnet-eval/probe.py              # offline environment probe (never installs/runs BeatNet)
tools/beatnet-eval/probe-report.json     # measured probe output for this machine (exit 2)
tools/beatnet-eval/score.py              # scores a DECLARED run; metadata/format + WAV-hash gate
tools/beatnet-eval/beatnet_eval/         # pure-stdlib corpus/metric/contract helpers
tools/beatnet-eval/fixtures/observations.schema.json
tools/beatnet-eval/tests/                # 48 unit tests
task-notes/TRACK-003.md                  # this note
```

## Tests executed

```
python3 -m unittest discover -s tools/beatnet-eval/tests
  Ran 48 tests ... OK
python3 tools/beatnet-eval/probe.py --json --out tools/beatnet-eval/probe-report.json
  exit 2, beatnet_importable=false   (expected on Python 3.13)
python3 tools/beatnet-eval/score.py --observations <no-metadata>.json ...
  exit 2, metadata/format gate: "missing 'provenance' metadata object"
```

The 48 tests cover the contract's mode/availability rules and rejection paths,
the corpus-WAV hash guard, metrics arithmetic, coverage/missing reporting and the
probe. The scorer tests use a **labelled synthetic unit fixture**; they are not
BeatNet observations and are not BeatNet evidence.

**Provenance is self-reported metadata.** The contract checks format/consistency
and the scorer verifies the WAV hash, but neither proves BeatNet produced the
beats. A synthetic document with well-formed metadata is accepted by design (a
test asserts this) — which is why the scorer labels its output *declared,
unverified* and why no score may be read as a BeatNet result without a separately
executed driver run and raw output.

## Executed vs unexecuted

Executed: source/licence/weight inspection and hashing (at the pinned commit);
dependency metadata inspection; madmom/numpy build+import attempts; probe; 48
tests; scorer metadata-gate rejection of a document with missing provenance.
Not executed: any BeatNet inference or measurement; any determination that
CC-BY-4.0 is or is not AGPLv3-compatible; any DBN run; any G3/CMake/corpus change.

## Handoff

- Feasibility-review commit: `f2cdf90`.
- Integration-review corrections commit: `7bac829` — scoped fixes to licence
  wording, provenance-is-metadata, IO mode semantics and coverage reporting; no
  inference, dependency provisioning, benchmark or gate change.
- SHA-reporting commit: the commit that fills in the line above; this file is the
  only change.
- Authoritative artifacts: `docs/research/BEATNET-FEASIBILITY.md` and
  `tools/beatnet-eval/provenance.json`.
- Recommended follow-up (separate task, own budget/licence decision): provision a
  pinned Python 3.9 env with `numpy<1.24` + CPU torch + madmom 0.16.1 and run
  BeatNet `online`/`realtime` via `score.py`. Do not fabricate output if that is
  not funded.
