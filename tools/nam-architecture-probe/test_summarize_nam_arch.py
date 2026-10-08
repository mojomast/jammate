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


class FixtureTests(unittest.TestCase):
    """Mutate a copy of one real run and check the validator's verdict."""

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
        return summ.validate_run(run_dir, self.pre, variant, path, digest, bin_sha, self.mdir)

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
            summ.validate_run(d, self.pre, "repaired", path, digest, "00" * 32, self.mdir)

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
            summ.validate_run(d, self.pre, "repaired", os.path.join(self.tmp, "absent.nam"),
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


if __name__ == "__main__":
    unittest.main()
