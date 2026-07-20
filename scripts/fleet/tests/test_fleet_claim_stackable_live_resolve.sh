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

# This suite exercises cmd_claim against the real (possibly-stale) main clone but
# does not care about clone freshness — disable the #1810 freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

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
# #2447 ancestry gate: the base-branch fetch is a no-op here, and
# merge-base --is-ancestor <sha> <base-oid> is answered from a fixture map
# (exit 0 = contained, 1 = not contained) so the check stays hermetic.
if [[ "$1" == "fetch" ]]; then
    exit 0
fi
if [[ "$1" == "merge-base" && "$2" == "--is-ancestor" ]]; then
    case "$3:$4" in
        sha100:oid906) exit 0 ;;  # #100 contained in base #906's head
        *)             exit 1 ;;  # e.g. sha100:oid905 → base #905 lacks it
    esac
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
        if [[ "$has_jq" -eq 1 && "$*" == *"--json body"* ]]; then
            # #2447 ancestry gate: the base issue's raw blocker body. #905/#906
            # are both blocked by merged #100; the open ancestor #101 carries no
            # further blocker so the recursion terminates.
            case "$issue_num" in
                905) echo "**Blocked by:** #100" ;;
                906) echo "**Blocked by:** #100" ;;
                *)   echo "" ;;
            esac
            exit 0
        fi
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
            if [[ "$*" == *mergeCommit* ]]; then
                # #2447 ancestry gate frontier: #100 merged as claude/100-m with
                # squash sha100. Only the ancestry call requests mergeCommit; the
                # blocker/find-stackable calls (headRefName only) still get [].
                printf '%s\n' '[{"number":100,"headRefName":"claude/100-m","mergeCommit":{"oid":"sha100"}}]'
            else
                echo "[]"
            fi
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
    "pr view")
        if [[ "$*" == *headRefOid* ]]; then
            # #2447 ancestry gate: base head oid for git merge-base --is-ancestor.
            case "$3" in
                905) echo "oid905" ;;
                906) echo "oid906" ;;
                *)   echo "oidx"   ;;
            esac
            exit 0
        fi
        # claim --stackable-on base re-verify (#1751): state + head + labels.
        # $3 is the PR id passed to --stackable-on.
        case "$3" in
            901) printf '%s' '{"state":"OPEN","headRefName":"claude/901-wip","labels":[{"name":"fleet:wip"}]}' ;;
            902) printf '%s' '{"state":"OPEN","headRefName":"claude/902-empty","labels":[{"name":"fleet:queued"}]}' ;;
            903) printf '%s' '{"state":"OPEN","headRefName":"claude/903-clean","labels":[{"name":"fleet:queued"}]}' ;;
            904) printf '%s' '{"state":"OPEN","headRefName":"claude/904-difffail","labels":[{"name":"fleet:queued"}]}' ;;
            905) printf '%s' '{"state":"OPEN","headRefName":"claude/905-anc","labels":[{"name":"fleet:queued"}]}' ;;
            906) printf '%s' '{"state":"OPEN","headRefName":"claude/906-anc","labels":[{"name":"fleet:queued"}]}' ;;
            *)   printf '%s' '{"state":"OPEN","headRefName":"claude/x","labels":[]}' ;;
        esac
        exit 0
        ;;
    "pr diff")
        # claim --stackable-on live diff: empty output = empty claim-commit,
        # non-empty = real diff, exit 1 = fetch failure (unverifiable base).
        case "$3" in
            902) : ;;                                  # empty diff (claim-commit only)
            903) printf '%s\n' "engine/render/x.cpp" ;;  # real non-empty diff
            904) exit 1 ;;                             # fetch failure
            *)   printf '%s\n' "engine/x.cpp" ;;
        esac
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

# --- #1751: claim --stackable-on rejects an OPEN-but-unsafe base -------------
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

# --- #2447: claim --stackable-on rejects a base missing a merged ancestor ----
# The base passes labels + diff (unsafe_base_reason clean) but its head lacks
# the squash of a merged blocker in its ancestry, so the ancestry gate refuses.
echo "T9: claim --stackable-on a base missing a merged ancestor (#905) → refused"
assert_refused 905 "missing merged ancestor #100" "ancestry-hole base refused"

echo "T10: claim --stackable-on a base that contains its merged ancestor (#906) → ancestry passes"
pass_err="$TMPROOT/anc_pass.err"
"$FLEET_CLAIM" claim 3006 worker-test --stackable-on 906 >/dev/null 2>"$pass_err" \
    && anc_rc=0 || anc_rc=$?
if grep -qE "missing merged ancestor|base-ancestry check failed|not a stackable base" "$pass_err"; then
    FAIL=$((FAIL + 1))
    echo "  FAIL: contained base wrongly refused at the base/ancestry gate:"
    sed 's/^/        /' "$pass_err"
else
    PASS=$((PASS + 1))
    echo "  ok: contained base cleared the ancestry gate (claim rc=$anc_rc)"
fi

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
