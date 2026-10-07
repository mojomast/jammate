"""Pinned provenance for the TRACK-003 BeatNet review.

The single source of truth is ``tools/beatnet-eval/provenance.json`` next to
this package. These constants mirror a few values that tests assert on so a
silent edit of the JSON cannot go unnoticed.
"""

from __future__ import annotations

import json
import os

PROVENANCE_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "provenance.json")

# Spec-cited repo and upstream commit reviewed for this task.
SPEC_CITED_REPO = "https://github.com/hashimkarim/beatnet"
SPEC_CITED_COMMIT = "be864a4b0f126aa90aeabdeedf23f48865e09512"
UPSTREAM_REPO = "https://github.com/mjhydri/BeatNet"
UPSTREAM_COMMIT = "81cedd4beeb7235262db80969a0c9ce9a48a0ed4"

# sha256 of the three inference weights actually loaded by BeatNet.py.
MODEL_WEIGHT_SHA256 = {
    "model_1_weights.pt": "619091bc317ca3e83b45591d46f6de3d5a41588bcb39fe9fe7be30cffa6aca84",
    "model_2_weights.pt": "5878a18c079fa0b0139879b14ed2b5b7595faef8c3d16210aed141fd00fa2d58",
    "model_3_weights.pt": "0c52a074ea38e8cb4a760ecfa3747c9cf91a1e3cd19f238eed80b0de763989ca",
}

LICENSE_SPDX = "CC-BY-4.0"


def load_provenance(path: str | None = None) -> dict:
    """Load the pinned provenance document."""
    with open(path or PROVENANCE_PATH, "r", encoding="utf-8") as fh:
        return json.load(fh)
