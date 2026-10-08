#!/usr/bin/env python3
"""Adversarial unit tests for the EVAL-LIVE-001 evidence validator.

Every test either proves the validator accepts a fully consistent synthetic
record, or proves it fails closed on one meaningful mutation. The mutations are
deliberately independent (counter, timestamp, availability, fixture, matrix,
identity, status) so a passing suite cannot be explained by a single over-broad
check.
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


def hard_pass(ev, fixtures_dir=None, source=None):
    errors = []
    checks = []
    predeclared = ve.load_json_strict(PREDECLARED_PATH)
    ve.validate_measured(
        ev, predeclared,
        {"fixtures_dir": fixtures_dir, "source": source,
         "predeclared": None, "product_build": None},
        errors, checks)
    hard_fail = [c for c in checks if c.severity == "hard" and not c.pass_]
    return (not hard_fail and not errors), errors, checks


class LiveJamValidatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="evallive001-")
        cls.fixtures_dir = os.path.join(cls.tmp, "fixtures")
        cls.fixtures = ms.make_fixtures(cls.fixtures_dir)

    def fresh(self):
        return ms.make_evidence(PREDECLARED_PATH, copy.deepcopy(self.fixtures))

    def cell(self, ev, cid):
        for c in ev["cells"]:
            if c["id"] == cid:
                return c
        raise AssertionError(f"no cell {cid}")

    def enabled_clean(self, ev):
        return self.cell(ev, "enabled_r48000_b128_clean")

    # -- positive ------------------------------------------------------------
    def test_positive_hard_pass(self):
        ok, errors, checks = hard_pass(self.fresh())
        self.assertTrue(ok, f"errors={errors}")
        self.assertGreater(len(checks), 50)

    def test_positive_cli(self):
        path = os.path.join(self.tmp, "evidence-pos.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        rc = ve.main(["--evidence", path, "--predeclared", PREDECLARED_PATH])
        self.assertEqual(rc, 0)

    def test_positive_with_local_fixtures(self):
        ok, errors, _ = hard_pass(self.fresh(), fixtures_dir=self.fixtures_dir)
        self.assertTrue(ok, f"errors={errors}")

    # -- JSON-level ----------------------------------------------------------
    def test_nan_in_measured_field_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["timing"]["callback_us_per_block"] = float("nan")
        path = os.path.join(self.tmp, "evidence-nan.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        rc = ve.main(["--evidence", path])
        self.assertEqual(rc, 2, "literal NaN in JSON must be rejected")

    def test_infinity_in_measured_field_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["output"]["rms_mean"] = float("inf")
        path = os.path.join(self.tmp, "evidence-inf.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        rc = ve.main(["--evidence", path])
        self.assertEqual(rc, 2)

    # -- matrix --------------------------------------------------------------
    def test_missing_cell(self):
        ev = self.fresh()
        ev["cells"] = ev["cells"][:-1]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("matrix" in e for e in errors), errors)

    def test_extra_cell(self):
        ev = self.fresh()
        extra = copy.deepcopy(ev["cells"][0])
        extra["id"] = "bogus_r48000_b128_clean"
        ev["cells"].append(extra)
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_matrix_dimension_mismatch(self):
        ev = self.fresh()
        ev["matrix"]["block_sizes"] = [128, 512]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- counters ------------------------------------------------------------
    def test_nonzero_alloc_overflow(self):
        ev = self.fresh()
        self.enabled_clean(ev)["warm"]["alloc_overflow"] = 3
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_alloc_total_inconsistent(self):
        ev = self.fresh()
        self.enabled_clean(ev)["warm"]["alloc_cxx_total"] = 5
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("alloc_cxx_total" in e for e in errors), errors)

    def test_free_total_inconsistent(self):
        ev = self.fresh()
        self.enabled_clean(ev)["warm"]["free_total"] = 2
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_negative_counter(self):
        ev = self.fresh()
        self.enabled_clean(ev)["cold"]["lock"] = -1
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_missing_snapshot_field(self):
        ev = self.fresh()
        del self.enabled_clean(ev)["warm"]["noop_frees"]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- clock/audio semantics ----------------------------------------------
    def test_audio_not_monotonic(self):
        ev = self.fresh()
        self.enabled_clean(ev)["progression"]["audio_sample_monotonic"] = False
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_audio_delta_mismatch(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["audio_sample_delta_ok"] = False
        c["audio_sample_delta_mismatches"] = 2
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_receipt_before_horizon(self):
        ev = self.fresh()
        self.enabled_clean(ev)["progression"]["receipt_before_horizon"] = 1
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_event_after_horizon(self):
        ev = self.fresh()
        self.enabled_clean(ev)["progression"]["event_after_horizon"] = 1
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_state_end_receipt_order_violation(self):
        ev = self.fresh()
        end = self.enabled_clean(ev)["state_end"]
        end["lastReceiptSampleTime"] = end["lastInputHorizonSampleTime"] - 1
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_silence_fabricated_lock(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")
        c["state_end"]["clock"]["lockState"] = "Locked"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_silence_fabricated_confidence(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")
        c["state_end"]["clock"]["confidence01"] = 0.7
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_silence_drums_playing(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")
        c["progression"]["drums_playing_seen"] = True
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_disabled_requested_running(self):
        ev = self.fresh()
        c = self.cell(ev, "disabled_r48000_b128_clean")
        c["progression"]["requested_running_seen"] = True
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_disabled_drums_playing(self):
        ev = self.fresh()
        c = self.cell(ev, "disabled_r48000_b128_noise")
        c["progression"]["drums_playing_seen"] = True
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_state_end_sample_rate_mismatch(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r96000_b512_clean")
        c["state_end"]["sampleRate"] = 48000.0
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- identity / backend --------------------------------------------------
    def test_selfcheck_failed(self):
        ev = self.fresh()
        ev["identity"]["instrument"]["selfcheck_pass"] = False
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_backend_unusable(self):
        ev = self.fresh()
        ev["identity"]["backend"]["usable"] = False
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_source_matches_product_false(self):
        ev = self.fresh()
        ev["identity"]["product"]["source_matches_product"] = False
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_missing_facade_symbol(self):
        ev = self.fresh()
        ev["identity"]["facade_symbols"]["readJamLiveState"] = False
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_bad_source_hash(self):
        ev = self.fresh()
        ev["identity"]["source"]["src_tree_hash"] = "xyz"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- timing / output -----------------------------------------------------
    def test_callback_count_mismatch(self):
        ev = self.fresh()
        self.enabled_clean(ev)["timing"]["callback_count"] = 1
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_missing_required_cell_field(self):
        ev = self.fresh()
        del self.enabled_clean(ev)["commands"]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_unmeasured_without_reason(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["measured"] = False
        c["unmeasured_reason"] = None
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- fixtures ------------------------------------------------------------
    def test_fixture_local_hash_mismatch(self):
        ev = self.fresh()
        ev["fixtures"]["entries"][0]["sha256"] = "0" * 64
        ok, errors, _ = hard_pass(ev, fixtures_dir=self.fixtures_dir)
        self.assertFalse(ok, "local fixture hash mismatch must fail closed")

    def test_fixture_unmeasured_without_reason(self):
        ev = self.fresh()
        ev["fixtures"] = {"unmeasured": True, "reason": ""}
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_fixture_bad_sample_exact_checksum(self):
        ev = self.fresh()
        ev["fixtures"]["entries"][1]["sample_exact_checksum"] = "nothex"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    # -- status --------------------------------------------------------------
    def test_unknown_status(self):
        ev = self.fresh()
        ev["status"] = "trust-me"
        path = os.path.join(self.tmp, "evidence-unknown.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        rc = ve.main(["--evidence", path])
        self.assertNotEqual(rc, 0)

    def test_expect_status_mismatch(self):
        path = os.path.join(self.tmp, "evidence-expect.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        rc = ve.main(["--evidence", path, "--expect-status", "awaiting-product"])
        self.assertNotEqual(rc, 0)


class AwaitingReceiptTests(unittest.TestCase):
    def base(self):
        return {
            "schema": ve.SCHEMA_AWAITING,
            "task": "EVAL-LIVE-001",
            "status": "awaiting-product",
            "clean": False,
            "invoked_binary": False,
            "missing": ["facade_symbol_submit", "facade_symbol_read",
                        "facade_definition", "shared_archive",
                        "source_matches_product"],
            "identity": {},
        }

    def test_valid_awaiting_passes(self):
        errors = []
        checks = []
        ve.validate_awaiting(self.base(), errors, checks)
        self.assertFalse(errors, errors)

    def test_awaiting_claims_clean_fails(self):
        ev = self.base()
        ev["clean"] = True
        errors = []
        ve.validate_awaiting(ev, errors, [])
        self.assertTrue(errors)

    def test_awaiting_invoked_binary_fails(self):
        ev = self.base()
        ev["invoked_binary"] = True
        errors = []
        ve.validate_awaiting(ev, errors, [])
        self.assertTrue(errors)

    def test_awaiting_missing_empty_fails(self):
        ev = self.base()
        ev["missing"] = []
        errors = []
        ve.validate_awaiting(ev, errors, [])
        self.assertTrue(errors)

    def test_awaiting_missing_without_facade_gap_fails(self):
        ev = self.base()
        ev["missing"] = ["something_else"]
        errors = []
        ve.validate_awaiting(ev, errors, [])
        self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
