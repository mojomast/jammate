#!/usr/bin/env python3
"""RT-003 differential comparator: pinned-upstream vs patched NAM LSTM.

Reads the raw float outputs and JSON summaries produced by the two
NamLstmDiffTest processes (one linked against the pinned upstream NAM, one
against the generated patched overlay) and checks every sample within a
predeclared tolerance. Exits non-zero on any config mismatch, structural
mismatch, or out-of-tolerance sample.

Tolerance: pass if |a-b| <= ABS_TOL + REL_TOL * max(|a|,|b|), with both
constants 1e-5. This is a floating-point reordering budget, not a
bit-exactness claim.
"""
import argparse
import json
import struct
import sys

ABS_TOL = 1.0e-5
REL_TOL = 1.0e-5


def read_floats(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) % 4 != 0:
        raise ValueError(f"{path}: size {len(data)} not a multiple of 4")
    return list(struct.unpack("<%df" % (len(data) // 4), data))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--original-bin", required=True)
    ap.add_argument("--patched-bin", required=True)
    ap.add_argument("--original-json", required=True)
    ap.add_argument("--patched-json", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    orig = read_floats(args.original_bin)
    patched = read_floats(args.patched_bin)
    ojson = json.load(open(args.original_json))
    pjson = json.load(open(args.patched_json))

    result = {"abs_tol": ABS_TOL, "rel_tol": REL_TOL, "configs": [],
              "max_abs_diff": 0.0, "max_rel_diff": 0.0, "all_pass": True}
    result["block"] = ojson.get("block")

    if [c["name"] for c in ojson["configs"]] != [c["name"] for c in pjson["configs"]]:
        print("FAIL: config name list differs", file=sys.stderr)
        return 2
    if len(orig) != len(patched):
        print(f"FAIL: float count differs {len(orig)} vs {len(patched)}", file=sys.stderr)
        return 2

    for oc, pc in zip(ojson["configs"], pjson["configs"]):
        name = oc["name"]
        o_off, o_n = oc["offset"], oc["count"]
        p_off, p_n = pc["offset"], pc["count"]
        if (o_off, o_n) != (p_off, p_n):
            print(f"FAIL: {name}: offset/count mismatch", file=sys.stderr)
            result["all_pass"] = False
            continue
        cfg_max_abs = 0.0
        cfg_max_rel = 0.0
        worst = -1
        for k in range(o_n):
            a = orig[o_off + k]
            b = patched[p_off + k]
            d = abs(a - b)
            denom = max(abs(a), abs(b))
            r = d / denom if denom > 0 else 0.0
            budget = ABS_TOL + REL_TOL * denom
            if d > budget:
                result["all_pass"] = False
                if worst < 0:
                    worst = k
            if d > cfg_max_abs:
                cfg_max_abs = d
            if r > cfg_max_rel:
                cfg_max_rel = r
        result["max_abs_diff"] = max(result["max_abs_diff"], cfg_max_abs)
        result["max_rel_diff"] = max(result["max_rel_diff"], cfg_max_rel)
        cfg_pass = worst < 0
        result["configs"].append({
            "name": name, "count": o_n, "max_abs_diff": cfg_max_abs,
            "max_rel_diff": cfg_max_rel, "pass": cfg_pass,
            "first_failing_index": worst,
            "original_fnv1a": oc["fnv1a"], "patched_fnv1a": pc["fnv1a"],
            "original_sum": oc["sum"], "patched_sum": pc["sum"],
        })
        print(f"  {'PASS' if cfg_pass else 'FAIL'}  {name:16s} n={o_n:7d} "
              f"max_abs={cfg_max_abs:.3e} max_rel={cfg_max_rel:.3e} "
              f"fnv {'==' if oc['fnv1a'] == pc['fnv1a'] else '!='}")

    json.dump(result, open(args.out_json, "w"), indent=2)
    print(f"overall max_abs_diff={result['max_abs_diff']:.3e} "
          f"max_rel_diff={result['max_rel_diff']:.3e} "
          f"{'PASS' if result['all_pass'] else 'FAIL'}")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
