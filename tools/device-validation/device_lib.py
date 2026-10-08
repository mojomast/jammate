#!/usr/bin/env python3
"""Shared, dependency-free helpers for tools/device-validation.

This module is deliberately stdlib-only (Python 3.8+ syntax, tested on 3.13).
It provides:

  * canonical JSON I/O that rejects non-finite constants;
  * a strict RIFF/WAVE reader (PCM 8/16/24/32 + IEEE float 32/64, mono or
    multi-channel, WAVE_FORMAT_EXTENSIBLE) and a deterministic writer;
  * signal quality checks (peak/RMS/DC/clipping/non-finite/silence);
  * two independent latency estimators used for physical loop-back analysis:
    a bounded normalised matched filter (preferred, two channels in one file)
    and a single-channel threshold onset;
  * the condition matrix from SPEC.md section 21.5 plus the musical play
    conditions from section 20.

Nothing here claims a measurement. A latency number is only a measurement when
``analyze_loopback`` sets ``valid`` true from actual recorded bytes; every tool
keeps that distinction explicit.
"""

import hashlib
import json
import math
import os
import struct
import time

# ---------------------------------------------------------------------------
# Schema and identity constants
# ---------------------------------------------------------------------------

SESSION_SCHEMA = "device-validation/session/1.0"
LATENCY_SCHEMA = "device-validation/latency/1.0"
PLAY_TRIAL_SCHEMA = "device-validation/play-trial/1.0"
FUNCTIONAL_SCHEMA = "device-validation/functional/1.0"
RECEIPT_SCHEMA = "device-validation/receipt/1.0"

SCHEMAS = (SESSION_SCHEMA, LATENCY_SCHEMA, PLAY_TRIAL_SCHEMA, FUNCTIONAL_SCHEMA)

# Receipt kinds and the metric groups a parseable receipt may carry.
RECEIPT_KINDS = ("play-trial", "functional")

# Actual identifiers from CMakeLists.txt and README.md of the product.
PRODUCT_DEFAULTS = {
    "name": "Guitar Companion",
    "company": "Raphael Fukuda",
    "bundle_id": "com.raphaelfukuda.GuitarCompanion",
    "plugin_manufacturer_code": "Raph",
    "plugin_code": "Gtco",
    "standalone_executable_windows": "Guitar Companion.exe",
    "vst3_bundle": "Guitar Companion.vst3",
    "cmake_plugin_target": "GuitarCompanion",
    "asio_cmake_option": "-DASIOSDK_DIR=<path containing common/iasiodrv.h>",
    "reference_config": "48 kHz / 128 frames / ASIO USB interface / same-interface headphones",
}

# SPEC.md 21.5 hardware matrix. ``target_ms`` is only set where the SPEC
# defines a numeric target (18.3: <= ~12 ms at 48 kHz / 128 frames ASIO).
LATENCY_CONDITIONS = {
    "asio_48k_64": {"backend": "asio", "os": "windows", "rate": 48000,
                    "block": 64, "target_ms": None},
    "asio_48k_128": {"backend": "asio", "os": "windows", "rate": 48000,
                     "block": 128, "target_ms": 12.0},
    "asio_48k_256": {"backend": "asio", "os": "windows", "rate": 48000,
                     "block": 256, "target_ms": None},
    "wasapi_low_latency": {"backend": "wasapi", "os": "windows", "rate": 48000,
                           "block": None, "target_ms": None},
}

# SPEC.md 21.5 non-latency edge conditions.
FUNCTIONAL_CONDITIONS = (
    "device_disconnect_reconnect",
    "input_channel_change",
    "silent_input",
    "clipped_input",
)

# SPEC.md 20 musical play-test material.
PLAY_CONDITIONS = (
    "clean_strumming",
    "distorted_rhythm",
    "palm_muted_metal",
    "blues_shuffle",
    "syncopated_funk",
    "sparse_single_note",
)

REQUIRED_HARDWARE_CONDITIONS = tuple(LATENCY_CONDITIONS) + FUNCTIONAL_CONDITIONS

KNOWN_BACKENDS = ("asio", "wasapi", "directsound", "coreaudio", "alsa",
                  "jack", "pipewire", "unknown")
KNOWN_OS = ("windows", "linux", "macos")

# Measurement provenance values that may ever back a measured number.
MEASURED_PROVENANCE = ("instrumented-raw", "receipt-attested", "recorded-loopback")
GATED_PROVENANCE = ("instrumented-raw", "recorded-loopback")
UNMEASURED_PROVENANCE = ("unmeasured", "operator-report")

