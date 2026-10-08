# tools/device-validation — physical interface, latency and play evidence

Usable, fail-closed tooling that collects and validates evidence for the
SPEC.md section 21.5 hardware matrix, the section 18.3 monitoring-latency target
and the section 20 musical play tests of **Guitar Companion** (Standalone
`Guitar Companion.exe`, VST3 `Guitar Companion.vst3`, CMake target
`GuitarCompanion`).

Nothing here fabricates a human or hardware run. A latency number is only
produced from actual recorded WAV bytes, and a play/callback/dropout number is
only marked *measured* when a raw receipt backs it. Anything else is recorded as
`unmeasured` or `operator-report`. Synthetic self-tests prove the validator
only and can never satisfy a physical gate.

## Layout

```
tools/device-validation/
  device_lib.py             stdlib WAV IO, quality checks, latency estimators
  init_session.py           one-command session / evidence manifest
  ingest_latency.py         loop-back WAV -> physical latency record
  ingest_play_trial.py      explicit play-trial evidence (receipt-gated)
  ingest_functional.py      SPEC 21.5 edge conditions (receipt-gated)
  validate_evidence.py      matrix + anti-fabrication validator
  make_synthetic_evidence.py labelled synthetic session for tooling tests
  probe_environment.py      read-only availability probe (no installs)
  schema/                   JSON Schema for session/latency/play-trial/functional
  tests/                    stdlib unittest suite
```

`docs/research/device-validation/` holds the protocol, this tool's example
diagnostic output and the outstanding physical-evidence list.

## 1. Initialise a session (one command)

```sh
python3 tools/device-validation/init_session.py \
  --out docs/research/device-validation/session-reference \
  --session-id 2026-10-08-windows-focusrite-asio \
  --source-sha <git-sha-of-the-Guitar-Companion-build> \
  --os windows --backend asio --driver "Focusrite USB ASIO" \
  --input "Analogue 1" --output "Analogue 1+2" \
  --sample-rate 48000 --block 128 --monitoring software-app
```

This writes `session.json` (identity, an empty `measurements` list), `raw/` and
`measurements/`. It refuses a non-empty directory unless `--force` is given, and
it never marks anything measured.

## 2. Measure physical round-trip monitoring latency

The reference configuration is **48 kHz / 128 frames / ASIO USB interface** with
headphones on the same interface (SPEC 17, 18.3). The target is
**<= ~12 ms** round-trip on that setup.

### Preferred: one two-channel recording

Record the emitted reference click on one channel and the physically returned
signal on another channel **of the same file**. The lag between them is the
round trip and cancels the recording-start offset:

```sh
python3 tools/device-validation/ingest_latency.py \
  --session <session-dir> \
  --wav raw/loopback-asio-48k-128.wav \
  --condition asio_48k_128 \
  --method two-channel --loopback-channel 0 --reference-channel 1
```

The tool detects silence, ambiguity, clipping, non-finite samples and
rate/channel mismatches and writes `measurements/latency-asio_48k_128.json` with
explicit `reasons` when it refuses to accept the recording.

### Weaker: one absolute onset

A single-channel recording is accepted only with an explicit reference onset
(default 0 ms). It is sensitive to the recording-start buffer offset and is
warned about, not silently trusted.

```sh
python3 tools/device-validation/ingest_latency.py --session <dir> \
  --wav raw/loopback.wav --condition asio_48k_128 \
  --method single-channel --reference-onset-ms 0
```

### Linux capture recipes

Enumerate first (read-only):

```sh
cat /proc/asound/cards
arecord -l
aplay -l
wpctl status            # PipeWire
```

Then capture, with the interface's **direct monitoring disabled** and the
output physically patched (or internally looped) to the input:

```sh
# ALSA, 48 kHz, stereo, 32-bit, 5 s
arecord -D hw:1,0 -f S32_LE -r 48000 -c 2 -d 5 raw/loopback.wav

# PipeWire
pw-record --rate 48000 --channels 2 --format s32 raw/loopback.wav

# ffmpeg (works even when arecord is absent)
ffmpeg -f alsa -i hw:1,0 -ar 48000 -ac 2 -t 5 raw/loopback.wav
```

Play the reference click out of the same interface while recording, or use a
two-input capture (one input on the click source, one on the returned signal).
Calibrate gain so the click peaks near **-12 dBFS** and never clips; keep any
noise gate, auto-gain, EQ or "direct monitor" processing off.

### Windows capture recipes

1. Build/run the Standalone `Guitar Companion.exe` (or the VST3 host) and open
   **Audio & MIDI**. Choose the ASIO driver (e.g. `Focusrite USB ASIO`), 48 kHz
   and 128 frames; the ASIO path exists only when the build was configured with
   `-DASIOSDK_DIR=<path containing common/iasiodrv.h>`.
2. Route the click through the app's monitored output and record the physical
   return on another input. Disable the interface's hardware **direct monitor**
   so the recorded signal is the software return, not the analog bypass.
