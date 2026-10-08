#!/usr/bin/env python3
"""Response-lag summary for the imposed tempo step (TRACK-005).

PRIMARY measurement is on the CAUSAL AVAILABILITY clock (the block end at which
the evidence became usable), NOT the event clock. For the variant a hit also
requires the derived report to be READY (`ready == 1`), so a fallback base value
that merely happens to be in band is never counted as a hit. The event-time
quantity is exported as an explicit SECONDARY column.

Only the FIRST within-band beat is reported; no "settling" is claimed. A separate
descriptive flag records whether every ready beat after the first hit stays in
band to the end of the clip.

The step is not placed at the nominal instant: the generator changes the period
only on the first generated beat >= nominal step (the "anchor"), and the interval
into the anchor is still the pre-step period. The truth anchor and pre/post
periods come from the generator's own `click_step_info` sidecar, so the lag from
nominal and from anchor are both reported.

    click_lag.py <raw-step.csv> <info.csv> <step-from> <step-to> <out-beats.csv> \
        <out-summary.csv>
"""
import csv
import sys


def _num(s):
    return None if s == "" else float(s)


def read_rows(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            if r["beatEvent"] != "1":
                continue
            rows.append({
                "event": float(r["eventSeconds"]),
                "avail": float(r["blockEndSeconds"]),
                "ready": r["ready"] == "1",
                "variant": float(r["variantBpm"]),
                "base": float(r["baseBpm"]),
            })
    return rows


def read_info(path):
    with open(path, newline="", encoding="utf-8") as fh:
        r = next(csv.DictReader(fh))
    return (float(r["nominalStepSeconds"]), float(r["anchorBeatSeconds"]),
            float(r["prePeriodSeconds"]), float(r["postPeriodSeconds"]))


def _first_hit(rows, reference, field, step_to, tol, require_ready):
    """First emitted beat at/after `reference` whose `field` is within tol of
    step_to (and, for the variant, ready). Returns (index, row) or (None, None)."""
    for i, r in enumerate(rows):
        if r["event"] < reference:
            continue
        if require_ready and not r["ready"]:
            continue
        if abs(r[field] - step_to) <= tol:
            return i, r
    return None, None


def _beats_from(rows, reference, hit_index):
    """Number of emitted beats with event time >= reference up to AND including
    the hit (the explicitly defined lag-in-beats)."""
    n = 0
    for i, r in enumerate(rows):
        if r["event"] < reference:
            continue
        n += 1
        if i == hit_index:
            return n
    return n


def analyze(rows, nominal, anchor, step_to):
    tol = 0.02 * step_to

    out = {}
    for k in ("variantFirstWithinAvailabilitySeconds", "variantLagSecondsFromNominal",
              "variantExecLagBeatsFromNominal", "variantFirstWithinEventSeconds",
              "variantEventLagSecondsFromNominal", "variantLagSecondsFromAnchor",
              "variantLagBeatsFromAnchor", "variantTrailingWithinToEnd",
              "variantLastWithinAvailabilitySeconds",
              "baseFirstWithinAvailabilitySeconds", "baseLagSecondsFromNominal",
              "baseLagBeatsFromNominal", "baseFirstWithinEventSeconds"):
        out[k] = ""
    # --- variant: primary availability clock, readiness-gated ---
    vi, vr = _first_hit(rows, nominal, "variant", step_to, tol, True)
    if vr is not None:
        out["variantFirstWithinAvailabilitySeconds"] = vr["avail"]
        out["variantLagSecondsFromNominal"] = vr["avail"] - nominal
        out["variantExecLagBeatsFromNominal"] = _beats_from(rows, nominal, vi)
        out["variantFirstWithinEventSeconds"] = vr["event"]
        out["variantEventLagSecondsFromNominal"] = vr["event"] - nominal
        if anchor >= 0.0:
            out["variantLagSecondsFromAnchor"] = vr["avail"] - anchor
            out["variantLagBeatsFromAnchor"] = _beats_from(rows, anchor, vi)
        # Descriptive trailing band (not a settling claim).
        last_within = None
        trailing = True
        for r in rows[vi:]:
            if not r["ready"]:
                continue
            if abs(r["variant"] - step_to) <= tol:
                last_within = r["avail"]
            else:
                trailing = False
        out["variantTrailingWithinToEnd"] = trailing
        out["variantLastWithinAvailabilitySeconds"] = last_within

    # --- base: availability clock (no readiness flag exists for the default) ---
    bi, br = _first_hit(rows, nominal, "base", step_to, tol, False)
    if br is not None:
        out["baseFirstWithinAvailabilitySeconds"] = br["avail"]
        out["baseLagSecondsFromNominal"] = br["avail"] - nominal
        out["baseLagBeatsFromNominal"] = _beats_from(rows, nominal, bi)
        out["baseFirstWithinEventSeconds"] = br["event"]
    return out


def main() -> int:
    raw, info_p, step_from, step_to, out_beats, out_summary = sys.argv[1:7]
    step_from = float(step_from)
    step_to = float(step_to)
    nominal, anchor, pre, post = read_info(info_p)
    rows = read_rows(raw)

    with open(out_beats, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["beatIndex", "eventSeconds", "availabilitySeconds", "ready",
                    "baseBpm", "variantBpm"])
        for i, r in enumerate(rows):
            w.writerow([i, f"{r['event']:.9f}", f"{r['avail']:.9f}",
                        1 if r["ready"] else 0, f"{r['base']:.9f}",
                        f"{r['variant']:.9f}"])

    res = analyze(rows, nominal, anchor, step_to)
    cols = ["nominalStepSeconds", "anchorBeatSeconds", "prePeriodSeconds",
            "postPeriodSeconds", "stepFromBpm", "stepToBpm",
            "variantFirstWithinAvailabilitySeconds", "variantLagSecondsFromNominal",
            "variantExecLagBeatsFromNominal", "variantFirstWithinEventSeconds",
            "variantEventLagSecondsFromNominal", "variantLagSecondsFromAnchor",
            "variantLagBeatsFromAnchor", "variantTrailingWithinToEnd",
            "variantLastWithinAvailabilitySeconds",
            "baseFirstWithinAvailabilitySeconds", "baseLagSecondsFromNominal",
            "baseLagBeatsFromNominal", "baseFirstWithinEventSeconds"]
    row = [f"{nominal:.9f}", f"{anchor:.9f}", f"{pre:.9f}", f"{post:.9f}",
           f"{step_from:.6f}", f"{step_to:.6f}"]
    for k in cols[6:]:
        v = res.get(k, "")
        row.append(f"{v:.9f}" if isinstance(v, float) else v)

    with open(out_summary, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(cols)
        w.writerow(row)

    print(f"anchor {anchor:.6f}s: variant lag(avail) "
          f"{res.get('variantLagSecondsFromNominal')} "
          f"({res.get('variantExecLagBeatsFromNominal')} beats); base "
          f"{res.get('baseLagSecondsFromNominal')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
