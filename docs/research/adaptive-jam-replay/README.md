# Adaptive actual-processor replay evidence

Scope: the actual processor, internal drum kit and callback-thread instrumentation
with zero guitar audio and injected rhythm observations. This does not measure
representative-guitar acquisition, physical monitoring latency, Windows ASIO,
worker-wide allocations or callback deadlines across the full product matrix.
The direct-call/archive `--wrap` probe detects the wrapped pthread entry points;
it does not intercept calls made internally by shared libraries such as
libstdc++. Its zero result is reported as no **detected** lock/wait calls.

The [protocol](../ADAPTIVE-REPLAY-PROTOCOL.md) was committed before measurement.
The [execution ledger](../../../EXECUTION-LEDGER.md) records acceptance status.
Every measured run is retained, including rejected evidence. `trace.jsonl.gz`
is the losslessly compressed original callback trace; `artifact-hashes.json`
checks the retained files. Build manifests preserve original source pins,
compiler commands and reused inputs. Absolute build paths in those machine
receipts identify the original environment, not portable setup instructions.

| Run | Source | Verdict | Finding |
|---|---|---|---|
| actual004 | `a2f9362` | **FAIL** | All six styles render, dynamics reach the audio owner, fill/echo occur and callback probe is clean; fill lasts92672 samples, outside96000 +/-512. Candidate120 BPM did not establish clock tempo. |
| actual005 | `26480c5` | **FAIL** | Acknowledged taps and Freeze establish119.680851 BPM; all other assertions pass, but fill lasts11264 samples. Engine repair is required. |
| actual006 | `34736ba` | **PASS (76 assertions)** | Post-resync join/grid and fill-downbeat repair `249c548`; six audible styles, applied dynamics, fill95744 samples within the original96000 +/-512 criterion, Stop/release and callback probe pass. |

Independent read-only review accepts actual006 and reproduces the result;
all59 fresh-source/header/tool hashes match its committed source. The engine
repair, source closure and instrumentation self-check are independently verified.
The hosted Linux replay at published `50046bf` reproduces these measurements
exactly; [all seven CI jobs pass](https://github.com/mojomast/jammate/actions/runs/37796290501),
including Windows Standalone/VST3 and the four portable tracker configurations.

The two failed runs are not acceptance evidence. No tolerances or production
clock policies were relaxed in response to either failure.

In actual006,3050 callbacks render2862 nonzero blocks, RMS0.162749064 and
peak1.75202882 with the limiter disabled. The callback-thread probe reports
zero detected allocations, frees, lock calls and waits. Settled catalogue indices
are Rock19, Hard Rock/Metal41, Blues69, Funk56, Pop22 and Shuffle69; styles may
share curated entries. The internal-kit audio uses zero guitar input. This is
bounded software integration evidence, not a production tracker promotion.

Pre-measurement `actual001..003` freshness-guard failures and the full product,
portable matrix and Ninja dependency-recovery logs are retained externally at
`/home/mojo/projects/build-ADAPTIVE-002-integration/`.

To reproduce, build the product from a clean committed checkout using the Linux
adaptive CI lane's CMake options, including `CMAKE_SUPPRESS_REGENERATION=ON`.
Then use a new, empty output directory:

```sh
python3 -B tools/adaptive-jam-replay/build.py \
  --product-build build/adaptive-product --out build/adaptive-replay
xvfb-run -a build/adaptive-replay/adaptive_jam_replay \
  build/adaptive-replay/trace.jsonl
```

The build refuses dirty sources, mixed-source product closures and stale
shared-code/test targets. It runs the instrumentation self-check before linking
the measured processor. The replay returns nonzero when any assertion fails.
