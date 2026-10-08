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
output independently of guitar sound.

## Second actual run — partial measured proof

[`actual-full-002/`](live-jam-replay/actual-full-002/) passes all structural
checks and the54-cell RT gate. It proves an actual second join, immediate stop
in one servicing block, rendered resync phase2→1 (one consumed command), a
session-generation change and coherent release. With zero input, the real
internal kit produces mean block RMS0.10052 and peak0.98541 across94 measured
blocks (48,128 samples), with8 drum steps. This window is not allocator-armed.

Overall acceptance still fails: the first injected join wait is8 seconds,
exactly the unchanged clock's acquisition minimum, leaving no interval for a
next-bar join. The bar-stop proof consequently fails too. The original verdict
is preserved. [Amendment8](live-jam-replay/CORRECTION-CONTRACT-8.md) allows the
configured acquisition window plus two120 BPM bars (12 seconds total), and
fixes receipt deduplication to use session identity rather than clock publication
identity.

## Third actual run — passes all preregistered gates

[`actual-full-003/`](live-jam-replay/actual-full-003/) records **4090 validator
checks, zero failures, all three gates passing**. The127-test adversarial
validator and instrumentation/support/facade self-checks pass before execution.

| Actual observation | Result |
|---|---|
| Callback matrix |54/54 cells measured; no detected allocation/free/lock/wait operations in armed cold/warm callback paths |
| Default experimental BTrack | Built-in120 BPM synthetic input joins at block752 (about8.02 s),59 drum steps |
| Injected initial/restarted joins | Both observed through real engine playback/steps; first at block788 (about8.41 s) |
| Stop next bar | Deferred while playing, then stops after187 blocks (about1.99 s) |
| Immediate Stop | Stops in one servicing block |
| Resync while playing | Rendered phase2→1; downbeat sample697344 in correction window[697344,697856), one consumed command |
| Drum-only processor output | Zero guitar input, internal sampler loaded, hosted kit off;94 blocks/48,128 samples, mean block RMS0.09969, peak0.98615,8 drum steps |
| Reprepare/shutdown | Session generation1→2; coherent released/not-playing payload confirmed |

The runtime takes106.2 seconds and does not time out. Matrix sample positions
advance exactly in the audio-owner domain; UI state remains coalescing-tolerant.
Source snapshots, exact harness/product archive identities, link closure, raw
cells, log, verdict and hashes are preserved. Actual001/002 remain rejected.
Independent final audit accepts both code and measurement gates and re-hashes
the harness/evidence and all eight linked archives successfully.
[Publication CI](LIVE-JAM-PUBLICATION.md) passes all six jobs at74cdde4,
including Windows Standalone/VST3 and8/8 drum/Jam UI suites. The MSVC-only
portability overlay preserves Linux tracker bytes and passes independent review.

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
