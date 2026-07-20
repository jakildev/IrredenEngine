#!/usr/bin/env bash
# #2419: an improvised `issue-<N>` token branch (or a `Closes #N` body) is
# recognized as live work by fleet-claim's two liveness surfaces:
#   - the cleanup sweep, via _issue_pr_state_from -> issue_pr_state (keeps the
#     claim while the PR is open, so it is not swept into a duplicate), and
#   - cmd_claim's open-PR guard (refuses a second claim on the same issue).
# The #1425 matcher recognized only claude/<N>- / claude/game-<N>- prefixes;
# the incident branch claude/game-worker-3-issue-255 slipped both surfaces,
# so the TTL sweep freed the live claim and a duplicate PR followed.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
source "$(dirname "$0")/lib_assert.sh"

# Source fleet-claim as a library (helpers only, no dispatch). Clear args so
# the top-level --repo parse is a no-op.
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

TOKEN_BRANCH="claude/game-worker-3-issue-255"

echo "== cleanup-sweep liveness (_issue_pr_state_from, the real helper) =="

# (b) A live token-branch PR classifies "active" -> the sweep keeps the claim.
prs='[{"headRefName":"'"$TOKEN_BRANCH"'","labels":[{"name":"fleet:wip"}]}]'
assert_eq "$(_issue_pr_state_from "$prs" 255 game)" "active" \
    "token branch PR keeps the claim (survives sweep)"

# A `Closes #N` body under an untrackable branch is the second signal.
prs='[{"headRefName":"claude/some-odd-branch","body":"Closes #255","labels":[]}]'
assert_eq "$(_issue_pr_state_from "$prs" 255 game)" "active" \
    "Closes #255 body keeps the claim (survives sweep)"

# Control: an unrelated open PR is not live -> the claim would be swept.
prs='[{"headRefName":"claude/999-unrelated","labels":[]}]'
assert_eq "$(_issue_pr_state_from "$prs" 255 game)" "none" \
    "unrelated branch is not live for #255"

echo "== cmd_claim open-PR guard (refuses a duplicate) =="

# Isolate the guard: neutralize the gates that run before it (each hits
# gh / git / FS and is covered by its own test).
_refuse_if_closed()            { return 0; }
assert_clone_fresh()           { return 0; }
check_blockers()               { return 0; }
check_model_tag()              { return 0; }
check_host_capability()        { return 0; }
reservation_holder_for_task()  { :; }
repo_from_ns()                 { echo "jakildev/IrredenEngine"; }

CLAIMS_DIR=$(mktemp -d)/claims

# gh stub: pr list returns an open token-branch PR for #255. The guard only
# consults pr list; any other call returns empty.
gh() {
    if [[ "${1:-}" == "pr" && "${2:-}" == "list" ]]; then
        printf '%s' '[{"number":900,"headRefName":"'"$TOKEN_BRANCH"'","body":""}]'
    fi
}

set +e
out=$(cmd_claim 255 worker-4 2>&1); rc=$?
set -e
assert_eq "$rc" "1" "cmd_claim refuses a duplicate on a token branch (exit 1)"
assert_contains "$out" "refusing duplicate claim" \
    "cmd_claim names the duplicate refusal"
assert_contains "$out" "900" "cmd_claim reports the existing PR number"

# The `Closes #N` body also blocks the claim under an untrackable branch.
gh() {
    if [[ "${1:-}" == "pr" && "${2:-}" == "list" ]]; then
        printf '%s' '[{"number":901,"headRefName":"claude/odd","body":"Closes #255"}]'
    fi
}
CLAIMS_DIR=$(mktemp -d)/claims
set +e
out=$(cmd_claim 255 worker-4 2>&1); rc=$?
set -e
assert_eq "$rc" "1" "cmd_claim refuses a duplicate on a Closes #N body (exit 1)"

# Control: an unrelated open PR must NOT fire the guard (no over-refusal). The
# claim may fail later (label path), but the duplicate refusal must be absent.
gh() {
    if [[ "${1:-}" == "pr" && "${2:-}" == "list" ]]; then
        printf '%s' '[{"number":902,"headRefName":"claude/999-unrelated","body":""}]'
    fi
    return 0
}
CLAIMS_DIR=$(mktemp -d)/claims
set +e
out=$(cmd_claim 255 worker-4 2>&1)
set -e
assert_absent "$out" "refusing duplicate claim" \
    "no duplicate refusal for an unrelated open PR"

summarize "fleet-claim issue-token liveness (#2419)"
