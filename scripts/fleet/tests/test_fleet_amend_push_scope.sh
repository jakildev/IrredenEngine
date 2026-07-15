#!/usr/bin/env bash
# Tests for fleet-pr-amend-push's #2402 worktree-scope assert: the push runs
# from the cwd repo, so a stale amend-ref sentinel in a shared main clone would
# route it from the wrong tree. The wrapper now calls fleet-assert-worktree
# before touching the sentinel — refuse from a main clone, proceed from a
# worktree.
#
# Hermetic: `git init`s two sandbox repos (a "main clone" whose toplevel lacks
# the /.claude/worktrees/ segment, and a worktree-shaped one) and drives the real
# wrapper. No network is reached — the assert and the sentinel-missing check both
# precede any git fetch/push.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

WRAPPER="$SCRIPT_DIR/fleet-pr-amend-push"
[[ -x "$WRAPPER" ]] || { echo "test setup: fleet-pr-amend-push not executable at $WRAPPER" >&2; exit 1; }

TMPROOT=$(mktemp -d -t fleet-amend-scope)
trap '[[ -n "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"' EXIT

MAIN="$TMPROOT/mainclone"
WT="$TMPROOT/eng/.claude/worktrees/worker-3"
mkdir -p "$MAIN" "$WT"
git -C "$MAIN" init -q
git -C "$WT" init -q

# run <cwd> [env-assignments...] → prints "<exit>\n<stderr>"
run_in() {
    local dir="$1"; shift
    ( cd "$dir" && "$@" "$WRAPPER" >/dev/null 2>"$TMPROOT/err"; echo "$?" )
}

echo "main clone: assert refuses before the sentinel is read"
rc=$(run_in "$MAIN")
assert_eq "$rc" "1" "amend-push refuses from a main-clone cwd (exit 1)"
assert_contains "$(cat "$TMPROOT/err")" "NOT a fleet worktree" \
    "refusal names the worktree guard"

echo "worktree: assert passes, falls through to the sentinel-missing check"
rc=$(run_in "$WT")
assert_eq "$rc" "1" "amend-push proceeds past the assert in a worktree (then sentinel-missing)"
assert_contains "$(cat "$TMPROOT/err")" "missing" \
    "got past the assert to the sentinel check (proves the assert allowed it)"

echo "main clone + FLEET_ALLOW_MAIN_CLONE: override lets it reach the sentinel check"
rc=$(run_in "$MAIN" env FLEET_ALLOW_MAIN_CLONE=1)
assert_eq "$rc" "1" "override reaches the sentinel-missing check (not the worktree refusal)"
assert_absent "$(cat "$TMPROOT/err")" "NOT a fleet worktree" \
    "override suppresses the worktree refusal"

summarize "fleet-pr-amend-push scope-guard tests"
