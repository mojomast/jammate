# EVAL-LIVE-001 fourth correction contract — audio-frame input timeline

**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` · **Superseded tools:** `566032f`
**Amendment:** `tools/live-jam-replay/protocol-amendment-4.json`
**Orchestrator preregistration:** `docs/research/LIVE-JAM-FIXTURE-TIMELINE.md` (main)

Committed before the corrected input timeline is reevaluated. Additive: all prior
protocol/pin/receipt/contract files and generated WAV bytes are preserved
byte-for-byte; new pins/receipts are new files.

## The observed bug

`InputGen::fill` advanced the shared WAV position **inside the channel loop** and
ignored the WAV sample rate. On a stereo `AudioBuffer`, every block advanced the
mono source by `2N`; channel 0 received frames `0..N-1` and channel 1 received
`N..2N-1`, so the two channels were not coherent and the effective fixture tempo
was doubled with a block discontinuity. Separately, a 48 kHz fixture played at
2× on a 96 kHz device because `WavMono.rate` was never used. The default long
scenario's nominal 120 BPM truth was therefore wrong and cannot be corrected
after measurement.

## Required mapping

- One source position per **audio frame**, independent of channel count; advance
  by `numSamples` device frames **once**, after all channels are filled.
- `sourcePosition(deviceFrame) = deviceFrame * wavSampleRate / deviceSampleRate`.
- Fractional source positions use bounded deterministic linear interpolation with
  wrap; mono is replicated coherently into every channel (no undisclosed gain).
- Absolute uint64 device frame starts at 0 and continues across cold and warm
  blocks without reset; source advance is independent of channel count.
- 48→48 is exact; 48→96 halves the index step; noninteger 48→44.1 is supported.
- Input generation/resampling runs **outside** the armed `processBlock`; WAV bytes
  are loaded/hashed off-callback; nothing is read from disk inside the armed
  region.
- The long default scenario uses the explicitly hashed 120 BPM fixture (or a true
  120 BPM builtin in device time), never a block-reset pseudo-random envelope.
- Built-in clean: true deterministic 120 BPM pulse/harmonic strum in device time
  `t=(deviceFrame+i)/deviceRate`, 0.5 s period, exponential decay, shared across
  channels, no per-channel random advance, no per-block phase reset. Built-in
  noise: LCG advanced once per frame, replicated across channels (declared).
  Silence: all zero.
- A WAV with non-positive/non-finite sample rate is rejected; an invalid device
  rate is rejected before playback (no divide-by-zero).

## Verification

`ReplayInput.h` is shared by the harness and the support self-test: mono/stereo
equal phase; source advance is `N` not `2N`; 48→48 and 48→96 index steps;
noninteger 44.1; repeated wrap; a ramp WAV at fs=4/device=8 gives the exact
wrapped interpolation sequence; a realistic fs=48 impulse every 24000 samples
lands at device frame 24000 at 48 kHz and 48000 at 96 kHz; chunk invariance
(128 vs 64+64); builtin clean absolute-time phase; deterministic noise per frame.

Each cell/identity/scenario declares `inputSignalKind`, `inputSourceRate`,
`deviceRate` and `channelMapping` additively; the original 54 expected cell ids
and the nominal clean 120 truth are unchanged in meaning (playback now correct).
`make_synthetic_evidence.py` WAV byte generation is unchanged; earlier tool pins
are preserved; tool pin 4 and a receipt are new files.

## Scope

Only `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md`. No processor/editor/engine/core/shared CMake/UI or
ledger source. No agents. Link only; no actual run until the orchestrator accepts
the fix.
