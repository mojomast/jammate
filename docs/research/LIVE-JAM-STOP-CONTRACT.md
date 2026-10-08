# Live Jam Stop semantics — pre-measurement clarification

This additive contract resolves the open Stop question found in the first
INT-LIVE-001/EVAL-LIVE-001 handoffs. It precedes actual processor measurements;
the original facade header and replay protocol remain preserved.

| Command | Behavior |
|---|---|
| `Stop` | Cancel pending join and stop injected accompaniment on the next audio block that services the accepted command |
| `StopAtNextBar` | Cancel pending join; if already playing, stop at the next musical bar boundary |
| `Reset` | Cancel pending join, stop on the next serviced block, and reset the Musical Clock's belief |

Acceptance into the UI queue is not audio acknowledgement. A full bridge queue
must preserve stop intent and retry on bounded control-worker ticks; it must not
discard the request or report stopped while the audio-owner echo remains playing.
The UI distinguishes queued/requested intent from actual playback telemetry.
The bound is command service on the processor callback, not a measured physical
device/output latency guarantee.

Stopping or clearing injected playback releases exclusive renderer ownership so
the existing manual/song transport can be used again without a device re-prepare.
Stop must not start the manual sequencer automatically. Old future join/tempo/
resync commands cannot restart an explicitly stopped Jam session.

Non-quiescent Stop/Reset use a bounded runtime command. They never call the
bridge's quiescent lifecycle reset, detach a pointer under the audio consumer,
join a worker from a UI callback, or reset a queue's indices while active.

Required regressions include queue-full join/stop recovery, Start→Stop before
the join boundary, Stop versus Stop-next-bar timing, release of manual transport,
and actual audio-owner acknowledgement. Replay source-pin amendments must freeze
the new bridge API bytes before actual scoring; original evidence stays separate.
