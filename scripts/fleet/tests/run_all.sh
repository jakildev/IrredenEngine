#!/usr/bin/env bash
# run_all.sh — run every fleet test suite in this directory once.
#
# These suites are not CMake tests, so `ctest` never sees them; this runner
# and the `fleet-tests.yml` workflow that calls it are the only things that
# execute them. Keep that workflow wired — an unexecuted suite goes red
# silently and stays that way.
#
# Discovery is by glob, so a new suite is picked up with no registration
# step. lib_assert.sh is deliberately named outside the test_* pattern and
# is therefore skipped (it is sourced, never executed).
#
# Each suite runs in its own process with the tests directory as cwd —
# matching how the suites are run by hand — and is timeout-guarded so one
# hung suite cannot wedge CI, even on a host where neither `timeout` nor
# `gtimeout` is on PATH (a pure-bash fallback covers that case). A `RUN`
# line is printed immediately before each suite starts, so a hang is
# diagnosable from which suite's result line never follows. Suites are
# hermetic by the authoring rules (no live GitHub, no live ~/.fleet), so
# this is safe to run unattended.
#
# The pane's own FLEET_* exports are removed from every suite's environment.
# fleet-dispatch-wrap exports them into each dispatched pane, and a subject
# that reads one would otherwise verdict on the pane instead of the fixture:
# green in CI, red only for the agents who run the suites most. The name set
# is read from the wrapper, so a new export is scrubbed with no edit here.
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
# A suite that cannot find its subject under test should print
# "SKIP: <reason>" to stderr and exit 3 — that is the shared skip status
# this runner recognizes. Do not `exit 0` from a guard that never actually
# exercised the subject; that counts as an unverified PASS.
#
# Exit status:
#   0  every selected suite passed or was skipped (or --list / --help)
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

# `env -u` arguments naming every FLEET_* variable the dispatch wrapper assigns.
scrub_args=()
dispatch_wrap="$TESTS_DIR/../fleet-dispatch-wrap"
if [[ -f "$dispatch_wrap" ]]; then
    while IFS= read -r scrub_name; do
        [[ -n "$scrub_name" ]] && scrub_args+=(-u "$scrub_name")
    done < <(grep -oE '(^|[^A-Za-z0-9_])FLEET_[A-Z0-9_]+=' "$dispatch_wrap" \
                 | sed -E 's/^[^F]*//; s/=$//' | sort -u)
fi

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
        # header grows or shrinks.
        -h|--help)  awk 'NR>1 && !/^#/{exit} NR>1{sub(/^# ?/, ""); print}' "$0"; exit 0 ;;
        *)          die_usage "unknown argument '$1'" ;;
    esac
done

[[ "$per_timeout" =~ ^[0-9]+$ ]] || die_usage "--timeout takes a non-negative integer"

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

# `timeout` is coreutils; absent on a stock macOS host, and observed missing
# from a native-Windows fleet host's PATH too. Either way, a missing external
# binary must never disable the per-suite guard outright — `run_with_timeout`
# below reimplements it in pure bash as the fallback.
# `RUN_ALL_NO_EXTERNAL_TIMEOUT` is a test-only seam (tests/test_run_all.sh)
# that forces the fallback on a host where the external binaries happen to
# be present, so that code path still gets exercised.
timeout_cmd=""
if [[ "$per_timeout" -gt 0 && -z "${RUN_ALL_NO_EXTERNAL_TIMEOUT:-}" ]]; then
    if command -v timeout >/dev/null 2>&1; then
        timeout_cmd="timeout $per_timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        timeout_cmd="gtimeout $per_timeout"
    fi
fi

# Portable per-suite timeout, used whenever no external `timeout`/`gtimeout`
# binary is on PATH: sends TERM, then KILL if the process is still alive 5s
# later. Mirrors coreutils timeout's exit-124-on-deadline contract via the
# marker file, since a signal-killed direct child's own exit status (128+sig)
# does not distinguish "we killed it" from any other signal death.
run_with_timeout() {
    local secs="$1"; shift
    local marker
    marker=$(mktemp "${TMPDIR:-/tmp}/run-all-timeout.XXXXXX")
    "$@" &
    local cmd_pid=$!
    (
        sleep "$secs"
        kill -TERM "$cmd_pid" 2>/dev/null && : > "$marker"
        sleep 5
        kill -KILL "$cmd_pid" 2>/dev/null
    ) &
    local watcher_pid=$!

    local rc=0
    wait "$cmd_pid" 2>/dev/null || rc=$?
    kill "$watcher_pid" 2>/dev/null
    wait "$watcher_pid" 2>/dev/null

    [[ -e "$marker" ]] && rc=124
    rm -f "$marker"
    return "$rc"
}

