#!/usr/bin/env bash
# cmd_find_stackable_blockers resolves each blocker ref live before the
# single-blocker check. A task whose body says:
#   **Blocked by:** #NNN, #MMM
# where the first issue is CLOSED and the second has an open PR → returns
# that PR.
#
# Stub dispatches:
#   gh issue view N --json state --jq .state  → canned state
#   gh pr list --state merged --json headRefName --jq ...  → empty list
#   gh pr list --state open --json ...         → canned list with one PR
#   fetch_issue_info shape: gh issue view N --json state,labels,body
#
# Issue stubs (for fetch_issue_info and check_blockers lookup):
#   issue 3001: blocked by 100 (done) and 101 (still open) — two refs, one closed
#   issue 3002: blocked by 101 and 102                     — two open refs
#   issue 3003: blocked by 100                             — single ref, closed (all resolved)
#   issue 3004: blocked by nothing                         — no blocker
#
# Issue/PR state stubs:
#   issue 100: CLOSED
#   issue 101: OPEN (with an open PR claude/101-work-branch)
#   issue 102: OPEN (no open PR)

set -euo pipefail

# This suite exercises cmd_claim against the real (possibly-stale) main clone
# but does not care about clone freshness — disable the freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

# Every issue fixture below is fleet:sonnet, so an inherited FLEET_ROLE_MODEL
# from the launching pane would fail T9's claim on the model-tag gate instead
# of the base check it is grading — a suite must not inherit its class.
unset FLEET_ROLE_MODEL

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
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
# The base-branch fetch is a no-op here so the claim path stays hermetic.
if [[ "$1" == "fetch" ]]; then
    exit 0
fi
exec /usr/bin/git "$@"
GITSTUB
chmod +x "$STUB_DIR/git"

# --- gh stub -----------------------------------------------------------------
cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env python3
#
# Recognized invocations:
#   gh issue view N --json state,labels,body             -> full issue info
#   gh issue view N --repo R --json state --jq .state    -> state only
#   gh pr list --state merged --limit 30 --json headRefName --jq ...
#   gh pr list --state open --json url,headRefName,author,number,body --limit 200
#   gh pr list --state open --json ... --jq ... (claim side)
#   gh pr view N / gh pr diff N                          -> --stackable-on re-verify
#   gh api .../labels ... / gh issue edit ... / gh label ...  -> no-op
import os
import re
import sys

args = sys.argv[1:]
has_jq = False
issue_num = ""
pr_state = ""
for arg in args:
    if arg == "--jq":
        has_jq = True
    if arg == "merged":
        pr_state = "merged"
    if arg == "open":
        pr_state = "open"
    if not issue_num and re.fullmatch(r"[0-9]+", arg):
        issue_num = arg


def emit(text):
    sys.stdout.buffer.write(text.encode("utf-8"))
    sys.stdout.flush()


STATE_ONLY = {"100": "CLOSED", "101": "OPEN", "102": "OPEN"}

ISSUE_INFO = {
    "3001": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100 (done), #101 (still open)\n"}',
    "3002": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #101, #102\n"}',
    "3003": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100\n"}',
    "3004": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** (none)\n"}',
    # Two separate **Blocked by:** lines — issue 100 CLOSED, issue
    # 101 OPEN with a PR → stacks on the remaining issue.
    "3005": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #100\n**Blocked by:** #101\n"}',
    # The blocker ref names an issue-less open PR by its OWN
    # number (PR 540). Neither the branch arm nor the Closes arm
    # can resolve it — only the number arm can.
    "3006": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #540\n"}',
    # Negative control: a PR-shaped ref with no open PR of that
    # number → still empty, the arm is not a wildcard.
    "3007": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #777\n"}',
    # A number-matched base is still subject to filter (b) — PR
    # 541 carries fleet:wip.
    "3008": r'{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}],"body":"**Blocked by:** #541\n"}',
}
ISSUE_INFO_DEFAULT = r'{"state":"OPEN","labels":[],"body":""}'

