# tempo-long-windows evidence (TRACK-007)

Committed results for the predeclared longer-window matrix. **No WAVs are
committed** (they are ~50 MiB and stay in scratch); everything needed to re-derive
and re-verify the claims from these files is here. See
[`../TEMPO-LONG-WINDOWS.md`](../TEMPO-LONG-WINDOWS.md) for the written results and
[`../PROTOCOL.md`](../PROTOCOL.md) for the frozen contract.

| file | what it is |
|---|---|
| `manifest.json` | byte-identical copy of `../fixtures/manifest.json` (fixture identities, hashes, framing, ground truth) |
| `provenance.json` | executed argv, verified binary/source pins, raw method-log hash, framing |
| `outcomes.json` | per fixture × backend acquisition / within-2-bar / locked BPM / BPM error / F-measure / interval + readiness summary |
| `comparison.json` | the protocol §8 paired records (default vs variant): BPM state, acquisition and within-2-bar gains/losses |
| `intervals.json` | consecutive emitted-interval summaries per backend, with gap-spanning intervals reported separately from outliers |
| `method-summary.json` | per fixture: beats, ready/fallback counts, first-ready event & availability clocks, ready-BPM range/median, interval states |
| `beat-equality.json` | per fixture sha256 of each backend's beat CSV; default and variant are byte-identical |
| `raw/<backend>/block128/` | current EVAL-007 scorer `results.json`, per-fixture records, summaries, command output |
| `diagnostic/<backend>/beats/` | pinned diagnostic CLI beat series (canonical series used for equality and intervals) |
| `raw-method/instance_0.csv.gz` | losslessly compressed current-CLI variant method log (one instance, reset per fixture); sha256 recorded in `provenance.json` |
| `artifact-hashes.txt` | sha256 of every retained file; `tests/test_long_windows_evidence.py` authenticates them and re-derives every reported claim |

Frame: block 128, uncompensated, one plugin instance reset per fixture, three
backends (default BTrack, fixed tempo variant, aubio), 16 fixtures. CPU fields are
resource measurements and are not claims. This tree is diagnostic evidence only: no
backend is selected and no production default is implied.