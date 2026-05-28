#!/usr/bin/env bash
# Tests for fleet-claim's check_blockers gate (issue-based).
#
# Regression coverage for #1281: the blocker parser extracts every `#N`
# reference from the **Blocked by:** field, including parenthetical PR
# references like `#100 (PR #200 must merge — context)`. Before the fix
# landed in #1280, the gate demanded state == CLOSED, so a parenthetical
# PR #N (which `gh issue view` reports as state == MERGED) was rejected
# even though the PR was already in. The fix accepts CLOSED or MERGED.
#
# These tests stub `gh` so check_blockers reads canned JSON instead of
# hitting GitHub. The stub dispatches on the subcommand + presence of
# `--jq` to distinguish:
#   - fetch_issue_info: `gh issue view N --json state,labels,body`
#   - check_blockers ref lookup: `gh issue view N --json state --jq .state`
#   - check_blockers PR-URL lookup: `gh pr view URL --json state --jq .state`
#
# Covers:
#   - parenthetical PR ref, issue CLOSED + PR MERGED → claim succeeds (the bug)
#   - parenthetical PR ref, PR still OPEN → claim fails
#   - "(none — commentary)" form → no-op pass
#   - PR-URL form MERGED → pass
#   - PR-URL form OPEN → fail
#   - bare `#N` issue OPEN → fail
#   - no Blocked-by line → pass

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

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# Stub for check_blockers tests.
#
# Recognised invocations:
#   gh issue view <N> --repo R --json state,labels,body         → full info
#   gh issue view <N> --repo R --json state --jq .state         → state only
#   gh pr view <URL> --json state --jq .state                   → state only
#   gh pr list --repo R --state open --json ... --jq ...        → []
#   gh api repos/.../issues/N/labels --method POST -f labels[]= → echo back
#   gh issue edit ... / gh label ...                            → no-op

has_jq=0
for arg in "$@"; do
    [[ "$arg" == "--jq" ]] && has_jq=1
done

case "$1 $2" in
    "issue view")
        issue_num="$3"
        if [[ "$has_jq" -eq 1 ]]; then
            # check_blockers state-only lookup for `#N` references.
            case "$issue_num" in
                100) echo "CLOSED" ;;   # issue, resolved
                101) echo "OPEN" ;;     # issue, still open
                200) echo "MERGED" ;;   # PR, merged
                201) echo "OPEN" ;;     # PR, still open
                *)   echo "OPEN" ;;
            esac
            exit 0
        fi
        # fetch_issue_info: full state+labels+body for the target issue.
        case "$issue_num" in
            2001)
                # The #1281 case: parenthetical PR ref, both resolved.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\n**Blocked by:** #100 (PR #200 must merge — context)\n"}'
                ;;
            2002)
                # Same shape but the PR is still open.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100 (PR #201 must merge — context)\n"}'
                ;;
            2003)
                # `(none …)` form — no blocker.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** (none — independent task)\n"}'
                ;;
            2004)
                # PR-URL form, MERGED.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/200\n"}'
                ;;
            2005)
                # PR-URL form, OPEN.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/201\n"}'
                ;;
            2006)
                # Bare `#N` issue ref, still open.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #101\n"}'
                ;;
            2007)
                # No Blocked-by line at all.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\nIndependent task.\n"}'
                ;;
            *)
                printf '%s' '{"state":"OPEN","labels":[],"body":""}'
                ;;
        esac
        exit 0
        ;;
    "pr view")
        url="$3"
        case "$url" in
            *pull/200) echo "MERGED" ;;
            *pull/201) echo "OPEN" ;;
            *)         echo "OPEN" ;;
        esac
        exit 0
        ;;
    "pr list")
        # Open-PR sanity check after the blocker gate clears. The caller
        # passes `--jq ".[] | select(...) | .number" | head -1`; an empty
        # array filtered by that jq produces no output, so emit nothing.
        exit 0
        ;;
    "api "*)
        # Cross-host claim-label acquire — echo back the requested label so
        # the lex-min tie-break sees us as the sole holder.
        label=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -f) shift
                    case "$1" in
                        labels\[\]=*) label="${1#labels[]=}" ;;
                    esac
                    ;;
            esac
            shift || true
        done
        if [[ -n "$label" ]]; then
            printf '[{"name":"%s"}]\n' "$label"
        else
            echo "[]"
        fi
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

release_quiet() {
    "$FLEET_CLAIM" release "$1" >/dev/null 2>&1 || true
}

# --- T1: parenthetical PR ref — #issue CLOSED + #pr MERGED (the #1281 case) -
echo "T1: parenthetical PR ref — #issue CLOSED + #pr MERGED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "#100 CLOSED + #200 MERGED via parenthetical → exit 0"
release_quiet 2001

# --- T2: same shape but the PR is still OPEN → fail -------------------------
echo "T2: parenthetical PR ref — #pr still OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "#100 CLOSED + #201 OPEN via parenthetical → exit 1"

# --- T3: (none — commentary) → pass ----------------------------------------
echo "T3: '(none — commentary)' → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2003 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "(none …) form bypasses gate → exit 0"
release_quiet 2003

# --- T4: PR URL form, MERGED → pass ----------------------------------------
echo "T4: PR-URL form, MERGED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "PR-URL MERGED → exit 0"
release_quiet 2004

# --- T5: PR URL form, OPEN → fail ------------------------------------------
echo "T5: PR-URL form, OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2005 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "PR-URL OPEN → exit 1"

# --- T6: bare `#N` issue OPEN → fail ----------------------------------------
echo "T6: bare '#N' issue OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2006 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "#101 OPEN → exit 1"

# --- T7: no Blocked-by line → pass -----------------------------------------
echo "T7: no Blocked-by line → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2007 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "missing field bypasses gate → exit 0"
release_quiet 2007

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
