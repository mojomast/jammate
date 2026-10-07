"""Metadata/format gate for a declared BeatNet observation file.

Important limitation: everything in the ``provenance`` block is
**self-reported metadata**. This module can check that the metadata is present,
well-formed and internally consistent, and the scorer separately checks that
each fixture's ``input_wav_sha256`` matches the committed corpus WAV. Neither
check proves that BeatNet actually produced the beats. A score computed here is
only meaningful when accompanied by separately executed driver and raw-output
evidence. This gate is therefore a format/metadata validator, **not** an
authenticity proof and there is deliberately no ``--allow-unverified`` bypass.

Mode semantics enforced here:

- ``availability_semantics == "per_chunk"`` is valid only for the causal
  particle-filter path: ``mode in {stream, realtime}`` and
  ``inference_model == "PF"``. Every observation must carry a finite
  ``available >= time`` (a realtime window is only causally available after the
  window completes, so this is never zero-lookahead).
- ``availability_semantics == "online_batch"`` is valid only for
  ``mode == "online"`` and ``inference_model == "PF"``, and must not carry any
  per-event ``available`` time.
- The non-causal ``offline``/DBN path is out of scope for this causal contract
  and is rejected.
"""

from __future__ import annotations

import math
import re

SCHEMA = "beatnet-eval/observations/v1"
MODES = {"stream", "realtime", "online", "offline"}
INFERENCE_MODELS = {"PF", "DBN"}
AVAILABILITY_SEMANTICS = {"per_chunk", "online_batch"}
PER_CHUNK_MODES = {"stream", "realtime"}
_HEX40 = re.compile(r"^[0-9a-f]{40}$")
_HEX64 = re.compile(r"^[0-9a-f]{64}$")


class InputContractError(ValueError):
    """Raised when an observations document fails the metadata/format gate."""


def _require(condition: bool, reason: str) -> None:
    if not condition:
        raise InputContractError(reason)


def _is_finite_number(value) -> bool:
    """True for a real finite number; bools and NaN/inf are rejected."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    return math.isfinite(value)


def validate_observations(doc: dict) -> None:
    _require(isinstance(doc, dict), "observations document must be a JSON object")
    _require(doc.get("schema") == SCHEMA,
             f"schema must be {SCHEMA!r}, got {doc.get('schema')!r}")

    prov = doc.get("provenance")
    _require(isinstance(prov, dict),
             "missing 'provenance' metadata object; a run must declare its provenance")
    _require(prov.get("source") == "BeatNet",
             "provenance.source must be 'BeatNet' (declared metadata; this is not an authenticity check)")
    commit = prov.get("repo_commit")
    _require(isinstance(commit, str) and bool(_HEX40.match(commit)),
             "provenance.repo_commit must be a 40-hex commit SHA")
    mode = prov.get("mode")
    _require(mode in MODES, f"provenance.mode must be one of {sorted(MODES)}")
    inference = prov.get("inference_model")
    _require(inference in INFERENCE_MODELS,
             f"provenance.inference_model must be one of {sorted(INFERENCE_MODELS)}")
    raw = prov.get("raw_output_sha256")
    _require(isinstance(raw, str) and bool(_HEX64.match(raw)),
             "provenance.raw_output_sha256 must be a 64-hex sha256 of the raw output")
    driver = prov.get("driver_sha256")
    _require(isinstance(driver, str) and bool(_HEX64.match(driver)),
             "provenance.driver_sha256 must be a 64-hex sha256 of the driver that produced it")

    semantics = doc.get("availability_semantics")
    _require(semantics in AVAILABILITY_SEMANTICS,
             f"availability_semantics must be one of {sorted(AVAILABILITY_SEMANTICS)}")

    # Causal contract: only the particle-filter path is in scope.
    _require(inference == "PF",
             "this causal contract covers the particle-filter (PF) path only; "
             f"inference_model {inference!r} is rejected")
    _require(mode != "offline",
             "mode 'offline' is non-causal and outside this causal observation contract")

    if semantics == "per_chunk":
        _require(mode in PER_CHUNK_MODES,
                 f"availability_semantics 'per_chunk' requires mode in {sorted(PER_CHUNK_MODES)}; "
                 f"got {mode!r} (online/offline cannot carry per-chunk availability)")
    else:  # online_batch
        _require(mode == "online",
                 f"availability_semantics 'online_batch' requires mode 'online'; got {mode!r}")

    fixtures = doc.get("fixtures")
    _require(isinstance(fixtures, list) and fixtures,
             "fixtures must be a non-empty list")

    seen_names = set()
    for i, fx in enumerate(fixtures):
        where = f"fixtures[{i}]"
        _require(isinstance(fx, dict), f"{where} must be an object")
        name = fx.get("name")
        _require(isinstance(name, str) and bool(name), f"{where}.name is required")
        _require(name not in seen_names, f"duplicate fixture name: {name!r}")
        seen_names.add(name)
        wav = fx.get("input_wav_sha256")
        _require(isinstance(wav, str) and bool(_HEX64.match(wav)),
                 f"{where}.input_wav_sha256 must be a 64-hex sha256 of the scored WAV")
        beats = fx.get("beats")
        _require(isinstance(beats, list), f"{where}.beats must be a list")
        for j, obs in enumerate(beats):
            at = f"{where}.beats[{j}]"
            _require(isinstance(obs, dict), f"{at} must be an object")
            time = obs.get("time")
            _require(_is_finite_number(time),
                     f"{at}.time must be a finite number (NaN/inf/bool rejected)")
            available = obs.get("available")
            if semantics == "per_chunk":
                _require(_is_finite_number(available),
                         f"{at}.available must be a finite number for per_chunk availability")
                _require(available >= time,
                         f"{at}.available ({available}) must be >= time ({time}); "
                         "an observation cannot be available before it happened")
            else:  # online_batch
                _require(available is None,
                         f"{at}.available must be null for online_batch (no per-event availability)")
        downbeats = fx.get("downbeats")
        if downbeats is not None:
            _require(isinstance(downbeats, list), f"{where}.downbeats must be a list")
            for j, db in enumerate(downbeats):
                _require(_is_finite_number(db), f"{where}.downbeats[{j}] must be a finite number")


def load_and_validate(doc: dict) -> dict:
    validate_observations(doc)
    return doc
