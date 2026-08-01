#!/usr/bin/env bash
# run_all.sh — run every fleet test suite in this directory once.
#
# These suites are not CMake tests, so `ctest` never sees them; this runner
# and the `fleet-tests.yml` workflow that calls it are the only things that
# execute them. Keep that workflow wired — an unexecuted suite goes red
# silently and stays that way (see #2712).
#
# Discovery is by glob, so a new suite is picked up with no registration
# step. lib_assert.sh is deliberately named outside the test_* pattern and
# is therefore skipped (it is sourced, never executed).
#
# Each suite runs in its own process with the tests directory as cwd —
# matching how the suites are run by hand — and is timeout-guarded so one
# hung suite cannot wedge CI. Suites are hermetic by the authoring rules
# (no live GitHub, no live ~/.fleet), so this is safe to run unattended.
#
# Usage:
#   run_all.sh [--only <substring>] [--list] [--timeout <seconds>]
#
# Options:
#   --only <substring>  Run only suites whose filename contains <substring>.
#   --list              Print the discovered suite list; run nothing.
#   --timeout <secs>    Per-suite timeout (default 120). 0 disables.
#   -h, --help          Show this help.
#
# Exit status:
#   0  every selected suite passed (or --list / --help)
#   1  at least one suite failed, timed out, or none matched --only
#   2  usage error
#
# Source of truth: scripts/fleet/tests/run_all.sh in the engine repo.
set -uo pipefail

PROG=$(basename "$0")
TESTS_DIR=$(cd "$(dirname "$0")" && pwd)

only=""
list_only=0
per_timeout=120

die_usage() {
    echo "$PROG: $1" >&2
    echo "usage: $PROG [--only <substring>] [--list] [--timeout <seconds>]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)     [[ $# -ge 2 && -n "$2" ]] || die_usage "--only needs a value"; only="$2"; shift 2 ;;
        --only=*)   only="${1#--only=}"; [[ -n "$only" ]] || die_usage "--only needs a value"; shift ;;
        --timeout)  [[ $# -ge 2 && -n "$2" ]] || die_usage "--timeout needs a value"; per_timeout="$2"; shift 2 ;;
        --timeout=*) per_timeout="${1#--timeout=}"; [[ -n "$per_timeout" ]] || die_usage "--timeout needs a value"; shift ;;
        --list)     list_only=1; shift ;;
        # Print the header block by *shape* (every comment line after the
        # shebang, stopping at the first line of code) rather than a fixed
        # line range — a range silently slices the wrong text the moment the
        # header grows or shrinks (#2436).
        -h|--help)  awk 'NR>1 && !/^#/{exit} NR>1{sub(/^# ?/, ""); print}' "$0"; exit 0 ;;
        *)          die_usage "unknown argument '$1'" ;;
    esac
done

[[ "$per_timeout" =~ ^[0-9]+$ ]] || die_usage "--timeout takes a non-negative integer"

# ----------------------------------------------------------------------
# Discover: test_*.sh run under bash, test_*.py under python3.
# ----------------------------------------------------------------------
suites=()
for f in "$TESTS_DIR"/test_*.sh "$TESTS_DIR"/test_*.py; do
    [[ -f "$f" ]] || continue                       # unmatched glob
    [[ -n "$only" && "$(basename "$f")" != *"$only"* ]] && continue
    suites+=("$f")
done

if [[ ${#suites[@]} -eq 0 ]]; then
    echo "$PROG: no suites matched${only:+ --only '$only'}" >&2
    exit 1
fi

if [[ "$list_only" -eq 1 ]]; then
    for f in "${suites[@]}"; do basename "$f"; done
    exit 0
fi

# `timeout` is coreutils; absent on a stock macOS host, where the guard is
# simply skipped rather than failing the run (CI is Linux and does have it).
timeout_cmd=""
if [[ "$per_timeout" -gt 0 ]]; then
    if command -v timeout >/dev/null 2>&1; then
        timeout_cmd="timeout $per_timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        timeout_cmd="gtimeout $per_timeout"
    fi
fi

# ----------------------------------------------------------------------
# Run: one process per suite, cwd = tests dir (how they run by hand).
# ----------------------------------------------------------------------
cd "$TESTS_DIR" || exit 1

passed=0
failed_names=()

for f in "${suites[@]}"; do
    name=$(basename "$f")
    case "$name" in
        *.py) interp=(python3) ;;
        *)    interp=(bash) ;;
    esac

    if out=$($timeout_cmd "${interp[@]}" "$f" 2>&1); then
        passed=$((passed + 1))
        printf 'PASS  %s\n' "$name"
    else
        rc=$?
        failed_names+=("$name")
        # 124 is coreutils timeout's "killed on deadline" status.
        if [[ "$rc" -eq 124 ]]; then
            printf 'FAIL  %s (timed out after %ss)\n' "$name" "$per_timeout"
        else
            printf 'FAIL  %s (exit %s)\n' "$name" "$rc"
        fi
        printf '%s\n' "$out" | sed 's/^/      | /'
    fi
done

echo
echo "$PROG: ${#suites[@]} suite(s) — $passed passed, ${#failed_names[@]} failed"
if [[ ${#failed_names[@]} -gt 0 ]]; then
    echo "$PROG: failed: ${failed_names[*]}" >&2
    exit 1
fi