# A sample at or above this magnitude is treated as clipping (PCM decoders
# normalise by the full-scale magnitude, so this is robust to converter scale).
CLIP_LEVEL = 1.0


class DeviceValidationError(Exception):
    """Hard, fail-closed input error (bad flag, missing/garbled file, ...)."""


class WavError(DeviceValidationError):
    """The file is not a readable WAV we can trust."""


# ---------------------------------------------------------------------------
# Small generic helpers
# ---------------------------------------------------------------------------

def utc_now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def is_hex64(value):
    return (isinstance(value, str) and len(value) == 64
            and all(c in "0123456789abcdef" for c in value))


def is_git_sha(value):
    return (isinstance(value, str) and len(value) == 40
            and all(c in "0123456789abcdef" for c in value))


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def finite(value):
    return is_number(value) and math.isfinite(value)


def nonneg_int(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _reject_constant(name):
    raise ValueError("non-finite JSON constant not allowed: %s" % name)


def load_json_strict(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.loads(fh.read(), parse_constant=_reject_constant)


def write_json(path, obj):
    """Deterministic JSON: sorted keys, 2-space indent, trailing newline."""
    parent = os.path.dirname(os.path.abspath(path))
    os.makedirs(parent, exist_ok=True)
    text = json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False,
                       allow_nan=False)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text + "\n")


def resolve_path(base_dir, path):
    if not path:
        return None
    if os.path.isabs(path):
        return path
    return os.path.normpath(os.path.join(base_dir, path))


def relative_or_absolute(base_dir, path):
    """Store paths relative to the session when they live inside it."""
    ap = os.path.abspath(resolve_path(base_dir, path))
    base = os.path.abspath(base_dir)
    try:
        rel = os.path.relpath(ap, base)
    except ValueError:
        return ap
    if rel.startswith(".."):
        return ap
    return rel


def is_contained(base_dir, path):
    """True when ``path`` resolves inside ``base_dir`` (no absolute, no '..')."""
    if not path or os.path.isabs(path):
        return False
    ap = os.path.abspath(resolve_path(base_dir, path))
    base = os.path.abspath(base_dir)
    try:
        rel = os.path.relpath(ap, base)
    except ValueError:
        return False
    return not (rel == ".." or rel.startswith(".." + os.sep))


def amp_from_dbfs(dbfs):
    return 10.0 ** (dbfs / 20.0)


def dbfs(amplitude):
    if amplitude <= 0.0:
        return -float("inf")
    return 20.0 * math.log10(amplitude)


def raw_ref(base_dir, path, role=None):
    """Record a raw file reference with the hash computed from actual bytes.

    The file must live inside the session directory: physical evidence paths
    are session-contained and relative, never absolute or ``..``.
    """
    if not path:
        return None
    if not is_contained(base_dir, path):
        raise DeviceValidationError(
            "raw file must be inside the session directory: %s" % path)
    resolved = resolve_path(base_dir, path)
    if resolved is None or not os.path.isfile(resolved):
        raise DeviceValidationError("raw file not found: %s" % resolved)
    return {
        "path": relative_or_absolute(base_dir, path),
        "sha256": sha256_file(resolved),
        "bytes": os.path.getsize(resolved),
        "role": role,
    }


# ---------------------------------------------------------------------------
# Receipts: parseable, raw-metric-bearing evidence
# ---------------------------------------------------------------------------

def load_receipt(base_dir, ref):
    """Return ``(receipt_dict_or_None, reason)`` for a raw reference.

    A parseable receipt is JSON with ``RECEIPT_SCHEMA``; anything else (plain
    log, empty file, wrong schema) yields ``None`` and the caller must fall back
    to ``receipt-attested`` (non-gating) or ``operator-report``.
    """
    if not ref or not ref.get("path"):
        return None, "no-receipt"
    resolved = resolve_path(base_dir, ref["path"])
    if not resolved or not os.path.isfile(resolved):
        return None, "receipt-missing"
    try:
        doc = load_json_strict(resolved)
    except (ValueError, OSError):
        return None, "receipt-unparseable"
    if not isinstance(doc, dict) or doc.get("schema") != RECEIPT_SCHEMA:
        return None, "receipt-schema-mismatch"
    return doc, None


