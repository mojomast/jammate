"""Trace model and fail-closed validation.

A trace is the evidence one backend produced over one recording, reduced to the
beats, tempo samples and receipt metadata the useful-lock scorer needs. Three
clocks are kept distinct and are never mixed:

  * event    — the device time of the audio a report is about;
  * horizon  — the device time at which the backend returned the evidence;
  * receipt  — the wall-clock publication time, plus a block-resolution audio
               receipt equal to the last processed block end.

A trace that is malformed, non-finite, non-monotonic, inconsistent with the
recording identity, or missing the receipt rules fails closed.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .integrity import (IntegrityError, finite, is_hex64, load_json_strict)
from .manifest import Finding, Recording

TRACE_SCHEMA = "guitar-lock-eval/trace/1.0"


@dataclass
class Beat:
    event_seconds: float
    horizon_seconds: float | None
    reported_sample: int | None = None
    reported_bpm: float | None = None
    bpm_phase_valid: bool | None = None


@dataclass
class TempoSample:
    event_seconds: float
    horizon_seconds: float | None
    bpm: float
    phase_valid: bool


@dataclass
class Receipt:
    measured: bool
    wall_utc: str | None = None
    audio_seconds: float | None = None


@dataclass
class Trace:
    path: str
    schema: str
    recording_id: str
    backend: str
    backend_kind: str
    parent_backend: str | None
    derivation: str | None
    audio_sha256: str
    sample_rate: float
    block_frames: int
    source: dict
    receipt: Receipt
    beats: list[Beat]
    tempo_samples: list[TempoSample]
    sha256: str = ""
    raw: dict = field(default_factory=dict)


def _opt_number(value, where: str):
    if value is None:
        return None
    if not finite(value):
        raise IntegrityError(f"{where} must be finite or null")
    return float(value)


def load_trace(path: str) -> Trace:
    from .integrity import sha256_file
    return parse_trace(load_json_strict(path), path, sha256_file(path))


def parse_trace(data: dict, path: str = "<dict>", sha: str = "") -> Trace:
    if data.get("schema") != TRACE_SCHEMA:
        raise IntegrityError(f"{path}: schema must be {TRACE_SCHEMA}, got {data.get('schema')!r}")
    for key in ("recording_id", "backend", "backend_kind", "audio_sha256"):
        if not isinstance(data.get(key), str) or data.get(key) == "":
            raise IntegrityError(f"{path}: {key} must be a non-empty string")
    kind = data["backend_kind"]
    if kind not in ("real", "derived"):
        raise IntegrityError(f"{path}: backend_kind must be real|derived")
    if kind == "derived":
        if not data.get("parent_backend") or not data.get("derivation"):
            raise IntegrityError(f"{path}: derived trace needs parent_backend and derivation")

    beats = []
    raw_beats = data.get("beats")
    if not isinstance(raw_beats, list):
        raise IntegrityError(f"{path}: beats must be a list")
    for i, b in enumerate(raw_beats):
        if not isinstance(b, dict) or not finite(b.get("event_seconds")):
            raise IntegrityError(f"{path}: beats[{i}].event_seconds must be finite")
        beats.append(Beat(
            event_seconds=float(b["event_seconds"]),
            horizon_seconds=_opt_number(b.get("horizon_seconds"), f"{path}.beats[{i}].horizon_seconds"),
            reported_sample=b.get("reported_sample"),
            reported_bpm=_opt_number(b.get("reported_bpm"), f"{path}.beats[{i}].reported_bpm"),
            bpm_phase_valid=b.get("bpm_phase_valid"),
        ))

    tempo = []
    raw_tempo = data.get("tempo_samples")
    if not isinstance(raw_tempo, list):
        raise IntegrityError(f"{path}: tempo_samples must be a list")
    for i, s in enumerate(raw_tempo):
        if not isinstance(s, dict) or not finite(s.get("event_seconds")):
            raise IntegrityError(f"{path}: tempo_samples[{i}].event_seconds must be finite")
        bpm = s.get("bpm")
        if not finite(bpm):
            raise IntegrityError(f"{path}: tempo_samples[{i}].bpm must be finite")
        tempo.append(TempoSample(
            event_seconds=float(s["event_seconds"]),
            horizon_seconds=_opt_number(s.get("horizon_seconds"), f"{path}.tempo_samples[{i}].horizon_seconds"),
            bpm=float(bpm),
            phase_valid=bool(s.get("phase_valid")),
        ))

    receipt_raw = data.get("receipt")
    if not isinstance(receipt_raw, dict) or not isinstance(receipt_raw.get("measured"), bool):
        raise IntegrityError(f"{path}: receipt must be an object with a boolean measured flag")
    receipt = Receipt(
        measured=receipt_raw["measured"],
        wall_utc=receipt_raw.get("wall_utc"),
        audio_seconds=_opt_number(receipt_raw.get("audio_seconds"), f"{path}.receipt.audio_seconds"),
    )

    from .integrity import sha256_file
    return Trace(
        path=path,
        schema=data["schema"],
        recording_id=data["recording_id"],
        backend=data["backend"],
        backend_kind=kind,
        parent_backend=data.get("parent_backend"),
        derivation=data.get("derivation"),
        audio_sha256=data["audio_sha256"],
        sample_rate=float(data.get("sample_rate", 0.0)),
        block_frames=int(data.get("block_frames", 0)),
        source=data.get("source", {}),
        receipt=receipt,
        beats=beats,
        tempo_samples=tempo,
        sha256=sha,
        raw=data,
    )


PLACEHOLDER_HASHES = frozenset({"0" * 64})


def valid_tool_hash(value) -> bool:
    """A declared producer hash must be 64-hex and not an all-zero placeholder."""
    return is_hex64(value) and value not in PLACEHOLDER_HASHES


def validate_trace(trace: Trace, rec: Recording,
                   expected_backend: str | None = None,
                   expected_kind: str | None = None,
                   expected_parent: str | None = None) -> list[Finding]:
    """Validate a trace against its recording and the timing rules. Hard = fail closed.

    `expected_backend`/`expected_kind`/`expected_parent` bind a trace to the slot
    it is being used in (so a mislabelled trace loaded from a directory cannot
    stand in for a real backend). The gate also re-checks this so a direct call
    cannot bypass it.
    """
    findings: list[Finding] = []

    def hard(cond, code, detail):
        if not cond:
            findings.append(Finding("hard", code, detail))

    where = f"{trace.path}"
    if expected_backend is not None:
        hard(trace.backend == expected_backend, "trace_backend_mismatch",
             f"{where}: backend {trace.backend!r} != expected {expected_backend!r}")
    if expected_kind is not None:
        hard(trace.backend_kind == expected_kind, "trace_backend_kind_mismatch",
             f"{where}: backend_kind {trace.backend_kind!r} != expected {expected_kind!r}")
    if expected_parent is not None:
        hard(trace.parent_backend == expected_parent, "trace_parent_mismatch",
             f"{where}: parent_backend {trace.parent_backend!r} != expected {expected_parent!r}")
    hard(trace.recording_id == rec.id, "trace_recording_mismatch",
         f"{where}: recording_id {trace.recording_id!r} != {rec.id!r}")
    hard(trace.audio_sha256 == rec.declared.get("audio_sha256"),
         "trace_audio_sha_mismatch",
         f"{where}: audio_sha256 does not match the recording identity")
    hard(abs(trace.sample_rate - float(rec.declared.get("sample_rate", -1))) <= 1e-6,
         "trace_rate_mismatch", f"{where}: sample_rate does not match the recording")
    hard(trace.block_frames > 0, "trace_block_missing", f"{where}: block_frames must be positive")
    tool_hash = trace.source.get("tool_sha256")
    if not is_hex64(tool_hash):
        hard(False, "trace_tool_identity",
             f"{where}: source.tool_sha256 must be 64-hex")
    elif tool_hash in PLACEHOLDER_HASHES:
        hard(False, "trace_tool_identity_placeholder",
             f"{where}: source.tool_sha256 is a placeholder; hash the actual producer")

    prev_event = None
    for i, b in enumerate(trace.beats):
        hard(finite(b.event_seconds), "trace_event_nonfinite",
             f"{where}: beats[{i}].event_seconds non-finite")
        if b.horizon_seconds is not None:
            hard(finite(b.horizon_seconds), "trace_horizon_nonfinite",
                 f"{where}: beats[{i}].horizon_seconds non-finite")
            hard(b.event_seconds <= b.horizon_seconds + 1e-9, "trace_event_after_horizon",
                 f"{where}: beats[{i}] event {b.event_seconds} > horizon {b.horizon_seconds}")
        if prev_event is not None:
            hard(b.event_seconds >= prev_event - 1e-9, "trace_beats_not_monotonic",
                 f"{where}: beats[{i}] event {b.event_seconds} < previous {prev_event}")
        prev_event = b.event_seconds
        if b.reported_bpm is not None:
            hard(finite(b.reported_bpm), "trace_bpm_nonfinite",
                 f"{where}: beats[{i}].reported_bpm non-finite")

    prev_event = None
    for i, s in enumerate(trace.tempo_samples):
        hard(finite(s.event_seconds), "trace_tempo_event_nonfinite",
             f"{where}: tempo_samples[{i}] non-finite event")
        if s.horizon_seconds is not None:
            hard(s.event_seconds <= s.horizon_seconds + 1e-9, "trace_tempo_event_after_horizon",
                 f"{where}: tempo_samples[{i}] event > horizon")
        if prev_event is not None:
            hard(s.event_seconds >= prev_event - 1e-9, "trace_tempo_not_monotonic",
                 f"{where}: tempo_samples[{i}] not monotonic")
        prev_event = s.event_seconds
        if s.phase_valid:
            hard(s.bpm > 0.0 and finite(s.bpm), "trace_phase_valid_bpm_invalid",
                 f"{where}: tempo_samples[{i}] phase_valid but bpm={s.bpm}")

    # Receipt rules (trace_timing_rules).
    horizons = [b.horizon_seconds for b in trace.beats if b.horizon_seconds is not None]
    horizons += [s.horizon_seconds for s in trace.tempo_samples if s.horizon_seconds is not None]
    max_horizon = max(horizons) if horizons else None
    if trace.receipt.measured:
        hard(bool(trace.receipt.wall_utc), "trace_receipt_wall_missing",
             f"{where}: measured receipt requires wall_utc")
        hard(trace.receipt.audio_seconds is not None, "trace_receipt_audio_missing",
             f"{where}: measured receipt requires audio_seconds")
        if trace.receipt.audio_seconds is not None and max_horizon is not None:
            hard(trace.receipt.audio_seconds >= max_horizon - 1e-9,
                 "trace_receipt_before_horizon",
                 f"{where}: receipt audio {trace.receipt.audio_seconds} < max horizon {max_horizon}")
    else:
        hard(trace.receipt.wall_utc is None and trace.receipt.audio_seconds is None,
             "trace_unmeasured_receipt_not_null",
             f"{where}: an unmeasured receipt must serialise null timestamps")
    return findings
