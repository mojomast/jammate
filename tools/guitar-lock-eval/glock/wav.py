"""WAV identity and header validation using only the standard library.

`read_wav_info` never decodes or trusts PCM; it reads the RIFF/WAVE header to
establish sample rate, channel count, sample width, frame count and duration so
an imported recording's declared identity can be checked against the bytes. A
format the evaluator cannot score (non-PCM float, non-mono) is reported as an
explicit `ok=False` error, never downgraded to a pass.
"""
from __future__ import annotations

import os
import wave
from dataclasses import dataclass

from .integrity import IntegrityError, sha256_file


@dataclass
class WavInfo:
    path: str
    exists: bool
    size_bytes: int
    sha256: str | None
    sample_rate: float
    channels: int
    sample_width_bytes: int
    frames: int
    duration_seconds: float
    compression: str
    ok: bool
    error: str

    def as_dict(self) -> dict:
        return {
            "path": self.path,
            "exists": self.exists,
            "size_bytes": self.size_bytes,
            "sha256": self.sha256,
            "sample_rate": self.sample_rate,
            "channels": self.channels,
            "sample_width_bytes": self.sample_width_bytes,
            "frames": self.frames,
            "duration_seconds": self.duration_seconds,
            "compression": self.compression,
            "ok": self.ok,
            "error": self.error,
        }


def read_wav_info(path: str, digest: bool = True) -> WavInfo:
    if not os.path.isfile(path):
        raise IntegrityError(f"audio file does not exist: {path}")
    size = os.path.getsize(path)
    sha = sha256_file(path) if digest else None
    try:
        with wave.open(path, "rb") as wf:
            channels = wf.getnchannels()
            width = wf.getsampwidth()
            rate = float(wf.getframerate())
            frames = wf.getnframes()
            compression = wf.getcomptype()
    except wave.Error as exc:
        raise IntegrityError(f"not a readable PCM WAV: {path}: {exc}") from exc

    duration = float(frames) / rate if rate > 0 else 0.0
    error = ""
    ok = True
    if compression != "NONE":
        ok, error = False, f"compressed WAV not supported ({compression})"
    elif channels != 1:
        ok, error = False, f"mono required, got {channels} channels"
    elif width not in (1, 2, 3, 4):
        ok, error = False, f"unsupported sample width {width} bytes"
    elif rate <= 0:
        ok, error = False, "non-positive sample rate"
    elif frames <= 0:
        ok, error = False, "zero frames"
    if not digest:
        sha = sha256_file(path)
    return WavInfo(path=path, exists=True, size_bytes=size, sha256=sha,
                   sample_rate=rate, channels=channels, sample_width_bytes=width,
                   frames=frames, duration_seconds=duration,
                   compression=compression, ok=ok, error=error)
