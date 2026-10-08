# First live Jam — integration and actual replay

## Accepted product implementation

- Pipeline handoff: `016c0c8` (bounded analysis tap, worker-owned Musical Clock,
  next-bar join policy and actual DrumEngine rendering).
- UI handoff: `8ac4517` (live facade, production1100×700 layout and coherent
  queued-intent/audio-echo presentation).
- Fresh Linux Standalone/VST3 build and36/36 registered product suites pass.
-20/20 product UI cases pass. Portable configurations pass27/28/28/29 suites
  with neither/BTrack/aubio/both optional backends and strict linkage guards.
- Worker engine evidence:82 actual DrumEngine cases pass, including bounded
  Stop/Stop-next-bar/manual recovery and queue-full staged-tempo retry.

The product links experimental BTrack by default. Production tracker selection,
representative guitar acquisition and physical-device deadlines remain open.

## First actual run — preserved rejected evidence

[`actual-full-001/`](live-jam-replay/actual-full-001/) contains the original raw
cells, merged evidence, verdict, source/product manifests, log and hash receipt.
It is a real processor measurement, with synthetic input recordings; it is not
synthetic evidence generated to mimic processor output.

-54 preregistered cells:48/96 kHz,128/512/4096 frames, disabled/enabled/pressure,
  clean/noise/silence. All pass the measured callback RT-operation gate.
- The default16-second scenario uses the declared built-in device-time120 BPM
  synthetic signal (`input_source_rate=0`), rather than the matrix's generated
  WAV files. Its implementation is source-pinned. It joins at block753
  (about8.03 seconds) and fires59 drum steps. Output includes guitar and drums;
  its generic output RMS is not a drum-isolated measurement.
- Overall verdict fails:108 structural checks because unsuccessful final
  latest-value reads left zero-valued end states, plus an unpaced injected
  lifecycle scenario that never joined. Those failures remain published as
  originally measured; no retrospective rewrite makes this run pass.

The first-run fixes retain the latest coherent snapshot on false reads, obtain
a bounded prepared baseline, and pace the injected scenario outside callbacks.
Acceptance also requires separate actual first/second joins, actual stop and
release, and a rendered resync phase correction. A short declared zero-input
window is being added to verify internal drum audio at the final processor
output independently of guitar sound. The fresh run will use a new directory.

## Scope

Callback instrumentation measures operations on the armed callback thread.
Worker allocation and physical interface latency are not measured. Wall time
around the instrumented callback is not a hardware deadline result. Injected
tracker observations test the transport contract; they do not select BTrack or
prove useful guitar lock within two bars. The default synthetic join above
takes longer than that target.

Original protocols, tool pins, amendments and self-test receipts are retained
alongside each new correction. See [the replay report](LIVE-JAM-REPLAY.md),
[the Stop contract](LIVE-JAM-STOP-CONTRACT.md) and
[the execution ledger](../../EXECUTION-LEDGER.md) for authoritative status.
