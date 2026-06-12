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
#   - parenthetical PR ref, PR CLOSED-abandoned → claim succeeds (documents
#     current `#N`-form behavior: gate accepts CLOSED|MERGED, so an abandoned
#     PR matched via `#N` passes. Note this differs from the explicit URL
#     form, which accepts MERGED only — if abandoned-PR refs ever need to
#     fail the gate, the `#N` branch in fleet-claim.check_blockers needs
#     to differentiate PR-state CLOSED from issue-state CLOSED.)
#   - cross-repo ref `[owner/]Repo#N`, CLOSED in the referenced repo → pass;
#     still OPEN in the referenced repo → fail (#1522 — the gate routes the
#     state probe to the referenced repo, not the issue's own).

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
#
# Argument parsing scans for the first bare positive integer rather than
# trusting positional `$3`. fleet-claim today calls `gh issue view N --repo
# R --json …`, but the stub stays valid if the call form ever shifts (e.g.
# `gh issue view --repo R N …`) — otherwise the stub silently falls
# through to the default `OPEN` branch and the tests pass for the wrong
# reason.

has_jq=0
issue_num=""
pr_url=""
repo=""        # captured from `--repo R` so cross-repo refs (#1522) can be
prev=""        # routed-checked: the same #N resolves differently per repo.
for arg in "$@"; do
    [[ "$arg" == "--jq" ]] && has_jq=1
    [[ "$prev" == "--repo" ]] && repo="$arg"
    if [[ -z "$issue_num" && "$arg" =~ ^[0-9]+$ ]]; then
        issue_num="$arg"
    fi
    if [[ -z "$pr_url" && "$arg" == https://github.com/*/pull/* ]]; then
        pr_url="$arg"
    fi
    prev="$arg"
done

case "$1 $2" in
    "issue view")
        if [[ "$has_jq" -eq 1 ]]; then
            # check_blockers state-only lookup for `#N` references.
            case "$issue_num" in
                100) echo "CLOSED" ;;   # issue, resolved
                101) echo "OPEN" ;;     # issue, still open
                200) echo "MERGED" ;;   # PR, merged
                201) echo "OPEN" ;;     # PR, still open
                202) echo "CLOSED" ;;   # PR, abandoned (closed without merge)
                125)
                    # #1522 cross-repo routing probe: CLOSED only when the check
                    # is routed to the *game* repo (the referenced repo); OPEN
                    # if it's mis-routed to the issue's own (engine) repo.
                    case "$repo" in
                        jakildev/irreden) echo "CLOSED" ;;
                        *)                 echo "OPEN" ;;
                    esac ;;
                126) echo "OPEN" ;;     # cross-repo (game) ref, still open
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
            2008)
                # Parenthetical PR ref, PR closed without merging.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100 (PR #202 abandoned)\n"}'
                ;;
            2009)
                # #1326: dependency declared only as `## Blocked on #N` header
                # prose (no **Blocked by:** field), the referenced issue OPEN.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\n## Blocked on #101\n\nWork.\n"}'
                ;;
            2010)
                # Header prose, referenced issue CLOSED → no longer a blocker.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"Blocked on #100\n"}'
                ;;
            2011)
                # Header prose with no #N / PR reference → not a real blocker.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Blocked on the redesign\n"}'
                ;;
            2012)
                # Canonical field wins over header prose: field says (none),
                # so the `## Blocked on #101` header must be ignored.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** (none — independent)\n\n## Blocked on #101\n"}'
                ;;
            2013)
                # #1296: two separate **Blocked by:** lines — the gate unions
                # them; #101 is still OPEN so the claim must be blocked.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100\n**Blocked by:** #101\n"}'
                ;;
            2014)
                # #1423: inline-bold form — **Blocked by: #N (label)** mid-line,
                # referenced issue #101 still OPEN → claim must be blocked.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Phase 3 of 4** · **Blocked by: #101 (Phase 2)**\n"}'
                ;;
            2015)
                # #1423: inline-bold form, referenced issue #100 CLOSED → pass.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Phase 3 of 4** · **Blocked by: #100 (Phase 2)**\n"}'
                ;;
            2016)
                # #1423: inline-bold form with no #N/PR ref — must not gate.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Blocked by: the redesign**\n"}'
                ;;
            2017)
                # #1522: cross-repo blocker in another repo (owner-qualified),
                # CLOSED there → claim succeeds once routed to the right repo.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** jakildev/irreden#125\n"}'
                ;;
            2018)
                # #1522: cross-repo blocker (bare repo qualifier), still OPEN in
                # the referenced repo → claim fails.
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** irreden#126\n"}'
                ;;
            *)
                printf '%s' '{"state":"OPEN","labels":[],"body":""}'
                ;;
        esac
        exit 0
        ;;
    "pr view")
        case "$pr_url" in
            *pull/200) echo "MERGED" ;;
            *pull/201) echo "OPEN" ;;
            *pull/202) echo "CLOSED" ;;
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

