#!/usr/bin/env python3
"""Adversarial unit tests for the corrected EVAL-LIVE-001 evidence validator.

Proves the validator accepts a consistent synthetic record only under the
explicit --allow-synthetic-selftest path, and fails closed on independent
mutations: scope/smoke/full, async coalescing cursor semantics, lag null policy,
allocation scope and hidden findings, backend identity, fixtures, counters and
statuses.
"""
import copy
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_synthetic_evidence as ms  # noqa: E402
import validate_evidence as ve  # noqa: E402

PREDECLARED_PATH = os.path.join(HERE, "predeclared.json")


def hard_pass(ev, fixtures_dir=None, source=None, allow_synthetic=True):
    errors = []
    checks = []
    predeclared = ve.load_json_strict(PREDECLARED_PATH)
    ve.validate_measured(ev, predeclared,
                         {"fixtures_dir": fixtures_dir, "source": source,
                          "predeclared": None, "product_build": None},
                         errors, checks, allow_synthetic)
    hard_fail = [c for c in checks if c.severity == "hard" and not c.pass_]
    return (not hard_fail and not errors), errors, checks


class CorrectedValidatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="evallive001c-")
        cls.fixtures_dir = os.path.join(cls.tmp, "fixtures")
        cls.fixtures = ms.make_fixtures(cls.fixtures_dir)

    def fresh(self, scope="full"):
        return ms.make_evidence(PREDECLARED_PATH, copy.deepcopy(self.fixtures), scope=scope)

    def cell(self, ev, cid):
        for c in ev["cells"]:
            if c["id"] == cid:
                return c
        raise AssertionError(f"no cell {cid}")

    def enabled_clean(self, ev):
        return self.cell(ev, "enabled_r48000_b128_clean")

    # -- positive / synthetic policy ----------------------------------------
    def test_positive_full_with_opt_in(self):
        ok, errors, checks = hard_pass(self.fresh("full"), allow_synthetic=True)
        self.assertTrue(ok, f"errors={errors}")
        self.assertGreater(len(checks), 100)

    def test_positive_smoke_with_opt_in(self):
        ok, errors, _ = hard_pass(self.fresh("smoke"), allow_synthetic=True)
        self.assertTrue(ok, f"errors={errors}")

    def test_synthetic_rejected_by_default(self):
        ok, errors, _ = hard_pass(self.fresh(), allow_synthetic=False)
        self.assertFalse(ok, "synthetic evidence must be rejected by default")
        self.assertTrue(any("synthetic" in e for e in errors), errors)

    def test_cli_synthetic_rejected_by_default(self):
        path = os.path.join(self.tmp, "ev-synth.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertEqual(ve.main(["--evidence", path]), 1)

    def test_cli_synthetic_accepted_with_flag(self):
        path = os.path.join(self.tmp, "ev-synth2.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 0)

    def test_nan_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["timing"]["callback_us_per_block"] = float("nan")
        path = os.path.join(self.tmp, "ev-nan.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 2)

    def test_infinity_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["output"]["rms_mean"] = float("inf")
        path = os.path.join(self.tmp, "ev-inf.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 2)

    # -- scope ---------------------------------------------------------------
    def test_full_missing_cell(self):
        ev = self.fresh()
        ev["cells"] = ev["cells"][:-1]
        ev["expected_cell_ids"] = ev["expected_cell_ids"][:-1]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("full scope" in e for e in errors), errors)

    def test_full_extra_cell(self):
        ev = self.fresh()
        extra = copy.deepcopy(ev["cells"][0]); extra["id"] = "bogus_r48000_b128_clean"
        ev["cells"].append(extra)
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_smoke_missing_cell(self):
        ev = self.fresh("smoke")
        ev["cells"] = ev["cells"][:-1]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("smoke" in e for e in errors), errors)

    def test_smoke_extra_cell(self):
        ev = self.fresh("smoke")
        extra = copy.deepcopy(ev["cells"][0]); extra["id"] = "bogus_r48000_b128_clean"
        ev["cells"].append(extra)
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_full_dimension_mismatch(self):
        ev = self.fresh()
        ev["matrix"]["block_sizes"] = [128, 512]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_diagnostic_without_reason(self):
        ev = self.fresh("smoke")
        ev["scope"] = "diagnostic"
        ev["scope_reason"] = ""
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_diagnostic_claiming_full(self):
        ev = self.fresh("full")
        ev["scope"] = "diagnostic"
        ev["scope_reason"] = "pretend full"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok, "a diagnostic scope must not present the full matrix")

    # -- counters ------------------------------------------------------------
    def test_nonzero_alloc_overflow(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["alloc_overflow"] = 3
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_alloc_total_inconsistent(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["alloc_cxx_total"] = 5
        ok, errors, _ = hard_pass(ev); self.assertFalse(ok)
        self.assertTrue(any("alloc_cxx_total" in e for e in errors), errors)

    def test_free_total_inconsistent(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["free_total"] = 2
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_negative_counter(self):
        ev = self.fresh(); self.enabled_clean(ev)["cold"]["lock"] = -1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_snapshot_field(self):
        ev = self.fresh(); del self.enabled_clean(ev)["warm"]["noop_frees"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- C1 cursor semantics -------------------------------------------------
    def test_audio_owner_delta_ok_false(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_delta_ok"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_delta_mismatch(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_delta_mismatches"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_backward(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_backward"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_reported_monotonic_false(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["reported_monotonic"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_reported_future(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["reported_future"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_many_coalesced_reads_are_acceptable(self):
        # Coalescing itself is legitimate; only a bad produced cursor fails.
        ev = self.fresh()
        c = self.cell(ev, "enabled_pressure_r48000_b128_clean")["progression"]
        c["coalesced_reads"] = 500
        c["skipped_publications"] = 500
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    # -- M9 lag null policy --------------------------------------------------
    def test_receipt_order_violation(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["receipt_order_violation"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_receipt_measured_but_lag_null(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["receipt_availability_lag_last"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_no_receipt_but_lag_non_null(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")["progression"]
        c["receipt_availability_lag_last"] = 128
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_receipt_without_new_receipts(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["new_receipts"] = 0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_cursor_lag_measured_but_null(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["worker_cursor_lag_last"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_cursor_lag_measured_when_owner_unmeasured(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")["progression"]
        c["audio_owner_measured"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- silence / control ---------------------------------------------------
    def test_silence_fabricated_lock(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["state_end"]["clock"]["lockState"] = "Locked"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_silence_fabricated_confidence(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["state_end"]["clock"]["confidence01"] = 0.7
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_silence_drums_playing(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["progression"]["drums_playing_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_disabled_requested_running(self):
        ev = self.fresh(); self.cell(ev, "disabled_r48000_b128_clean")["progression"]["requested_running_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_disabled_drums_playing(self):
        ev = self.fresh(); self.cell(ev, "disabled_r48000_b128_noise")["progression"]["drums_playing_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_state_end_sample_rate_mismatch(self):
        ev = self.fresh(); self.cell(ev, "enabled_r96000_b512_clean")["state_end"]["sampleRate"] = 48000.0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- identity / backend --------------------------------------------------
    def test_selfcheck_failed(self):
        ev = self.fresh(); ev["identity"]["instrument"]["selfcheck_pass"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_unusable(self):
        ev = self.fresh(); ev["identity"]["backend"]["usable"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_wrong_kind(self):
        ev = self.fresh(); ev["identity"]["backend"]["kind"] = "injectedTest"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_macro_missing(self):
        ev = self.fresh(); ev["identity"]["backend"]["macro_defined"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_source_matches_product_false(self):
        ev = self.fresh(); ev["identity"]["product"]["source_matches_product"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_facade_symbol(self):
        ev = self.fresh(); ev["identity"]["facade_symbols"]["readJamLiveState"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_bad_source_hash(self):
        ev = self.fresh(); ev["identity"]["source"]["src_tree_hash"] = "xyz"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- timing / fields -----------------------------------------------------
    def test_callback_count_mismatch(self):
        ev = self.fresh(); self.enabled_clean(ev)["timing"]["callback_count"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_required_cell_field(self):
        ev = self.fresh(); del self.enabled_clean(ev)["commands"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_unmeasured_without_reason(self):
        ev = self.fresh(); c = self.enabled_clean(ev); c["measured"] = False; c["unmeasured_reason"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- M8 allocation scope and findings -----------------------------------
    def test_allocation_scope_wrong(self):
        ev = self.fresh(); ev["allocation_scope"] = "pipeline-wide"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_allocations_claimed_measured(self):
        ev = self.fresh(); ev["worker_allocations"]["measured"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_hidden_callback_finding(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        # findings left empty -> hidden
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("findings" in e for e in errors), errors)

    def test_findings_force_measured_findings_status(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "warm_callback_alloc", "detail": "alloc=1"}]
        ev["status"] = "measured"  # wrong: must be measured-findings
        ok, _, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_findings_status_correct(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "warm_callback_alloc", "detail": "alloc=1"}]
        ev["status"] = "measured-findings"
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    # -- fixtures ------------------------------------------------------------
    def test_fixture_local_hash_mismatch(self):
        ev = self.fresh(); ev["fixtures"]["entries"][0]["sha256"] = "0" * 64
        ok, _, _ = hard_pass(ev, fixtures_dir=self.fixtures_dir); self.assertFalse(ok)

    def test_fixture_unmeasured_without_reason(self):
        ev = self.fresh(); ev["fixtures"] = {"unmeasured": True, "reason": ""}
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_fixture_bad_sample_exact_checksum(self):
        ev = self.fresh(); ev["fixtures"]["entries"][1]["sample_exact_checksum"] = "nothex"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_fixture_not_marked_synthetic(self):
        ev = self.fresh(); ev["fixtures"]["entries"][0]["not_guitar_recording"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- status --------------------------------------------------------------
    def test_unknown_status(self):
        ev = self.fresh(); ev["status"] = "trust-me"
        path = os.path.join(self.tmp, "ev-unknown.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertNotEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 0)

    def test_expect_status_mismatch(self):
        path = os.path.join(self.tmp, "ev-expect.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertNotEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest",
                                     "--expect-status", "awaiting-product"]), 0)


class AwaitingAndTimeoutTests(unittest.TestCase):
    def base_awaiting(self, status="awaiting-product"):
        return {"schema": ve.SCHEMA_AWAITING, "task": "EVAL-LIVE-001", "status": status,
                "clean": False, "invoked_binary": False,
                "missing": ["facade_symbol_submit", "facade_symbol_read", "facade_definition"],
                "identity": {}}

    def test_valid_awaiting_product(self):
        errors = []; checks = []
        ve.validate_awaiting(self.base_awaiting(), errors, checks)
        self.assertFalse(errors, errors)

    def test_valid_awaiting_backend(self):
        ev = self.base_awaiting("awaiting-backend")
        ev["missing"] = ["backend_usable", "backend_exact", "backend_macro"]
        errors = []; ve.validate_awaiting(ev, errors, [])
        self.assertFalse(errors, errors)

    def test_awaiting_claims_clean_fails(self):
        ev = self.base_awaiting(); ev["clean"] = True
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_invoked_binary_fails(self):
        ev = self.base_awaiting(); ev["invoked_binary"] = True
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_missing_empty_fails(self):
        ev = self.base_awaiting(); ev["missing"] = []
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_missing_without_facade_or_backend_fails(self):
        ev = self.base_awaiting(); ev["missing"] = ["something_else"]
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_timed_out_valid(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "measured_partial": True, "timeout_s": 300}
        errors = []; checks = []
        ve.validate_timed_out(ev, errors, checks)
        self.assertFalse(errors, errors)

    def test_timed_out_invoked_false_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": False,
              "measured_partial": True, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, []); self.assertTrue(errors)

    def test_timed_out_partial_false_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "measured_partial": False, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, []); self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
