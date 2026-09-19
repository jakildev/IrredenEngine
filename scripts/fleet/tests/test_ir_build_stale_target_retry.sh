#!/usr/bin/env bash
# Tests for ir-build's post-failure retry (engine/tools/bin/ir-build): a
# --target naming a target added to the CMake tree since the build dir's
# last configure fails with make's "No rule to make target" before make
# ever reaches CMake's own regenerate check — every already-known target
# already carries that check as a make prerequisite, so this path is
# exclusively the unknown-target case. On a build failure, ir-build now
# runs the check directly and retries the build once if the check
# regenerated the tree.
#
# Hermetic: cmake is stubbed on PATH with a stateful call log (the T6
# pattern in test_ir_build_dir_resolution.sh) that models the two shapes
# ir-build issues (`--build <dir> -j<N> --target <T>` and `--build <dir>
# --target cmake_check_build_system`) and fails closed on any other
# invocation (scripts/fleet/CLAUDE.md "A CLI stub models the tool's
# argument parsing"). No real cmake, no compiler, no network.
#
# The "fresh + unknown target" arm (T2) is, from the stub's point of view,
# indistinguishable from "compile error on a known target": the build
# fails, the check finds nothing to regenerate, there is no retry. There is
# no separate arm for that case because the stub cannot tell the two apart
# — both are "the target genuinely does not build".

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=lib_preflight.sh
source "$(dirname "$0")/lib_preflight.sh"
IR_BUILD="$SCRIPT_DIR/engine/tools/bin/ir-build"

if [[ ! -x "$IR_BUILD" ]]; then
    echo "SKIP: $IR_BUILD not found or not executable" >&2
    exit 3
fi

# Isolate the helpers' lock-dir side effects from the host's live locks —
# ir-build's own concurrency_helpers.sh honors IR_LOCK_ROOT when set.
IR_LOCK_ROOT="$(mktemp -d)/locks"
export IR_LOCK_ROOT

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

# Bound every ir-build invocation so a regression reports a FAIL instead of
# hanging the suite (the T5a pattern in test_ir_build_dir_resolution.sh).
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

FAKE="$(mktemp -d)"
trap 'rm -rf "$FAKE" "${IR_LOCK_ROOT%/locks}"' EXIT

STUB_BIN="$FAKE/stub-bin"
mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/cmake" <<'STUB_EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$STUB_CALL_LOG"

has_j=0
is_check=0
for arg in "$@"; do
    case "$arg" in
        -j*)                        has_j=1 ;;
        cmake_check_build_system)   is_check=1 ;;
    esac
done

if (( is_check && ! has_j )); then
    case "$STUB_CHECK_MODE" in
        regenerate) echo "-- Build files have been written to: $STUB_BUILD_DIR"; exit 0 ;;
        noop)       exit 0 ;;
        fail)       echo "CMake Error: fixture check failure" >&2; exit 1 ;;
        *)          echo "stub cmake: unset/unknown STUB_CHECK_MODE" >&2; exit 90 ;;
    esac
elif (( has_j && ! is_check )); then
    n=$(( $(cat "$STUB_BUILD_CALLS") + 1 ))
    printf '%s\n' "$n" > "$STUB_BUILD_CALLS"
    case "$STUB_BUILD_MODE" in
        succeed)     exit 0 ;;
        fail-once)
            if (( n == 1 )); then
                echo "make: *** No rule to make target 'FixtureTarget'.  Stop." >&2
                exit 2
            fi
            exit 0
            ;;
        fail-always) echo "make: *** No rule to make target 'FixtureTarget'.  Stop." >&2; exit 2 ;;
        *)           echo "stub cmake: unset/unknown STUB_BUILD_MODE" >&2; exit 90 ;;
    esac
else
    echo "stub cmake: unmodeled invocation shape: $*" >&2
    exit 99
fi
STUB_EOF
chmod +x "$STUB_BIN/cmake"

