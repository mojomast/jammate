"""Input contract for a *real* BeatNet observation file.

The scorer must never accept a hand-written or guessed result. This module
enforces that the document declares a concrete, pinned BeatNet run: source,
repo commit, model, mode, inference model, raw-output hash and driver hash,
plus a per-fixture input WAV hash and honest availability semantics.

There is deliberately no ``--allow-unverified`` bypass.
"""

from __future__ import annotations

import re

SCHEMA = "beatnet-eval/observations/v1"
MODES = {"stream", "realtime", "online", "offline"}
INFERENCE_MODELS = {"PF", "DBN"}
AVAILABILITY_SEMANTICS = {"per_chunk", "online_batch"}
_HEX40 = re.compile(r"^[0-9a-f]{40}$")
_HEX64 = re.compile(r"^[0-9a-f]{64}$")


class InputContractError(ValueError):
    """Raised when an observations document is not a verified real run."""


def _require(condition: bool, reason: str) -> None:
    if not condition:
        raise InputContractError(reason)


def validate_observations(doc: dict) -> None:
    _require(isinstance(doc, dict), "observations document must be a JSON object")
    _require(doc.get("schema") == SCHEMA,
             f"schema must be {SCHEMA!r}, got {doc.get('schema')!r}")

    prov = doc.get("provenance")
    _require(isinstance(prov, dict), "missing 'provenance' object; refusing unverified input")
    _require(prov.get("source") == "BeatNet",
             "provenance.source must be 'BeatNet'; refusing input that does not declare BeatNet")
    commit = prov.get("repo_commit")
    _require(isinstance(commit, str) and bool(_HEX40.match(commit)),
             "provenance.repo_commit must be a 40-hex commit SHA")
    _require(prov.get("mode") in MODES,
             f"provenance.mode must be one of {sorted(MODES)}")
    _require(prov.get("inference_model") in INFERENCE_MODELS,
             f"provenance.inference_model must be one of {sorted(INFERENCE_MODELS)}")
    raw = prov.get("raw_output_sha256")
    _require(isinstance(raw, str) and bool(_HEX64.match(raw)),
             "provenance.raw_output_sha256 must be a 64-hex sha256 of the raw BeatNet output")
    driver = prov.get("driver_sha256")
    _require(isinstance(driver, str) and bool(_HEX64.match(driver)),
             "provenance.driver_sha256 must be a 64-hex sha256 of the driver that produced it")

    semantics = doc.get("availability_semantics")
    _require(semantics in AVAILABILITY_SEMANTICS,
             f"availability_semantics must be one of {sorted(AVAILABILITY_SEMANTICS)}")
    if semantics == "online_batch":
        _require(prov.get("mode") == "online",
                 "availability_semantics 'online_batch' requires provenance.mode 'online'")

    fixtures = doc.get("fixtures")
    _require(isinstance(fixtures, list) and fixtures,
             "fixtures must be a non-empty list")

    any_available = False
    for i, fx in enumerate(fixtures):
        where = f"fixtures[{i}]"
        _require(isinstance(fx, dict), f"{where} must be an object")
        _require(bool(fx.get("name")), f"{where}.name is required")
        wav = fx.get("input_wav_sha256")
        _require(isinstance(wav, str) and bool(_HEX64.match(wav)),
                 f"{where}.input_wav_sha256 must be a 64-hex sha256 of the scored WAV")
        beats = fx.get("beats")
        _require(isinstance(beats, list), f"{where}.beats must be a list")
        for j, obs in enumerate(beats):
            _require(isinstance(obs, dict), f"{where}.beats[{j}] must be an object")
            _require(isinstance(obs.get("time"), (int, float)),
                     f"{where}.beats[{j}].time must be a number")
            available = obs.get("available")
            _require(available is None or isinstance(available, (int, float)),
                     f"{where}.beats[{j}].available must be a number or null")
            if available is not None:
                any_available = True

    if semantics == "per_chunk":
        _require(any_available,
                 "availability_semantics 'per_chunk' requires at least one non-null available time")
    if semantics == "online_batch":
        _require(not any_available,
                 "availability_semantics 'online_batch' must not carry per-event available times")


def load_and_validate(doc: dict) -> dict:
    validate_observations(doc)
    return doc
