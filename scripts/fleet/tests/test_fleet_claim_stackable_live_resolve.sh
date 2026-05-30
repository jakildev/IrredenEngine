#!/usr/bin/env bash
# Tests for fleet-claim find-stackable-blockers with live blocker resolution (T-1296).
#
# Part (b) of the fix: cmd_find_stackable_blockers now resolves each #N ref
# live before the single-blocker check. A task whose body says:
#   **Blocked by:** #100, #101
# where #100 is CLOSED and #101 has an open PR → returns the #101 PR.
#
# Stub dispatches:
#   gh issue view N --json state --jq .state  → canned state
#   gh pr list --state merged --json headRefName --jq ...  → empty list
#   gh pr list --state open --json ...         → canned list with one PR
#   fetch_issue_info shape: gh issue view N --json state,labels,body
#
# Issue stubs (for fetch_issue_info and check_blockers lookup):
#   3001: **Blocked by:** #100 (done), #101 (still open)  — two refs, one closed
#   3002: **Blocked by:** #101, #102                      — two open refs
#   3003: **Blocked by:** #100                            — single ref, closed (all resolved)
#   3004: **Blocked by:** (none)                          — no blocker
#
# Issue/PR state stubs:
#   #100: CLOSED
#   #101: OPEN (with an open PR claude/101-work-branch)
#   #102: OPEN (no open PR)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_output() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $(printf '%q' "$expected")"
        echo "        actual:   $(printf '%q' "$actual")"
    fi
}

assert_nonempty() {
    local actual="$1" msg="$2"
    if [[ -n "$actual" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg (expected non-empty output, got nothing)"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

# --- git stub (fleet-claim needs a git remote to derive repo_from_ns) -------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/git" <<'GITSTUB'
#!/usr/bin/env bash
if [[ "$*" == *"remote get-url"* ]]; then
    echo "git@github.com:jakildev/IrredenEngine.git"
    exit 0
fi
exec /usr/bin/git "$@"
GITSTUB
chmod +x "$STUB_DIR/git"

# --- gh stub -----------------------------------------------------------------
cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
#
# Recognized invocations:
#   gh issue view N --json state,labels,body             → full issue info
#   gh issue view N --repo R --json state --jq .state    → state only
#   gh pr list --state merged --limit 30 --json headRefName --jq ...
#   gh pr list --state open --json url,headRefName,author,number,body --limit 200
#   gh pr list --state open --json ... --jq ... (claim side)
#   gh api .../labels ... / gh issue edit ... / gh label ...  → no-op

has_jq=0
issue_num=""
pr_state=""
is_merged_list=0
is_open_list=0
for arg in "$@"; do
    [[ "$arg" == "--jq" ]] && has_jq=1
    [[ "$arg" == "merged" ]] && pr_state="merged"
    [[ "$arg" == "open"   ]] && pr_state="open"
    if [[ -z "$issue_num" && "$arg" =~ ^[0-9]+$ ]]; then
        issue_num="$arg"
    fi
done

case "$1 $2" in
    "issue view")
        if [[ "$has_jq" -eq 1 ]]; then
            # check_blockers / find-stackable-blockers state-only lookup
            case "$issue_num" in
                100) echo "CLOSED" ;;
                101) echo "OPEN"   ;;
                102) echo "OPEN"   ;;
                *)   echo "OPEN"   ;;
            esac
            exit 0
        fi
        # fetch_issue_info: full body
        case "$issue_num" in
            3001)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100 (done), #101 (still open)\n"}'
                ;;
            3002)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #101, #102\n"}'
                ;;
            3003)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100\n"}'
                ;;
            3004)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** (none)\n"}'
                ;;
            3005)
                # #1296: two separate **Blocked by:** lines — #100 CLOSED,
                # #101 OPEN with a PR → stacks on the remaining #101.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100\n**Blocked by:** #101\n"}'
                ;;
            *)
                printf '%s' '{"state":"OPEN","labels":[],"body":""}'
                ;;
        esac
        exit 0
        ;;
    "pr list")
        if [[ "$pr_state" == "merged" ]]; then
            # No merged PRs in these fixtures — resolution uses gh issue view only.
            echo "[]"
            exit 0
        fi
        if [[ "$pr_state" == "open" ]]; then
            # Return one open PR for issue #101.
            printf '%s\n' '[{"url":"https://github.com/jakildev/IrredenEngine/pull/536","headRefName":"claude/101-work-branch","author":{"login":"bot"},"number":536,"body":"Closes #101"}]'
            exit 0
        fi
        echo "[]"
        exit 0
        ;;
    "api "*)
        label=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -f) shift; case "$1" in labels\[\]=*) label="${1#labels[]=}" ;; esac ;;
            esac
            shift || true
        done
        [[ -n "$label" ]] && printf '[{"name":"%s"}]\n' "$label" || echo "[]"
        exit 0
        ;;
    "issue edit"|"label "*)
        exit 0
        ;;
esac
exit 0
GHSTUB
chmod +x "$STUB_DIR/gh"

export PATH="$STUB_DIR:$PATH"

# --- T1: two refs, one closed → returns the one open PR ----------------------
echo "T1: multi-blocker (#100 closed, #101 open with PR) → returns PR for #101"
result=$("$FLEET_CLAIM" find-stackable-blockers 3001 2>/dev/null || true)
assert_nonempty "$result" "find-stackable-blockers returns PR line"
# The returned line should reference #101's branch
echo "  result: $result"
if echo "$result" | grep -q "claude/101-work-branch"; then
    PASS=$((PASS + 1))
    echo "  ok: result includes claude/101-work-branch"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: result does not include claude/101-work-branch"
fi

# --- T2: two open refs → empty output ----------------------------------------
echo "T2: two open blockers (#101, #102) → empty output"
result=$("$FLEET_CLAIM" find-stackable-blockers 3002 2>/dev/null || true)
assert_output "$result" "" "two open blockers → empty"

# --- T3: single closed ref → empty output (all resolved, claim directly) -----
echo "T3: single closed ref (#100) → empty output"
result=$("$FLEET_CLAIM" find-stackable-blockers 3003 2>/dev/null || true)
assert_output "$result" "" "single closed blocker → empty (no stackable needed)"

# --- T4: (none) blocker → empty output ---------------------------------------
echo "T4: (none) blocker → empty output"
result=$("$FLEET_CLAIM" find-stackable-blockers 3004 2>/dev/null || true)
assert_output "$result" "" "(none) blocker → empty"

# --- T5: multi-LINE blockers (#100 closed, #101 open) → returns #101 PR ------
echo "T5: two **Blocked by:** lines (#100 closed, #101 open) → returns PR for #101"
result=$("$FLEET_CLAIM" find-stackable-blockers 3005 2>/dev/null || true)
assert_nonempty "$result" "multi-line: find-stackable-blockers returns PR line"
echo "  result: $result"
if echo "$result" | grep -q "claude/101-work-branch"; then
    PASS=$((PASS + 1))
    echo "  ok: multi-line result includes claude/101-work-branch"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: multi-line result does not include claude/101-work-branch"
fi

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
