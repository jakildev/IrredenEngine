#!/usr/bin/env bash
# Tests for the shell side of GitHub CLI accounting: the gh-accounting-bin/gh
# launcher and the exported `gh` function in gh-function.sh.
#
# Hermetic: the "real" gh and every stub are temp-dir Python scripts (plus the
# gh.bat twin on native Windows, where Python cannot run an extensionless
# script), the event root is a temp dir, and GitHub is never reached.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_assert.sh"
source "$SCRIPT_DIR/lib_hermetic.sh"

FLEET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LAUNCHER="$FLEET_DIR/gh-accounting-bin/gh"
FUNCTION_SH="$FLEET_DIR/gh-accounting-bin/gh-function.sh"
for subject in "$LAUNCHER" "$FUNCTION_SH" "$FLEET_DIR/fleet_github.py"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject not found: $subject" >&2
        exit 3
    fi
done

TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/gh-accounting.XXXXXX")"
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT
hermetic_poison_gh_env "$TMPROOT"

PYTHON="$(command -v python3)"
IS_WINDOWS=0
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;; esac
native() { if (( IS_WINDOWS )); then cygpath -m "$1"; else printf '%s' "$1"; fi; }

# make_gh <dir> <tag> — prints the path a PATH lookup of `gh` resolves to.
make_gh() {
    mkdir -p "$1"
    cat > "$1/gh" <<EOF
#!$PYTHON
import json, os, sys
sys.stdout.buffer.write(b"OUT $2 " + json.dumps(sys.argv[1:]).encode() + b" \xc3\xa9\n")
sys.stderr.buffer.write(b"ERR $2\n")
sys.exit(int(os.environ.get("STUB_EXIT", "0")))
EOF
    chmod +x "$1/gh"
    if (( IS_WINDOWS )); then
        printf '@"%s" "%%~dp0gh" %%*\r\n' "$(native "$PYTHON")" > "$1/gh.bat"
        native "$1/gh.bat"
    else
        printf '%s' "$1/gh"
    fi
}

REAL_DIR="$TMPROOT/real"; STUB_DIR="$TMPROOT/stub"; EVENTS="$TMPROOT/events"
REAL="$(make_gh "$REAL_DIR" real)"
make_gh "$STUB_DIR" stub >/dev/null
SYS_PATH="$PATH"

# Interpreter wrapper: records that the configured absolute interpreter ran.
PY_LOG="$TMPROOT/python.log"
PY_WRAP="$TMPROOT/py-wrap"
printf '#!%s\necho ran >> "%s"\nexec "%s" "$@"\n' "$BASH" "$PY_LOG" "$PYTHON" > "$PY_WRAP"
chmod +x "$PY_WRAP"

export FLEET_GH_ACCOUNTING=1 FLEET_GH_REAL="$REAL" FLEET_GH_LAUNCHER="$LAUNCHER" \
       FLEET_GH_PYTHON="$PY_WRAP" FLEET_GH_EVENT_ROOT="$(native "$EVENTS")" FLEET_ROLE=worker

event_count() { find "$EVENTS" -type f -name '*.json' ! -name '.*' 2>/dev/null | wc -l | tr -d ' '; }

echo "1. the launcher is byte-transparent and counts one attempt"
ARG='repos/o/r/pulls?per_page=100&page=2'
STUB_EXIT=7 PATH="$REAL_DIR:$SYS_PATH" "$REAL_DIR/gh" api "$ARG" >"$TMPROOT/want.out" 2>"$TMPROOT/want.err"
want_rc=$?
STUB_EXIT=7 PATH="$REAL_DIR:$SYS_PATH" "$LAUNCHER" api "$ARG" >"$TMPROOT/got.out" 2>"$TMPROOT/got.err"
got_rc=$?
assert_eq "$(od -An -tx1 < "$TMPROOT/got.out")" "$(od -An -tx1 < "$TMPROOT/want.out")" \
    "stdout bytes identical to the real gh"
assert_eq "$(od -An -tx1 < "$TMPROOT/got.err")" "$(od -An -tx1 < "$TMPROOT/want.err")" \
    "stderr bytes identical"
assert_eq "$got_rc" "7" "exit status passed through (real gh: $want_rc)"
assert_contains "$(cat "$TMPROOT/got.out")" "\"$ARG\"" "an & in an argument reaches gh intact"
assert_eq "$(event_count)" "1" "one event per counted attempt"
assert_eq "$(grep -c ran "$PY_LOG")" "1" "the configured FLEET_GH_PYTHON is the interpreter that ran"

echo "2. the exported function routes a role's shell gh through the launcher"
# shellcheck source=../gh-accounting-bin/gh-function.sh
source "$FUNCTION_SH"
before="$(event_count)"
out="$(PATH="$REAL_DIR:$SYS_PATH" gh pr view 1 2>/dev/null)"
assert_contains "$out" "OUT real" "the real gh answered"
assert_eq "$(event_count)" "$((before + 1))" "counted through the function"

before="$(event_count)"
out="$(PATH="$REAL_DIR:$SYS_PATH" bash -c 'PATH="$0:$PATH"; gh pr view 1' "$REAL_DIR" 2>/dev/null)"
assert_contains "$out" "OUT real" "a child bash inherits the function"
assert_eq "$(event_count)" "$((before + 1))" "counted after an rc-style PATH re-prepend in the child"

echo "3. stubs and overrides stay hermetic under live accounting"
before="$(event_count)"
out="$(PATH="$STUB_DIR:$REAL_DIR:$SYS_PATH" gh api 'x?per_page=100&y=1' 2>/dev/null)"
assert_contains "$out" 'OUT stub ["api", "x?per_page=100&y=1"]' "a PATH-prepended stub wins, argument intact"
out="$(PATH="$REAL_DIR:$SYS_PATH" bash -c 'PATH="$0:$PATH"; gh pr view 1' "$STUB_DIR" 2>/dev/null)"
assert_contains "$out" "OUT stub" "a stub a child prepends later still wins"
assert_eq "$(event_count)" "$before" "no event for any stub call"

echo "4. accounting off: the function is plain command gh"
before="$(event_count)"
out="$(FLEET_GH_ACCOUNTING=0 PATH="$REAL_DIR:$SYS_PATH" gh pr view 1 2>/dev/null)"
assert_contains "$out" "OUT real" "reaches the PATH gh"
assert_eq "$(event_count)" "$before" "nothing counted"

echo "5. no gh at all behaves like bash"
out="$(PATH="$TMPROOT/empty" gh pr view 1 2>&1)"; rc=$?
assert_eq "$rc" "127" "exit 127"
assert_contains "$out" "gh: command not found" "bash's own message"

summarize "gh accounting shell tests"