cd "$TESTS_DIR" || exit 1

passed=0
failed_names=()
failed_ids=()
skipped_names=()

# Skip status: a suite whose subject under test is missing exits with this
# code instead of 0, so a vacuous run is never folded into "passed".
SKIP_STATUS=3

# A failed suite's failure identity: a checksum of its exit status and its
# distinct failure blocks, so two runs of one suite read alike only when they
# failed the same assertions for the same reason. A block is a failure header
# (lib_assert's `FAIL:`, unittest's `FAIL:`/`ERROR:`) plus the detail under it:
# a unittest block runs to its closing dash/equals separator and carries the
# traceback and the AssertionError text; a lib_assert block runs to the next
# blank or `ok:` line and carries the expected/actual lines the assert_*
# helpers print. The header alone is not enough — `assertEqual(1, 2)` and
# `assertEqual(1, 3)` in one test share it. Random mktemp/tempfile names are
# masked; any other run-to-run noise in a block only makes an inherited red
# read as the head's own, the closed direction. `?` when the output names no
# failure or the suite timed out: nothing then proves two runs failed alike,
# and fleet-decisions reads `?` as never matching.
failure_identity() {  # $1 = exit status, $2 = suite output
    local lines
    lines=$(printf '%s\n' "$2" | awk '
        function flush() { if (blk != "") print blk; blk = ""; mode = "" }
        /^[ \t]*(FAIL|ERROR): / {
            flush(); sub(/^[ \t]+/, ""); blk = $0; mode = "head"; next
        }
        mode == "" { next }
        (/^-+$/ || /^=+$/) && length($0) >= 20 {
            if (mode == "head") { mode = "unittest"; next }
            flush(); next
        }
        mode == "head" { mode = "assert" }
        mode == "assert" && (/^[ \t]*$/ || /^[ \t]*ok: /) { flush(); next }
        /^[ \t]*$/ { next }
        { sub(/^[ \t]+/, ""); blk = blk " | " $0 }
        END { flush() }' \
        | sed -E 's#/tmp\.[A-Za-z0-9]{10}#/tmp.XXXXXXXXXX#g; s#/tmp[a-z0-9_]{8}#/tmpXXXXXXXX#g' \
        | LC_ALL=C sort -u)
    if [[ -z "$lines" || "$1" -eq 124 ]]; then
        echo "?"
        return
    fi
    printf '%s\n%s\n' "$1" "$lines" | cksum | awk '{print $1}'
}

for f in "${suites[@]}"; do
    name=$(basename "$f")
    case "$name" in
        *.py) interp=(python3) ;;
        *)    interp=(bash) ;;
    esac

    printf 'RUN   %s\n' "$name"
    if [[ -n "$timeout_cmd" ]]; then
        out=$(env ${scrub_args[@]+"${scrub_args[@]}"} $timeout_cmd "${interp[@]}" "$f" 2>&1)
        rc=$?
    elif [[ "$per_timeout" -gt 0 ]]; then
        out=$(run_with_timeout "$per_timeout" env ${scrub_args[@]+"${scrub_args[@]}"} "${interp[@]}" "$f" 2>&1)
        rc=$?
    else
        out=$(env ${scrub_args[@]+"${scrub_args[@]}"} "${interp[@]}" "$f" 2>&1)
        rc=$?
    fi
    if [[ "$rc" -eq 0 ]]; then
        passed=$((passed + 1))
        printf 'PASS  %s\n' "$name"
    elif [[ "$rc" -eq "$SKIP_STATUS" ]]; then
        skipped_names+=("$name")
        printf 'SKIP  %s\n' "$name"
        printf '%s\n' "$out" | sed 's/^/      | /'
    else
        failed_names+=("$name")
        failed_ids+=("$name@$(failure_identity "$rc" "$out")")
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
echo "$PROG: ${#suites[@]} suite(s) — $passed passed, ${#failed_names[@]} failed, ${#skipped_names[@]} skipped"
if [[ ${#skipped_names[@]} -gt 0 ]]; then
    echo "$PROG: skipped: ${skipped_names[*]}"
fi
if [[ ${#failed_names[@]} -gt 0 ]]; then
    echo "$PROG: failed: ${failed_names[*]}" >&2
    # A GitHub Actions annotation: each failed suite as `<name>@<identity>`
    # becomes a check-run annotation that fleet-decisions reads to tell a
    # head's own red from one master already carries. Inert outside Actions.
    echo "::error title=fleet-tests failed suites::${failed_ids[*]}"
    exit 1
fi