# --- T8: parenthetical PR ref — PR CLOSED-abandoned → claim succeeds --------
# Documents current behavior — the `#N` branch accepts CLOSED|MERGED, so an
# abandoned PR matched via `#N` passes the gate. Contrast with the URL form
# (T5, OPEN → fail) which only accepts MERGED. If fleet ever needs to reject
# closed-abandoned PR refs uniformly, the #N branch in check_blockers needs
# to detect PR-state CLOSED separately from issue-state CLOSED — at which
# point this test's expectation flips to exit 1.
echo "T8: parenthetical PR ref — PR CLOSED-abandoned → claim succeeds (current behavior)"
actual=0; "$FLEET_CLAIM" claim 2008 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "#100 CLOSED + #202 CLOSED (abandoned) → exit 0"
release_quiet 2008

# --- T9: header-prose blocker — `## Blocked on #N`, #N OPEN → fail ----------
# #1326: a dependency declared only in a `Blocked on #N` header (no
# **Blocked by:** field) used to slip through as claimable. The gate now reads
# the header-prose form.
echo "T9: header prose '## Blocked on #101' — #101 OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2009 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "header-prose #101 OPEN → exit 1"

# --- T10: header-prose blocker — referenced issue CLOSED → pass --------------
echo "T10: header prose 'Blocked on #100' — #100 CLOSED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2010 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "header-prose #100 CLOSED → exit 0"
release_quiet 2010

# --- T11: header prose without a #N / PR ref → not a gate --------------------
echo "T11: header prose 'Blocked on the redesign' (no ref) → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2011 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "ref-less header prose bypasses gate → exit 0"
release_quiet 2011

# --- T12: canonical field wins over header prose ----------------------------
echo "T12: field '(none)' overrides a '## Blocked on #101' header → succeeds"
actual=0; "$FLEET_CLAIM" claim 2012 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "field (none) takes precedence over header prose → exit 0"
release_quiet 2012

# --- T13: multi-line **Blocked by:** — a later ref still OPEN → fail (#1296) -
echo "T13: two **Blocked by:** lines (#100 CLOSED, #101 OPEN) → claim fails"
actual=0; "$FLEET_CLAIM" claim 2013 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "multi-line blocked-by, #101 OPEN → exit 1"

# --- T14: inline-bold form — #101 OPEN → fail (#1423) -----------------------
echo "T14: inline-bold '**Blocked by: #101 (Phase 2)**' — #101 OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2014 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "inline-bold #101 OPEN → exit 1"

# --- T15: inline-bold form — #100 CLOSED → pass (#1423) ---------------------
echo "T15: inline-bold '**Blocked by: #100 (Phase 2)**' — #100 CLOSED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2015 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "inline-bold #100 CLOSED → exit 0"
release_quiet 2015

# --- T16: inline-bold with no #N/PR ref — not a gate (#1423) ----------------
echo "T16: inline-bold 'Blocked by: the redesign' (no ref) → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2016 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "inline-bold no-ref bypasses gate → exit 0"
release_quiet 2016

# --- T17: cross-repo blocker, CLOSED in the referenced repo → succeeds (#1522)
# The same #125 reads OPEN in engine but CLOSED in game; the gate must route
# `jakildev/irreden#125` to game (where it's CLOSED) and let the claim through.
# Before #1522 it checked engine (OPEN) and froze the task out forever.
echo "T17: cross-repo 'jakildev/irreden#125' CLOSED in game → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2017 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "cross-repo game#125 CLOSED → exit 0 (routed to game, not engine)"
release_quiet 2017

# --- T18: cross-repo blocker, still OPEN in the referenced repo → fails (#1522)
echo "T18: cross-repo 'irreden#126' OPEN in game → claim fails"
actual=0; "$FLEET_CLAIM" claim 2018 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "cross-repo game#126 OPEN → exit 1"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
