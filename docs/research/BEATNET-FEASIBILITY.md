# BeatNet feasibility review — TRACK-003

**Task:** DEVPLAN §11 `TRACK-003 — BeatNet research benchmark`.
**Gate:** G3 (tracker selection) — this note does not select a tracker and does
not change G3.
**Verdict:** **INFEASIBLE now** within this environment and budget, without
provisioning a supported interpreter or patching unmaintained third-party
source. **No BeatNet accuracy, acquisition, latency, CPU or startup number is
produced.** The task is **PARTIAL**, not complete and not faked.
**Author:** TRACK-003 subagent. **Date:** 2026-10-07. **Base:** `64b39ee`
(`wp/TRACK-003-beatnet`).

The machine-readable evidence is `tools/beatnet-eval/provenance.json`; the
reproducible offline probe is `tools/beatnet-eval/probe.py`.

---

## 0. What "feasible" had to clear

`DEVPLAN §11` asks for accuracy, acquisition, CPU/GPU, startup time, package
size, and the solo-guitar vs mastered-song question. `SPEC §12.1` adds BeatNet
"as a research benchmark if practical". `SPEC §25.5` forbids importing BeatNet
models or datasets into release artifacts until their exact redistribution terms
are reviewed. The brief adds: no GPU assumed; total downloads/install ≤ 500 MB;
no package production wiring; do not invent scores.

A benchmark is "practical now" only if a **meaningful causal** BeatNet run can be
executed here. The causal path matters because the project's tracker contract is
causal (`IRhythmTracker`), and `SPEC §12.3` must separate **event time** from
**decision availability**. This review therefore tests the causal path, not the
offline path.

---

## 1. Pinned official source and licence (exact)

Two repositories matter. `SPEC §27` cites the fork; the authoritative upstream
is the original author's repo. Both were inspected at a pinned commit on
2026-10-07.

| | repo | pinned commit | date | fork | version |
|---|---|---|---|---|---|
| SPEC-cited | `https://github.com/hashimkarim/beatnet` | `be864a4b0f126aa90aeabdeedf23f48865e09512` | 2026-04-24 | yes, of `mjhydri/BeatNet` | tracks upstream |
| upstream | `https://github.com/mjhydri/BeatNet` | `81cedd4beeb7235262db80969a0c9ce9a48a0ed4` | 2026-04-13 | no | `setup.py` 1.2.0 |

- The fork's HEAD is *"Merge remote-tracking branch 'mjhydri/main'"*; it carries
  no inference-path change observed. Both report licence **CC-BY-4.0**.
- `LICENSE` in both repos is byte-identical:
  **18650 bytes, sha256 `7e7170e3cebf88a9f60c7b8421418323c09304da1af4d5e90f4da1dc1c8a2661`**.
- PyPI `BeatNet` is still at **1.1.3** (upstream git is 1.2.0). Its metadata has
  an **empty licence field**:
  - wheel `BeatNet-1.1.3-py3-none-any.whl`, 9 155 419 B,
    sha256 `1ecfa17bdcbe899975a88bdb6efebd6970d846a4f3b6cfd5c6f320647c641c7e`
  - sdist `BeatNet-1.1.3.tar.gz`, 9 159 220 B,
    sha256 `bb76dd00561562602b63fa301ffedef55fb87a484ce80a230d8914cd3c39bd43`

**Licence finding.** The repository licence (CC-BY-4.0) is the *only* stated
term and it covers **both the code and the bundled weights**. There is no
separate weights licence or weights terms file. CC-BY-4.0 is a **content**
licence, not a software licence: it carries no patent grant and is not
compatible with combining/redistributing code under this project's AGPLv3
shipping path. So the model-weight terms are *identified but unresolved for
shipping*, and `SPEC §25.5` continues to bar model/dataset import into release
artifacts. Benchmark-only, non-runtime study is the most this evidence supports.

---