def receipt_value(receipt, group, key):
    """Return ``(found, value)`` for ``metrics.<group>.<key>``."""
    if not isinstance(receipt, dict):
        return False, None
    metrics = receipt.get("metrics")
    if not isinstance(metrics, dict):
        return False, None
    block = metrics.get(group)
    if not isinstance(block, dict) or key not in block:
        return False, None
    return True, block[key]


# ---------------------------------------------------------------------------
# Canonical parameter derivation and validation
# ---------------------------------------------------------------------------

LATENCY_PARAM_KEYS = (
    "method", "loopback_channel", "reference_channel", "reference_wav",
    "reference_onset_ms", "search_start_ms", "search_end_ms",
    "onset_threshold_dbfs", "silence_dbfs", "min_correlation",
    "min_dominance", "template_ms", "ambiguity_sep_ms",
    "expected_rate", "expected_channels", "target_ms",
)


def validate_latency_params(params):
    """Structural validation of a latency analysis params block.

    Returns a list of human-readable problems; an empty list means the block is
    safe to derive from.
    """
    problems = []
    if not isinstance(params, dict):
        return ["params-not-object"]
    method = params.get("method")
    if method not in ("two-channel", "single-channel"):
        problems.append("method-%r" % method)
    for key in ("loopback_channel",):
        if not nonneg_int(params.get(key)):
            problems.append("%s-not-nonneg-int" % key)
    if method == "two-channel":
        if not nonneg_int(params.get("reference_channel")):
            problems.append("reference_channel-not-nonneg-int")
        elif params.get("reference_channel") == params.get("loopback_channel"):
            problems.append("reference-equals-loopback")
    for key in ("search_start_ms", "search_end_ms", "onset_threshold_dbfs",
                "silence_dbfs", "min_correlation", "min_dominance",
                "template_ms", "ambiguity_sep_ms"):
        if not finite(params.get(key)):
            problems.append("%s-not-finite" % key)
    if finite(params.get("search_start_ms")) and finite(params.get("search_end_ms")):
        if params["search_start_ms"] >= params["search_end_ms"]:
            problems.append("search-window-empty")
    if finite(params.get("template_ms")) and params["template_ms"] <= 0.0:
        problems.append("template-ms-not-positive")
    if finite(params.get("ambiguity_sep_ms")) and params["ambiguity_sep_ms"] <= 0.0:
        problems.append("ambiguity-sep-not-positive")
    mc = params.get("min_correlation")
    if finite(mc) and not (0.0 < mc <= 1.0):
        problems.append("min-correlation-out-of-range")
    md = params.get("min_dominance")
    if finite(md) and md < 1.0:
        problems.append("min-dominance-below-one")
    if params.get("reference_onset_ms") is not None \
            and not finite(params.get("reference_onset_ms")):
        problems.append("reference-onset-not-finite")
    if params.get("expected_rate") is not None \
            and not nonneg_int(params.get("expected_rate")):
        problems.append("expected-rate-not-nonneg-int")
    if params.get("expected_channels") is not None \
            and not nonneg_int(params.get("expected_channels")):
        problems.append("expected-channels-not-nonneg-int")
    if params.get("target_ms") is not None and not finite(params.get("target_ms")):
        problems.append("target-ms-not-finite")
    return problems


def canonical_latency_expectations(condition, interface):
    """Canonical expected_rate/target_ms for a condition, from the SPEC.

    These come from the immutable condition spec and the session interface, not
    from the record's own ``analysis.params``, so a tampered record cannot move
    its own threshold or expected rate.
    """
    spec = condition_spec(condition)
    expected_rate = spec.get("rate")
    if expected_rate is None:
        expected_rate = (interface or {}).get("sample_rate")
    return {"expected_rate": expected_rate, "target_ms": spec.get("target_ms")}


def required_channels(params):
    if params.get("method") == "two-channel":
        return max(int(params["loopback_channel"]),
                   int(params["reference_channel"])) + 1
    return int(params["loopback_channel"]) + 1


# ---------------------------------------------------------------------------
# WAV reading / writing
# ---------------------------------------------------------------------------

