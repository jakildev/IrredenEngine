#!/usr/bin/env bash
# Combined capture/profile launches reserve the stronger benchmark resources.
# The launcher is real; resource acquisition and the demo are strict stubs.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/lib_preflight.sh"
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
SUBJECT="$REPO_ROOT/engine/tools/bin/ir-run"
FLEET_SHIM="$REPO_ROOT/scripts/fleet/fleet-run"
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$SUBJECT" || ! -x "$FLEET_SHIM" ]]; then
    echo "SKIP: ir-run or fleet-run is missing" >&2
    exit 3
fi

FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/ir-run-capture-priority.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT
TOOLS="$FIXTURE/worktree/engine/tools"
SHIM_DIR="$FIXTURE/worktree/scripts/fleet"
BUILD="$FIXTURE/build"
mkdir -p "$TOOLS/bin" "$TOOLS/lib" "$SHIM_DIR" "$BUILD"
cp "$SUBJECT" "$TOOLS/bin/ir-run"
cp "$FLEET_SHIM" "$SHIM_DIR/fleet-run"

cat > "$TOOLS/lib/concurrency_helpers.sh" <<'EOF'
IR_IS_WINDOWS=0
ir_inherited_lock_covers() {
    [[ "$#" == 1 ]] || return 2
    return 1
}
EOF
cat > "$TOOLS/bin/ir-acquire" <<'EOF'
#!/usr/bin/env bash
[[ "$#" -ge 3 && "$2" == -- ]] || exit 2
verb="$1"
case "$verb" in gpu|benchmark) ;; *) exit 2 ;; esac
shift 2
printf '%s\n' "$verb" > "$TEST_ACQUIRE_LOG"
if [[ "$verb" == benchmark ]]; then
    [[ -n "${IR_QUIET_REPORT_FILE:-}" ]] || exit 2
    case "${TEST_QUIET_MODE:-guarded}" in
        guarded) printf 'GUARDED\n' > "$IR_QUIET_REPORT_FILE" ;;
        refused) printf 'REFUSED\n' > "$IR_QUIET_REPORT_FILE"; exit 75 ;;
        breach) printf 'BREACH\n' > "$IR_QUIET_REPORT_FILE"; "$@"; exit 76 ;;
        *) exit 2 ;;
    esac
fi
"$@"
exit "$?"
EOF
cat > "$BUILD/capture-probe" <<'EOF'
#!/usr/bin/env bash
printf 'window-mode=%s\n' "${IR_WINDOW_MODE:-unset}"
printf 'args='
printf '<%s>' "$@"
printf '\n'
exit "${TEST_CHILD_EXIT:-0}"
EOF
chmod +x "$TOOLS/bin/ir-run" "$TOOLS/bin/ir-acquire" \
    "$SHIM_DIR/fleet-run" "$BUILD/capture-probe"
export TEST_ACQUIRE_LOG="$FIXTURE/acquire.log"
unset IR_WINDOW_MODE FLEET_WINDOW_MODE FLEET_RUN_DEFAULT_TIMEOUT
unset IR_RUN_HOST_CLOSE_IDENTITY IR_RUN_HOST_CLOSE_CLOCK IR_RUN_HOST_CLOSE_QUERY
unset IR_QUIET_REPORT_FILE TEST_QUIET_MODE TEST_CHILD_EXIT

run_case() {
    : > "$TEST_ACQUIRE_LOG"
    OUT=$("$SHIM_DIR/fleet-run" --build-dir "$BUILD" capture-probe "$@" 2>&1)
    RC=$?
    VERB=$(<"$TEST_ACQUIRE_LOG")
}

assert_benchmark() {
    local expected_args
    expected_args=$(printf '<%s>' "$@")
    run_case "$@"
    assert_eq "$RC" "0" "mixed capture exits cleanly: $*"
    assert_eq "$VERB" "benchmark" "profiling has acquisition priority: $*"
    assert_contains "$OUT" "QUIET=guarded exe=capture-probe" "mixed run retains quiet provenance: $*"
    assert_contains "$OUT" "window-mode=offscreen" "mixed run retains unattended capture mode: $*"
    assert_contains "$OUT" "args=$expected_args" "mixed run preserves child argument order: $*"
}

for capture in --auto-screenshot --auto-record; do
    assert_benchmark --auto-profile 5 "$capture" 3
    assert_benchmark "$capture" 3 --auto-profile 5
    assert_benchmark --auto-profile=5 "$capture" 3
    assert_benchmark "$capture" 3 --auto-profile=5
    assert_benchmark --auto-profile 5 "$capture=3"
    assert_benchmark "$capture=3" --auto-profile 5
    assert_benchmark --auto-profile=5 "$capture=3"
    assert_benchmark "$capture=3" --auto-profile=5
done
assert_benchmark --auto-profile 5 --auto-screenshot 3 --auto-record 2
assert_benchmark --auto-screenshot 3 --auto-profile 5 --auto-record 2
assert_benchmark --auto-screenshot 3 --auto-record 2 --auto-profile 5
run_case --auto-profile 5 --auto-record 2 --auto-screenshot 3 --window-mode normal
assert_eq "$VERB" "benchmark" "late window-mode flag retains benchmark acquisition"
assert_contains "$OUT" "window-mode=unset" "explicit window-mode suppresses the default env"
assert_contains "$OUT" "<--window-mode><normal>" "explicit window mode reaches the child"

for capture in --auto-screenshot --auto-record; do
    run_case "$capture" 3
    assert_eq "$RC" "0" "capture alone succeeds: $capture"
    assert_eq "$VERB" "gpu" "capture alone keeps GPU acquisition: $capture"
    assert_absent "$OUT" "QUIET=" "capture alone does not claim benchmark provenance: $capture"
    run_case "$capture=3"
    assert_eq "$VERB" "gpu" "inline capture alone keeps GPU acquisition: $capture"
done
run_case --auto-profile 5
assert_eq "$VERB" "benchmark" "profile alone retains benchmark acquisition"

TEST_CHILD_EXIT=3 run_case --auto-profile 5 --auto-screenshot 3
assert_eq "$RC" "3" "mixed automatic run propagates child exit 3"
assert_contains "$OUT" "RESULT=CRASH exe=capture-probe exit=3" "mixed child failure keeps CRASH verdict"

TEST_QUIET_MODE=refused run_case --auto-profile 5 --auto-screenshot 3
assert_eq "$RC" "75" "mixed run propagates quiet refusal"
assert_contains "$OUT" "RESULT=HOST-NOT-QUIET" "mixed run retains quiet-refusal verdict"
assert_absent "$OUT" "args=" "quiet refusal never launches the child"
TEST_QUIET_MODE=breach run_case --auto-profile=5 --auto-record=3
assert_eq "$RC" "76" "mixed run propagates contamination"
assert_contains "$OUT" "RESULT=CONTAMINATED" "mixed run retains contamination verdict"
assert_absent "$OUT" "RESULT=CLEAN" "contaminated mixed run is never clean"

"$TOOLS/bin/ir-acquire" wrong -- true
assert_eq "$?" "2" "acquisition stub rejects unsupported verbs"
"$TOOLS/bin/ir-acquire" benchmark --wrong true
assert_eq "$?" "2" "acquisition stub rejects unsupported flags"

summarize "ir-run capture priority tests"