# PR 536 is issue 101's PR (branch + Closes arms).
# PRs 540 / 541 are ISSUE-LESS: non-claude branch, no Closes ref -
# reachable only by the number arm. PR 541 is fleet:wip so filter
# (b) can be exercised on a number-matched base. Neither matches
# issue 101 (number, branch, and body all disagree), so the
# single-match contract of T1/T5 is unaffected.
OPEN_PR_LIST = r'[{"url":"https://github.com/jakildev/IrredenEngine/pull/536","headRefName":"claude/101-work-branch","author":{"login":"bot"},"number":536,"body":"Closes #101"},{"url":"https://github.com/jakildev/IrredenEngine/pull/540","headRefName":"audit/stage-select-dedup","author":{"login":"jakildev"},"number":540,"body":"Audit-driven, no backing issue."},{"url":"https://github.com/jakildev/IrredenEngine/pull/541","headRefName":"audit/wip-thing","author":{"login":"jakildev"},"number":541,"body":"No backing issue.","labels":[{"name":"fleet:wip"}]}]'

# claim --stackable-on base re-verify: state + head + labels.
PR_VIEW = {
    "901": r'{"state":"OPEN","headRefName":"claude/901-wip","labels":[{"name":"fleet:wip"}]}',
    "902": r'{"state":"OPEN","headRefName":"claude/902-empty","labels":[{"name":"fleet:queued"}]}',
    "903": r'{"state":"OPEN","headRefName":"claude/903-clean","labels":[{"name":"fleet:queued"}]}',
    "904": r'{"state":"OPEN","headRefName":"claude/904-difffail","labels":[{"name":"fleet:queued"}]}',
    # An approved base whose ONLY formerly-disqualifying label is
    # fleet:awaiting-base - the label is deliberately still present, so
    # the accept is graded against a live carrier, not a cleaned-up one.
    "905": r'{"state":"OPEN","headRefName":"claude/905-awaiting-base","labels":[{"name":"fleet:approved"},{"name":"fleet:awaiting-base"}]}',
}
PR_VIEW_DEFAULT = r'{"state":"OPEN","headRefName":"claude/x","labels":[]}'

verb = " ".join(args[:2])
if verb == "issue view":
    if has_jq:
        # check_blockers / find-stackable-blockers state-only lookup
        emit(STATE_ONLY.get(issue_num, "OPEN") + "\n")
        sys.exit(0)
    # fetch_issue_info: full body
    emit(ISSUE_INFO.get(issue_num, ISSUE_INFO_DEFAULT))
    sys.exit(0)
if verb == "pr list":
    if pr_state == "open":
        emit(OPEN_PR_LIST + "\n")
    else:
        emit("[]\n")
    sys.exit(0)
if verb == "pr view":
    # $3 is the PR id passed to --stackable-on.
    pr_id = args[2] if len(args) > 2 else ""
    emit(PR_VIEW.get(pr_id, PR_VIEW_DEFAULT))
    sys.exit(0)
if verb == "pr diff":
    # claim --stackable-on live diff: empty output = empty claim-commit,
    # non-empty = real diff, exit 1 = fetch failure (unverifiable base).
    pr_id = args[2] if len(args) > 2 else ""
    if pr_id == "902":
        pass
    elif pr_id == "903":
        emit("engine/render/x.cpp\n")
    elif pr_id == "904":
        sys.exit(1)
    elif pr_id == "905":
        emit("scripts/fleet/witness\n")
    else:
        emit("engine/x.cpp\n")
    sys.exit(0)
if args[:1] == ["api"]:
    label = ""
    i = 0
    while i < len(args):
        if args[i] == "-f" and i + 1 < len(args):
            i += 1
            if args[i].startswith("labels[]="):
                label = args[i][len("labels[]="):]
        i += 1
    if label:
        emit('[{"name":"%s"}]\n' % label)
    else:
        emit('[[{"name":"%s"}]]\n' % os.environ.get("FLEET_CLAIM_CANDIDATE", ""))
    sys.exit(0)
sys.exit(0)
GHSTUB
chmod +x "$STUB_DIR/gh"
# Native-Windows twin: find-stackable-blockers reaches `gh` from PYTHON
# (subprocess), which resolves it through shutil.which() and finds this `.bat`
# (a bare extensionless script is skipped). Inert on POSIX hosts.
# See scripts/fleet/CLAUDE.md's native-Windows PATHEXT rule.
cat > "$STUB_DIR/gh.bat" <<'BATEOF'
@echo off
python3 "%~dp0gh" %*
BATEOF

export PATH="$STUB_DIR:$PATH"

