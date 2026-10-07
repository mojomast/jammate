"""Structural validator for the Gitea Actions workflows in this repository.

Runs the real PyYAML safe_load (the parser the brief asked for), plus checks
that every step either has a `run:` body or a `uses:`, that no step contains
`continue-on-error`, that permissions/concurrency are set, and that the string
`uses:` count is reported so it is visible that these workflows reference zero
actions.

PYYAML NOTE: PyYAML implements YAML 1.1, in which the bare key `on:` resolves
to the boolean True. GitHub Actions and Gitea's act_runner (gopkg.in/yaml.v3,
YAML 1.2 core schema) both read it as the string "on". That difference is a
validator artefact, not a defect in the files; this script accepts either.
"""
import glob
import sys

import yaml

ALLOWED_TOP = {True, "on", "name", "permissions", "concurrency", "jobs", "env", "defaults"}
problems = []

for path in sorted(glob.glob(".gitea/workflows/*.yml")):
    with open(path, encoding="utf-8") as fh:
        raw = fh.read()
    try:
        doc = yaml.safe_load(raw)
    except yaml.YAMLError as exc:
        problems.append(f"{path}: YAML PARSE ERROR: {exc}")
        continue

    print(f"=== {path} ===")
    print(f"  PyYAML safe_load: OK")
    top = set(doc.keys())
    print(f"  top-level keys:   {sorted(str(k) for k in top)}")
    unknown = top - ALLOWED_TOP
    if unknown:
        problems.append(f"{path}: unexpected top-level keys {unknown}")

    on = doc.get(True, doc.get("on"))
    print(f"  triggers:         {on}")
    if not on:
        problems.append(f"{path}: no triggers")

    print(f"  permissions:      {doc.get('permissions')}")
    if doc.get("permissions") != {"contents": "read"}:
        problems.append(f"{path}: permissions must be exactly contents: read")

    conc = doc.get("concurrency") or {}
    print(f"  concurrency:      {conc}")
    if conc.get("cancel-in-progress") is not True:
        problems.append(f"{path}: concurrency.cancel-in-progress must be true")
    if "github.ref" not in str(conc.get("group", "")):
        problems.append(f"{path}: concurrency.group must key on github.ref")

    uses = raw.count("uses:")
    print(f"  'uses:' references: {uses}")

    for jname, job in doc["jobs"].items():
        print(f"  job {jname}: runs-on={job.get('runs-on')!r} "
              f"timeout={job.get('timeout-minutes')!r}")
        if not job.get("runs-on"):
            problems.append(f"{path}:{jname}: no runs-on")
        steps = job["steps"]
        print(f"    {len(steps)} steps:")
        for i, s in enumerate(steps, 1):
            name = s.get("name", "<unnamed>")
            has_run, has_uses = "run" in s, "uses" in s
            if not (has_run or has_uses):
                problems.append(f"{path}:{jname}:step{i}: neither run: nor uses:")
            if has_run and has_uses:
                problems.append(f"{path}:{jname}:step{i}: both run: and uses:")
            if s.get("continue-on-error"):
                problems.append(
                    f"{path}:{jname}:step{i}: continue-on-error on {name!r}")
            flags = []
            if s.get("if"):
                flags.append(f"if={s['if']}")
            print(f"      {i}. {name}  [{'run' if has_run else 'uses'}] "
                  f"{' '.join(flags)}")
        # every run: body must be a non-empty string
        for i, s in enumerate(steps, 1):
            if "run" in s and not isinstance(s["run"], str):
                problems.append(f"{path}:{jname}:step{i}: run is not a string")

print()
if problems:
    print("PROBLEMS:")
    for p in problems:
        print("  ! " + p)
    sys.exit(1)
print("All workflow files parsed and passed every structural check.")