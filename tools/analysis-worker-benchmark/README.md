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
  `librhythm-eval-btrack.so` and `librhythm-eval-aubio.so`. No GPL code is
  **compiled into** this binary; it is only loaded at run time, and the loaded
  plugins' own licences (GPLv3 / GPLv3-or-later) still apply. Their SHA-256 is
  recorded per run.
- Audio is a deterministic, generated 120 BPM click train. No recording, no
  randomness, no network.

## Modes

| Mode | Producer | Consumer | Purpose |
|---|---|---|---|
| `throughput` | one bounded burst of `burst` blocks, then waits for the worker to drain that burst before the next | drains the evidence queue as it goes | Worker throughput under a clean burst → drain pipeline. The ring does not overrun and the evidence queue does not drop (asserted to be 0 in the committed run). |
| `pressure` | pushes as fast as the bounded ring allows, retrying on refusal | drains only after all frames finish | Proves the bounded publication policy. The queue keeps the **first 32** envelopes and **drops the incoming** ones after that (never overwrites/keeps-latest); the drop count is reported. |

`ringOverruns` in **pressure** mode counts refused `AnalysisAudioRing::push`
calls. The synthetic producer retries, so no audio is lost there; in a real
callback a refused push *is* a dropped block, which is the ring's documented
policy. In **throughput** mode `ringOverruns` is zero by construction.

## Usage

```sh
tools/analysis-worker-benchmark/run_benchmark.sh [plugin-dir] [out-dir]
```

Defaults: plugin dir `/home/mojo/projects/build-EVAL-005/main-core`, out dir
`./results`. Missing plugins fail closed. The build/scratch directory is
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
| btrack | 44100 | 0.0753 | 0.0954 | 4.998 | **66×** | 2.93 M |
| btrack | 48000 | 0.0817 | 0.1012 | 5.000 | **61×** | 2.94 M |
| aubio | 44100 | 0.0668 | 0.0739 | 4.998 | **75×** | 3.30 M |
| aubio | 48000 | 0.0737 | 0.0810 | 5.000 | **68×** | 3.26 M |

Pressure mode dropped `processed − 32` envelopes as designed (32 = the bounded
queue capacity) and reported a matching `droppedObservations` count, e.g. btrack
48 kHz: 1875 processed, 1843 dropped, 32 retained.

"Ready" in the CSV means only that the pinned backend loaded and the worker fed
it every block. It is not a tracker-quality result and selects no backend —
G3 remains open (SPEC gates unchanged).

The runner rejects invalid numeric/mode/capacity input before allocations, failed
backend factories/reset/process calls, incomplete queue accounting, and output
flush/close failures. A 60-second progress deadline bounds producer retries for
backends that return; it cannot cancel an indefinitely blocking backend. Run the
scripted failure checks with:

```sh
python3 -B tools/analysis-worker-benchmark/test_failclosed.py \
  --binary /path/to/analysis_worker_benchmark --cxx c++
```

## Limitations

- A synthetic click train, not music; the tracked BPM is not scored here.
- **Wall vs CPU.** The `wall_seconds` column is the end-to-end producer/consumer
  wall time and includes the burst hand-off and the worker's 1 ms idle poll
  between bursts. It is a conservative floor, not the peak rate. The
  `cpu_seconds` column is `std::clock()` process CPU across all threads and is
  reported alongside, never asserted. Neither is a latency measurement.
- The pressure mode's retained set is the first `kObservationQueueCapacity`
  envelopes, not the latest; the rest are dropped and counted.
- No audio device, no latency/dropout claim.