class Wav(object):
    __slots__ = ("path", "sample_rate", "channels", "bits", "format_code",
                 "frames", "samples", "clip_counts", "nonfinite_counts")

    def __init__(self, path, sample_rate, channels, bits, format_code, samples,
                 clip_counts, nonfinite_counts):
        self.path = path
        self.sample_rate = sample_rate
        self.channels = channels
        self.bits = bits
        self.format_code = format_code  # 1 = PCM int, 3 = IEEE float
        self.frames = len(samples[0]) if samples else 0
        self.samples = samples          # list[channel] -> list[float]
        self.clip_counts = clip_counts  # list[channel] -> int
        self.nonfinite_counts = nonfinite_counts


def _decode_pcm(raw, bits, channels):
    bytes_per = bits // 8
    frame_bytes = bytes_per * channels
    frames = len(raw) // frame_bytes
    out = [[] for _ in range(channels)]
    clips = [0] * channels
    if bits == 8:
        imax, imin = 255, 0
        for f in range(frames):
            base = f * frame_bytes
            for c in range(channels):
                u = raw[base + c]
                if u == imax or u == imin:
                    clips[c] += 1
                out[c].append((u - 128) / 128.0)
    elif bits == 16:
        imax, imin = 32767, -32768
        for f in range(frames):
            base = f * frame_bytes
            for c in range(channels):
                v = struct.unpack_from("<h", raw, base + 2 * c)[0]
                if v == imax or v == imin:
                    clips[c] += 1
                out[c].append(v / 32768.0)
    elif bits == 24:
        imax, imin = 8388607, -8388608
        for f in range(frames):
            base = f * frame_bytes
            for c in range(channels):
                o = base + 3 * c
                v = raw[o] | (raw[o + 1] << 8) | (raw[o + 2] << 16)
                if v & 0x800000:
                    v -= 0x1000000
                if v == imax or v == imin:
                    clips[c] += 1
                out[c].append(v / 8388608.0)
    elif bits == 32:
        imax, imin = 2147483647, -2147483648
        for f in range(frames):
            base = f * frame_bytes
            for c in range(channels):
                v = struct.unpack_from("<i", raw, base + 4 * c)[0]
                if v == imax or v == imin:
                    clips[c] += 1
                out[c].append(v / 2147483648.0)
    else:
        raise WavError("unsupported PCM bit depth: %d" % bits)
    return out, clips


def _decode_float(raw, bits, channels):
    fmt = "<f" if bits == 32 else "<d"
    width = bits // 8
    frame_bytes = width * channels
    frames = len(raw) // frame_bytes
    out = [[] for _ in range(channels)]
    clips = [0] * channels
    nonfinite = [0] * channels
    for f in range(frames):
        base = f * frame_bytes
        for c in range(channels):
            v = struct.unpack_from(fmt, raw, base + width * c)[0]
            if not math.isfinite(v):
                nonfinite[c] += 1
            elif abs(v) >= CLIP_LEVEL:
                clips[c] += 1
            out[c].append(v)
    return out, clips, nonfinite


