#!/usr/bin/env python3
"""RT-004 tests: committed NAM architecture evidence + fail-closed validator fixtures.

Evidence tests assert facts about the committed run artifacts (independently
re-read from the raw per-case CSVs and the real model files), not just the
summariser's own output. Fixture tests mutate a copy of one real run and check
that the validator fails closed or reports the right status.

Run: python3 -m unittest -v tools/nam-architecture-probe/test_summarize_nam_arch.py
"""
import csv
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "docs", "research", "nam-architecture-probe")
RUNS = os.path.join(OUT, "runs")

spec = importlib.util.spec_from_file_location("summ", os.path.join(HERE, "summarize_nam_arch.py"))
summ = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summ)


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def predeclared():
    with open(os.path.join(HERE, "predeclared.json"), encoding="utf-8") as f:
        return json.load(f)


def models_dir(pre):
    d = os.path.join(REPO, pre["models_dir"])
    if not os.path.isdir(d):
        d = os.path.join("/home/mojo/projects/guitars", pre["models_dir"])
    return d


def summary():
    with open(os.path.join(OUT, "summary.json"), encoding="utf-8") as f:
        return json.load(f)


def raw_csv(variant, arch_id):
    with open(os.path.join(RUNS, variant, arch_id, "cases.csv"), newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pre = predeclared()
        cls.s = summary()
        cls.runs = {(r["variant"], r["architecture_id"]): r for r in cls.s["runs"]}

    def test_all_ten_runs_present_and_measured(self):
        self.assertEqual(len(self.s["runs"]), 10)
        self.assertEqual(len(self.runs), 10)
        self.assertTrue(self.s["coverage_complete"])
        self.assertEqual(self.s["unmeasured_runs"], 0)
        self.assertEqual(self.s["failed_runs"], 0)
        self.assertEqual(self.s["clean_runs"], 7)
        self.assertEqual(self.s["findings_runs"], 3)

    def test_each_run_has_exact_26_case_matrix(self):
        for key, r in self.runs.items():
            self.assertTrue(r["loaded"], key)
            self.assertEqual(r["dry_rows"], 18, key)
            self.assertEqual(r["nam_rows"], 8, key)
            tags = sorted((row["tag"], row["rate"], row["block"]) for row in r["rows"] if row["key"][0] == "nam")
            expected = sorted((t, rate, block) for t in ("nam", "nam+drums")
                              for rate in (48000, 96000) for block in (128, 512))
            self.assertEqual([(t, rt, b) for t, rt, b in tags], expected, key)

    def test_warm_allocations_match_independent_csv_reread(self):
        for variant in ("repaired", "original_control"):
            for a in self.pre["architectures"]:
                rows = raw_csv(variant, a["id"])
                nam = [r for r in rows if r["tag"].startswith("nam")]
                total = sum(int(r["warm_alloc_cxx_total"]) + int(r["warm_alloc_c_total"]) for r in nam)
                self.assertEqual(total, self.runs[(variant, a["id"])]["nam_warm_alloc_total"], (variant, a["id"]))

    def test_repaired_lstm_control_is_zero_and_original_reproduces_rt002(self):
        self.assertEqual(self.runs[("repaired", "lstm_control")]["nam_warm_alloc_total"], 0)
        self.assertEqual(self.runs[("repaired", "lstm_control")]["status"], "measured-clean")
        orig = self.runs[("original_control", "lstm_control")]
        self.assertEqual(orig["nam_warm_alloc_total"], 491520)
        self.assertEqual(orig["nam_warm_alloc_per_host_sample_max"], 2.0)
        self.assertEqual(orig["nam_warm_alloc_per_host_sample_min"], 1.0)

    def test_a2_wavenet_max_positive_in_both_variants_with_nam_call_sites(self):
        for variant in ("repaired", "original_control"):
            r = self.runs[(variant, "a2_wavenet_max")]
            self.assertEqual(r["status"], "measured-findings")
            self.assertEqual(r["nam_warm_alloc_total"], 491520)
            self.assertEqual(r["dry_rows_nonzero"], 0)
            self.assertEqual(r["nam_rows_nonzero"], 8)
            functions = " ".join(c["function"] for c in r["call_sites"])
            self.assertIn("ActivationPReLU", functions)

    def test_a1_and_slimmable_and_container_zero_on_repaired(self):
        for arch in ("a1_wavenet_standard", "slimmable_wavenet", "a2_slimmable_container"):
            self.assertEqual(self.runs[("repaired", arch)]["status"], "measured-clean", arch)
            self.assertEqual(self.runs[("repaired", arch)]["nam_warm_alloc_total"], 0, arch)

    def test_noop_frees_reported_separately_not_as_heap_frees(self):
        for r in self.s["runs"]:
            self.assertGreater(r["noopfree_total_reported_separately"], 0)
            self.assertEqual(r["nam_warm_free_total"], r["nam_warm_alloc_total"])

    def test_per_sample_output_not_claimed_checked(self):
        for r in self.s["runs"]:
            self.assertFalse(r["per_sample_output_checked"])

    def test_model_identity_matches_real_files_and_versions(self):
        mdir = models_dir(self.pre)
        for a in self.pre["architectures"]:
            path = os.path.join(mdir, a["file"])
            self.assertTrue(os.path.isfile(path), path)
            self.assertEqual(sha(path), a["sha256"])
            with open(os.path.join(OUT, "models", a["id"] + ".json"), encoding="utf-8") as f:
                ident = json.load(f)
            self.assertEqual(ident["sha256"], a["sha256"])
            self.assertEqual(ident["architecture"], a["expected_architecture"])
            self.assertEqual(ident["version"], a["expected_version"])
            self.assertNotIn("weights", ident, "weights must not be shipped")

    def test_a2_container_default_submodel_is_last_index(self):
        with open(os.path.join(OUT, "models", "a2_slimmable_container.json"), encoding="utf-8") as f:
            ident = json.load(f)
        self.assertEqual(ident["config"]["default_active_submodel_index"], 1)
        self.assertEqual(len(ident["config"]["submodels"]), 2)

    def test_binary_and_archive_identities_disambiguate_repaired_and_original(self):
        rep = self.pre["binaries"]["repaired"]
        orig = self.pre["binaries"]["original_control"]
        self.assertNotEqual(rep["nam_archive_sha256"], orig["nam_archive_sha256"])
        self.assertNotEqual(rep["sha256"], orig["sha256"])
        self.assertEqual(rep["nam_archive_sha256"][:8], "dbd11fb2")
        self.assertEqual(orig["nam_archive_sha256"][:8], "af023ffc")
        for name in ("repaired", "original_control"):
            self.assertEqual(sha(self.pre["binaries"][name]["path"]), self.pre["binaries"][name]["sha256"])

    def test_source_pin_matches_current_processor_sources(self):
        for rel, expected in self.pre["source_pin"]["files"].items():
            self.assertEqual(sha(os.path.join(REPO, rel)), expected, rel)

    def test_manifest_verifies_every_artifact(self):
        with open(os.path.join(OUT, "manifest.sha256"), encoding="utf-8") as f:
            lines = [l.rstrip("\n") for l in f if l.strip()]
        self.assertGreater(len(lines), 40)
        for line in lines:
            digest, rel = line.split("  ", 1)
            self.assertEqual(sha(os.path.join(OUT, rel)), digest, rel)


class FixtureMixin:
    """Fixture helpers shared by the fail-closed fixture classes.

    Mixed in rather than subclassed: the adversarial classes add their own cases
    and must not re-run FixtureTests' inherited ones."""

    pre = None
    mdir = None
    tmp = None

    @classmethod
    def setUpClass(cls):
        cls.pre = predeclared()
        cls.mdir = models_dir(cls.pre)
        cls.tmp = tempfile.mkdtemp(prefix="rt004-fixture-")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def fixture(self, variant, arch_id, name):
        dst = os.path.join(self.tmp, name)
        shutil.copytree(os.path.join(RUNS, variant, arch_id), dst)
        return dst

    def model(self, arch_id):
        a = next(x for x in self.pre["architectures"] if x["id"] == arch_id)
        return os.path.join(self.mdir, a["file"]), a["sha256"]

    def validate(self, run_dir, variant, arch_id):
        path, digest = self.model(arch_id)
        bin_sha = self.pre["binaries"][variant]["sha256"]
        return summ.validate_run(run_dir, self.pre, variant, arch_id, path, digest, bin_sha, self.mdir)

    def rewrite(self, path, fn):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        new = fn(text)
        self.assertNotEqual(new, text, "fixture mutation did not apply")
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)

    def csv_rows(self, run_dir):
        with open(os.path.join(run_dir, "cases.csv"), newline="", encoding="utf-8") as f:
            r = list(csv.reader(f))
        return r[0], r[1:]

    def write_csv(self, run_dir, header, rows):
        with open(os.path.join(run_dir, "cases.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(rows)

class FixtureTests(FixtureMixin, unittest.TestCase):
    """Mutate a copy of one real run and check the validator's verdict."""

    def test_real_positive_run_is_findings_not_failure(self):
        d = self.fixture("repaired", "a2_wavenet_max", "pos")
        r = self.validate(d, "repaired", "a2_wavenet_max")
        self.assertEqual(r["status"], "measured-findings")
        self.assertGreater(r["nam_warm_alloc_total"], 0)

    def test_real_clean_run_is_clean(self):
        d = self.fixture("repaired", "lstm_control", "clean")
        self.assertEqual(self.validate(d, "repaired", "lstm_control")["status"], "measured-clean")

    def test_non_zero_probe_exit_fails_closed(self):
        d = self.fixture("repaired", "lstm_control", "exit")
        self.rewrite(os.path.join(d, "exit-status.txt"), lambda t: t.replace("exit=0", "exit=5"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_model_sha_mismatch_in_status_fails(self):
        d = self.fixture("repaired", "lstm_control", "shaexit")
        self.rewrite(os.path.join(d, "exit-status.txt"), lambda t: t.replace("model_sha256=df9f", "model_sha256=0000"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_binary_identity_mismatch_fails(self):
        d = self.fixture("repaired", "lstm_control", "bin")
        with self.assertRaises(summ.Failed):
            path, digest = self.model("lstm_control")
            summ.validate_run(d, self.pre, "repaired", "lstm_control", path, digest, "00" * 32, self.mdir)

    def test_wrong_variant_binary_fails(self):
        d = self.fixture("repaired", "lstm_control", "variant")
        with self.assertRaises(summ.Failed):
            self.validate(d, "original_control", "lstm_control")

    def test_model_identity_not_witnessed_in_log_fails(self):
        d = self.fixture("repaired", "lstm_control", "ident")
        self.rewrite(os.path.join(d, "probe.log"),
                     lambda t: t.replace("requesting async load: /", "requesting async load: /other/"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_loaded_true_without_activation_witness_fails(self):
        d = self.fixture("repaired", "lstm_control", "noact")
        self.rewrite(os.path.join(d, "probe.log"),
                     lambda t: t.replace("model became active during the dry run", "xx"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_unloaded_model_is_unmeasured_not_clean(self):
        d = self.fixture("repaired", "lstm_control", "unloaded")
        header, rows = self.csv_rows(d)
        dry_only = [r for r in rows if r[0] == "dry" or r[0] == "dry+drums"]
        self.write_csv(d, header, dry_only)
        self.rewrite(os.path.join(d, "probe.log"),
                     lambda t: t.replace('loaded=1 resampling=1 error=""',
                                         'loaded=0 resampling=0 error="Invalid .nam file"')
                               .replace("model became active during the dry run", "xx")
                               + "\n[nam] UNMEASURED: model did not become active\n")
        self.rewrite(os.path.join(d, "findings.json"), lambda t: t.replace('"nam_cases": 8', '"nam_cases": 0'))
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "unmeasured")
        self.assertEqual(r["nam_rows"], 0)

    def test_unloaded_model_still_carrying_nam_rows_fails(self):
        d = self.fixture("repaired", "lstm_control", "unloaded_rows")
        self.rewrite(os.path.join(d, "probe.log"),
                     lambda t: t.replace('loaded=1 resampling=1 error=""', 'loaded=0 resampling=0 error="x"')
                               .replace("model became active during the dry run", "xx")
                               + "\n[nam] UNMEASURED: x\n")
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_loaded_with_nonempty_error_fails(self):
        d = self.fixture("repaired", "lstm_control", "errloaded")
        self.rewrite(os.path.join(d, "probe.log"),
                     lambda t: t.replace('error=""', 'error="oops"'))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_missing_nam_case_fails(self):
        d = self.fixture("repaired", "lstm_control", "missing")
        header, rows = self.csv_rows(d)
        rows = [r for i, r in enumerate(rows) if not (r[0].startswith("nam") and i == len(rows) - 1)]
        self.assertEqual(len([r for r in rows if r[0].startswith("nam")]), 7)
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_duplicate_case_fails(self):
        d = self.fixture("repaired", "lstm_control", "dup")
        header, rows = self.csv_rows(d)
        self.write_csv(d, header, rows + [rows[-1]])
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_nonfinite_out_rms_fails(self):
        d = self.fixture("repaired", "lstm_control", "nan")
        header, rows = self.csv_rows(d)
        idx = header.index("out_rms")
        rows[-1][idx] = "nan"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_negative_count_fails(self):
        d = self.fixture("repaired", "lstm_control", "neg")
        header, rows = self.csv_rows(d)
        idx = header.index("warm_malloc")
        rows[0][idx] = "-1"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_fractional_count_fails(self):
        d = self.fixture("repaired", "lstm_control", "frac")
        header, rows = self.csv_rows(d)
        idx = header.index("warm_calloc")
        rows[0][idx] = "1.5"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_wrong_header_fails(self):
        d = self.fixture("repaired", "lstm_control", "hdr")
        header, rows = self.csv_rows(d)
        header[-1] = "out_rms2"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_short_csv_row_fails(self):
        d = self.fixture("repaired", "lstm_control", "short")
        header, rows = self.csv_rows(d)
        rows[3] = rows[3][:-2]
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_malformed_findings_json_fails(self):
        d = self.fixture("repaired", "lstm_control", "badjson")
        self.rewrite(os.path.join(d, "findings.json"), lambda t: t[:-5])
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_findings_nam_count_disagreeing_with_csv_fails(self):
        d = self.fixture("repaired", "a2_wavenet_max", "disagree")
        self.rewrite(os.path.join(d, "findings.json"),
                     lambda t: t.replace('"nam_cases_with_alloc": 8', '"nam_cases_with_alloc": 0'))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "a2_wavenet_max")

    def test_missing_model_file_fails(self):
        d = self.fixture("repaired", "lstm_control", "nomodel")
        with self.assertRaises(summ.Failed):
            summ.validate_run(d, self.pre, "repaired", "lstm_control",
                              os.path.join(self.tmp, "absent.nam"),
                              "df9f78c49f49c2bb32411df47e3f53746075adb206b92d017e06379d1e56234a",
                              self.pre["binaries"]["repaired"]["sha256"], self.mdir)

    def test_missing_probe_log_fails(self):
        d = self.fixture("repaired", "lstm_control", "nolog")
        os.remove(os.path.join(d, "probe.log"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_self_check_absence_fails(self):
        d = self.fixture("repaired", "lstm_control", "noself")
        self.rewrite(os.path.join(d, "probe.log"), lambda t: t.replace("self-check PASS", "self-check FAIL"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")


class ProtocolBindingTests(FixtureMixin, unittest.TestCase):
    """Fix 1: the warm-block budget, timeout and architecture identity come from
    the predeclared protocol, never from the run's own exit status. A coherently
    rewritten budget (rows AND exit status together) must still be rejected."""

    def coherent_budget(self, run_dir, new_budget):
        header, rows = self.csv_rows(run_dir)
        i = header.index("warm_blocks")
        for r in rows:
            r[i] = str(new_budget)
        self.write_csv(run_dir, header, rows)
        self.rewrite(os.path.join(run_dir, "exit-status.txt"),
                     lambda t: t.replace(f"warm_blocks={self.pre['matrix']['warm_blocks']}",
                                         f"warm_blocks={new_budget}"))

    def test_coherently_changed_budget_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "budget")
        self.coherent_budget(d, 64)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("protocol", str(ctx.exception))

    def test_row_budget_alone_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "budget_row")
        header, rows = self.csv_rows(d)
        rows[0][header.index("warm_blocks")] = "64"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_exit_status_budget_alone_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "budget_exit")
        self.rewrite(os.path.join(d, "exit-status.txt"), lambda t: t.replace("warm_blocks=128", "warm_blocks=64"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_timeout_deviation_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "timeout")
        self.rewrite(os.path.join(d, "exit-status.txt"), lambda t: t.replace("timeout_s=600", "timeout_s=5"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_architecture_id_mismatch_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "archid")
        self.rewrite(os.path.join(d, "exit-status.txt"),
                     lambda t: t.replace("architecture_id=lstm_control", "architecture_id=a2_wavenet_max"))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_missing_architecture_id_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "noarchid")
        p = os.path.join(d, "exit-status.txt")
        with open(p, encoding="utf-8") as f:
            kept = [l for l in f if not l.startswith("architecture_id=")]
        with open(p, "w", encoding="utf-8") as f:
            f.writelines(kept)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("architecture_id", str(ctx.exception))

    def test_protocol_budget_helper_rejects_bad_predeclaration(self):
        for bad in ({"matrix": {"timeout_s": 600}}, {"matrix": {"warm_blocks": 0}},
                    {"matrix": {"warm_blocks": "128", "timeout_s": 600}},
                    {"matrix": {"warm_blocks": True, "timeout_s": 600}}):
            with self.assertRaises(summ.Failed):
                summ.protocol_budget(bad)


class ExactRateTests(FixtureMixin, unittest.TestCase):
    """Fix 2: rates/blocks must be exact integers. A float->int truncation used to
    fold 48000.5 into the valid 48000 key."""

    def set_rate(self, run_dir, value, occurrence=0):
        """Rewrite the rate of one NAM row only, so the 48000 case set stays distinct."""
        header, rows = self.csv_rows(run_dir)
        i = header.index("rate")
        seen = 0
        for r in rows:
            if r[0] == "nam" and r[i] == "48000":
                if seen == occurrence:
                    r[i] = value
                    break
                seen += 1
        self.write_csv(run_dir, header, rows)

    def test_fractional_rate_is_rejected(self):
        d = self.fixture("repaired", "lstm_control", "fracrate")
        self.set_rate(d, "48000.5")
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("exact integer", str(ctx.exception))

    def test_integral_float_rate_accepted_as_exact(self):
        d = self.fixture("repaired", "lstm_control", "intfloat")
        self.set_rate(d, "48000.0")
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "measured-clean")

    def test_unexpected_integer_rate_is_rejected_by_matrix(self):
        d = self.fixture("repaired", "lstm_control", "unexp_rate")
        # Rewrite rate AND duration together, so the rate is internally consistent
        # and rejection must come from the case matrix, not from the duration check.
        header, rows = self.csv_rows(d)
        i_rate, i_dur, i_wb = header.index("rate"), header.index("duration_s"), header.index("warm_blocks")
        seen = 0
        for r in rows:
            if r[0] == "nam" and r[i_rate] == "48000" and seen == 0:
                r[i_rate] = "44100"
                r[i_dur] = f"{int(r[i_wb]) * int(r[header.index('block')]) / 44100.0:.4f}"
                break
            if r[0] == "nam" and r[i_rate] == "48000":
                seen += 1
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("matrix mismatch", str(ctx.exception))

    def test_exact_int_helper_rejects_fraction(self):
        self.assertEqual(summ.as_exact_int("512", "block"), 512)
        self.assertEqual(summ.as_exact_int("48000.0", "rate"), 48000)
        for bad in ("48000.5", "nan", "inf", "abc", ""):
            with self.assertRaises(summ.Failed):
                summ.as_exact_int(bad, "rate")


class OverflowAndAggregateTests(FixtureMixin, unittest.TestCase):
    """Fix 3: capture-overflow counters are retained as findings (never
    measured-clean), and the warm aggregates may never hide a positive
    per-kind counter."""

    def set_cell(self, run_dir, col, value, tag_prefix="nam"):
        header, rows = self.csv_rows(run_dir)
        i = header.index(col)
        for r in rows:
            if r[0].startswith(tag_prefix):
                r[i] = value
                break
        self.write_csv(run_dir, header, rows)

    def test_cold_alloc_overflow_blocks_clean(self):
        d = self.fixture("repaired", "lstm_control", "coldovf")
        self.set_cell(d, "cold_alloc_overflow", "7", tag_prefix="dry")
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "measured-findings")
        self.assertEqual(r["dry_rows_with_capture_overflow"], 1)
        self.assertEqual(r["dry_rows_nonzero"], 1)

    def test_cold_lock_overflow_blocks_clean(self):
        d = self.fixture("repaired", "lstm_control", "coldlockovf")
        self.set_cell(d, "cold_lock_overflow", "3", tag_prefix="dry")
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "measured-findings")
        self.assertIn("capture overflow", r["status_reason"])
        self.assertEqual(r["dry_rows_with_capture_overflow"], 1)

    def test_warm_overflow_alone_blocks_clean(self):
        d = self.fixture("repaired", "lstm_control", "warmovf")
        self.set_cell(d, "warm_alloc_overflow", "5")
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "measured-findings")
        self.assertEqual(r["nam_rows_with_capture_overflow"], 1)

    def test_overflow_is_recorded_not_dropped(self):
        d = self.fixture("repaired", "lstm_control", "ovfkept")
        self.set_cell(d, "warm_alloc_overflow", "9")
        r = self.validate(d, "repaired", "lstm_control")
        row = [x for x in r["rows"] if x["warm_alloc_overflow"] == 9]
        self.assertEqual(len(row), 1)
        self.assertEqual(r["capture_overflow_total"], 9)

    def test_historical_overflow_runs_stay_findings(self):
        """The committed positive runs already carry warm overflow; they must stay
        findings rather than being reclassified."""
        for variant, arch in (("repaired", "a2_wavenet_max"),
                              ("original_control", "a2_wavenet_max"),
                              ("original_control", "lstm_control")):
            d = self.fixture(variant, arch, f"hist_{variant}_{arch}")
            r = self.validate(d, variant, arch)
            self.assertEqual(r["status"], "measured-findings", (variant, arch))
            self.assertGreater(r["nam_warm_alloc_total"], 0, (variant, arch))
            self.assertGreater(r["nam_rows_with_capture_overflow"], 0, (variant, arch))

    def test_warm_cxx_total_below_visible_components_fails(self):
        d = self.fixture("repaired", "lstm_control", "cxxagg")
        header, rows = self.csv_rows(d)
        i_new, i_tot = header.index("warm_cxxnew"), header.index("warm_alloc_cxx_total")
        for r in rows:
            if r[0].startswith("nam"):
                r[i_new] = "4"
                r[i_tot] = "1"
                break
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("warm_alloc_cxx_total", str(ctx.exception))

    def test_warm_c_total_mismatch_fails(self):
        d = self.fixture("repaired", "lstm_control", "cagg")
        header, rows = self.csv_rows(d)
        i_m, i_tot = header.index("warm_malloc"), header.index("warm_alloc_c_total")
        for r in rows:
            if r[0].startswith("nam"):
                r[i_m] = "8"
                r[i_tot] = "0"
                break
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("warm_alloc_c_total", str(ctx.exception))

    def test_extraneous_cxx_total_is_allowed(self):
        """nothrow/aligned new have no CSV columns, so the C++ total may legitimately
        exceed the visible sum; that must not be treated as an error."""
        d = self.fixture("repaired", "lstm_control", "cxxextra")
        header, rows = self.csv_rows(d)
        i_tot = header.index("warm_alloc_cxx_total")
        # One NAM row only: the visible C++ components stay 0, so a C++ total of 9
        # can only come from nothrow/aligned new, which the CSV cannot show.
        for r in rows:
            if r[0].startswith("nam"):
                r[i_tot] = "9"
                break
        self.write_csv(d, header, rows)
        # findings.json reports how many NAM rows allocated, so it must follow.
        self.rewrite(os.path.join(d, "findings.json"),
                     lambda t: t.replace('"nam_cases_with_alloc": 0', '"nam_cases_with_alloc": 1'))
        r = self.validate(d, "repaired", "lstm_control")
        self.assertEqual(r["status"], "measured-findings")
        self.assertEqual(r["nam_rows_nonzero"], 1)
        self.assertEqual(r["nam_warm_alloc_total"], 9)
        row = [x for x in r["rows"] if x["warm_alloc_cxx_total"] == 9][0]
        self.assertEqual(row["warm_alloc_c_total"], 0)

    def test_positive_c_component_cannot_be_hidden_by_zero_aggregate(self):
        d = self.fixture("repaired", "lstm_control", "hidden")
        header, rows = self.csv_rows(d)
        i_c, i_tot = header.index("warm_calloc"), header.index("warm_alloc_c_total")
        for r in rows:
            if r[0].startswith("nam"):
                r[i_c] = "16"
                r[i_tot] = "0"
                break
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")


class PinAndArchiveTests(unittest.TestCase):
    """Fix 4: the RT-003 pin file is mandatory and the actual NAM/shared archives
    on disk are hashed, not merely read out of the pin text."""

    @classmethod
    def setUpClass(cls):
        cls.pre = predeclared()

    def test_real_archives_verify_on_disk(self):
        pins = summ.check_archive_pins(self.pre)
        for name in ("repaired", "original_control"):
            self.assertTrue(pins[name]["nam_archive_sha256_verified_on_disk"])
            self.assertTrue(pins[name]["shared_archive_sha256_verified_on_disk"])

    def test_repaired_and_original_nam_archives_are_distinct_files(self):
        rep = self.pre["binaries"]["repaired"]
        orig = self.pre["binaries"]["original_control"]
        self.assertNotEqual(os.path.realpath(rep["nam_archive_path"]),
                            os.path.realpath(orig["nam_archive_path"]))
        self.assertEqual(summ.sha256_file(rep["nam_archive_path"]), rep["nam_archive_sha256"])
        self.assertEqual(summ.sha256_file(orig["nam_archive_path"]), orig["nam_archive_sha256"])

    def test_missing_pin_file_is_rejected(self):
        pre = json.loads(json.dumps(self.pre))
        pre["binaries"]["repaired"]["pin_file"] = os.path.join(OUT, "absent-pin.txt")
        with self.assertRaises(summ.Failed) as ctx:
            summ.check_archive_pins(pre)
        self.assertIn("pin file missing", str(ctx.exception))

    def test_missing_nam_archive_path_is_rejected(self):
        pre = json.loads(json.dumps(self.pre))
        pre["binaries"]["repaired"]["nam_archive_path"] = os.path.join(OUT, "absent.a")
        with self.assertRaises(summ.Failed) as ctx:
            summ.check_archive_pins(pre)
        self.assertIn("NAM archive missing", str(ctx.exception))

    def test_missing_shared_archive_path_is_rejected(self):
        pre = json.loads(json.dumps(self.pre))
        pre["binaries"]["original_control"]["shared_archive_path"] = os.path.join(OUT, "absent2.a")
        with self.assertRaises(summ.Failed):
            summ.check_archive_pins(pre)

    def test_tampered_nam_archive_hash_is_rejected(self):
        pre = json.loads(json.dumps(self.pre))
        pre["binaries"]["repaired"]["nam_archive_sha256"] = "00" * 32
        with self.assertRaises(summ.Failed):
            summ.check_archive_pins(pre)

    def test_pin_shared_archive_path_mismatch_is_rejected(self):
        pre = json.loads(json.dumps(self.pre))
        pre["binaries"]["repaired"]["shared_archive_path"] = \
            "/home/mojo/projects/build-RT-003-integration/product/libGuitarCompanionAssets.a"
        with self.assertRaises(summ.Failed) as ctx:
            summ.check_archive_pins(pre)
        self.assertIn("shared_archive", str(ctx.exception))


class RunnerPreflightTests(unittest.TestCase):
    """Fix 4: the runner refuses to measure on a protocol deviation."""

    RUNNER = os.path.join(HERE, "run_nam_arch.sh")

    def run_runner(self, env, out_dir):
        e = dict(os.environ)
        e.update(env)
        e["NAM_ARCH_OUT"] = out_dir
        return subprocess.run(["bash", self.RUNNER], capture_output=True, text=True, env=e)

    def test_invalid_variant_is_rejected_before_measuring(self):
        out = os.path.join(tempfile.gettempdir(), "rt004-pref-badvar")
        p = self.run_runner({"NAM_ARCH_ONLY": "bogus"}, out)
        self.assertEqual(p.returncode, 2)
        self.assertIn("not a predeclared variant", p.stderr)
        self.assertNotIn("exit=", p.stdout)

    def test_changed_budget_is_rejected_before_measuring(self):
        out = os.path.join(tempfile.gettempdir(), "rt004-pref-budget")
        p = self.run_runner({"NAM_ARCH_WARM": "64"}, out)
        self.assertEqual(p.returncode, 2)
        self.assertIn("protocol warm_blocks=128", p.stderr)
        self.assertFalse(os.path.exists(os.path.join(out, "repaired", "lstm_control", "cases.csv")))

    def test_changed_timeout_is_rejected_before_measuring(self):
        out = os.path.join(tempfile.gettempdir(), "rt004-pref-timeout")
        p = self.run_runner({"NAM_ARCH_TIMEOUT": "5"}, out)
        self.assertEqual(p.returncode, 2)
        self.assertIn("protocol timeout_s=600", p.stderr)

    def test_missing_model_is_rejected_before_measuring(self):
        out = os.path.join(tempfile.gettempdir(), "rt004-pref-nomodel")
        p = self.run_runner({"NAM_MODELS_DIR": "/home/mojo/projects/guitars-build-resume/tmp/rt004-empty-models"},
                            out)
        self.assertEqual(p.returncode, 2)
        self.assertIn("model missing", p.stderr)
        self.assertIn("refusing to measure", p.stderr)

    def test_preflight_runs_before_any_probe(self):
        with open(self.RUNNER, encoding="utf-8") as f:
            text = f.read()
        self.assertLess(text.index("if ! preflight"), text.index("timeout \"$TIMEOUT_S\""))


class SchemaShapeTests(FixtureMixin, unittest.TestCase):
    """Fix 5: malformed nested JSON shapes raise Failed, never AttributeError."""

    def bad_json(self, name, payload):
        d = self.fixture("repaired", "lstm_control", name)
        with open(os.path.join(d, "findings.json"), "w", encoding="utf-8") as f:
            f.write(payload)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_top_level_list_fails(self):
        self.bad_json("list", "[]")

    def test_top_level_null_fails(self):
        self.bad_json("null", "null")

    def test_top_level_scalar_fails(self):
        self.bad_json("scalar", "42")

    def test_full_as_list_fails(self):
        self.bad_json("fulllist", '{"full": [], "checks": {}}')

    def test_full_as_null_fails(self):
        self.bad_json("fullnull", '{"full": null}')

    def test_missing_selfcheck_flag_fails(self):
        d = self.fixture("repaired", "lstm_control", "noselfcheckflag")
        self.rewrite(os.path.join(d, "findings.json"), lambda t: t.replace('"selfcheck_pass": true,', ""))
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("selfcheck_pass", str(ctx.exception))

    def test_non_bool_flag_fails(self):
        d = self.fixture("repaired", "lstm_control", "strflag")
        self.rewrite(os.path.join(d, "findings.json"),
                     lambda t: t.replace('"args_ok": true', '"args_ok": "yes"'))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")

    def test_string_dry_cases_fails(self):
        d = self.fixture("repaired", "lstm_control", "strdry")
        self.rewrite(os.path.join(d, "findings.json"), lambda t: t.replace('"dry_cases": 18', '"dry_cases": "18"'))
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("dry_cases", str(ctx.exception))

    def test_nam_cases_string_fails(self):
        d = self.fixture("repaired", "lstm_control", "strnam")
        self.rewrite(os.path.join(d, "findings.json"), lambda t: t.replace('"nam_cases": 8', '"nam_cases": "8"'))
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")


class DrumWitnessTests(FixtureMixin, unittest.TestCase):
    """Fix 5: a real drums case must carry bounded activity evidence."""

    def test_committed_runs_witness_drum_activity(self):
        for a in self.pre["architectures"]:
            for variant in ("repaired", "original_control"):
                r = self.validate(os.path.join(RUNS, variant, a["id"]), variant, a["id"])
                self.assertEqual(r["nam_drums_rows"], 4, (variant, a["id"]))
                self.assertEqual(r["nam_drums_rows_with_activity"], 4, (variant, a["id"]))
                self.assertGreater(r["nam_drums_voice_events_total"], 0, (variant, a["id"]))

    def test_drums_case_without_activity_fails(self):
        d = self.fixture("repaired", "lstm_control", "nodrums")
        header, rows = self.csv_rows(d)
        i = header.index("drum_active_blocks")
        for r in rows:
            if r[0] == "nam+drums":
                r[i] = "0"
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("no drum-bus activity", str(ctx.exception))

    def test_voice_events_beyond_bound_fails(self):
        d = self.fixture("repaired", "lstm_control", "manyvoices")
        header, rows = self.csv_rows(d)
        i = header.index("voice_events")
        bound = self.pre["matrix"]["warm_blocks"] * self.pre["matrix"]["num_drum_voices"]
        for r in rows:
            if r[0] == "nam+drums":
                r[i] = str(bound + 1)
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed) as ctx:
            self.validate(d, "repaired", "lstm_control")
        self.assertIn("bounded maximum", str(ctx.exception))

    def test_active_blocks_beyond_warm_budget_fails(self):
        d = self.fixture("repaired", "lstm_control", "overactive")
        header, rows = self.csv_rows(d)
        i = header.index("drum_active_blocks")
        for r in rows:
            if r[0] == "nam+drums":
                r[i] = str(self.pre["matrix"]["warm_blocks"] + 1)
        self.write_csv(d, header, rows)
        with self.assertRaises(summ.Failed):
            self.validate(d, "repaired", "lstm_control")


if __name__ == "__main__":
    unittest.main()
