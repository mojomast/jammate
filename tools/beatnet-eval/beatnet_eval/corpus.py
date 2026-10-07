"""Read the repository's rhythm corpus ground truth (pure stdlib).

Ground truth is the committed ``testdata/rhythm/manifest.json``. Loading the
manifest hashes the actual WAV bytes against its recorded hashes; audio is not
decoded. Declared observation hashes are separately checked by the scorer.
"""

from __future__ import annotations

import hashlib
import json
import os
from typing import Any


def load_manifest(corpus_dir: str) -> dict:
    path = os.path.join(corpus_dir, "manifest.json")
    with open(path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    for fixture in fixtures(manifest):
        wav_path = os.path.join(corpus_dir, fixture["file"])
        digest = hashlib.sha256()
        with open(wav_path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                digest.update(chunk)
        if digest.hexdigest() != fixture_sha256(fixture):
            raise ValueError(f"fixture {fixture['name']!r}: actual WAV sha256 does not match manifest")
    return manifest


def fixtures(manifest: dict) -> list[dict]:
    return list(manifest.get("fixtures", []))


def fixture_by_name(manifest: dict, name: str) -> dict:
    for fx in manifest.get("fixtures", []):
        if fx.get("name") == name:
            return fx
    raise KeyError(f"fixture not in manifest: {name!r}")


def core_names(manifest: dict) -> set[str]:
    return set(manifest.get("tagVocabulary", {}).get("core", []))


def ground_truth_beats(fixture: dict) -> list[float]:
    return [float(t) for t in fixture.get("beats", [])]


def beats_per_bar(fixture: dict) -> int:
    meter = fixture.get("meter") or {}
    value = meter.get("beatsPerBar")
    if not isinstance(value, int) or value <= 0:
        raise ValueError(f"fixture {fixture.get('name')!r} has no usable meter.beatsPerBar")
    return value


def nominal_bpm(fixture: dict) -> float | None:
    value = fixture.get("nominalBpm")
    return float(value) if value is not None else None


def fixture_sha256(fixture: dict) -> str:
    value = fixture.get("sha256")
    if not isinstance(value, str) or len(value) != 64:
        raise ValueError(f"fixture {fixture.get('name')!r} has no sha256")
    return value


def summarise(manifest: dict) -> dict[str, Any]:
    fx = fixtures(manifest)
    return {
        "fixtures": len(fx),
        "core": len(core_names(manifest)),
        "totalDurationSeconds": manifest.get("totalDurationSeconds"),
        "totalBytes": manifest.get("totalBytes"),
        "toleranceSeconds": (manifest.get("conventions") or {}).get("beatToleranceSeconds"),
    }
