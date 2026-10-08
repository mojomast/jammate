#!/usr/bin/env python3
"""EVAL-LIVE-001 fail-closed validator for live-Jam replay evidence.

Standalone and portable by default: it validates the *recorded* evidence JSON
(pins, counters, semantics, matrix completeness, fixture identity) with no local
hard paths and without requiring any binary artifact. Optional `--source`,
`--product-build`, `--predeclared` and `--fixtures-dir` arguments add explicit
local cross-checks; when they are absent the recorded values are still
cross-checked against each other.

Two evidence shapes are accepted:

  * ``status == "measured"``      — a real replay run. The full counter,
    semantics, matrix and fixture checks run.
  * ``status == "awaiting-product"`` or ``"awaiting-backend"`` — a fail-closed
    receipt produced when the product is not live-capable. It must name the
    missing items, must not claim clean, and must not have invoked a binary.

Nothing is hidden: every failed check is reported with the cell/field that
produced it, and the process exits non-zero on ANY hard-check failure.
"""
import argparse
import hashlib
import json
import math
import os
import sys

SCHEMA_MEASURED = "live-jam-replay/evidence/1.0"
SCHEMA_AWAITING = "live-jam-replay/awaiting-product/1.0"
STATUS_MEASURED = "measured"
STATUS_AWAITING_PRODUCT = "awaiting-product"
STATUS_AWAITING_BACKEND = "awaiting-backend"
ALL_STATUSES = {STATUS_MEASURED, STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND}

CXX_ALLOC = ("cxx_new", "cxx_new_array", "cxx_new_nothrow", "cxx_new_aligned")
C_ALLOC = ("c_malloc", "c_calloc", "c_realloc")
FREE = ("cxx_delete", "cxx_delete_array", "cxx_delete_sized", "cxx_delete_aligned", "c_free")
LOCK = ("lock", "trylock", "cond", "unlock")
OVERFLOW = ("alloc_overflow", "lock_overflow")

CELL_REQUIRED = ("id", "rate", "block", "pipeline", "input", "warm_blocks",
                 "realtime_paced", "measured", "unmeasured_reason", "cold", "warm",
                 "timing", "output", "state_start", "state_end", "progression",
                 "commands")
SNAPSHOT_NUMERIC = CXX_ALLOC + C_ALLOC + FREE + LOCK + OVERFLOW + (
    "noop_frees", "blocked_lock", "locked_ns", "max_lock_ns",
    "alloc_cxx_total", "alloc_c_total", "free_total")
PROGRESSION_NUMERIC = ("audio_sample_start", "audio_sample_end",
                       "audio_sample_delta_mismatches", "generation_changes",
                       "candidate_bpm_nonzero", "receipt_count",
                       "receipt_measured_count", "receipt_before_horizon",
                       "event_after_horizon", "max_receipt_lag_samples",
                       "last_receipt_sample")
PROGRESSION_BOOL = ("audio_sample_monotonic", "audio_sample_delta_ok",
                    "prepared_seen", "requested_running_seen", "join_pending_seen",
                    "drums_playing_seen")

REQUIRED_AWAITING_MISSING_KEYS = {
    "facade_symbol_submit", "facade_symbol_read",
    "facade_definition", "shared_archive", "source_matches_product",
}


class Check:
    def __init__(self, ident, severity, passed, detail=""):
        self.id = ident
        self.severity = severity
        self.pass_ = bool(passed)
        self.detail = detail

    def as_dict(self):
        return {"id": self.id, "severity": self.severity,
                "pass": self.pass_, "detail": self.detail}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def is_hex64(value):
    return isinstance(value, str) and len(value) == 64 and all(
        c in "0123456789abcdef" for c in value)


def _reject_constant(name):
    raise ValueError(f"non-finite JSON constant not allowed: {name}")


