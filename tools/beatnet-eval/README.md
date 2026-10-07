# tools/beatnet-eval — offline BeatNet feasibility tooling (TRACK-003)

This directory is the reproducible, offline half of the TRACK-003 BeatNet
feasibility review. It contains **no BeatNet code, no model weights and no
benchmark numbers**. It cannot and does not produce a BeatNet observation.

Read `docs/research/BEATNET-FEASIBILITY.md` for the findings. This README is the
runbook.

## Verdict in one line

The pinned official BeatNet (source SHA and licence in `provenance.json`) is
**not runnable in this environment** (Python 3.13.5, no GPU, ≤ 500 MB download
budget) without either provisioning a supported interpreter or patching an
unmaintained dependency. No BeatNet score exists. See `dependency_blockers` in
`provenance.json` for the measured failures.

## Commands

```bash
# 1. Measure the current environment; never installs, never runs BeatNet.
python3 tools/beatnet-eval/probe.py --json --out tools/beatnet-eval/probe-report.json
# exit 2 == BeatNet is not importable here (expected on Python 3.13)

# 2. Optionally hash a pinned BeatNet checkout's weights against provenance.json:
python3 tools/beatnet-eval/probe.py --beatnet-dir /path/to/BeatNet

# 3. Run the unit tests (stdlib only, no third-party deps):
python3 -m unittest discover -s tools/beatnet-eval/tests -v

# 4. Score a DECLARED run (provenance is self-reported metadata; the scorer
#    verifies metadata/format and the corpus WAV hash, but not authenticity):
python3 tools/beatnet-eval/score.py \
    --observations /path/to/declared-observations.json \
    --corpus testdata/rhythm --out /tmp/beatnet-score
```

## Producing a declared observation file (future, on a supported interpreter)

This is the procedure a future task must follow. It is **not** executed here
because no supported interpreter exists on this machine. Do not fabricate output.

1. Provision a pinned environment where the pinned BeatNet can run. As measured
   in `provenance.json`, that means Python 3.9 with `numpy<1.24` (madmom 0.16.1
   needs the old numpy API) and a CPU `torch`, or an explicitly patched
   maintained madmom. Record every version.
2. Run the pinned BeatNet at its pinned commit (`81cedd4b…` / fork
   `be864a4b…`) in `mode='online'` (or `'realtime'` for availability), with each
   corpus WAV, model 1/2/3. The 22050 Hz input assumption is handled by
   BeatNet's own `librosa.load`.
3. Emit `beatnet-eval/observations/v1` JSON as in
   `fixtures/observations.schema.json`, including the raw-output hash, the
   driver hash, and the actual `input_wav_sha256` for each fixture.
4. Score with `score.py`. The scorer recomputes metrics from the **committed**
   manifest and rejects any input whose WAV hash differs, and reports fixture
   coverage/missing data so omission is visible.

The provenance block is **self-reported metadata**. The scorer checks its format
and the WAV hash; it cannot prove BeatNet produced the beats. Any score is
therefore a *declared, unverified* result until a separately executed driver run
and its raw output are recorded and reviewed.

`mode='online'` uses the causal particle filter but reads the whole file first,
so it must declare `availability_semantics: "online_batch"` and
`available: null`. Only `realtime`/`stream` carry a per-event causal
availability. In `realtime` the feature window extends 64 ms beyond the current
hop, so an observation is causally available only after the window completes —
this is not zero-lookahead. That event-vs-availability split is the SPEC 12.3
concern this project cares about.

## Files

| file | purpose |
|---|---|
| `provenance.json` | pinned SHAs, licence status, weight hashes, measured blockers, budget |
| `provenance.json` → `dependency_blockers` | the measured reasons no run happened |
| `probe.py` | offline environment probe (exit 2 = not runnable) |
| `probe-report.json` | measured probe output for this machine |
| `score.py` | scores a declared (unverified) run; metadata/format + WAV-hash gate |
| `beatnet_eval/` | pure-stdlib corpus/metric/contract helpers |
| `fixtures/observations.schema.json` | the input contract (schema only, no data) |
| `tests/` | unit tests; synthetic fixtures are labelled and are not BeatNet evidence |
