#!/usr/bin/env bash
# Tests for `fleet-claim decline <kind> <N> [<agent>] --reason "<why>"` — the
# honest exit for a dispatch target the iteration cannot work
# (docs/agents/FLEET-RUNTIME.md § The dispatch target). Pins:
#
#   - the `declined:` comment grammar, round-tripped through its reader
#     (fleet_completion.py assess -> `declined`), not just string-matched;
#   - the release routed through FLEET_TARGET_RELEASE — the same kind table
#     the dispatcher claimed with — for every kind;
#   - the argument gates, the agent default (the cwd basename — the pane's
#     worktree), the game namespace, and that a failed comment still
#     releases (a labelled target over a readable record beats a silent one).
#
# The release commands themselves are covered by their own suites; here they
# are overridden to log the routing. gh is a function stub.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/tests/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

# `fleet-claim decline` reads these values to describe the caller. This suite's
# unknown-role fallback case cannot distinguish an intentionally unknown caller
# from a fleet iteration that exported its own role, so keep the harness clean.
unset FLEET_ROLE FLEET_ROLE_MODEL

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
mkdir -p "$FLEET_CLAIMS_DIR"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
GH_LOG="$TMPROOT/gh.log"
REL_LOG="$TMPROOT/release.log"

set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

STUB_COMMENT_FAIL=0
gh() {
    printf '%s\n' "$*" >> "$GH_LOG"
    case "$*" in
        *"/comments -f body="*) (( STUB_COMMENT_FAIL )) && return 1; return 0 ;;
    esac
    return 0
}
cmd_release()           { printf 'release %s\n' "$*" >> "$REL_LOG"; }
cmd_amending_release()  { printf 'amending-release %s\n' "$*" >> "$REL_LOG"; }
cmd_resolving_release() { printf 'resolving-release %s\n' "$*" >> "$REL_LOG"; }
cmd_planning_release()  { printf 'planning-release %s\n' "$*" >> "$REL_LOG"; }
cmd_review_release()    { printf 'review-release %s\n' "$*" >> "$REL_LOG"; }

reset_state() { : > "$GH_LOG"; : > "$REL_LOG"; REPO_NS=""; }
decline() {  # runs cmd_decline, echoes its exit status; stdout/stderr to $TMPROOT/out
    local rc=0
    cmd_decline "$@" > "$TMPROOT/out" 2>&1 || rc=$?
    printf '%s\n' "$rc"
}

echo "T1: argument gates — unknown kind, bad number, missing reason, unknown flag"
reset_state
assert_eq "$(decline bogus 42 pool-3 --reason x)" "2" "unknown kind exits 2"
assert_eq "$(decline task abc pool-3 --reason x)" "2" "non-numeric target exits 2"
assert_eq "$(decline task 42 pool-3)" "2" "missing --reason exits 2"
assert_eq "$(decline task 42 pool-3 --bogus)" "2" "unknown flag exits 2"
assert_eq "$(cat "$GH_LOG" "$REL_LOG")" "" "a refused decline posts nothing and releases nothing"

echo "T2: a task decline posts the record first, then releases"
reset_state
assert_eq "$(FLEET_ROLE=worker FLEET_ROLE_MODEL=opus decline task 42 pool-3 --reason "needs a mac host")" "0" \
    "decline exits 0"
assert_eq "$(cat "$GH_LOG")" \
    "api repos/jakildev/IrredenEngine/issues/42/comments -f body=declined: worker/opus @mac-pool-3 needs a mac host" \
    "one gh call: the declined: comment, in the grammar fleet_completion reads"
assert_eq "$(cat "$REL_LOG")" "release 42 pool-3" "task kind releases via cmd_release"
assert_contains "$(cat "$TMPROOT/out")" "declined: task#42 (worker/opus @mac-pool-3): needs a mac host" \
    "stdout names the target, who, and why"

echo "T3: the posted comment round-trips through the reader"
body=$(sed -n 's/.*-f body=//p' "$GH_LOG" | head -n 1)
printf '["fleet:claim-mac-pool-3"]\n' > "$TMPROOT/labels.json"
printf '[{"created_at":"2026-09-06T17:26:19Z","body":"%s"}]\n' "$body" > "$TMPROOT/comments.json"
verdict=$(python3 "$SCRIPT_DIR/fleet_completion.py" assess --claim-label fleet:claim-mac-pool-3 \
    --host-agent mac-pool-3 --since 0 --issue-json "$TMPROOT/labels.json" \
    --comments-json "$TMPROOT/comments.json" | cut -f1 | tr -d '\r' || true)
assert_eq "$verdict" "declined" "fleet_completion.py reads the posted comment as a decline"

echo "T4: the game namespace posts to the game repo"
reset_state
REPO_NS=game
assert_eq "$(decline feedback 7 pool-3 --reason "no")" "0" "game decline exits 0"
assert_contains "$(cat "$GH_LOG")" "api repos/jakildev/irreden/issues/7/comments -f body=declined: " \
    "comment posted on the game repo"
assert_eq "$(cat "$REL_LOG")" "amending-release 7 pool-3" "feedback releases via amending-release under the agent"
REPO_NS=""

echo "T5: every kind releases through FLEET_TARGET_RELEASE"
for pair in "stack:release 9 pool-3" "conflict:resolving-release 9 pool-3" "plan:planning-release 9 pool-3" \
            "review:review-release 9 pool-3" "planreview:review-release 9 pool-3" "smoke:review-release 9 pool-3"; do
    kind="${pair%%:*}"; expected="${pair#*:}"
    reset_state
    assert_eq "$(decline "$kind" 9 pool-3 --reason r)" "0" "$kind decline exits 0"
    assert_eq "$(cat "$REL_LOG")" "$expected" "$kind -> $expected"
done

echo "T6: a failed comment warns but still releases"
reset_state
STUB_COMMENT_FAIL=1
assert_eq "$(decline task 42 pool-3 --reason "x")" "0" "decline exits 0 despite the failed comment"
STUB_COMMENT_FAIL=0
assert_contains "$(cat "$TMPROOT/out")" "WARN could not post the declined: comment" "warned"
assert_eq "$(cat "$REL_LOG")" "release 42 pool-3" "still released"

echo "T7: agent defaults to the cwd basename; --reason= form accepted"
reset_state
assert_eq "$(unset FLEET_ROLE FLEET_ROLE_MODEL; decline task 42 --reason="inline form")" "0" \
    "agent-less decline exits 0"
assert_contains "$(cat "$GH_LOG")" "body=declined: unknown/unknown @mac-$(basename "$PWD") inline form" \
    "unknown role/class spelled out, agent = cwd basename, reason from --reason="

echo "T8: the CLI arm gates its arguments before dispatching"
rc=0; "$FLEET_CLAIM" decline task >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "2" "decline with no number exits 2"
rc=0; "$FLEET_CLAIM" decline >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "2" "bare decline exits 2"

if [[ -z "${FLEET_TEST_SELFCHECK:-}" ]]; then
    echo "T9: suite is hermetic against ambient fleet role variables"
    if ! FLEET_TEST_SELFCHECK=1 FLEET_ROLE=worker FLEET_ROLE_MODEL=opus \
        bash "$0" >"$TMPROOT/selfcheck.log" 2>&1; then
        cat "$TMPROOT/selfcheck.log" >&2
        exit 1
    fi
fi

summarize "fleet-claim decline tests"