## 2. Model weights (exact SHA, where they live, what they are)

All pretrained weights ship **inside the repo and the PyPI distribution**
(`MANIFEST.in`: `include src/BeatNet/models/*.pt`). Three weights are actually
loaded by `BeatNet.py` (`BDA(272, 150, 2)`), one per training corpus:

| file | trained on | bytes | git blob SHA-1 | sha256 (computed here) |
|---|---|---|---|---|
| `model_1_weights.pt` | GTZAN | 1 612 179 | `9243fc43c8f6f63b2e46db9fa8b621b145595f81` | `619091bc317ca3e83b45591d46f6de3d5a41588bcb39fe9fe7be30cffa6aca84` |
| `model_2_weights.pt` | Ballroom | 1 612 179 | `d1c2d7b3862d30417f2102dcab96143402c0e27d` | `5878a18c079fa0b0139879b14ed2b5b7595faef8c3d16210aed141fd00fa2d58` |
| `model_3_weights.pt` | Rock Corpus | 1 612 179 | `6c1c388586ddc2ed8b1335a8435aed2ebae3bae0` | `0c52a074ea38e8cb4a760ecfa3747c9cf91a1e3cd19f238eed80b0de763989ca` |

The weights were downloaded to scratch only to compute the sha256 values; they
are **not committed** and not part of any artifact. The repo also ships three
duplicate `model-{1,2,3}.pt` files. Same licence as §1 — CC-BY-4.0, no separate
terms.

---

## 3. Causal streaming vs offline: modes and lookahead

`BeatNet.py` implements four modes (README and source agree). Facts from source,
not from the paper:

| mode | execution | inference | causal? | per-event availability measurable? |
|---|---|---|---|---|
| `stream` | microphone, one 20 ms hop at a time | PF | yes | yes |
| `realtime` | file read one 20 ms hop at a time | PF | yes | yes |
| `online` | **whole file in one batch**, then PF | PF | causal *algorithm* | **no** — the file must be fully read first |
| `offline` | whole file in one batch | madmom DBN | no | no |

Feature/inference constants (hard-coded): sample rate **22 050 Hz**; hop
**20 ms** (441 samples); window **64 ms** (1411 samples); 24 log-mel bands plus
a positive first difference; PF at **50 fps**. The corpus is 48 kHz PCM, so
BeatNet resamples internally via `librosa.load`.

**Lookahead/latency evidence.** The `stream` extractor comment says verbatim:

> *"Given the training input window's origin set to center, this streaming data
> formation causes 0.084 (s) delay compared to the trained model that needs to be
> fixed."*

plus a **5-hop warmup (≈100 ms)** before the first non-zero activation
(`if self.counter < 5: self.pred = zeros`). `realtime` builds its feature window
as `audio[hop*(N-2) : hop*N + win_length]`, so the 64 ms window extends beyond
the current hop. These are non-trivial and would need to be quantified against
the C++ harness's own framing — but they cannot be measured here (see §5).

**Hard dependency even for the causal path.** `log_spect.py` imports
`madmom.audio.signal/stft/spectrogram` and `madmom.processors`;
`particle_filtering_cascade.py` imports `madmom.features.beats_hmm` and
`madmom.ml.hmm`; and `BeatNet.py` has a **top-level**
`from madmom.features import DBNDownBeatTrackingProcessor`. So `online`/`realtime`
PF mode cannot avoid madmom either — the offline DBN decoder is not the only
reason madmom is required.

---

## 4. Solo guitar vs the mastered-song assumption

The README states plainly that the models were trained on **mastered songs** and
that streaming/inference should be fed "as loud input as possible" because of it.
This is exactly the gap `SPEC §12.2` and the corpus exist to expose: solo guitar
through an interface has weaker low end, a narrower spectrum, and no drum-derived
downbeat structure. The point is recorded as a design concern, not as a measured
result — **no accuracy claim is made in either direction.**

