#!/usr/bin/env bash
# Tests for ir-run's clean-exit RESULT reporting (engine/tools/bin/ir-run).
#
# The fleet's clean-exit policy (docs/agents/FLEET.md §"Clean-exit policy")
# keys off ir-run's one-line verdicts, so their shape and exit-code
# propagation are contract, not cosmetics:
#
#   RESULT=CLEAN exe=<name> exit=0                — ran + exited 0
#   RESULT=CRASH exe=<name> exit=<rc> signal=<..> — FAILED, code propagated
#   RESULT=ALIVE-TIMEOUT exe=<name> ...           — watchdog kill, healthy
#
# Hermetic: a fake build dir with tiny scripts stands in for real demos;
# the --timeout path is exercised so no ir-acquire lock is taken.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$IR_RUN" ]]; then
    echo "test setup: ir-run not found at $IR_RUN" >&2
    exit 1
fi

FAKE_BUILD="$(mktemp -d)"
trap 'rm -rf "$FAKE_BUILD"' EXIT

make_exe() {
    local name="$1" body="$2"
    printf '#!/usr/bin/env bash\n%s\n' "$body" > "$FAKE_BUILD/$name"
    chmod +x "$FAKE_BUILD/$name"
}

make_exe clean-exit 'exit 0'
make_exe plain-fail 'exit 3'
# Real signal death (not `exit 139`): the reporter decodes 128+N.
make_exe segfaulter 'kill -SEGV $$'
make_exe sleeper 'sleep 30'

run_ir() {
    # Runs ir-run with the fake build dir; captures combined output and rc.
    OUT="$("$IR_RUN" --build-dir "$FAKE_BUILD" "$@" 2>&1)"
    RC=$?
}

echo "clean exit before timeout:"
run_ir --timeout 10 clean-exit
assert_eq "$RC" "0" "clean exit propagates rc 0"
assert_contains "$OUT" "RESULT=CLEAN exe=clean-exit exit=0" "CLEAN verdict line"

echo "plain non-zero exit:"
run_ir --timeout 10 plain-fail
assert_eq "$RC" "1" "crash exits 1 in timeout mode"
assert_contains "$OUT" "RESULT=CRASH exe=plain-fail exit=3 signal=none" \
    "CRASH verdict names the code, signal=none"
assert_contains "$OUT" "clean-exit policy" "failure text cites the policy"

echo "signal death:"
run_ir --timeout 10 segfaulter
assert_eq "$RC" "1" "signal death exits 1 in timeout mode"
assert_contains "$OUT" "RESULT=CRASH exe=segfaulter exit=139 signal=SIGSEGV" \
    "CRASH verdict decodes SIGSEGV from 139"

echo "watchdog kill of a healthy process:"
run_ir --timeout 1 sleeper
assert_eq "$RC" "0" "watchdog kill stays exit 0 (healthy for smoke)"
assert_contains "$OUT" "RESULT=ALIVE-TIMEOUT exe=sleeper" "ALIVE-TIMEOUT verdict"

summarize "ir-run result-reporting tests"
