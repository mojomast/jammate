# Adaptive actual-processor integration protocol

This additive protocol covers the adaptive wave, separately from the preserved
first-live-slice `actual-full-001/002/003` measurements and source pins.

`tools/adaptive-jam-replay/build.py` fresh compiles the processor, editor, drum
engine, live UI and all JUCE-free core sources, then links against a recorded
read-only product closure. Build manifests record fresh source/header hashes,
compiler commands, reused archives and output identities. The allocator/lock
instrumentation self-check must pass before the replay links.

The replay feeds zero guitar audio to the real `processBlock`, with an explicitly
tagged injected tracker returning deterministic 120 BPM/phase/energy evidence.
It advances one 48 kHz / 512-frame sample domain and paces worker service outside
the armed callback. It requests a real join, all six style changes, intensity and
complexity changes, an explicit fill with automatic fills disabled, and Stop.
Raw per-block JSONL records engine cursor/playback/groove/fill/steps, worker style
and audio-owner-derived fill echo, and output RMS. Queue acceptance is separate
from application and sound. The product closure must be built from the same clean
source tree with all product targets up-to-date; mixed-tree archives are refused.

Required outcomes:

- Actual join within the declared 1400-block (~14.93 s audio-time) wiring window.
  This is not a two-bar guitar-acquisition test.
- Each style reflected by the worker, continuing actual playback, and actual
  groove changes across styles after a 400-block settling window per style.
- Worker confirms intensity/complexity settings and automatic fills disabled.
  Actual-engine unit cases separately verify the rendered dynamic controls.
- Explicit fill observed in audio-owner echo and subsequent reversion within
  600 blocks, with `fillAmount=0` to remove automatic-fill ambiguity. Audio-owner
  engine fill start/end observations must span 96,000 samples within the
  512-sample callback observation resolution (one 120 BPM 4/4 bar).
- Immediate Stop releases injected ownership within 30 servicing blocks.
- Every processed callback advances exactly 512 audio-owner samples.
- Audible actual internal-kit output with zero guitar: more than 100 nonzero
  blocks and output peak above 0.01.
- No detected allocation/free/mutex/condition-wait operations on armed callback
  paths, including initial join, style changes, fill and stop.
- Released facade reports neither prepared nor playing.

Actual-engine unit cases additionally establish exact musical boundary behavior,
fill reversion, queue rejection and compatibility. This replay proves integration
and callback-thread paths only: no representative guitar, physical latency,
worker-wide allocator or Windows-ASIO acceptance is inferred.

Retain failed runs and logs. Correct harness defects additively and record their
changes before rerunning; do not replace a failed receipt with a successful one.
