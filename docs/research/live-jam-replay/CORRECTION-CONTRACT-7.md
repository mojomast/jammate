# EVAL-LIVE-001 seventh correction contract — drum-only zero-input window

**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` · **Superseded tools:** `201ba30`
**Amendment:** `tools/live-jam-replay/protocol-amendment-7.json`
**Preserved:** actual-full-001 (read-only) and amendments 1–6 + receipts (immutable history)

Committed before the corrected injected scenario is reevaluated. Additive.

## Why

The injected scenario proves transport wiring via engine getters, but not that
the **actual processor output** carries internal-kit drums. A generic
output-nonzero check includes the guitar input and cannot attribute output to the
kit. Add a declared zero-input window that measures the real processor output
with the embedded kit.

## Procedure

After the third-join resync phase proof, while the engine is playing:

1. Switch the input generator to declared silence (`input_kind=silence`,
   `wav=nullptr`) so every callback buffer is written **exact-zero by the
   caller** (not cleared after the fact).
2. Run 0.5 s of paced callbacks as a wash (guitar/filter tails after the prior
   clean signal; the fresh product default has no NAM/effect capture loaded).
3. Measure 1.0 s of paced callbacks: actual processor output RMS/peak/nonzero
   blocks, injected steps delta, engine playing, internal-kit
   `samplesLoaded()==true` and `drumEngine.useVst.load()==false`.

Recorded: `drum_only_window.input_kind=silence`,
`warmup_seconds=0.5`, `measured_seconds=1.0`, `sample_count`, `sample_rate`,
`rms`, `peak`, `nonzero_blocks`, `steps_delta`, `engine_playing`,
`sampler_loaded`, `use_vst`, `zero_input_declared`,
`allocator_coverage=unmeasured`.

## Rationale

With zero input and the embedded kit loaded (`use_vst=false`), any nonzero
output plus growing injected steps is internal-kit output from the real product
chain, not the guitar input. This is **INJECTED CONTRACT** evidence, not a
real-guitar or physical-device claim. The window is unarmed, so callback
allocator counters are not claimed for it; the 54-cell RT gate is unchanged and
the window does not alter the 54-cell matrix coverage or expected ids.

## Gate

The injected join gate requires `zero_input_declared=true`, `sampler_loaded=true`,
`use_vst=false`, `sample_count >= sample_rate`, `rms/peak > 1e-7`,
`nonzero_blocks > 0`, `steps_delta > 0`, `engine_playing=true`,
`allocator_coverage=unmeasured`. Missing fields fail.

Semantics unchanged (`structural AND rt_gate AND join_gate`; default no-lock
diagnostic). A truthful actual-002 run is required; failures are never hidden.
`make_synthetic_evidence.py` WAV bytes and the selected source WAVs/absolute-frame
phase metadata are unchanged.

## Scope

Only `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md`. No processor/editor/engine/core/shared CMake/UI or
ledger source. No agents. Link only; no measurement from the worker.
