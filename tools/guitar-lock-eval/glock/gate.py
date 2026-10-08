"""Population aggregation and fail-closed gate evaluation.

The >=95% useful-lock target is evaluated ONLY over the gate population
(representative, human-annotated, licensed, steady 4/4 real recordings). Any
missing, malformed or identity-mismatched member of that population fails the
run closed: the gate can never pass on residual data. Synthetic and derived
recordings are aggregated separately as a clearly labelled diagnostic and never
count toward the gate.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .manifest import Finding, Recording, gate_eligibility
from .scoring import Criteria, FixtureScore

LABELS = (
    "useful_lock", "acquired_within_window", "tempo_correct", "phase_usable",
    "holdover", "half_time_lock", "double_time_lock",
    "false_lock", "false_lock_phase_ok_tempo_wrong",
    "false_lock_tempo_ok_phase_wrong", "no_lock",
)


@dataclass
class PopulationResult:
    label: str                     # "gate" | "diagnostic"
    backend: str
    population: list[str] = field(default_factory=list)
    evaluated: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    eligibility: dict = field(default_factory=dict)
    scores: list[FixtureScore] = field(default_factory=list)
    useful_lock_count: int = 0
    fraction: float | None = None
    gate_pass: bool = False
    fail_closed: list[str] = field(default_factory=list)
    label_counts: dict = field(default_factory=dict)

    def as_dict(self) -> dict:
        return {
            "label": self.label,
            "backend": self.backend,
            "population": self.population,
            "evaluated": self.evaluated,
            "missing": self.missing,
            "eligibility": self.eligibility,
            "useful_lock_count": self.useful_lock_count,
            "fraction": self.fraction,
            "gate_pass": self.gate_pass,
            "fail_closed": self.fail_closed,
            "label_counts": self.label_counts,
            "fixtures": [s.as_dict() for s in self.scores],
        }


def _counts(scores: list[FixtureScore]) -> dict:
    out = {k: 0 for k in LABELS}
    for s in scores:
        for k in LABELS:
            out[k] += 1 if getattr(s, k) else 0
    return out


def evaluate_gate(backend: str, recordings: list[Recording],
                  scores: dict[str, FixtureScore],
                  criteria: Criteria) -> PopulationResult:
    """Evaluate the >=95% gate over the eligible real population for one backend."""
    res = PopulationResult(label="gate", backend=backend)
    for rec in recordings:
        ok, reason = gate_eligibility(rec, {
            "min_annotated_beats": criteria.min_annotated_beats})
        res.eligibility[rec.id] = {"eligible": ok, "reason": reason}
        if ok:
            res.population.append(rec.id)
    res.population.sort()
    for rec_id in res.population:
        s = scores.get(rec_id)
        if s is None:
            res.missing.append(rec_id)
            res.fail_closed.append(f"{backend}: no trace for gate recording {rec_id}")
            continue
        if s.classification != "real":
            res.fail_closed.append(
                f"{backend}: gate recording {rec_id} classified {s.classification}")
            continue
        res.evaluated.append(rec_id)
        res.scores.append(s)
        if s.useful_lock:
            res.useful_lock_count += 1
    if not res.population:
        res.fail_closed.append("empty gate population: no representative real annotated steady recording")
    denom = len(res.population)
    res.fraction = (res.useful_lock_count / denom) if denom else None
    res.label_counts = _counts(res.scores)
    res.gate_pass = (not res.fail_closed
                     and res.fraction is not None
                     and res.fraction >= criteria.min_gate_fraction)
    return res


def evaluate_diagnostic(backend: str, recordings: list[Recording],
                        scores: dict[str, FixtureScore]) -> PopulationResult:
    """Aggregate a non-gate population as an explicitly labelled diagnostic.

    This includes every recording for which a score exists (real or not); it is
    never the gate and never claims a pass.
    """
    res = PopulationResult(label="diagnostic", backend=backend)
    for rec in recordings:
        s = scores.get(rec.id)
        if s is None:
            continue
        res.population.append(rec.id)
        res.evaluated.append(rec.id)
        res.scores.append(s)
        if s.useful_lock:
            res.useful_lock_count += 1
    res.population.sort()
    denom = len(res.population)
    res.fraction = (res.useful_lock_count / denom) if denom else None
    res.label_counts = _counts(res.scores)
    # A diagnostic never claims the gate.
    res.gate_pass = False
    return res
