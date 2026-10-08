# Live Jam replay fixture timeline — pre-measurement correction

This clarification is committed before the first actual live-processor replay.
It preserves the original protocols, fixtures and self-test receipts. It changes
the input playback mapping, not the callback counter or accompaniment gates.

Inspection of the initial `InputGen::fill` implementation found a shared WAV
position incremented inside each channel loop. Stereo therefore advanced the
mono source twice per audio frame. Playback also ignored the WAV sample rate,
so a 48 kHz fixture was accelerated again on a 96 kHz device timeline.

## Required mapping

- One input source position corresponds to one **audio frame**, independent of
  the number of output/input buffer channels. Advance by `numSamples` device
  frames after filling all channels, not once per channel.
- A source WAV position is `deviceFrame * wavSampleRate / deviceSampleRate`.
  Mono input is replicated coherently across channels. Any deliberate channel
  gain difference must be declared; it does not alter playback speed.
- Fractional source positions use bounded deterministic interpolation. WAV
  looping preserves the source duration and nominal onset period in device
  time. Cold and warm blocks continue the same frame position without reset.
- Input generation/resampling occurs outside the armed `processBlock` region.
  The actual processor still receives, processes and publishes the full device-
  rate buffer. No analysis receipt is invented from the fixture horizon.
- The long default-backend scenario uses the explicitly hashed synthetic
  120 BPM fixture, not a block-reset pseudo-random envelope labelled as that
  rhythm. Built-in nonrhythmic control signals must retain distinct identities.

## Verification before measurement

Check mono and stereo mapping, consecutive chunks, 48→48 and48→96 kHz playback,
and a noninteger rate ratio. Assert source advancement and onset positions in
device frames. Preserve generated WAV bytes/manifest hashes and the earlier
tool pins; record the corrected playback source in a new additive tool receipt.
Actual default-backend lock/join outcomes remain observations, including a
legitimate no-lock/no-join result. An injected tracker proves transport wiring,
not default tracker quality or representative-guitar acquisition performance.