3. Capture with a host (Reaper, Audacity) set to the same 48 kHz rate, or with
   ffmpeg DirectShow:

```powershell
ffmpeg -f dshow -i audio="Line (Focusrite USB Audio)" -ar 48000 -ac 2 -t 5 raw\loopback.wav
```

Start the recorder before playback and keep both reference and return in one
file (channel-split) whenever the interface exposes a loopback or two inputs.

### Recording caveats that invalidate a measurement

- **Hardware direct monitoring bypasses the app** and measures only analog/ADC
  path, not application monitoring latency. Disable it for app-latency tests.
- A **separate** reference file cannot be used as an absolute timebase unless
  both recordings share a synchronised start; prefer one two-channel file.
- The WAV **sample rate must match** the session/interface rate, and at least the
  loop-back (and reference) channels must be present, or the record is invalid.
- Bluetooth devices, "communications" WASAPI modes and vendor effects with
  hidden latency are not the reference ASIO setup and may legitimately fail the
  12 ms target.

## 3. Ingest a play trial (receipt-gated)

```sh
python3 tools/device-validation/ingest_play_trial.py --session <dir> \
  --condition clean_strumming \
  --style Rock --jam-mode follow --intensity 0.5 --complexity 0.4 \
  --fill-amount 0.3 --follow-tightness 0.5 --meter 4/4 --tempo-bpm 120 \
  --useful-lock yes --lock-window-bars 2 --time-to-lock-s 1.8 \
  --lock-receipt raw/diagnostics-trace.json \
  --timing-receipt raw/diagnostics-trace.json \
  --callback-receipt raw/diagnostics-trace.json \
  --callback-p99-ms 2.4 --callback-deadline-misses 0 --analysis-overruns 0 \
  --dropouts 0 --dropout-receipt raw/diagnostics-trace.json \
  --join-sensible yes --stayed-stable yes --overreacted no \
  --fills-musical yes --push-pull yes --recovered-tap-resync yes
```

Rules enforced:

- any numeric timing/dropout/callback/lock value must be accompanied by the
  matching `--*-receipt`; otherwise it must be omitted (recorded `unmeasured`);
- `--useful-lock yes` requires a measured two-bar window and time-to-lock with a
  receipt — a bare assertion is rejected;
- the six SPEC 20 judgements are stored as `operator-report`, never as measured.

The play conditions are `clean_strumming`, `distorted_rhythm`,
`palm_muted_metal`, `blues_shuffle`, `syncopated_funk`, `sparse_single_note`.

## 4. Ingest the edge conditions

```sh
python3 tools/device-validation/ingest_functional.py --session <dir> \
  --condition device_disconnect_reconnect --outcome pass \
  --receipt raw/disconnect-log.txt
```

Conditions: `device_disconnect_reconnect`, `input_channel_change`,
`silent_input`, `clipped_input`. A receipt is mandatory and is hashed.

## 5. Validate

```sh
python3 tools/device-validation/validate_evidence.py --session <dir> \
  --gate all --summary-md <dir>/summary.md
```

Exit codes: `0` gate passes, `1` hard failure (schema, hash mismatch, edited /
unbacked measurement, unknown identity), `2` no hard failure but the matrix is
empty/incomplete/failing.

The validator:

- re-derives every latency from the raw WAV with the recorded parameters, so an
  edited number is rejected (`latency-recheck-value`);
- re-hashes every raw file and receipt;
- cross-checks each record's interface identity against the session manifest;
- requires the SPEC 21.5 matrix to be non-empty and every cell measured;
- requires Windows ASIO cells to identify backend **and** driver and carry an
  actual physical measurement (callback estimates alone do not count);
- requires `asio_48k_128` physical latency `<= 12 ms` for the monitoring gate;
- excludes every `synthetic: true` record from the physical gates and reports it
  only under `synthetic_selftest`.

## 6. Synthetic self-test

```sh
python3 tools/device-validation/make_synthetic_evidence.py --out /tmp/devval-synthetic
python3 tools/device-validation/validate_evidence.py \
  --session /tmp/devval-synthetic --allow-synthetic-selftest
```

Expect `overall_pass: false`, `synthetic_selftest: PASS`, exit `2`. The clean
two-channel case has a known 96-sample (2.0 ms at 48 kHz) delay; the ambiguous,
silent, clipped, non-finite and rate-mismatch cases must each be rejected with
their named reason.

## 7. Environment probe (read-only)

```sh
python3 tools/device-validation/probe_environment.py --out /tmp/devval-env.json
```

Reports device nodes and capture programs. It installs nothing and changes no
device state.

## Tests

```sh
python3 -m unittest discover -s tools/device-validation/tests -v
```

Stdlib only. The "complete matrix" fixtures are constructed in temp directories
to exercise aggregation; they are never committed or presented as a real run.
