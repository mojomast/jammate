#!/usr/bin/env python3
"""RT-003 acceptance unit tests for compare_diff.py.

Builds small synthetic fixture pairs and invokes the real comparator as a
subprocess, so the tests exercise its acceptance behaviour rather than
re-implementing it. Focus is on the unsafe cases: non-finite values, malformed
structure, tampered summaries, and the byte-identity (bit-exact vs
within-budget) distinction.
"""
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, "compare_diff.py")

# name -> out_channels, must match NamLstmDiffTest.cpp
CONFIGS = [("mono_1x3", 1), ("mono_2x5", 1), ("mono_4x16", 1),
           ("stereo_3x7", 2), ("multi_io_2x11", 2), ("zero_layer", 3)]
BLOCK = 4
WARM = 1
FRAMES = WARM + 1


def fbits(v):
    return struct.pack("<f", v)


def fnv1a(values):
    h = 2166136261
    for v in values:
        for byte in struct.pack("<f", v):
            h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def base_values():
    vals = []
    for ci, (_, oc) in enumerate(CONFIGS):
        n = oc * BLOCK * FRAMES
        for k in range(n):
            vals.append(((ci + 1) * 0.1) + (k * 0.01))
    return vals


def summary(vals, seed=0x5eed1234):
    off = 0
    cfgs = []
    for name, oc in CONFIGS:
        n = oc * BLOCK * FRAMES
        sub = vals[off:off + n]
        cfgs.append({
            "name": name, "in_channels": oc, "out_channels": oc,
            "num_layers": 0, "input_size": oc, "hidden_size": oc,
            "offset": off, "count": n,
            "sum": math.fsum(sub), "sumsq": math.fsum(x * x for x in sub),
            "min": min(sub), "max": max(sub), "fnv1a": fnv1a(sub),
        })
        off += n
    return {"seed": seed, "warm_blocks": WARM, "block": BLOCK,
            "all_finite": all(math.isfinite(v) for v in vals), "configs": cfgs}


def pack(vals):
    return struct.pack("<%df" % len(vals), *vals)


