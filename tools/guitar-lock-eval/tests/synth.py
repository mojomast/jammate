"""Shared synthetic builders for the guitar-lock-eval tests.

These build WAV headers and trace/annotation objects in memory so the scorer and
validators can be tested without any real or third-party audio. Every object is
labelled `synthetic-test`; nothing here is evidence.
"""
from __future__ import annotations

import json
import math
import os
import struct
import wave

from glock.manifest import Annotation, Recording
from glock.trace import Beat, Receipt, TempoSample, Trace


def write_silence_wav(path: str, rate: int = 48000, seconds: float = 8.0,
                      channels: int = 1, width: int = 2) -> str:
    frames = int(rate * seconds)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(width)
        wf.setframerate(rate)
        wf.writeframes(b"\x00" * (frames * channels * width))
    return path


def make_annotation(nominal_bpm: float = 120.0, bpb: int = 4, n_beats: int = 32,
                    source: str = "human", true_silence_spans=None) -> Annotation:
    period = 60.0 / nominal_bpm
    beats = [round(i * period, 9) for i in range(n_beats)]
    return Annotation(
        source=source, license="test", tempo_profile="constant",
        beats=beats, onsets=list(beats), beats_per_bar=bpb,
        meter_numerator=4, meter_denominator=4,
        true_silence_spans=[list(s) for s in (true_silence_spans or [])],
        nominal_bpm=nominal_bpm,
    )


def make_recording(rec_id: str = "synth", nominal_bpm: float = 120.0,
                   classification: str = "synthetic",
                   annotation: Annotation | None = None,
                   tags=None, representative: bool = True,
                   license: str = "test", ownership: str = "test") -> Recording:
    ann = annotation or make_annotation(nominal_bpm)
    return Recording(
        id=rec_id, audio_path="unused.wav", classification=classification,
        license=license, ownership=ownership, provenance="synthetic-test",
        representative=representative, tags=list(tags or []),
        annotation=ann,
        declared={"audio_sha256": "0" * 64, "sample_rate": 48000.0,
                  "channels": 1, "sample_width_bytes": 2,
                  "frames": 384000, "duration_seconds": 8.0},
    )


def perfect_trace(nominal_bpm: float = 120.0, n_beats: int = 32,
                  start: float = 0.0, bpm: float | None = None,
                  backend: str = "synthetic-test",
                  horizon_offset: float = 0.01) -> Trace:
    ann = make_annotation(nominal_bpm, n_beats=n_beats)
    beats = []
    tempo = []
    for i, event in enumerate(ann.beats):
        e = start + event
        beats.append(Beat(event_seconds=e, horizon_seconds=e + horizon_offset,
                          reported_sample=int(e * 48000),
                          reported_bpm=(bpm if bpm is not None else nominal_bpm),
                          bpm_phase_valid=True))
        tempo.append(TempoSample(event_seconds=e, horizon_seconds=e + horizon_offset,
                                 bpm=(bpm if bpm is not None else nominal_bpm),
                                 phase_valid=True))
    return Trace(path="<synthetic>", schema="guitar-lock-eval/trace/1.0",
                 recording_id="synth", backend=backend, backend_kind="real",
                 parent_backend=None, derivation=None,
                 audio_sha256="0" * 64, sample_rate=48000.0, block_frames=128,
                 source={"tool": "synthetic-test", "tool_sha256": "0" * 64},
                 receipt=Receipt(measured=True, wall_utc="2026-01-01T00:00:00Z",
                                 audio_seconds=(start + ann.beats[-1] + horizon_offset)),
                 beats=beats, tempo_samples=tempo)


def offset_beats_trace(nominal_bpm: float = 120.0, n_beats: int = 32,
                       first_correct_index: int = 0,
                       bad_offset: float | None = None) -> Trace:
    """Trace whose early beats are offset (bad phase) then lock correctly.

    `bad_offset` defaults to half a beat, which cannot match the grid.
    """
    period = 60.0 / nominal_bpm
    if bad_offset is None:
        bad_offset = period / 2.0
    ann = make_annotation(nominal_bpm, n_beats=n_beats)
    beats, tempo = [], []
    for i, event in enumerate(ann.beats):
        e = event + (bad_offset if i < first_correct_index else 0.0)
        beats.append(Beat(event_seconds=e, horizon_seconds=e + 0.01,
                          reported_sample=int(e * 48000),
                          reported_bpm=nominal_bpm, bpm_phase_valid=True))
        tempo.append(TempoSample(event_seconds=e, horizon_seconds=e + 0.01,
                                 bpm=nominal_bpm, phase_valid=True))
    return Trace(path="<synthetic>", schema="guitar-lock-eval/trace/1.0",
                 recording_id="synth", backend="synthetic-test", backend_kind="real",
                 parent_backend=None, derivation=None, audio_sha256="0" * 64,
                 sample_rate=48000.0, block_frames=128,
                 source={"tool": "synthetic-test", "tool_sha256": "0" * 64},
                 receipt=Receipt(measured=True, wall_utc="2026-01-01T00:00:00Z",
                                 audio_seconds=ann.beats[-1] + 0.01),
                 beats=beats, tempo_samples=tempo)
