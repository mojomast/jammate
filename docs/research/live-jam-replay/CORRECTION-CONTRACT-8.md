# Actual003 — acquisition budget and receipt identity

This amendment is committed before changed-tool evaluation and actual003.
Actual001 and actual002 remain unchanged, including their original verdicts.

Actual002 passes structural/callback gates and proves actual resync, immediate
stop, release/reprepare and drum-only processor output. Its injected first join
does not occur inside the8-second wait, so the first join/bar-stop gate fails.
The frozen `ClockConfig::acquireWindowSeconds` is8.0 seconds: the wait ends at
the acquisition minimum, leaving no next-bar scheduling interval.

The injected120 BPM scenario now allows the configured acquisition window plus
two4/4 bars (4 seconds), total12 seconds. Production clock/tracker settings are
unchanged. This is an executable transport-proof budget, not a relaxation of
the still-open useful-guitar-lock-within-two-bars gate. First actual join and
deferred bar stop remain mandatory; no failed observation is converted to pass.
The configured acquisition and total wait budgets are recorded in the raw data.

Receipt deduplication uses session generation plus event, horizon and receipt
samples. `ClockSnapshot::generation` is a publication counter that changes even
without a new observation; it must not make repeated observation timestamps
look like new receipts. A support regression will exercise repeated publication
and new-session/new-receipt cases. Lag units/ordering and RT gates are unchanged.

The corrected harness/helper/self-test hashes are recorded additively before
measurement. The54-cell matrix, canonical WAV bytes, all original protocol
files, prior amendments, tool pins and raw actual001/002 results are preserved.
