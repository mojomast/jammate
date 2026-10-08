"""Deterministic reporting: per-fixture CSV, JSON and a Markdown summary."""
from __future__ import annotations

from .gate import PopulationResult

CSV_COLUMNS = [
    "recording_id", "backend", "backend_kind", "classification", "core",
    "predicted_beats", "truth_beats", "matched_beats", "f_measure",
    "acquired", "acquisition_bars", "acquired_within_window",
    "locked_bpm", "bpm_relative_error", "tempo_correct",
    "phase_measured", "phase_mean_abs_ms", "phase_p95_abs_ms", "phase_usable",
    "useful_lock", "holdover", "half_time_lock", "double_time_lock",
    "false_lock", "false_lock_phase_ok_tempo_wrong",
    "false_lock_tempo_ok_phase_wrong", "no_lock",
    "lock_start_event_seconds", "lock_confirm_event_seconds",
]


def _cell(value):
    if value is None:
        return ""
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, float):
        return f"{value:.10g}"
    return str(value)


def fixture_csv_rows(populations: list[PopulationResult]) -> tuple[list[str], list[list[str]]]:
    rows = []
    for pop in populations:
        for score in pop.scores:
            d = score.as_dict()
            rows.append([_cell(d.get(c)) for c in CSV_COLUMNS])
    return CSV_COLUMNS, rows


def _pct(value) -> str:
    return "n/a" if value is None else f"{value * 100:.1f}%"


def _flag(value) -> str:
    return "yes" if value else "no"


def markdown_summary(meta: dict, populations: list[PopulationResult]) -> str:
    lines = []
    lines.append("# Representative-guitar useful-lock evaluation (EVAL-GUITAR-009)\n")
    lines.append(f"- generated: `{meta.get('generated_utc', '')}`")
    lines.append(f"- base commit: `{meta.get('base_commit', '')}`")
    lines.append(f"- protocol: `{meta.get('protocol_path', '')}` sha256 `{meta.get('protocol_sha256', '')}`")
    lines.append(f"- import manifest: `{meta.get('manifest_path', '')}` sha256 `{meta.get('manifest_sha256', '')}`")
    lines.append(f"- block frames: {meta.get('block_frames')}")
    lines.append(f"- evidence class: **{meta.get('evidence_class', '')}**")
    lines.append(f"- protocol unchanged during measurement: {_flag(meta.get('protocol_unchanged'))}\n")

    lines.append("## Comparison (useful lock = correct BPM AND usable phase within two bars)\n")
    lines.append("| backend | population | n | useful lock | fraction | half | double | false lock | no lock | gate |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")
    for pop in populations:
        lc = pop.label_counts
        lines.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            pop.backend, pop.label, len(pop.population), pop.useful_lock_count,
            _pct(pop.fraction), lc.get("half_time_lock", 0), lc.get("double_time_lock", 0),
            lc.get("false_lock", 0), lc.get("no_lock", 0),
            "PASS" if pop.gate_pass else ("FAIL" if pop.label == "gate" else "n/a (diagnostic)")))
    lines.append("")

    for pop in populations:
        lines.append(f"## {pop.backend} ({pop.label})\n")
        if pop.fail_closed:
            lines.append("Fail-closed reasons:")
            for reason in pop.fail_closed:
                lines.append(f"- {reason}")
            lines.append("")
        if pop.population:
            lines.append("| recording | class | useful | acq bars | locked BPM | BPM err | phase mean abs ms | false lock | no lock |")
            lines.append("|---|---|---|---|---|---|---|---|---|")
            for s in pop.scores:
                lines.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
                    s.recording_id, s.classification, _flag(s.useful_lock),
                    "" if s.acquisition_bars is None else f"{s.acquisition_bars:.3f}",
                    "" if s.locked_bpm is None else f"{s.locked_bpm:.3f}",
                    "" if s.bpm_relative_error is None else f"{s.bpm_relative_error * 100:.2f}%",
                    "" if s.phase_mean_abs_ms is None else f"{s.phase_mean_abs_ms:.1f}",
                    _flag(s.false_lock), _flag(s.no_lock)))
            lines.append("")
        if pop.missing:
            lines.append(f"Missing traces: {', '.join(pop.missing)}\n")
        lines.append("Gate eligibility:")
        for rec_id, info in sorted(pop.eligibility.items()):
            if pop.label == "gate":
                lines.append(f"- {rec_id}: {'eligible' if info['eligible'] else 'not eligible'} ({info['reason']})")
        lines.append("")
    return "\n".join(lines) + "\n"
