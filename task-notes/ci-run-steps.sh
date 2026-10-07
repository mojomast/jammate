#!/usr/bin/env bash
# Executes the `run:` bodies of a workflow locally, the way a runner would:
#   bash --noprofile --norc -eo pipefail <script>
# which is GitHub's documented default shell and what Gitea's act_runner uses.
#
# Usage: bash task-notes/ci-run-steps.sh <workflow.yml> [step-number ...]
#        (no step numbers = all steps)
#
# Every step runs in the same shell session environment the runner would give
# it: CI_WORKDIR / CI_REPO_URL / CI_COMMIT_SHA / GITHUB_ENV / GITHUB_WORKSPACE.
# The runner's shell state is NOT carried between steps, so neither does this.
set -u

export PATH=/tmp/opencode/venv/bin:$PATH
WF="$1"; shift
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HARNESS_ENV="${HARNESS_ENV:-/tmp/opencode/harness.env}"
if [ -f "$HARNESS_ENV" ]; then
  set -a; . "$HARNESS_ENV"; set +a
fi

export CI_REPO_URL="${CI_REPO_URL:-file://$ROOT}"
export CI_COMMIT_SHA="${CI_COMMIT_SHA:-$(git -C "$ROOT" rev-parse HEAD)}"
export GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-/tmp/opencode/harness-ws}"
export GITHUB_ENV="${GITHUB_ENV:-/tmp/opencode/harness-gh-env}"
: > "$GITHUB_ENV"
rm -rf "$GITHUB_WORKSPACE"

extract() { # $1=step number
  python3 - "$WF" "$1" <<'PY'
import sys, yaml
wf, n = sys.argv[1], int(sys.argv[2])
doc = yaml.safe_load(open(wf))
job = list(doc["jobs"].values())[0]
sys.stdout.write(job["steps"][n - 1].get("run", ""))
PY
}

names() {
  python3 - "$WF" <<'PY'
import sys, yaml
doc = yaml.safe_load(open(sys.argv[1]))
for i, s in enumerate(list(doc["jobs"].values())[0]["steps"], 1):
    print(f"{i}\t{s.get('name','<unnamed>')}")
PY
}

if [ "$#" -eq 0 ]; then
  nums="$(names | cut -f1)"
else
  nums="$*"
fi

rc_total=0
for n in $nums; do
  name="$(names | awk -F'\t' -v n="$n" '$1==n{print $2}')"
  script="$(extract "$n")"
  file="/tmp/opencode/step-${n}.sh"
  printf '%s\n' "$script" > "$file"
  echo "############################################################"
  echo "# STEP $n: $name"
  echo "# (\$ bash --noprofile --norc -eo pipefail, as the runner runs it)"
  echo "############################################################"
  bash --noprofile --norc -eo pipefail "$file"
  rc=$?
  echo "---- STEP $n EXIT=$rc ----"
  if [ "$rc" -ne 0 ]; then rc_total=$rc; fi
done
echo
echo "### harness: worst step exit = ${rc_total}"
exit "$rc_total"