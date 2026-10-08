# Live Jam publication — verified

The first experimental live guitar→Musical Clock→drums slice is integrated,
published and independently accepted. Real Jam controls, bounded queue/lifecycle
recovery and internal-kit output are verified. Production tracker selection
and representative-guitar/physical-interface acceptance remain open.

## Final product CI

[Run37748022994](https://github.com/mojomast/jammate/actions/runs/37748022994)
at `74cdde43f0534ff7598e614711e303bcf85ebb72` passes all six jobs:

| Lane | Result |
|---|---|
| Core OFF / BTrack / aubio / both |27/27,28/28,28/28,29/29 suites |
| NAM allocation repairs |9/9 suites |
| Windows2022 / MSVC | Standalone/VST3 build and8/8 drum/Jam UI suites |

Receipt: [`github-ci/run-37748022994.json`](github-ci/run-37748022994.json),
including per-entry hashes of downloaded logs. Archive SHA256:
`b5084e46d4b0f52a79a3feb112004fd51f62ba28e0a619e17914b0f0a0e8b373`.

The earlier Windows failure at23c4299 is preserved with its logs. Its repair
generates a source-pinned MSVC-only BTrack copy, replacing six GNU VLAs with
uninitialised RAII storage and defining the private math constant macro.
Vendored/Linux source remains unchanged. Strict compilation, rejection controls
and36,000 bit-identical native comparison rows pass; independent review accepts.

## Actual Linux processor evidence

[Actual003](live-jam-replay/actual-full-003/) passes4051 hard checks,36 advisory
checks and3 gates. All54 callback cells are measured with no detected armed
callback allocation/free/lock/wait operations. Default experimental BTrack joins
the synthetic120 BPM signal after about8.02 seconds. Injected contract evidence
proves first/restarted joins, deferred bar stop, one-block immediate Stop,
rendered resync, release/reprepare and drum-only output with zero guitar input.
All earlier rejected runs and original protocols/amendments remain unchanged.

Worker allocations and supplemental drum-window allocator behavior are not
measured by the callback RT gate. CI establishes Windows compiler/format/test
compatibility, not Windows ASIO or physical latency. Useful lock within two
bars on representative guitar material remains unproven; the synthetic default
join above exceeds that target. These open gates do not block the accepted
first slice or imply a production tracker choice.

See [integration details](LIVE-JAM-ACTUAL-INTEGRATION.md),
[user controls](../USER-GUIDE.md#jam-listen-join-and-follow) and
[the ledger](../../EXECUTION-LEDGER.md) for scope and reproduction.
