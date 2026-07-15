#!/usr/bin/env bash
# ir_run_exit_code_test.sh — assert ir-run validates exit codes for
# self-terminating verbs.
#
# T-336 fixed a Metal static-destruction segfault that landed AFTER
# screenshots were written, so the per-shot comparator passed while the
# process crashed. ir-run's `--auto-screenshot` path now refuses to
# exec'-replace (which would hide the signal behind a bash builtin); it
# runs as a child and reports the clean-exit verdict (RESULT=CRASH … +
# the clean-exit-policy failure text) on non-zero, propagating the code.
# This test pins that behavior on the --auto-screenshot path — the
# GPU-lock path, distinct from the --timeout coverage in
# scripts/fleet/tests/test_ir_run_result_reporting.sh — so a future
# revert doesn't silently re-open the gap.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"

# Stage a fake build dir holding two scripts pretending to be demo exes.
# One exits 0, the other exits 139 (the bash convention for SIGSEGV).
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

# ir-run's --auto-screenshot detection wraps in ir-acquire gpu; that
# really takes the GPU lock. Point IR_LOCK_ROOT (read by
# concurrency_helpers.sh) at a tmp dir so we don't trample the user's
# host locks and parallel CI jobs don't serialize on this test.
export IR_LOCK_ROOT
IR_LOCK_ROOT="$(mktemp -d)"

mkdir -p "$BUILD_DIR/creations/demos/clean/scripts"
cat > "$BUILD_DIR/creations/demos/clean/IRFakeClean" <<'EOF'
#!/usr/bin/env bash
echo "fake-clean: ran with args: $*"
exit 0
EOF
chmod +x "$BUILD_DIR/creations/demos/clean/IRFakeClean"

mkdir -p "$BUILD_DIR/creations/demos/crash/scripts"
cat > "$BUILD_DIR/creations/demos/crash/IRFakeCrash" <<'EOF'
#!/usr/bin/env bash
echo "fake-crash: ran with args: $*; pretending to segfault on shutdown"
exit 139
EOF
chmod +x "$BUILD_DIR/creations/demos/crash/IRFakeCrash"

pass=0
fail=0
check() {
    local label="$1" cond="$2"
    if eval "$cond"; then
        echo "  PASS: $label"
        pass=$(( pass + 1 ))
    else
        echo "  FAIL: $label"
        fail=$(( fail + 1 ))
    fi
}

echo "[1] auto-screenshot + clean exit: ir-run returns 0"
"$IR_RUN" --build-dir "$BUILD_DIR" IRFakeClean --auto-screenshot 1 > /tmp/ir-run-clean.log 2>&1
rc=$?
check "exit code 0" "[[ $rc -eq 0 ]]"

echo "[2] auto-screenshot + crash on shutdown: ir-run propagates non-zero"
set +e
"$IR_RUN" --build-dir "$BUILD_DIR" IRFakeCrash --auto-screenshot 1 > /tmp/ir-run-crash.log 2>&1
rc=$?
set -e
check "exit code matches binary (139)" "[[ $rc -eq 139 ]]"
check "RESULT=CRASH verdict names the propagated code + decoded signal" \
    "grep -q 'RESULT=CRASH exe=IRFakeCrash exit=139 signal=SIGSEGV' /tmp/ir-run-crash.log"
check "diagnostic cites the clean-exit policy" \
    "grep -q 'clean-exit policy' /tmp/ir-run-crash.log"

echo
echo "ir_run_exit_code_test.sh: $pass passed, $fail failed"
exit "$fail"
