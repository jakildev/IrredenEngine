#!/usr/bin/env bash
# Tests for `fleet-claim branch-check` (#2419) — the mint-time guard that
# validates a branch encodes its claimed issue number, so the fleet PR-open
# flow never pushes a branch the claim-liveness matcher can't tie back to the
# issue (the incident: `claude/game-worker-3-issue-255` was invisible to the
# sweep, freeing the claim and spawning a duplicate PR).
#
# The subcommand is purely LOCAL — no network — so this test needs no gh stub;
# it only exercises the shared matcher via fleet-claim and, for the default
# path, a throwaway git repo. Hermetic per scripts/fleet/CLAUDE.md.
#
# Covers:
#   - prefix-form match (engine) and token-form match (repo-agnostic)
#   - mismatch → exit 1 with the observed branch + expected form
#   - game namespace: prefix match + game-form in the expected-form message
#   - leading-number precedence (a token is suppressed when a number leads)
#   - default-to-current-branch (matching and mismatching) + detached HEAD
#   - legacy T-NNN issue arg is rejected (exit 2)

set -euo pipefail

# branch-check is a pure local subcommand, but disable the clone-freshness gate
# defensively so nothing in fleet-claim startup can mask an exit code.
export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected exit: $expected"
        echo "        actual exit:   $actual"
    fi
}

# --- explicit-branch forms --------------------------------------------------

echo "T1: prefix-form branch matches its issue (engine)"
actual=0; "$FLEET_CLAIM" branch-check 2419 claude/2419-widen-matcher >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "branch-check 2419 claude/2419-… → exit 0"

echo "T2: issue-<N> token branch matches (repo-agnostic)"
actual=0; "$FLEET_CLAIM" branch-check 255 claude/game-worker-3-issue-255 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "branch-check 255 claude/game-worker-3-issue-255 → exit 0"

echo "T3: mismatch → exit 1 with observed branch + expected form"
err=$("$FLEET_CLAIM" branch-check 255 claude/some-unrelated-topic 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "branch-check 255 claude/some-unrelated-topic → exit 1"
assert_contains "$err" "does not encode issue #255" "mismatch names the issue"
assert_contains "$err" "claude/255-" "mismatch prints the expected prefix form"

echo "T4: game namespace — prefix form matches"
actual=0; "$FLEET_CLAIM" --repo game branch-check 255 claude/game-255-topic >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "--repo game branch-check 255 claude/game-255-topic → exit 0"

echo "T5: game namespace — expected-form message lists the game prefix"
err=$("$FLEET_CLAIM" --repo game branch-check 255 claude/nope 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "--repo game branch-check 255 claude/nope → exit 1"
assert_contains "$err" "claude/game-255-" "game mismatch prints the game prefix form"

echo "T6: leading-number precedence — a trailing token does NOT match"
# claude/2419-fix-issue-1425-recurrence resolves to #2419 only, never #1425.
actual=0; "$FLEET_CLAIM" branch-check 1425 claude/2419-fix-issue-1425-recurrence >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 1 "branch-check 1425 on a #2419-leading branch → exit 1 (token suppressed)"
actual=0; "$FLEET_CLAIM" branch-check 2419 claude/2419-fix-issue-1425-recurrence >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "branch-check 2419 on the same branch → exit 0 (leading number wins)"

# --- default-to-current-branch (needs a throwaway git repo) -----------------

REPO="$TMPROOT/repo"
mkdir -p "$REPO"
git -C "$REPO" init -q
git -C "$REPO" -c user.email=t@t -c user.name=t commit -q --allow-empty -m init
git -C "$REPO" checkout -q -b claude/700-default-branch-topic

echo "T7: default branch (current) matches the issue"
actual=0; ( cd "$REPO" && "$FLEET_CLAIM" branch-check 700 ) >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "branch-check 700 on branch claude/700-… → exit 0"

echo "T8: default branch mismatch → exit 1"
actual=0; ( cd "$REPO" && "$FLEET_CLAIM" branch-check 701 ) >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 1 "branch-check 701 on branch claude/700-… → exit 1"

echo "T9: detached HEAD (no named branch) → exit 2"
git -C "$REPO" checkout -q --detach HEAD
actual=0; ( cd "$REPO" && "$FLEET_CLAIM" branch-check 700 ) >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 2 "branch-check with no named branch → exit 2"

# --- arg validation ---------------------------------------------------------

echo "T10: legacy T-NNN issue arg is rejected"
actual=0; "$FLEET_CLAIM" branch-check T-255 claude/255-x >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 2 "branch-check T-255 … → exit 2 (legacy form rejected)"

summarize "fleet-claim branch-check tests"
