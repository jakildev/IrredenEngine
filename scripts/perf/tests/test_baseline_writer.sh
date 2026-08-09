#!/usr/bin/env bash
# Executed control for perf-gate.yml's baseline-writer step.
#
# Runs the SHIPPED step body — extracted from the workflow, not retyped —
# against a local bare repo standing in for origin. The writer is the half of
# the gate that had never once succeeded, and its failure mode is silence: a
# baseline that is not persisted looks exactly like a baseline that matched
# (#2817). These arms pin the behaviours that distinguish the two.
#
# Requires git and python3 only; no network, no build.
# Usage: scripts/perf/tests/test_baseline_writer.sh

set -u

WORKFLOW_STEP="Update baseline on the perf-baseline branch"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
WORKFLOW="$REPO_ROOT/.github/workflows/perf-gate.yml"

LAB="$(mktemp -d "${TMPDIR:-/tmp}/perfgate-writer.XXXXXX")"
trap 'rm -rf "$LAB"' EXIT

FAILURES=0
check () { # $1=arm  $2=condition-result(0/1)  $3=detail
  if [[ "$2" -eq 0 ]]; then
    echo "  PASS  $1: $3"
  else
    echo "  FAIL  $1: $3"
    FAILURES=$((FAILURES + 1))
  fi
}

# --- Extract the step body from the workflow (stdlib python, no pyyaml) ----

BODY="$LAB/writer-body.sh"
python3 - "$WORKFLOW" "$WORKFLOW_STEP" "$BODY" <<'PY'
import sys

workflow, step_name, out = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(workflow).read().splitlines()

# Find "- name: <step>", then its "run: |" block: every following line
# indented deeper than the `run:` key, dedented by that depth.
i = next((n for n, l in enumerate(lines) if l.strip() == f"- name: {step_name}"), None)
if i is None:
    sys.exit(f"step not found in {workflow}: {step_name}")

run_at = next((n for n in range(i + 1, len(lines))
               if lines[n].strip().startswith("run:")), None)
if run_at is None:
    sys.exit(f"no run: block under step: {step_name}")

key_indent = len(lines[run_at]) - len(lines[run_at].lstrip())
body, body_indent = [], None
for line in lines[run_at + 1:]:
    if not line.strip():
        body.append("")
        continue
    indent = len(line) - len(line.lstrip())
    if indent <= key_indent:
        break
    if body_indent is None:
        body_indent = indent
    body.append(line[body_indent:])

if not body:
    sys.exit(f"empty run: block under step: {step_name}")
open(out, "w").write("\n".join(body) + "\n")
print(f"extracted {len(body)} lines of '{step_name}'")
PY
[[ -s "$BODY" ]] || { echo "FATAL: could not extract the writer step body" >&2; exit 1; }

# The extracted body is the real thing only if it still carries the workflow
# expression we substitute below; a silently-truncated extraction would make
# every arm below vacuous.
grep -q 'steps.head.outputs.dir' "$BODY" \
  || { echo "FATAL: extracted body does not reference steps.head.outputs.dir" >&2; exit 1; }

# --- Fixtures -------------------------------------------------------------

git init -q --bare "$LAB/origin.git"
git init -q "$LAB/work"
git -C "$LAB/work" config user.email perf-gate-test@example.com
git -C "$LAB/work" config user.name "perf gate test"
mkdir -p "$LAB/work/docs/perf/baseline_latest"
touch "$LAB/work/docs/perf/baseline_latest/.gitkeep"
git -C "$LAB/work" add -A
git -C "$LAB/work" commit -q -m "root"
git -C "$LAB/work" branch -M master
git -C "$LAB/work" remote add origin "$LAB/origin.git"
git -C "$LAB/work" push -q -u origin master
MASTER_BEFORE=$(git -C "$LAB/origin.git" rev-parse master)

mk_head () { # $1=dir  $2=slug  $3=frame avg ms
  mkdir -p "$1"
  printf 'Frame time:  avg=%sms p50=%sms p95=%sms p99=%sms min=%sms max=%sms\n' \
    "$3" "$3" "$3" "$3" "$3" "$3" > "$1/z4-s8.txt"
  printf 'noise\n' > "$1/run.log"     # must NOT be filed as baseline
  touch "$1/.cell_marker"             # must NOT be filed as baseline
  python3 -c "
import json, sys
json.dump({'cells': [{'id': 'z4-s8', 'report': 'z4-s8.txt'}],
           'calibration': {'host_slug': sys.argv[2], 'ref_ms': 1.0,
                           'ref_target_ms': 1.0,
                           'host_fingerprint': {'slug': sys.argv[2]}}},
          open(sys.argv[1] + '/manifest.json', 'w'))" "$1" "$2"
}