# --- T1: two refs, one closed → returns the one open PR ----------------------
echo "T1: multi-blocker (#100 closed, #101 open with PR) → returns PR for #101"
result=$("$FLEET_CLAIM" find-stackable-blockers 3001 2>/dev/null || true)
assert_nonempty "$result" "find-stackable-blockers returns PR line"
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

# --- T5: multi-LINE blockers (issue 100 closed, issue 101 open) → returns PR
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

# --- `Blocked by: #<PR>` — the ref names an issue-less open PR -------------
# The blocker PR has no backing issue, so branch_matches_issue and
# body_closes_issue are both False for it by construction; only the number arm
# can resolve the ref. Without that arm the finder prints nothing and the task
# is unpickable for the blocker's whole pre-merge window.
echo "T9: blocker ref IS an issue-less open PR's own number (#540) → returns PR 540"
result=$("$FLEET_CLAIM" find-stackable-blockers 3006 2>/dev/null || true)
assert_nonempty "$result" "PR-number blocker ref returns a PR line"
echo "  result: $result"
if echo "$result" | grep -q "audit/stage-select-dedup"; then
    PASS=$((PASS + 1))
    echo "  ok: result includes audit/stage-select-dedup (PR #540)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: result does not include audit/stage-select-dedup"
fi

echo "T10: PR-number blocker ref with no open PR of that number (#777) → empty"
result=$("$FLEET_CLAIM" find-stackable-blockers 3007 2>/dev/null || true)
assert_output "$result" "" "unmatched PR-number ref → empty (arm is not a wildcard)"

echo "T11: number-matched base still honors filter (b) — #541 is fleet:wip → empty"
result=$("$FLEET_CLAIM" find-stackable-blockers 3008 2>/dev/null || true)
assert_output "$result" "" "wip number-matched base → empty (offer/accept agree, #1751)"

# --- claim --stackable-on rejects an OPEN-but-unsafe base ------------------
# The base re-verify runs before any blocker/model/reservation gate, so an
# unsafe base refuses the claim outright regardless of the rest of the flow.
assert_refused() {
    local pr_id="$1" reason="$2" msg="$3"
    local err="$TMPROOT/refuse.err"
    if "$FLEET_CLAIM" claim 3001 worker-test --stackable-on "$pr_id" >/dev/null 2>"$err"; then
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg — claim SUCCEEDED on an unsafe base"
        return
    fi
    if grep -q "$reason" "$err"; then
        PASS=$((PASS + 1))
        echo "  ok: $msg (cited: $reason)"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg — refused but reason '$reason' not in stderr:"
        sed 's/^/        /' "$err"
    fi
}

echo "T6: claim --stackable-on an OPEN-but-WIP base (#901) → refused"
assert_refused 901 "not a stackable base (fleet:wip)" "WIP base refused"

echo "T7: claim --stackable-on an OPEN empty-claim base (#902, empty diff) → refused"
assert_refused 902 "not a stackable base (empty claim-commit)" "empty-claim base refused"

echo "T8: claim --stackable-on a base whose diff fetch fails (#904) → refused"
assert_refused 904 "refusing to stack on an unverifiable base" "unverifiable base refused"

# --- an approved base carrying only fleet:awaiting-base IS stackable -------
# The merger mints fleet:awaiting-base on every stacked PR whose base is still
# open, so rejecting it makes depth-2+ stacking impossible. Claimed against a
# base that still carries the label. Runs last because it is the only case
# here that leaves a claim behind.
echo "T9: claim --stackable-on an approved fleet:awaiting-base base (#905) → accepted"
t9_err="$TMPROOT/t9.err"
if "$FLEET_CLAIM" claim 3004 worker-test --stackable-on 905 >/dev/null 2>"$t9_err"; then
    PASS=$((PASS + 1))
    echo "  ok: awaiting-base base accepted"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: awaiting-base base refused:"
    sed 's/^/        /' "$t9_err"
fi
# Not vacuous: prove the claim took the STACKABLE path, not a silent fallback
# to master (claim-base reads the --stackable-on sidecar).
t9_base=$("$FLEET_CLAIM" claim-base 3004 2>/dev/null || true)
assert_output "$t9_base" "claude/905-awaiting-base" "claim recorded the awaiting-base PR as the stack base"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