---

## 5. Dependency, package, startup and CPU footprint — measured

### 5.1 The environment

- Debian 13, kernel 6.12, 4 CPUs, no GPU (`/dev/dri` absent, no `nvidia-smi`).
- **Only interpreter present: Python 3.13.5** (`/usr/bin/python3`); no 3.9–3.12,
  no pyenv/conda.
- `/tmp` is a 7.9 GB tmpfs that was **100 % full (39 MB free)**; all scratch was
  placed under `/home/mojo/projects/build-TRACK-003`. `/home` had ≈ 8 GB free.
- Network reachable; downloads used ≈ 150 MB of the 500 MB budget.

### 5.2 Measured blockers (exact, not inferred)

`tools/beatnet-eval/provenance.json` → `dependency_blockers` carries the full
strings. Summary:

- **B1 — madmom 0.16.1 (2018, unmaintained) cannot run unmodified on 3.13.**
  - `pip install madmom==0.16.1` (isolated build) fails at metadata:
    `ModuleNotFoundError: No module named 'Cython'` (the sdist declares no
    build-system Cython requirement).
  - The era-correct `numpy<1.24` cannot be built on 3.13:
    `AttributeError: module 'pkgutil' has no attribute 'ImpImporter'`.
  - It *does* build with modern Cython/numpy/scipy + `--no-build-isolation`
    (wheel 21 806 757 B), but then **importing** any required path fails:
    `ModuleNotFoundError: pkg_resources` → fixed with `setuptools<81`; then
    `ImportError: cannot import name 'MutableSequence' from 'collections'`
    (removed in 3.10); then, after a scratch-only `collections` shim,
    `AttributeError: module 'numpy' has no attribute 'float'` (removed in 1.24).
  - Net effect: madmom needs the `numpy<1.24` API **and** a Python that can build
    `numpy<1.24`. Python 3.13 satisfies neither.
- **B2 — PyPI BeatNet 1.1.3 hard-pins `numba==0.54.1`**, which supports
  Python ≤ 3.9. (The git tree's 1.2.0 `setup.py` dropped the pin; the published
  release still has it.)
- **B3 — default PyPI `torch` wheel is 554.6 MB**, over the 500 MB budget by
  itself. The CPU-only `torch-2.6.0+cpu-cp313-cp313-linux_x86_64.whl` is
  178 571 455 B (sha256
  `e70ee2e37ad27a90201d101a41c2e10df7cf15a9ebd17c084f54cf2518c57bdf`) and would
  need the extra index plus an explicit version pin.
- **B4 — no compatible interpreter and no GPU.** A CPU-only run would require
  provisioning a separate supported interpreter first.

### 5.3 Budget sketch (metadata-derived; torch deliberately not installed)

CPU path, cp313 wheels: torch+cpu 178.6 MB, scipy 35.3 MB, numpy 16.7 MB,
llvmlite ≈ 59.7 MB, numba 3.8 MB, madmom sdist 20.0 MB, Cython 1.35 MB, plus
librosa/matplotlib/tensorboard/mido/pyyaml/pytest and transitives. Downloads for
a full closure are **well over 300 MB**; once installed, the CPU torch wheel
alone unpacks to roughly 1 GB. The review venv, containing only
Cython+numpy+scipy+madmom (no torch), already measured **277 MB installed**. None
of this is a BeatNet measurement; it is why the review stopped before installing
torch.

---

## 6. Verdict and what would change it

**Infeasible now.** The causal BeatNet path cannot be imported on the only
available interpreter, and the dependency chain cannot be satisfied on Python
3.13 without patching unmaintained source or provisioning another interpreter.
That is a measured blocker, not a preference. **No benchmark was run.**

The bounded ways to make it feasible, each a separate decision with its own
budget and licence review:

