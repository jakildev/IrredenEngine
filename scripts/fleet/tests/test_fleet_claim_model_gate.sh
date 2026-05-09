#!/usr/bin/env bash
# Tests for fleet-claim's check_model_tag gate.
#
# Covers:
#   - sonnet role rejects an opus-tagged task (exit 1)
#   - opus role accepts an opus-tagged task (exit 0)
#   - sonnet role accepts a sonnet-tagged task (exit 0)
#   - opus role rejects a sonnet-tagged task (exit 1)
#   - FLEET_ROLE_MODEL unset/empty passes any task (bash guard, exit 0)
#   - task with no Model: field passes any role (python fall-through, exit 0)
#   - task ID not found in TASKS.md passes (python fall-through, exit 0)
#   - TASKS.md not found passes (bash guard, exit 0)

set -euo pipefail

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

# Inline the Python gate logic from check_model_tag (fleet-claim:316-356).
# Accepts explicit task_id, tasks_file, role_model args so it can be driven
# from a synthetic TASKS.md fixture without a live git tree or env vars.
run_python_gate() {
    local task_id="$1" tasks_file="$2" role_model="$3"
    python3 - "$task_id" "$tasks_file" "$role_model" <<'PYEOF'
import sys, re

task_id = sys.argv[1]
tasks_file = sys.argv[2]
role_model = sys.argv[3].lower()

with open(tasks_file) as f:
    content = f.read()

current_id = None
task_model = None

for line in content.splitlines():
    if re.match(r'^- \[.\] \*\*.+\*\*', line):
        current_id = None
        continue

    m_id = re.match(r'^\s+- \*\*ID:\*\*\s*(.+)', line)
    if m_id:
        current_id = m_id.group(1).strip()
        continue

    m_model = re.match(r'^\s+- \*\*Model:\*\*\s*(.+)', line)
    if m_model and current_id == task_id:
        task_model = m_model.group(1).strip().lower()
        break

if task_model is None:
    sys.exit(0)

if task_model != role_model:
    print(
        f"fleet-claim: refuse {task_id} — tagged [{task_model}], "
        f"this role is [{role_model}]; use a [{task_model}] agent",
        file=sys.stderr,
    )
    sys.exit(1)

sys.exit(0)
PYEOF
}

# Mirrors the bash guards in check_model_tag before the Python gate runs.
# Reproduces the three bash-level short-circuits verbatim:
#   1. FLEET_ROLE_MODEL unset/empty → 0 (opt-in gate)
#   2. tasks_file path not found    → 0 (safe fall-through)
#   3. otherwise → delegate to run_python_gate
run_gate() {
    local task_id="$1" role_model="${2:-}" tasks_file="${3:-}"

    if [[ -z "$role_model" ]]; then
        return 0
    fi

    if [[ -z "$tasks_file" || ! -f "$tasks_file" ]]; then
        return 0
    fi

    run_python_gate "$task_id" "$tasks_file" "$role_model"
}

TMPROOT=$(mktemp -d)

# --- Synthetic TASKS.md fixture -----------------------------------------------
TASKS="$TMPROOT/TASKS.md"
cat >"$TASKS" <<'TASKSEOF'
# TASKS

## Open

- [ ] **Task A** — opus task
  - **ID:** T-A01
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)

- [ ] **Task B** — sonnet task
  - **ID:** T-B01
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)

- [ ] **Task C** — no model field
  - **ID:** T-C01
  - **Owner:** free
  - **Blocked by:** (none)
TASKSEOF

# --- T1: sonnet rejects opus task --------------------------------------------
echo "T1: sonnet role rejects opus-tagged task"
actual=0; run_python_gate T-A01 "$TASKS" sonnet 2>/dev/null || actual=$?
assert_exit "$actual" 1 "sonnet rejects [opus] task → exit 1"

# --- T2: opus accepts opus task ----------------------------------------------
echo "T2: opus role accepts opus-tagged task"
actual=0; run_python_gate T-A01 "$TASKS" opus 2>/dev/null || actual=$?
assert_exit "$actual" 0 "opus accepts [opus] task → exit 0"

# --- T3: sonnet accepts sonnet task ------------------------------------------
echo "T3: sonnet role accepts sonnet-tagged task"
actual=0; run_python_gate T-B01 "$TASKS" sonnet 2>/dev/null || actual=$?
assert_exit "$actual" 0 "sonnet accepts [sonnet] task → exit 0"

# --- T4: opus rejects sonnet task --------------------------------------------
echo "T4: opus role rejects sonnet-tagged task"
actual=0; run_python_gate T-B01 "$TASKS" opus 2>/dev/null || actual=$?
assert_exit "$actual" 1 "opus rejects [sonnet] task → exit 1"

# --- T5: FLEET_ROLE_MODEL unset passes any task (bash guard) -----------------
echo "T5: FLEET_ROLE_MODEL unset passes any task"
actual=0; run_gate T-A01 "" "$TASKS" 2>/dev/null || actual=$?
assert_exit "$actual" 0 "empty FLEET_ROLE_MODEL bypasses gate → exit 0"

# --- T6: task with no Model: field passes any role ---------------------------
echo "T6: task with no Model: field passes any role"
actual=0; run_python_gate T-C01 "$TASKS" sonnet 2>/dev/null || actual=$?
assert_exit "$actual" 0 "no Model: field → fall-through exit 0"

# --- T7: task ID not found in TASKS.md passes --------------------------------
echo "T7: task ID not found in TASKS.md passes"
actual=0; run_python_gate T-ZZZ "$TASKS" sonnet 2>/dev/null || actual=$?
assert_exit "$actual" 0 "unknown task ID → fall-through exit 0"

# --- T8: TASKS.md not found passes (bash guard) ------------------------------
echo "T8: TASKS.md not found passes any role"
actual=0; run_gate T-A01 "opus" "$TMPROOT/no-such-file.md" 2>/dev/null || actual=$?
assert_exit "$actual" 0 "missing TASKS.md → bash guard exit 0"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
