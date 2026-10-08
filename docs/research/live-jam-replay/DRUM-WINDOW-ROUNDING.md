# Drum-only window duration — pre-actual002 correction

Review of the seventh amendment implementation found that integer flooring
produced93×512=47,616 samples at48 kHz, below the declared one-second minimum.
The warmup also fell below its0.5-second target. Neither implementation has
been used for an actual drum-only measurement yet.

Both durations now round up to whole callbacks. At48 kHz/512 this produces47
warmup blocks and94 measured blocks (48,128 samples). Reported durations use
the actual sample count/rate rather than retaining an unachieved target.
The existing minimum-duration gate remains unchanged and is not relaxed.

The changed harness source is pinned in a new additive receipt before actual002.
All prior protocols, amendments, tool pins, worker receipts and actual001 raw
evidence remain unchanged. Callback RT coverage remains the54-cell matrix;
the supplemental zero-input window reports allocator coverage as unmeasured.
