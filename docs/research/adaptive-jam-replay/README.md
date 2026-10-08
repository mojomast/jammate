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

These failed runs are not acceptance evidence. No tolerances or production
clock policies were relaxed in response to either failure.

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
