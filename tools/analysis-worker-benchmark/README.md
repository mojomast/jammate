# analysis-worker-benchmark

Throughput harness for `jam::RhythmAnalyzer` (DEVPLAN **ANALYSIS-001**).

This tool answers one narrow question: when audio is submitted in bounded bursts,
does the analysis worker sustain **more than real time**? It reports the steady
**wall** time and the **CPU** time and derives a realtime factor from wall time.
It is **not** a quality gate and **not** a latency gate — no pass/fail threshold
is applied to any timing value.

## What runs

- The worker (`src/jam/RhythmAnalyzer.cpp`) and the bounded ring
  (`src/jam/AnalysisAudioRing.h`) are compiled from this worktree, so the
  measurement is of the ANALYSIS-001 code, not of a stale archive.
- The beat trackers are the **pinned, read-only** `dlopen` plugins built by
  EVAL-005:
  `librhythm-eval-btrack.so` and `librhythm-eval-aubio.so`. No GPL backend is
  linked into this tool (SPEC.md 25.6). Their SHA-256 is recorded per run.
- Audio is a deterministic, generated 120 BPM click train. No recording, no
  randomness, no network.

## Modes

| Mode | Producer | Consumer | Purpose |
|---|---|---|---|
| `throughput` | one bounded burst of `burst` blocks, then waits for the worker to drain that burst before the next | drains the evidence queue | Worker throughput under a clean burst → drain pipeline. The ring does not overrun. |
| `pressure` | pushes as fast as the bounded ring allows, retrying on refusal | does not drain | Proves the bounded publication policy: once capacity is reached every incoming envelope is dropped and counted. |

`ringOverruns` in **pressure** mode counts refused `AnalysisAudioRing::push`
calls. The synthetic producer retries, so no audio is lost there; in a real
callback a refused push *is* a dropped block, which is the ring's documented
policy. In **throughput** mode `ringOverruns` is zero by construction.

## Usage

```sh
tools/analysis-worker-benchmark/run_benchmark.sh [plugin-dir] [out-dir]
```

Defaults: plugin dir `/home/mojo/projects/build-EVAL-005/main-core` (falls back
to `…/build-EVAL-005/core`), out dir `./results`. The build/scratch directory is
`/home/mojo/projects/build-ANALYSIS-001/benchmark`
(`ANALYSIS_BENCH_SCRATCH` overrides). Results are held to a ≤ 1 MiB budget.

Outputs:

- `results/throughput.csv` — one row per `(backend, rate, mode)`.
- `results/benchmark.json` — machine-readable runs plus the source hash.
- `results/manifest.txt` — plugin/source hashes, compiler and UTC date.

## Measured (GCC 14.2, `-O2`, 5 s, 128-frame blocks, ring 64, burst 32)

Throughput mode, both pinned plugins, 44.1 kHz and 48 kHz (no queue drops).
Run-to-run wall time varies by roughly ±10%; these are the values committed in
`results/throughput.csv`.

| backend | rate | wall s | CPU s | audio s | realtime × | frames/s |
|---|---|---|---|---|---|---|
| btrack | 44100 | 0.0777 | 0.0976 | 4.998 | **64×** | 2.84 M |
| btrack | 48000 | 0.0814 | 0.1025 | 5.000 | **62×** | 2.95 M |
| aubio | 44100 | 0.0786 | 0.0861 | 4.998 | **64×** | 2.80 M |
| aubio | 48000 | 0.0700 | 0.0781 | 5.000 | **71×** | 3.43 M |

Pressure mode dropped `processed − 32` envelopes as designed (32 = the bounded
queue capacity) and reported a matching `droppedObservations` count, e.g. btrack
48 kHz: 1875 processed, 1843 dropped, 32 retained.

"Ready" in the CSV means only that the pinned backend loaded and the worker fed
it every block. It is not a tracker-quality result and selects no backend —
G3 remains open (SPEC gates unchanged).

## Limitations

- A synthetic click train, not music; the tracked BPM is not scored here.
- CPU time is `std::clock()` (process CPU across threads), reported alongside
  wall time but never asserted.
- Wall time in throughput mode includes the burst hand-off and the worker's 1 ms
  idle poll between bursts, so it is a conservative floor, not the peak rate.
- No audio device, no latency/dropout claim.