1. Provision a pinned **Python 3.9** environment with `numpy<1.24`, the CPU
   `torch`, and madmom 0.16.1 built normally; then run `online` (accuracy) and
   `realtime` (availability) modes. This is the only path that keeps the pinned
   official stack intact.
2. Adopt a **maintained madmom fork** with its own pinned SHA and licence
   review — changes the dependency's provenance.
3. Skip madmom by reimplementing its feature front-end — then it is no longer
   "the official BeatNet", and its license/weights question remains.

The model-weight licence question (CC-BY-4.0, §1) is independent of any of these
and still blocks shipping under `SPEC §25.5`.

---

## 7. Reproducible offline procedure (executed now)

`tools/beatnet-eval/` is committed and offline-only:

- `probe.py` measures the environment and exits 2 when BeatNet is not runnable.
  Measured here: **exit 2**, `beatnet_importable: false`, blockers listed; the
  report is committed as `tools/beatnet-eval/probe-report.json`.
- `score.py` scores a **declared real** BeatNet run and refuses anything
  unverified (no bypass flag). It verifies each fixture's WAV sha256 against the
  committed manifest before scoring and keeps event time separate from causal
  availability.
- `tests/` — **31 unit tests pass** on this machine
  (`python3 -m unittest discover -s tools/beatnet-eval/tests`), covering the
  contract rejection paths, metric math and the probe. The scorer tests use a
  **labelled synthetic unit fixture**, explicitly not BeatNet observations.
- `fixtures/observations.schema.json` — the schema a real driver must satisfy.

Procedure to obtain a real file (not executed; see `tools/beatnet-eval/README.md`)
is pinned to the commits in §1 and the weight hashes in §2.

---

## 8. Executed vs unexecuted (no ambiguity)

Executed here:

- Pinned source, licence and weight inspection; sha256 of weights and `LICENSE`.
- `pip` metadata inspection for BeatNet and its dependency closure (no large
  wheel installed; torch deliberately not installed).
- Real build/import attempts for madmom and numpy variants, capturing exact
  errors (B1/B2).
- `probe.py` and the full 31-test suite.
- Scorer refusal of a fabricated document (exit 2).

**Not** executed: any BeatNet inference; any accuracy/acquisition/F-measure/BPM/
phase/CPU/startup measurement for BeatNet; any model-weight licence conclusion
beyond "CC-BY-4.0 is the only stated term"; any change to CMake, trackers, the
harness, the corpus, packaging or `src/`.

## 9. Boundedness / non-claims

This is a feasibility review, not a model-ecosystem survey. It does not test the
offline DBN path, does not benchmark BeatNet on real guitar, does not compare it
to BTrack/aubio, and does not touch G3. The G3 evidence of record remains the
EVAL-004 comparison (BTrack F 0.7099 / acquisition 4-of-11 / BPM 2.34 %; aubio
F 0.5357 / 7-of-11 / 1.33 %); G3 stays **OPEN**.

## 10. References (accessed 2026-10-07)

- `https://github.com/mjhydri/BeatNet` @ `81cedd4beeb7235262db80969a0c9ce9a48a0ed4` (LICENSE, README.md, setup.py, `src/BeatNet/BeatNet.py`, `log_spect.py`, `particle_filtering_cascade.py`, `models/`).
- `https://github.com/hashimkarim/beatnet` @ `be864a4b0f126aa90aeabdeedf23f48865e09512` (SPEC §27 URL; fork metadata).
- `https://pypi.org/pypi/BeatNet/json` (1.1.3 metadata and digests).
- `https://pypi.org/pypi/madmom/json` (0.16.1 sdist, sha256 `64a86a29106e7c4e0c5f4ec96801c2d1a8492db710e4fe27e097da5517d68cb2`).
- `https://download.pytorch.org/whl/cpu/torch/` (CPU wheel sizes and hashes).
- `docs/research/tracker-comparison/comparison.md`, `task-notes/EVAL-004.md` (current G3 evidence).