class CompareDiffTest(unittest.TestCase):
    def run_compare(self, ob, pb, oj, pj):
        d = tempfile.mkdtemp(prefix="rt003cmp_")
        paths = {
            "original-bin": os.path.join(d, "o.bin"),
            "patched-bin": os.path.join(d, "p.bin"),
            "original-json": os.path.join(d, "o.json"),
            "patched-json": os.path.join(d, "p.json"),
            "out-json": os.path.join(d, "d.json"),
        }
        with open(paths["original-bin"], "wb") as f:
            f.write(ob)
        with open(paths["patched-bin"], "wb") as f:
            f.write(pb)
        with open(paths["original-json"], "w") as f:
            json.dump(oj, f)
        with open(paths["patched-json"], "w") as f:
            json.dump(pj, f)
        r = subprocess.run([sys.executable, COMPARE,
                            "--original-bin", paths["original-bin"],
                            "--patched-bin", paths["patched-bin"],
                            "--original-json", paths["original-json"],
                            "--patched-json", paths["patched-json"],
                            "--out-json", paths["out-json"]],
                           capture_output=True, text=True)
        out = None
        if os.path.exists(paths["out-json"]):
            try:
                with open(paths["out-json"]) as f:
                    out = json.load(f)
            except Exception:
                out = None
        return r.returncode, out, r.stderr + r.stdout

    def pair(self, patch_vals=None, orig_vals=None, patch_json_mut=None,
             orig_json_mut=None, patch_bin_override=None, orig_bin_override=None):
        ov = base_values() if orig_vals is None else orig_vals
        pv = list(ov) if patch_vals is None else patch_vals
        ob = pack(ov) if orig_bin_override is None else orig_bin_override
        pb = pack(pv) if patch_bin_override is None else patch_bin_override
        oj = summary(ov)
        pj = summary(pv)
        if orig_json_mut:
            orig_json_mut(oj)
        if patch_json_mut:
            patch_json_mut(pj)
        return ob, pb, oj, pj

    # --- pass cases ----------------------------------------------------------
    def test_exact_bytes_passes_bit_exact(self):
        ob, pb, oj, pj = self.pair()
        rc, out, _ = self.run_compare(ob, pb, oj, pj)
        self.assertEqual(rc, 0)
        self.assertTrue(out["all_pass"])
        self.assertTrue(out["bit_exact"])

    def test_within_budget_not_bitexact_passes_but_flags(self):
        pv = base_values()
        pv[3] += 1e-7
        ob, pb, oj, pj = self.pair(patch_vals=pv)
        rc, out, _ = self.run_compare(ob, pb, oj, pj)
        self.assertEqual(rc, 0)
        self.assertTrue(out["all_pass"])
        self.assertFalse(out["raw_bytes_identical"])
        self.assertFalse(out["bit_exact"])
        self.assertLessEqual(out["max_abs_diff"], 1e-5 + 1e-5 * 0.2)

    def test_signed_zero_numeric_equal_not_byte_identical(self):
        ov = base_values()
        pv = list(ov)
        idx = 0
        if ov[idx] == 0.0:
            pv[idx] = -0.0
        else:
            ov[idx] = 0.0
            pv[idx] = -0.0
        ob, pb, oj, pj = self.pair(patch_vals=pv, orig_vals=ov)
        rc, out, _ = self.run_compare(ob, pb, oj, pj)
        self.assertEqual(rc, 0)
        self.assertFalse(out["raw_bytes_identical"])
        self.assertEqual(out["max_abs_diff"], 0.0)

    # --- unsafe: non-finite --------------------------------------------------
    def test_nan_fails(self):
        pv = base_values()
        pv[5] = float("nan")
        ob, pb, oj, pj = self.pair(patch_vals=pv)
        # keep declared all_finite True so the binary detector is exercised
        rc, out, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_pos_inf_fails(self):
        pv = base_values()
        pv[5] = float("inf")
        ob, pb, oj, pj = self.pair(patch_vals=pv)
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_neg_inf_fails(self):
        ov = base_values()
        ov[5] = float("-inf")
        ob, pb, oj, pj = self.pair(orig_vals=ov)
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    # --- unsafe: structure ---------------------------------------------------
    def test_out_of_budget_fails(self):
        pv = base_values()
        pv[2] += 1.0
        ob, pb, oj, pj = self.pair(patch_vals=pv)
        rc, out, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_empty_configs_fails(self):
        ob, pb, oj, pj = self.pair()
        pj["configs"] = []
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_missing_config_fails(self):
        ob, pb, oj, pj = self.pair()
        del pj["configs"][-1]
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_duplicate_config_fails(self):
        ob, pb, oj, pj = self.pair()
        pj["configs"][1]["name"] = pj["configs"][0]["name"]
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_missing_tail_fails(self):
        ov = base_values()
        ob = pack(ov)
        pb = pack(ov)[:-8]   # truncate one config's worth
        oj, pj = summary(ov), summary(ov)
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_overlap_gap_fails(self):
        ob, pb, oj, pj = self.pair()
        pj["configs"][2]["offset"] += 1   # gap/overlap
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_negative_count_fails(self):
        ob, pb, oj, pj = self.pair()
        pj["configs"][1]["count"] = -1
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_wrong_expected_count_fails(self):
        ob, pb, oj, pj = self.pair()
        pj["configs"][0]["count"] += 1
        pj["configs"][0]["offset"] += 0
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    # --- unsafe: metadata + tampered summary ---------------------------------
    def test_metadata_seed_mismatch_fails(self):
        ob, pb, oj, pj = self.pair(patch_json_mut=lambda j: j.update({"seed": 123}))
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_metadata_block_mismatch_fails(self):
        def mut(j):
            j["block"] = 8
        ob, pb, oj, pj = self.pair(patch_json_mut=mut)
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_raw_fnv_tamper_fails(self):
        ob, pb, oj, pj = self.pair(orig_json_mut=lambda j: j["configs"][0].update({"fnv1a": 0}))
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_declared_sum_tamper_fails(self):
        ob, pb, oj, pj = self.pair(patch_json_mut=lambda j: j["configs"][1].update({"sum": 999.0}))
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)

    def test_all_finite_false_in_json_fails(self):
        def mut(j):
            j["all_finite"] = False
        ob, pb, oj, pj = self.pair(patch_json_mut=mut)
        rc, _, _ = self.run_compare(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
