"""Read the repository's rhythm corpus ground truth (pure stdlib).

Ground truth is the committed ``testdata/rhythm/manifest.json``. This module
does not read or decode audio; a scorer only needs the manifest beat times and
per-fixture metadata, and the audio hashes are checked by the caller.
"""

from __future__ import annotations

import json
import os
from typing import Any


def load_manifest(corpus_dir: str) -> dict:
    path = os.path.join(corpus_dir, "manifest.json")
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


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