SKU_A="linux-x86_64-epyc-7763-64-unknown"
SKU_B="linux-x86_64-xeon-platinum-8573c-unknown"
mk_head "$LAB/head-a" "$SKU_A" 9.5
mk_head "$LAB/head-b" "$SKU_B" 9.5
mk_head "$LAB/head-noslug" "" 9.5

export RUNNER_TEMP="$LAB/runner-temp"; mkdir -p "$RUNNER_TEMP"
export BASELINE_BRANCH=perf-baseline

run_writer () { # $1=head dir -> exit code of the shipped step body
  sed "s|\${{ steps.head.outputs.dir }}|$1|g" "$BODY" > "$LAB/step.sh"
  ( cd "$LAB/work" && bash -e "$LAB/step.sh" ) > "$LAB/out.txt" 2>&1
}

on_branch () { git -C "$LAB/origin.git" ls-tree -r --name-only "$BASELINE_BRANCH" 2>/dev/null; }
commits_on_branch () { git -C "$LAB/origin.git" rev-list --count "$BASELINE_BRANCH" 2>/dev/null || echo 0; }

echo "perf-gate baseline writer control"

# --- A: unborn branch -----------------------------------------------------
run_writer "$LAB/head-a"; rc=$?
check A $rc "creates the branch from nothing and exits 0"
on_branch | grep -q "docs/perf/baseline_latest/$SKU_A/manifest.json"
check A $? "manifest filed under the head SKU"
on_branch | grep -q "docs/perf/baseline_latest/$SKU_A/host.json"
check A $? "host.json sidecar filed"
if on_branch | grep -qE '\.log$|\.cell_marker$'; then check A 1 "temp files excluded"; \
  else check A 0 "temp files (.log, .cell_marker) excluded"; fi

# --- B: a second SKU appends rather than replacing -------------------------
run_writer "$LAB/head-b"; rc=$?
check B $rc "second SKU exits 0"
on_branch | grep -q "docs/perf/baseline_latest/$SKU_B/manifest.json"
check B $? "second SKU filed"
on_branch | grep -q "docs/perf/baseline_latest/$SKU_A/manifest.json"
check B $? "first SKU SURVIVES (append, not replace)"

# --- C: re-running with identical input commits nothing --------------------
BEFORE=$(commits_on_branch)
run_writer "$LAB/head-a"; rc=$?
check C $rc "unchanged input exits 0"
grep -q "nothing to commit" "$LAB/out.txt"
check C $? "reports 'nothing to commit'"
[[ "$(commits_on_branch)" == "$BEFORE" ]]
check C $? "no empty commit added"

# --- D: retry-once on a refused push ---------------------------------------
cat > "$LAB/origin.git/hooks/pre-receive" <<'HOOK'
#!/usr/bin/env bash
STAMP="$GIT_DIR/reject-once.stamp"
if [[ -f "$STAMP" ]]; then exit 0; fi
touch "$STAMP"
echo "test-hook: refusing the first push (simulated concurrent update)" >&2
exit 1
HOOK
chmod +x "$LAB/origin.git/hooks/pre-receive"
mk_head "$LAB/head-a" "$SKU_A" 8.0          # new payload, so there IS a commit
run_writer "$LAB/head-a"; rc=$?
check D $rc "a refused first push is retried and succeeds"
grep -q "push refused on attempt 1" "$LAB/out.txt"
check D $? "the refusal is reported, not swallowed"
git -C "$LAB/origin.git" show "$BASELINE_BRANCH:docs/perf/baseline_latest/$SKU_A/z4-s8.txt" \
  2>/dev/null | grep -q 'avg=8.0ms'
check D $? "the retry's payload is the one that landed"
rm -f "$LAB/origin.git/hooks/pre-receive"

# --- E: a persistently refused push must go RED ----------------------------
cat > "$LAB/origin.git/hooks/pre-receive" <<'HOOK'
#!/usr/bin/env bash
echo "test-hook: refusing every push" >&2
exit 1
HOOK
chmod +x "$LAB/origin.git/hooks/pre-receive"
mk_head "$LAB/head-a" "$SKU_A" 7.2
run_writer "$LAB/head-a"; rc=$?
[[ $rc -ne 0 ]]
check E $? "two refusals fail the step instead of passing quietly (rc=$rc)"
rm -f "$LAB/origin.git/hooks/pre-receive"

# --- F: a manifest with no host slug must go RED ---------------------------
run_writer "$LAB/head-noslug"; rc=$?
[[ $rc -ne 0 ]]
check F $? "a run with no calibration.host_slug fails the step (rc=$rc)"

# --- G: master is never written --------------------------------------------
[[ "$(git -C "$LAB/origin.git" rev-parse master)" == "$MASTER_BEFORE" ]]
check G $? "master is untouched by every arm above"

echo
if [[ $FAILURES -gt 0 ]]; then
  echo "$FAILURES assertion(s) failed"
  exit 1
fi
echo "all arms passed"