# --- T0: the stub itself fails closed on an unmodeled invocation ------------
echo "T0: stub cmake fails closed on an unmodeled invocation"
T0_LOG="$FAKE/t0-calls.log"; : > "$T0_LOG"
rc=0
STUB_CALL_LOG="$T0_LOG" "$STUB_BIN/cmake" --version >/dev/null 2>"$FAKE/t0.err" || rc=$?
assert_eq "$rc" "99" "unmodeled invocation exits 99"
assert_contains "$(cat "$FAKE/t0.err")" "unmodeled invocation shape" \
    "unmodeled invocation names itself in the failure"

# --- fixture helpers ---------------------------------------------------------

# new_engine_fixture <dir> — a plain engine-shaped root (CMakePresets.json +
# engine/), git-inited so ir_worktree_root resolves HERE via git, not the
# real engine checkout ir-build is running from.
new_engine_fixture() {
    local dir="$1"
    mkdir -p "$dir/engine"
    touch "$dir/CMakePresets.json"
    git -C "$dir" init -q
}

# seed_build_dir <dir> — pre-configured build dir, so ir-build's own
# auto-configure block (which would try a real --preset) never fires.
seed_build_dir() {
    mkdir -p "$1"
    touch "$1/CMakeCache.txt"
}

# call_sequence <call-log> — one call per line, mapped to "build" or "check"
# and joined with commas, e.g. "build,check,build".
call_sequence() {
    local line kind
    local seq=()
    while IFS= read -r line; do
        case "$line" in
            *cmake_check_build_system*) kind=check ;;
            *)                          kind=build ;;
        esac
        seq+=("$kind")
    done < "$1"
    local IFS=,
    echo "${seq[*]}"
}

# run_ir_build <cwd> <target> <logfile> — runs the real ir-build with the
# stub cmake first on PATH, combined stdout+stderr captured to <logfile>.
# STUB_* env vars are read from the current exported environment.
run_ir_build() {
    local cwd="$1" target="$2" logfile="$3"
    (
        cd "$cwd" || exit 127
        export PATH="$STUB_BIN:$PATH"
        if [[ -n "$TIMEOUT_CMD" ]]; then
            "$TIMEOUT_CMD" 30 "$IR_BUILD" --target "$target"
        else
            "$IR_BUILD" --target "$target"
        fi
    ) >"$logfile" 2>&1
}

# --- T1: stale build dir + unknown target — build, check, build -> rc 0 -----
echo "T1: stale build dir + unknown target"
ENG1="$FAKE/eng1"; new_engine_fixture "$ENG1"; seed_build_dir "$ENG1/build"
STUB_BUILD_DIR="$ENG1/build"
STUB_BUILD_MODE=fail-once
STUB_CHECK_MODE=regenerate
STUB_CALL_LOG="$FAKE/t1-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t1-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
rc=0
run_ir_build "$ENG1" FixtureTarget "$FAKE/t1.log" || rc=$?
assert_eq "$rc" "0" "T1: retried build reports rc 0"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build,check,build" \
    "T1: call sequence is build, check, build"
assert_contains "$(cat "$FAKE/t1.log")" "retrying" "T1: ir-build announces the retry"

# --- T2: fresh build dir + unknown target — build, check -> no retry --------
echo "T2: fresh build dir + unknown target (also covers a compile error on a known target)"
ENG2="$FAKE/eng2"; new_engine_fixture "$ENG2"; seed_build_dir "$ENG2/build"
STUB_BUILD_DIR="$ENG2/build"
STUB_BUILD_MODE=fail-always
STUB_CHECK_MODE=noop
STUB_CALL_LOG="$FAKE/t2-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t2-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
rc=0
run_ir_build "$ENG2" FixtureTarget "$FAKE/t2.log" || rc=$?
assert_eq "$rc" "2" "T2: no-retry failure surfaces the original build's status"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build,check" \
    "T2: call sequence is build, check — no second build"