def read_wav(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    if len(raw) < 12 or raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise WavError("not a RIFF/WAVE file: %s" % path)
    pos = 12
    fmt = None
    data = None
    fmt_size = 0
    while pos + 8 <= len(raw):
        cid = raw[pos:pos + 4]
        csize = struct.unpack_from("<I", raw, pos + 4)[0]
        body = pos + 8
        if body + csize > len(raw):
            raise WavError("truncated chunk %r in %s" % (cid, path))
        if cid == b"fmt ":
            if csize < 16:
                raise WavError("short fmt chunk in %s" % path)
            fmt = struct.unpack_from("<HHIIHH", raw, body)
            fmt_size = csize
        elif cid == b"data":
            data = raw[body:body + csize]
        pos = body + csize + (csize & 1)
    if fmt is None:
        raise WavError("no fmt chunk in %s" % path)
    if data is None:
        raise WavError("no data chunk in %s" % path)
    audio_format, channels, sample_rate, _byte_rate, _align, bits = fmt
    if audio_format == 0xFFFE:
        if fmt_size < 40:
            raise WavError("short WAVE_FORMAT_EXTENSIBLE fmt in %s" % path)
        # body offset was pos+8 of the fmt chunk; recompute via stored fmt_size
        # find fmt body again is unnecessary: GUID subformat starts 24 bytes in.
        # We re-locate it from the raw scan for clarity.
        fpos = raw.find(b"fmt ")
        sub = raw[fpos + 8 + 24:fpos + 8 + 26]
        audio_format = struct.unpack("<H", sub)[0]
    if channels < 1:
        raise WavError("zero channels in %s" % path)
    if audio_format == 1:
        samples, clips = _decode_pcm(data, bits, channels)
        nonfinite = [0] * channels
    elif audio_format == 3:
        if bits not in (32, 64):
            raise WavError("unsupported float bit depth: %d" % bits)
        samples, clips, nonfinite = _decode_float(data, bits, channels)
    else:
        raise WavError("unsupported WAV format code %d in %s"
                       % (audio_format, path))
    if not samples or not samples[0]:
        raise WavError("zero audio frames in %s" % path)
    return Wav(path, sample_rate, channels, bits, audio_format, samples,
               clips, nonfinite)


def write_wav(path, channels, sample_rate, fmt="pcm16"):
    """Deterministic writer. ``channels`` is a list of per-channel float lists.

    Supported ``fmt``: ``pcm16`` (format 1, 16-bit) and ``float32`` (format 3).
    Float samples may be non-finite on purpose so tests can exercise detection.
    """
    if fmt not in ("pcm16", "float32"):
        raise WavError("unsupported write format: %s" % fmt)
    nch = len(channels)
    if nch < 1 or any(len(c) != len(channels[0]) for c in channels):
        raise WavError("channel lists must be non-empty and equal length")
    nframes = len(channels[0])
    code = 1 if fmt == "pcm16" else 3
    bits = 16 if fmt == "pcm16" else 32
    payload = bytearray()
    if fmt == "pcm16":
        for i in range(nframes):
            for c in range(nch):
                v = channels[c][i]
                if not math.isfinite(v):
                    v = 0.0
                v = max(-1.0, min(1.0, v))
                payload += struct.pack("<h", int(round(v * 32767.0)))
    else:
        for i in range(nframes):
            for c in range(nch):
                payload += struct.pack("<f", channels[c][i])
    block_align = nch * bits // 8
    byte_rate = sample_rate * block_align
    fmt_chunk = struct.pack("<HHIIHH", code, nch, sample_rate, byte_rate,
                            block_align, bits)
    chunks = (b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
              + b"data" + struct.pack("<I", len(payload)) + bytes(payload))
    header = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE"
    with open(path, "wb") as fh:
        fh.write(header + chunks)
    return path


def make_click(rate, amplitude=0.8, freq=1500.0, duration_ms=2.0, decay_ms=0.7,
               offset_ms=0.0, total_ms=60.0):
    """Deterministic short broadband burst used as a latency impulse.

    Deliberately short (a few cycles): a sustained tone has a broad
    autocorrelation and cannot separate nearby loop-back paths.
    """
    total = max(1, int(rate * total_ms / 1000.0))
    start = int(rate * offset_ms / 1000.0)
    dur = max(1, int(rate * duration_ms / 1000.0))
    decay = max(1, int(rate * decay_ms / 1000.0))
    sig = [0.0] * total
    for i in range(dur):
        if start + i >= total:
            break
        sig[start + i] = amplitude * math.sin(2.0 * math.pi * freq * i / rate) \
            * math.exp(-i / decay)
    return sig


# ---------------------------------------------------------------------------
# Signal analysis
# ---------------------------------------------------------------------------

def channel_quality(x, clip_count=0):
    n = len(x)
    peak = 0.0
    sumsq = 0.0
    dc = 0.0
    nonfinite = 0
    for v in x:
        if not math.isfinite(v):
            nonfinite += 1
            continue
        a = abs(v)
        if a > peak:
            peak = a
        sumsq += v * v
        dc += v
    rms = math.sqrt(sumsq / n) if n else 0.0
    return {
        "frames": n,
        "peak": peak,
        "peak_dbfs": dbfs(peak) if peak > 0 else None,
        "rms": rms,
        "rms_dbfs": dbfs(rms) if rms > 0 else None,
        "dc_offset": (dc / n) if n else 0.0,
        "clipped_samples": int(clip_count),
        "nonfinite_samples": int(nonfinite),
    }


def _moving_max_envelope(x, win):
    env = []
    for i in range(0, len(x), win):
        m = 0.0
        for v in x[i:i + win]:
            if math.isfinite(v):
                a = abs(v)
                if a > m:
                    m = a
        env.append((i, m))
    return env


def _refine_peak(x, start, rate, window_ms=20.0):
    end = min(len(x), start + max(1, int(rate * window_ms / 1000.0)))
    best_i = start
    best = -1.0
    for i in range(start, end):
        v = x[i]
        if math.isfinite(v) and abs(v) > best:
            best = abs(v)
            best_i = i
    return best_i, max(best, 0.0)


def threshold_onsets(x, rate, threshold_dbfs, hop_ms=2.0, gap_ms=15.0,
                     noise_factor=4.0):
    """Return a strength-ordered list of onset events.

    Each event: ``{"sample", "time_ms", "strength"}``. A single isolated click
    yields one event; repeated comparable clicks yield several, which callers
    use to detect ambiguity rather than silently pick one.
    """
    win = max(1, int(rate * hop_ms / 1000.0))
    env = _moving_max_envelope(x, win)
    if not env:
        return []
    vals = sorted(v for _, v in env)
    floor = vals[len(vals) // 10] if vals else 0.0
    thr = max(floor * noise_factor, amp_from_dbfs(threshold_dbfs))
    events = []
    last_sample = None
    for idx, (start, value) in enumerate(env):
        if value < thr:
            continue
        if last_sample is not None and (start - last_sample) < rate * gap_ms / 1000.0:
            # Same event: keep the stronger of the two.
            if events and value > events[-1]["strength_raw"]:
                sample, strength = _refine_peak(x, start, rate)
                events[-1].update(sample=sample,
                                  time_ms=1000.0 * sample / rate,
                                  strength_raw=value,
                                  strength=strength)
            last_sample = start
            continue
        sample, strength = _refine_peak(x, start, rate)
        events.append({"sample": sample, "time_ms": 1000.0 * sample / rate,
                       "strength_raw": value, "strength": strength})
        last_sample = start
    events.sort(key=lambda e: e["strength"], reverse=True)
    for e in events:
        e.pop("strength_raw", None)
    return events


def _norm(template):
    s = 0.0
    for t in template:
        s += t * t
    return math.sqrt(s)


def matched_filter_scores(template, x, ref_start, lag_min, lag_max):
    """Normalised cross-correlation of ``template`` against ``x``.

    Lag ``d`` means the loop-back segment starts at ``ref_start + d``. Positive
    lag therefore means the return arrived later than the reference.
    """
    tlen = len(template)
    tnorm = _norm(template)
    scores = []
    if tnorm <= 0.0:
        return scores
    for d in range(lag_min, lag_max + 1):
        start = ref_start + d
        if start < 0 or start + tlen > len(x):
            continue
        dot = 0.0
        xnorm = 0.0
        ok = True
        for k in range(tlen):
            v = x[start + k]
            if not math.isfinite(v):
                ok = False
                break
            dot += template[k] * v
            xnorm += v * v
        if not ok:
            continue
        denom = tnorm * math.sqrt(xnorm)
        scores.append((d, dot / denom if denom > 0 else 0.0))
    return scores


def pick_candidates(scores, min_height, min_sep):
    """Local maxima above ``min_height`` separated by at least ``min_sep``.

    Returns ``(best, second, dominance, ambiguous)`` where dominance is
    ``best / second`` (or +inf when no distinct second peak exists).
    """
    peaks = []
    n = len(scores)
    for i in range(n):
        d, s = scores[i]
        if s < min_height:
            continue
        if i > 0 and scores[i - 1][1] > s:
            continue
        if i + 1 < n and scores[i + 1][1] > s:
            continue
        peaks.append((d, s))
    peaks.sort(key=lambda p: p[1], reverse=True)
    selected = []
    for d, s in peaks:
        if all(abs(d - d2) >= min_sep for d2, _ in selected):
            selected.append((d, s))
    best = selected[0] if selected else None
    second = selected[1] if len(selected) > 1 else None
    if best is None:
        return None, None, 0.0, True
    if second is None or second[1] <= 0.0:
        return best, None, float("inf"), False
    dominance = best[1] / second[1]
    return best, second, dominance, False


def _fail_reasons(quality, expected_rate, expected_channels, wav, silent_dbfs):
    reasons = []
    if quality["nonfinite_samples"] > 0:
        reasons.append("nonfinite-samples")
    if quality["clipped_samples"] > 0:
        reasons.append("clipped-samples")
    rms_dbfs = quality["rms_dbfs"]
    if rms_dbfs is None or rms_dbfs <= silent_dbfs:
        reasons.append("silent")
    if expected_rate is not None and wav.sample_rate != expected_rate:
        reasons.append("sample-rate-mismatch")
    if expected_channels is not None and wav.channels < expected_channels:
        reasons.append("channel-count-mismatch")
    return reasons


def analyze_loopback(wav_path, params, base_dir=None):
    """Estimate physical round-trip latency from a recorded loop-back WAV.

    Returns a JSON-ready analysis dict. ``valid`` is true only when the
    recording is clean, matched to the declared configuration and the chosen
    estimator found an unambiguous onset.
    """
    base = base_dir or os.getcwd()
    path = resolve_path(base, wav_path)
    wav = read_wav(path)
    lp = int(params["loopback_channel"])
    if lp < 0 or lp >= wav.channels:
        raise DeviceValidationError("loopback channel %d out of range" % lp)
    quality = channel_quality(wav.samples[lp], wav.clip_counts[lp])
    reasons = _fail_reasons(quality, params.get("expected_rate"),
                            params.get("expected_channels"), wav,
                            params.get("silence_dbfs", -60.0))
    method = params["method"]
    onset = {"detected": False, "method": method, "ambiguous": False,
             "candidates": []}
    latency_ms = None

    if method == "two-channel":
        ref = int(params["reference_channel"])
        if ref < 0 or ref >= wav.channels:
            raise DeviceValidationError("reference channel %d out of range"
                                        % ref)
        if ref == lp:
            raise DeviceValidationError("loopback and reference channels match")
        ref_quality = channel_quality(wav.samples[ref], wav.clip_counts[ref])
        events = threshold_onsets(wav.samples[ref], wav.sample_rate,
                                  params["onset_threshold_dbfs"])
        if not events:
            reasons.append("reference-onset-missing")
            onset["reference"] = {"detected": False, "events": 0}
        else:
            ref_event = events[0]
            onset["reference"] = {
                "detected": True,
                "sample": ref_event["sample"],
                "time_ms": ref_event["time_ms"],
                "strength": ref_event["strength"],
                "event_count": len(events),
                "quality": ref_quality,
            }
            max_template = max(2, int(wav.sample_rate
                                      * params["template_ms"] / 1000.0))
            limit = min(len(wav.samples[ref]), ref_event["sample"] + max_template)
            start = ref_event["sample"]
            peak = 0.0
            for i in range(start, limit):
                v = abs(wav.samples[ref][i])
                if v > peak:
                    peak = v
            floor = max(0.05 * peak, 1e-9)
            end = start + 1
            for i in range(start, limit):
                if abs(wav.samples[ref][i]) >= floor:
                    end = i + 1
            template = wav.samples[ref][start:end]
            onset["reference"]["template_frames"] = len(template)
            if len(template) < 2:
                reasons.append("reference-template-too-short")
            else:
                lag_min = int(wav.sample_rate * params["search_start_ms"] / 1000.0)
                lag_max = int(wav.sample_rate * params["search_end_ms"] / 1000.0)
                scores = matched_filter_scores(template, wav.samples[lp],
                                               ref_event["sample"],
                                               lag_min, lag_max)
                # Polarity-robust: match on |correlation|, so an inverted
                # return is found at the correct lag instead of a spurious
                # positive side-lobe. Ambiguity separation is a small window
                # independent of the template width, so nearby distinct paths
                # (e.g. 96 vs 200 samples) are not collapsed.
                abs_scores = [(d, abs(s)) for d, s in scores]
                min_sep = max(1, int(wav.sample_rate
                                     * params.get("ambiguity_sep_ms", 2.0) / 1000.0))
                best, second, dominance, ambiguous = pick_candidates(
                    abs_scores, params["min_correlation"], min_sep)
                signed = dict(scores)
                onset["candidates"] = [
                    {"lag_samples": d, "abs_correlation": s,
                     "correlation": signed.get(d),
                     "latency_ms": 1000.0 * d / wav.sample_rate}
                    for d, s in (scores_sorted(abs_scores)[:5])
                ]
                if best is None:
                    reasons.append("no-correlation-peak")
                elif ambiguous or dominance < params["min_dominance"]:
                    reasons.append("ambiguous-onset")
                    onset["ambiguous"] = True
                else:
                    latency_ms = 1000.0 * best[0] / wav.sample_rate
                    detected_sample = ref_event["sample"] + best[0]
                    onset.update(detected=True, sample=detected_sample,
                                 time_ms=1000.0 * detected_sample / wav.sample_rate,
                                 correlation=signed.get(best[0]),
                                 abs_correlation=best[1],
                                 dominance=dominance,
                                 lag_samples=best[0])
                onset["ambiguous"] = bool(onset.get("ambiguous")) or bool(ambiguous)
                onset["dominance"] = dominance if math.isfinite(dominance) else None
                if second is not None:
                    onset["second_abs_correlation"] = second[1]

    elif method == "single-channel":
        ref_onset_ms = params.get("reference_onset_ms")
        onset["reference"] = {"detected": ref_onset_ms is not None,
                              "time_ms": ref_onset_ms}
        if ref_onset_ms is None:
            reasons.append("missing-reference-onset")
        events = threshold_onsets(wav.samples[lp], wav.sample_rate,
                                  params["onset_threshold_dbfs"])
        onset["candidates"] = [
            {"sample": e["sample"], "time_ms": e["time_ms"],
             "strength": e["strength"]}
            for e in events[:5]
        ]
        if not events:
            reasons.append("onset-missing")
        else:
            best = events[0]
            if len(events) > 1 and events[1]["strength"] > 0:
                dominance = best["strength"] / events[1]["strength"]
            else:
                dominance = float("inf")
            ambiguous = len(events) > 1 and dominance < params["min_dominance"]
            onset.update(detected=True, sample=best["sample"],
                         time_ms=best["time_ms"], strength=best["strength"])
            onset["dominance"] = dominance if math.isfinite(dominance) else None
            onset["ambiguous"] = bool(ambiguous)
            if ambiguous:
                reasons.append("ambiguous-onset")
            elif ref_onset_ms is not None:
                latency_ms = best["time_ms"] - ref_onset_ms
    else:
        raise DeviceValidationError("unknown method: %s" % method)

    if latency_ms is not None and latency_ms < 0:
        reasons.append("negative-latency")
        latency_ms = None

    target_ms = params.get("target_ms")
    target_met = None
    if latency_ms is not None and target_ms is not None:
        target_met = latency_ms <= target_ms

    # De-duplicate reasons while preserving order.
    seen = set()
    deduped = []
    for r in reasons:
        if r not in seen:
            seen.add(r)
            deduped.append(r)

    return {
        "method": method,
        "params": dict(params),
        "wav": {
            "sample_rate": wav.sample_rate,
            "channels": wav.channels,
            "bits": wav.bits,
            "format_code": wav.format_code,
            "frames": wav.frames,
            "duration_s": (wav.frames / wav.sample_rate) if wav.sample_rate else None,
        },
        "quality": quality,
        "onset": onset,
        "physical_roundtrip_latency_ms": latency_ms,
        "target_ms": target_ms,
        "target_met": target_met,
        "valid": latency_ms is not None and not deduped,
        "reasons": deduped,
    }


def scores_sorted(scores):
    return sorted(scores, key=lambda p: p[1], reverse=True)


# ---------------------------------------------------------------------------
# Condition / matrix helpers
# ---------------------------------------------------------------------------

def condition_kind(condition):
    if condition in LATENCY_CONDITIONS:
        return "latency"
    if condition in FUNCTIONAL_CONDITIONS:
        return "functional"
    if condition in PLAY_CONDITIONS:
        return "play"
    return "unknown"


def condition_spec(condition):
    meta = dict(LATENCY_CONDITIONS.get(condition, {}))
    meta.setdefault("kind", condition_kind(condition))
    return meta


def interface_reasons(condition, interface):
    """Identity-level mismatches between a condition and selected interface.

    These are separate from DSP/quality reasons but are equally disqualifying;
    both ingest and the validator must produce the identical list.
    """
    spec = condition_spec(condition)
    reasons = []
    if spec.get("backend") and interface.get("backend") != spec["backend"]:
        reasons.append("interface-backend-mismatch")
    if spec.get("os") and interface.get("os") != spec["os"]:
        reasons.append("interface-os-mismatch")
    rate = spec.get("rate") or interface.get("sample_rate")
    if rate and interface.get("sample_rate") != rate:
        reasons.append("interface-rate-mismatch")
    if spec.get("block") and interface.get("block_frames") != spec["block"]:
        reasons.append("interface-block-mismatch")
    if interface.get("backend") == "asio" and not interface.get("driver"):
        reasons.append("asio-driver-missing")
    # A loop-back through a hardware direct-monitor path never exercises the
    # application's software monitoring, so it cannot satisfy a latency cell.
    if condition in LATENCY_CONDITIONS \
            and interface.get("monitoring") != "software-app":
        reasons.append("monitoring-not-software-app")
    return reasons
