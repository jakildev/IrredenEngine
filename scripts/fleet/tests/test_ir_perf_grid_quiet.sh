#!/usr/bin/env bash
# Quiet-window outcome tests for engine/tools/bin/ir-perf-grid.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
SUBJECT="$REPO_ROOT/engine/tools/bin/ir-perf-grid"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$SUBJECT" ]]; then
    echo "SKIP: ir-perf-grid not found at $SUBJECT" >&2
    exit 3
fi

FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/ir-perf-grid-quiet.XXXXXX")
trap 'rm -rf "$FIXTURE"' EXIT

WORKTREE="$FIXTURE/worktree"
TOOLS="$WORKTREE/engine/tools"
STUBS="$FIXTURE/stubs"
mkdir -p "$TOOLS/bin" "$TOOLS/lib" "$WORKTREE/scripts/perf" \
    "$WORKTREE/build" "$STUBS"
cp "$SUBJECT" "$TOOLS/bin/ir-perf-grid"
chmod +x "$TOOLS/bin/ir-perf-grid"
: > "$WORKTREE/CMakePresets.json"

cat > "$TOOLS/lib/concurrency_helpers.sh" <<'EOF'
IR_CACHE_ROOT="$TEST_CACHE_ROOT"
ir_worktree_root() {
    printf '%s\n' "$TEST_WORKTREE_ROOT"
}
_ir_config() {
    case "$2" in
        calibration_max_age_days) printf '30\n' ;;
        ref_bench_target_ms) printf '50\n' ;;
        *) printf '\n' ;;
    esac
}
EOF

cat > "$TOOLS/bin/ir-host-probe" <<'EOF'
#!/usr/bin/env bash
if [[ "$#" -eq 1 && "$1" == "--slug" ]]; then
    printf 'test-host\n'
elif [[ "$#" -eq 0 ]]; then
    printf '{"os":"test"}\n'
else
    exit 2
fi
EOF

cat > "$WORKTREE/build/ir_ref_bench" <<'EOF'
#!/usr/bin/env bash
[[ "$#" -eq 0 ]] || exit 2
printf '{"ms": 10.0}\n'
EOF

cat > "$WORKTREE/scripts/perf/perf_grid_matrix.sh" <<'EOF'
#!/usr/bin/env bash
[[ "$#" -eq 0 ]] || exit 2
mkdir -p "$TEST_OUTPUT_DIR"
printf '{}\n' > "$TEST_OUTPUT_DIR/manifest.json"
printf 'perf_grid_matrix: done, output in %s\n' "$TEST_OUTPUT_DIR"
EOF

cat > "$TOOLS/bin/ir-acquire" <<'EOF'
#!/usr/bin/env bash
[[ "${1:-}" == "benchmark" && "${2:-}" == "--" ]] || exit 2
shift 2
case "$IR_PERF_GRID_TEST_MODE" in
    refused)
        printf 'REFUSED\n' > "${IR_QUIET_REPORT_FILE:-$TEST_FALLBACK_REPORT}"
        exit 75
        ;;
    breached)
        "$@"
        printf 'BREACH\n' > "${IR_QUIET_REPORT_FILE:-$TEST_FALLBACK_REPORT}"
        exit 76
        ;;
    unguarded)
        "$@"
        rc=$?
        printf 'UNGUARDED\n' > "${IR_QUIET_REPORT_FILE:-$TEST_FALLBACK_REPORT}"
        exit "$rc"
        ;;
    *) exit 2 ;;
esac
EOF

cat > "$STUBS/git" <<'EOF'
#!/usr/bin/env bash
[[ "$#" -eq 6 && "$1" == "-C" && "$2" == "$TEST_WORKTREE_ROOT" && \
    "$3" == "ls-tree" && "$4" == "HEAD" && "$5" == "--" && \
    "$6" == "engine/math" ]] || exit 2
printf '040000 tree deadbeef\tengine/math\n'
EOF

chmod +x "$TOOLS/bin/ir-acquire" "$TOOLS/bin/ir-host-probe" \
    "$WORKTREE/build/ir_ref_bench" \
    "$WORKTREE/scripts/perf/perf_grid_matrix.sh" "$STUBS/git"

export TEST_WORKTREE_ROOT="$WORKTREE"
export TEST_CACHE_ROOT="$FIXTURE/cache"
export TEST_FALLBACK_REPORT="$FIXTURE/quiet.report"
export PATH="$STUBS:$PATH"

run_case() {
    local mode="$1" name="$2"
    export IR_PERF_GRID_TEST_MODE="$mode"
    export TEST_OUTPUT_DIR="$FIXTURE/$name"
    set +e
    OUTPUT=$(cd "$WORKTREE" && "$TOOLS/bin/ir-perf-grid" 2>&1)
    RC=$?
    set -e
}

run_case refused refused-output
assert_eq "$RC" "75" "quiet refusal propagates exit 75"
assert_contains "$OUTPUT" "host did not reach the guarded quiet state" \
    "quiet refusal has its own diagnostic"
if [[ ! -e "$TEST_OUTPUT_DIR" ]]; then
    ok "quiet refusal writes no matrix output"
else
    bad "quiet refusal writes no matrix output"
fi

run_case breached breached-output
assert_eq "$RC" "76" "quiet breach propagates exit 76"
if [[ ! -e "$TEST_OUTPUT_DIR" && -d "$TEST_OUTPUT_DIR.contaminated" ]]; then
    ok "quiet breach quarantines the matrix output"
else
    bad "quiet breach quarantines the matrix output"
fi
BREACHED_MANIFEST=""
if [[ -f "$TEST_OUTPUT_DIR.contaminated/manifest.json" ]]; then
    BREACHED_MANIFEST=$(<"$TEST_OUTPUT_DIR.contaminated/manifest.json")
fi
assert_absent "$BREACHED_MANIFEST" '"calibration"' \
    "quiet breach skips the calibration splice"

run_case unguarded unguarded-output
assert_eq "$RC" "0" "disabled quiet window preserves matrix success"
if [[ -f "$TEST_OUTPUT_DIR/UNGUARDED" ]]; then
    ok "disabled quiet window marks the matrix output unguarded"
else
    bad "disabled quiet window marks the matrix output unguarded"
fi
UNGUARDED_MANIFEST=$(<"$TEST_OUTPUT_DIR/manifest.json")
assert_contains "$UNGUARDED_MANIFEST" '"calibration"' \
    "unguarded success still receives calibration metadata"

summarize "ir-perf-grid quiet tests"