# --- T3: the check itself fails (a real CMakeLists.txt error) ---------------
echo "T3: check itself fails — exits with the check's own status, no retry"
ENG3="$FAKE/eng3"; new_engine_fixture "$ENG3"; seed_build_dir "$ENG3/build"
STUB_BUILD_DIR="$ENG3/build"
STUB_BUILD_MODE=fail-always
STUB_CHECK_MODE=fail
STUB_CALL_LOG="$FAKE/t3-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t3-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
rc=0
run_ir_build "$ENG3" FixtureTarget "$FAKE/t3.log" || rc=$?
assert_eq "$rc" "1" "T3: exits with the check's own status"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build,check" \
    "T3: call sequence is build, check — no retry after a check failure"
assert_contains "$(cat "$FAKE/t3.log")" "fixture check failure" \
    "T3: the check's own error output reaches the log"

# --- T4: success first try — the check is never invoked ---------------------
echo "T4: success first try"
ENG4="$FAKE/eng4"; new_engine_fixture "$ENG4"; seed_build_dir "$ENG4/build"
STUB_BUILD_DIR="$ENG4/build"
STUB_BUILD_MODE=succeed
STUB_CHECK_MODE=noop
STUB_CALL_LOG="$FAKE/t4-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t4-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
rc=0
run_ir_build "$ENG4" FixtureTarget "$FAKE/t4.log" || rc=$?
assert_eq "$rc" "0" "T4: success first try reports rc 0"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build" \
    "T4: only the build call is issued — the check never runs"

# --- T5: stale arm again, with IRREDEN_BUILD_DIR override -------------------
echo "T5: stale arm under an IRREDEN_BUILD_DIR override"
ENG5="$FAKE/eng5"; new_engine_fixture "$ENG5"   # cwd only; BUILD_DIR is overridden
OVERRIDE_DIR="$FAKE/override-build"; seed_build_dir "$OVERRIDE_DIR"
STUB_BUILD_DIR="$OVERRIDE_DIR"
STUB_BUILD_MODE=fail-once
STUB_CHECK_MODE=regenerate
STUB_CALL_LOG="$FAKE/t5-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t5-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
export IRREDEN_BUILD_DIR="$OVERRIDE_DIR"
rc=0
run_ir_build "$ENG5" FixtureTarget "$FAKE/t5.log" || rc=$?
unset IRREDEN_BUILD_DIR
assert_eq "$rc" "0" "T5: IRREDEN_BUILD_DIR override retries and succeeds"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build,check,build" \
    "T5: same call sequence under the override"

# --- T6: stale arm from a downstream-creation worktree layout ---------------
echo "T6: stale arm from a downstream-creation worktree layout"
ENG6="$FAKE/eng6"
mkdir -p "$ENG6/engine"; touch "$ENG6/CMakePresets.json"
CREATION_WT="$ENG6/creations/game/.claude/worktrees/agent-1"
mkdir -p "$CREATION_WT"
git -C "$CREATION_WT" init -q
CREATION_BUILD="$ENG6/build-game-agent-1"
seed_build_dir "$CREATION_BUILD"
STUB_BUILD_DIR="$CREATION_BUILD"
STUB_BUILD_MODE=fail-once
STUB_CHECK_MODE=regenerate
STUB_CALL_LOG="$FAKE/t6-calls.log"; : > "$STUB_CALL_LOG"
STUB_BUILD_CALLS="$FAKE/t6-build-calls"; echo 0 > "$STUB_BUILD_CALLS"
export STUB_BUILD_DIR STUB_BUILD_MODE STUB_CHECK_MODE STUB_CALL_LOG STUB_BUILD_CALLS
rc=0
run_ir_build "$CREATION_WT" FixtureTarget "$FAKE/t6.log" || rc=$?
assert_eq "$rc" "0" "T6: creation-worktree build dir retries and succeeds"
assert_eq "$(call_sequence "$STUB_CALL_LOG")" "build,check,build" \
    "T6: same call sequence for the creation-worktree route"

summarize "ir-build stale-target retry tests"