def load_json_strict(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.loads(f.read(), parse_constant=_reject_constant)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def finite(value):
    return is_number(value) and math.isfinite(value)


def get_nonneg_int(obj, key, errors, where):
    if key not in obj:
        errors.append(f"{where}: missing integer field {key}")
        return None
    v = obj[key]
    if isinstance(v, bool) or not isinstance(v, int) or v < 0:
        errors.append(f"{where}: {key} must be a non-negative integer, got {v!r}")
        return None
    return v


def check_counter_consistency(snap, where, errors):
    for k in SNAPSHOT_NUMERIC:
        get_nonneg_int(snap, k, errors, where)
    try:
        if snap["alloc_cxx_total"] != sum(snap[k] for k in CXX_ALLOC):
            errors.append(f"{where}: alloc_cxx_total != sum of C++ new forms")
        if snap["alloc_c_total"] != sum(snap[k] for k in C_ALLOC):
            errors.append(f"{where}: alloc_c_total != sum of C alloc forms")
        if snap["free_total"] != sum(snap[k] for k in FREE):
            errors.append(f"{where}: free_total != sum of free forms")
    except KeyError:
        pass


def cell_id(rate, block, pipeline, inp):
    return f"{pipeline}_r{int(rate)}_b{int(block)}_{inp}"


def expected_cell_ids(matrix):
    ids = []
    for rate in matrix["rates_hz"]:
        for block in matrix["block_sizes"]:
            for pipe in matrix["pipelines"]:
                for inp in matrix["inputs"]:
                    ids.append(cell_id(rate, block, pipe, inp))
    return ids


def validate_measured(ev, predeclared, local, errors, checks):
    def add(ident, severity, passed, detail=""):
        checks.append(Check(ident, severity, passed, detail))
        if not passed and severity == "hard":
            errors.append(f"[{ident}] {detail}")

    # ---- schema and top-level
    add("schema", "hard", ev.get("schema") == SCHEMA_MEASURED,
        f"schema must be {SCHEMA_MEASURED}, got {ev.get('schema')!r}")
    add("status", "hard", ev.get("status") == STATUS_MEASURED,
        f"status must be {STATUS_MEASURED}, got {ev.get('status')!r}")

    ident = ev.get("identity", {})
    inst = ident.get("instrument", {}) if isinstance(ident, dict) else {}
    add("instrument_selfcheck", "hard", inst.get("selfcheck_pass") is True,
        "instrument self-check must pass before any measured evidence")
    src = ident.get("source", {}) if isinstance(ident, dict) else {}
    prod = ident.get("product", {}) if isinstance(ident, dict) else {}
    syms = ident.get("facade_symbols", {}) if isinstance(ident, dict) else {}
    backend = ident.get("backend", {}) if isinstance(ident, dict) else {}

    add("facade_symbol_submit", "hard", syms.get("submitJamCommand") is True,
        "product archive must define GuitarCompanionProcessor::submitJamCommand")
    add("facade_symbol_read", "hard", syms.get("readJamLiveState") is True,
        "product archive must define GuitarCompanionProcessor::readJamLiveState")
    add("backend_usable", "hard", backend.get("usable") is True,
        "a live backend must be usable for measured evidence")
    add("source_facade_definition", "hard",
        src.get("facade_definitions_present") is True,
        "source PluginProcessor.cpp must define the frozen facade")
    add("source_matches_product", "hard",
        prod.get("source_matches_product") is True,
        "product archive source must equal --source (no stale header/archive pair)")

    for key in ("shared_archive_sha256", "nam_archive_sha256",
                "assets_archive_sha256", "build_ninja_sha256"):
        add(f"product_{key}", "hard", is_hex64(prod.get(key)),
            f"product.{key} must be a 64-hex sha256")
    for key in ("plugin_processor_h_sha256", "plugin_processor_cpp_sha256",
                "jam_live_interface_h_sha256", "src_tree_hash"):
        add(f"source_{key}", "hard", is_hex64(src.get(key)),
            f"source.{key} must be a 64-hex sha256")

    proto = ev.get("protocol", {})
    for key in ("predeclared_sha256", "validator_sha256", "harness_source_sha256"):
        add(f"protocol_{key}", "hard", is_hex64(proto.get(key)),
            f"protocol.{key} must be a 64-hex sha256")

    # ---- exact matrix
    matrix = ev.get("matrix", {})
    cells = ev.get("cells", [])
    add("cells_is_list", "hard", isinstance(cells, list) and len(cells) > 0,
        "cells must be a non-empty list")
    want = expected_cell_ids(predeclared["matrix"])
    have = [c.get("id") for c in cells] if isinstance(cells, list) else []
    add("matrix_exact", "hard", sorted(have) == sorted(want),
        f"cell set must equal the predeclared {len(want)}-cell matrix; "
        f"missing={sorted(set(want) - set(have))[:6]} extra={sorted(set(have) - set(want))[:6]}")
    add("matrix_count_consistent", "hard",
        matrix.get("rates_hz") == predeclared["matrix"]["rates_hz"]
        and matrix.get("block_sizes") == predeclared["matrix"]["block_sizes"]
        and matrix.get("pipelines") == predeclared["matrix"]["pipelines"]
        and matrix.get("inputs") == predeclared["matrix"]["inputs"],
        "evidence matrix dimensions must equal the frozen predeclared matrix")

    warm_by_rate_block = {}
    measured_enabled = 0
    disabled_cells = []
    enabled_cells = []
    for c in cells if isinstance(cells, list) else []:
        cid = c.get("id", "?")
        for k in CELL_REQUIRED:
            if k not in c:
                errors.append(f"cell {cid}: missing field {k}")
        warm = c.get("warm_blocks")
        key = (c.get("rate"), c.get("block"))
        if isinstance(warm, int):
            warm_by_rate_block.setdefault(key, set()).add(warm)
        for snap_name in ("cold", "warm"):
            snap = c.get(snap_name)
            if isinstance(snap, dict):
                check_counter_consistency(snap, f"cell {cid}.{snap_name}", errors)
                add(f"{cid}.{snap_name}.alloc_overflow", "hard",
                    snap.get("alloc_overflow") == 0,
                    "alloc record overflow means detail evidence was dropped (never clean)")
                add(f"{cid}.{snap_name}.lock_overflow", "hard",
                    snap.get("lock_overflow") == 0,
                    "lock record overflow means detail evidence was dropped (never clean)")
            else:
                errors.append(f"cell {cid}: {snap_name} snapshot missing")
        if c.get("pipeline") == "disabled":
            disabled_cells.append(c)
        else:
            enabled_cells.append(c)

    for key, values in warm_by_rate_block.items():
        if len(values) != 1:
            errors.append(f"warm_blocks inconsistent for rate/block {key}: {sorted(values)}")

    # ---- per-cell semantics
    for c in cells if isinstance(cells, list) else []:
        cid = c.get("id", "?")
        measured = c.get("measured") is True
        if not measured:
            reason = c.get("unmeasured_reason")
            add(f"{cid}.unmeasured_explicit", "hard",
                isinstance(reason, str) and len(reason) > 0,
                f"unmeasured cell {cid} must carry an explicit non-empty reason")
            continue
        if c.get("pipeline") != "disabled":
            measured_enabled += 1

        timing = c.get("timing", {})
        for key in ("callback_count", "audio_deadline_ms", "elapsed_wall_s",
                    "callback_wall_ms_sum", "callback_us_per_block"):
            add(f"{cid}.timing.{key}", "hard", finite(timing.get(key)),
                f"timing.{key} must be finite for a measured cell")
        warm_n = c.get("warm_blocks")
        add(f"{cid}.timing.callback_count", "hard",
            isinstance(warm_n, int) and timing.get("callback_count") == warm_n + 1,
            "callback_count must be warm_blocks + 1 (one cold callback)")

        output = c.get("output", {})
        for key in ("rms_mean", "rms_max", "peak"):
            add(f"{cid}.output.{key}", "hard", finite(output.get(key)),
                f"output.{key} must be finite for a measured cell")

        prog = c.get("progression", {})
        for key in PROGRESSION_NUMERIC:
            add(f"{cid}.progression.{key}", "hard",
                isinstance(prog.get(key), int) and prog.get(key) >= 0,
                f"progression.{key} must be a non-negative integer")
        for key in PROGRESSION_BOOL:
            add(f"{cid}.progression.{key}", "hard", isinstance(prog.get(key), bool),
                f"progression.{key} must be boolean")

        add(f"{cid}.audio_monotonic", "hard", prog.get("audio_sample_monotonic") is True,
            "audioSampleTime must be monotonic across callbacks")
        add(f"{cid}.audio_delta", "hard",
            prog.get("audio_sample_delta_ok") is True
            and prog.get("audio_sample_delta_mismatches") == 0,
            "audioSampleTime must advance by exactly one block per callback, including stopped")
        add(f"{cid}.receipt_not_before_horizon", "hard",
            prog.get("receipt_before_horizon") == 0,
            "lastReceiptSampleTime must never precede lastInputHorizonSampleTime when measured")
        add(f"{cid}.event_not_after_horizon", "hard",
            prog.get("event_after_horizon") == 0,
            "lastEventSampleTime must never exceed lastInputHorizonSampleTime")

        end = c.get("state_end", {})
        clock = end.get("clock", {}) if isinstance(end, dict) else {}
        if isinstance(end, dict):
            add(f"{cid}.state_end.prepared", "hard", end.get("prepared") is True,
                "state_end.prepared must be true after a measured replay")
            add(f"{cid}.state_end.sample_rate", "hard",
                finite(end.get("sampleRate")) and end.get("sampleRate") == c.get("rate"),
                "state_end.sampleRate must equal the cell rate")
            if end.get("receiptMeasured") is True:
                lr = end.get("lastReceiptSampleTime")
                lh = end.get("lastInputHorizonSampleTime")
                le = end.get("lastEventSampleTime")
                add(f"{cid}.state_end.receipt_order", "hard",
                    all(isinstance(x, int) for x in (lr, lh, le)) and lr >= lh >= le,
                    "when receiptMeasured, lastReceiptSampleTime >= "
                    "lastInputHorizonSampleTime >= lastEventSampleTime")
        if c.get("input") == "silence":
            # The clock publishes an explicit configured fallback while no belief
            # exists; the honest no-fabrication signal is no lock and no
            # confidence, not a zero BPM.
            add(f"{cid}.silence_no_lock", "hard",
                clock.get("lockState") == "Acquiring",
                "silence must not reach a clock lock")
            add(f"{cid}.silence_no_confidence", "hard",
                clock.get("confidence01") in (0, 0.0),
                "silence must not fabricate clock confidence")
            add(f"{cid}.silence_no_drums", "hard",
                prog.get("drums_playing_seen") is False,
                "no usable clock lock can be reached from silence, so drums must not play")
        if c.get("pipeline") == "disabled":
            add(f"{cid}.disabled_no_start", "hard",
                prog.get("requested_running_seen") is False,
                "control cell must not report requestedRunning")
            add(f"{cid}.disabled_no_drums", "hard",
                prog.get("drums_playing_seen") is False,
                "control cell must not report drums playing")

    # Enabled cells that were rejected as unmeasured must not exist in a
    # measured-focused evidence with a usable backend.
    add("enabled_measured_present", "hard", measured_enabled > 0,
        "a measured run with a usable backend must measure at least one enabled cell")

    # ---- overhead contrast (advisory; recorded for the report)
    def key_of(c):
        return (c.get("rate"), c.get("block"), c.get("input"))
    disabled_by = {key_of(c): c for c in disabled_cells if c.get("measured")}
    for c in enabled_cells:
        if not c.get("measured"):
            continue
        d = disabled_by.get(key_of(c))
        if d is None:
            continue
        du = d["timing"]["callback_us_per_block"]
        eu = c["timing"]["callback_us_per_block"]
        ratio = eu / du if du and du > 0 else None
        add(f"{c['id']}.overhead_ratio", "advisory", ratio is not None,
            f"enabled/disabled callback us/block ratio = {ratio!r} (wall, not a deadline)")

    # ---- fixtures
    fixtures = ev.get("fixtures")
    add("fixtures_present", "hard", isinstance(fixtures, dict),
        "fixtures block must be present (entries or explicit unmeasured)")
    if isinstance(fixtures, dict):
        if fixtures.get("unmeasured") is True:
            add("fixtures_unmeasured_reason", "hard",
                isinstance(fixtures.get("reason"), str) and len(fixtures["reason"]) > 0,
                "unmeasured fixtures need an explicit reason")
        else:
            add("fixtures_manifest_sha256", "hard",
                is_hex64(fixtures.get("manifest_sha256")),
                "fixture manifest sha256 must be 64-hex")
            entries = fixtures.get("entries")
            add("fixtures_entries", "hard", isinstance(entries, list) and len(entries) > 0,
                "fixture entries must be a non-empty list")
            for e in entries if isinstance(entries, list) else []:
                for k in ("sha256", "sample_exact_checksum"):
                    if not is_hex64(e.get(k)):
                        add(f"fixture_{e.get('id')}_{k}", "hard", False,
                            f"fixture {e.get('id')}.{k} must be 64-hex")
                if not (isinstance(e.get("id"), str) and len(e["id"]) > 0):
                    add("fixture_id", "hard", False, "fixture id must be a non-empty string")
                if not (isinstance(e.get("path"), str) and len(e["path"]) > 0):
                    add(f"fixture_{e.get('id')}_path", "hard", False,
                        "fixture path must be a non-empty string")
                for k in ("sample_rate", "channels", "frames", "sample_width_bytes"):
                    if not (isinstance(e.get(k), int) and e.get(k) > 0):
                        add(f"fixture_{e.get('id')}_{k}", "hard", False,
                            f"fixture {e.get('id')}.{k} must be a positive integer")
                if local.get("fixtures_dir"):
                    p = os.path.join(local["fixtures_dir"], e.get("path", ""))
                    if not os.path.isfile(p):
                        add(f"fixture_{e.get('id')}_local", "hard", False,
                            f"fixture file missing: {p}")
                    elif sha256_file(p) != e.get("sha256"):
                        add(f"fixture_{e.get('id')}_local", "hard", False,
                            f"fixture file sha256 mismatch: {p}")

    # ---- explicit local cross-checks (optional)
    _cross_check_local(ev, predeclared, local, add, errors)

    return checks


def validate_awaiting(ev, errors, checks):
    def add(ident, severity, passed, detail=""):
        checks.append(Check(ident, severity, passed, detail))
        if not passed and severity == "hard":
            errors.append(f"[{ident}] {detail}")

    add("schema", "hard",
        ev.get("schema") in (SCHEMA_AWAITING, "live-jam-replay/evidence/1.0"),
        f"awaiting schema unexpected: {ev.get('schema')!r}")
    add("status", "hard", ev.get("status") in (STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND),
        f"awaiting status unexpected: {ev.get('status')!r}")
    add("not_clean", "hard", ev.get("clean") is False,
        "an awaiting receipt must explicitly not claim clean")
    add("binary_not_invoked", "hard", ev.get("invoked_binary") is False,
        "no existing binary may be invoked when the product is not live-capable")
    missing = ev.get("missing")
    add("missing_named", "hard", isinstance(missing, list) and len(missing) > 0,
        "missing items must be named")
    if isinstance(missing, list):
        add("missing_covers_facade", "hard",
            bool(REQUIRED_AWAITING_MISSING_KEYS.intersection(set(missing))),
            f"missing must include a facade/identity gap; got {missing}")
    return checks


def _cross_check_local(ev, predeclared, local, add, errors):
    if local.get("predeclared"):
        add("local_predeclared_hash", "hard",
            sha256_file(local["predeclared"]) == ev.get("protocol", {}).get("predeclared_sha256"),
            "recorded predeclared_sha256 must match the local predeclared file")
    if local.get("source"):
        src = ev.get("identity", {}).get("source", {})
        for rel, key in (("src/PluginProcessor.h", "plugin_processor_h_sha256"),
                         ("src/PluginProcessor.cpp", "plugin_processor_cpp_sha256"),
                         ("src/jam/JamLiveInterface.h", "jam_live_interface_h_sha256")):
            p = os.path.join(local["source"], rel)
            if not os.path.isfile(p):
                add(f"local_source_{key}", "hard", False, f"missing local source {p}")
            elif sha256_file(p) != src.get(key):
                add(f"local_source_{key}", "hard", False,
                    f"recorded {key} mismatch for {p}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--evidence", required=True)
    p.add_argument("--predeclared")
    p.add_argument("--source")
    p.add_argument("--product-build")
    p.add_argument("--fixtures-dir")
    p.add_argument("--expect-status", choices=sorted(ALL_STATUSES))
    p.add_argument("--json", dest="json_out")
    p.add_argument("--summary-md")
    args = p.parse_args(argv)

    errors = []
    checks = []
    try:
        ev = load_json_strict(args.evidence)
    except Exception as e:  # noqa: BLE001 - report parse failure explicitly
        print(f"FAIL: cannot parse evidence JSON: {e}", file=sys.stderr)
        return 2

    if args.expect_status and ev.get("status") != args.expect_status:
        errors.append(f"expected status {args.expect_status}, got {ev.get('status')!r}")

    status = ev.get("status")
    if status == STATUS_MEASURED:
        try:
            predeclared = NORMALIZED_PREDECLARED
            if args.predeclared:
                predeclared = load_json_strict(args.predeclared)
            checks = validate_measured(
                ev, predeclared,
                {"source": args.source, "product_build": args.product_build,
                 "predeclared": args.predeclared, "fixtures_dir": args.fixtures_dir},
                errors, [])
        except Exception as e:  # noqa: BLE001
            errors.append(f"validation crashed: {e}")
    elif status in (STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND):
        checks = validate_awaiting(ev, errors, [])
    else:
        errors.append(f"unknown status {status!r}")

    hard_fail = [c for c in checks if c.severity == "hard" and not c.pass_]
    adv_fail = [c for c in checks if c.severity == "advisory" and not c.pass_]
    verdict = {
        "schema": "live-jam-replay/verdict/1.0",
        "evidence": os.path.basename(args.evidence),
        "status": status,
        "hard_checks": len([c for c in checks if c.severity == "hard"]),
        "hard_failures": len(hard_fail),
        "advisory_checks": len([c for c in checks if c.severity == "advisory"]),
        "advisory_failures": len(adv_fail),
        "hard_pass": len(hard_fail) == 0 and not errors,
        "checks": [c.as_dict() for c in checks],
        "errors": errors,
    }

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(verdict, f, indent=2, sort_keys=False)
            f.write("\n")

    if args.summary_md:
        with open(args.summary_md, "w", encoding="utf-8") as f:
            f.write(f"# EVAL-LIVE-001 replay evidence verdict\n\n")
            f.write(f"- status: `{status}`\n")
            f.write(f"- hard checks: {verdict['hard_checks']} "
                    f"(failures {verdict['hard_failures']})\n")
            f.write(f"- advisory checks: {verdict['advisory_checks']} "
                    f"(failures {verdict['advisory_failures']})\n")
            f.write(f"- verdict: **{'PASS' if verdict['hard_pass'] else 'FAIL'}**\n\n")
            for c in checks:
                mark = "PASS" if c.pass_ else "FAIL"
                f.write(f"- [{mark}][{c.severity}] {c.id}: {c.detail}\n")

    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)
    print(f"hard_pass={verdict['hard_pass']} hard_failures={verdict['hard_failures']} "
          f"advisory_failures={verdict['advisory_failures']}")
    return 0 if verdict["hard_pass"] else 1


# A normalized copy of the frozen predeclared matrix is embedded so the validator
# is portable and does not require the tool directory to be present. When
# --predeclared is supplied, the recorded file is used instead.
NORMALIZED_PREDECLARED = {
    "matrix": {
        "rates_hz": [48000, 96000],
        "block_sizes": [128, 512, 4096],
        "pipelines": ["disabled", "enabled", "enabled_pressure"],
        "inputs": ["clean", "noise", "silence"],
    }
}


if __name__ == "__main__":
    sys.exit(main())
