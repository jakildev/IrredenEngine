#!/usr/bin/env bash
# Tests for fleet-claim's pre-acquire claim gates: check_host_capability
# (issue-based, #1998) and check_no_foreign_review_claim (#2801).
#
# The gate refuses a `fleet:needs-gl-host` claim from a host that can't run
# the OpenGL backend. GL-capable hosts are {linux, windows}; macOS GL is 4.1
# (< the shaders' required 4.5), so a Metal host genuinely cannot build/run/
# verify the GL backend. Host is resolved via derive_host (FLEET_TEST_HOST
# seam); the label comes from `gh issue view`, stubbed here so the gate runs
# without a live GitHub round-trip. The gate is the claim-side backstop for
# the dispatcher's claimability filter (fleet_task_class.py).
#
# Covers:
#   - mac host refuses a fleet:needs-gl-host claim (exit 1)
#   - linux host passes the host gate (claim succeeds, exit 0)
#   - windows host passes the host gate (claim succeeds, exit 0)
#   - unknown host is fail-closed → refused (exit 1)
#   - issue without the label passes on a mac host (gate is opt-in, exit 0)
#   - gh failure soft-degrades to pass (exit 0)
#   - amending-claim (#2524): mac host refuses a fleet:needs-gl-host PR,
#     linux host passes it, an unlabeled PR passes on mac
#   - amending-claim (#2801): a fleet:reviewing-* claim held by ANOTHER agent
#     refuses the amend AND mutates no labels; same-host and cross-host
#     foreign claims both refuse; the claiming agent's OWN reviewing label is
#     a pass-through
#
# The #2801 arm asserts the PR's label set is untouched, not just the exit
# code: _acquire_label_on POSTs the fleet:amending-* label BEFORE it decides
# the lex-min, so a guard placed inside it would leave that label stranded on
# a PR the worker then abandons. The gate therefore has to run ahead of
# _cmd_pr_label_claim, and "no POST happened" is the assertion that pins it.

set -euo pipefail

# This suite exercises cmd_claim against the real (possibly-stale) main clone but
# does not care about clone freshness — disable the #1810 freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

# PASS/FAIL counters, ok/bad, and the summarize exit idiom (scripts/fleet's
# convention — don't re-copy them). summarize's "passed: N  failed: M" line is
# also what fleet-positive-control reads to score a control run; the
# hand-rolled counters this replaces printed a tally the tool could not parse,
# so the #2801 arms below had no scoreable positive control.
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

# Exit-code assertions stay local, built on ok/bad (lib_assert.sh's documented
# split: the assert_* family covers strings, tests define their own for exit
# codes and path existence).
assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

# Stub `gh` so check_host_capability reads canned JSON instead of hitting
# GitHub. Dispatches on the issue number passed via `gh issue view <N>`
# (PR numbers share the issues label namespace, so PRs go through the same
# arm).
#   2001 — carries fleet:needs-gl-host (GL-only task)
#   2002 — no host label (gate is opt-in)
#   2003 — gh failure (soft-degrade contract)
#   3001 — PR carrying fleet:needs-gl-host (GL-gated design-unblocked resume)
#   3002 — PR without the label
#   3003 — PR under ANOTHER agent's same-host review claim (mac-pool-9)
#   3004 — PR under the claiming agent's OWN review claim (mac-test-agent)
#   3005 — PR under another agent's CROSS-host review claim (linux-pool-2)
# Every label-mutating `gh api ... --method POST` is appended to $GH_POST_LOG
# so a test can assert the refuse path mutated nothing.
# The `api` arm emulates the cross-host fleet:claim-* lock acquire so a
# gate-passing claim runs through to success (echo the requested label back
# as the lex-min winner). All issues carry fleet:opus so a stray ambient
# FLEET_ROLE_MODEL=opus still passes the model gate ahead of the host gate.
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1 $2" in
    "issue view")
        issue_num="$3"
        case "$issue_num" in
            2001)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-gl-host"},{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":""}'
                ;;
            2002)
                echo '{"state":"OPEN","labels":[{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":""}'
                ;;
            2003)
                exit 1
                ;;
            3001)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"},{"name":"fleet:needs-gl-host"}],"body":""}'
                ;;
            3002)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"}],"body":""}'
                ;;
            3003)
                echo '{"state":"OPEN","labels":[{"name":"fleet:has-nits"},{"name":"fleet:reviewing-mac-pool-9"}],"body":""}'
                ;;
            3004)
                echo '{"state":"OPEN","labels":[{"name":"fleet:has-nits"},{"name":"fleet:reviewing-mac-test-agent"}],"body":""}'
                ;;
            3005)
                echo '{"state":"OPEN","labels":[{"name":"fleet:has-nits"},{"name":"fleet:reviewing-linux-pool-2"}],"body":""}'
                ;;
            *)
                echo '{"state":"OPEN","labels":[],"body":""}'
                ;;
        esac
        exit 0
        ;;
    "api "*)
        # gh api repos/.../issues/<N>/labels --method POST -f "labels[]=X" →
        # echo the requested label back as a single-element JSON array so the
        # lex-min winner is us.
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
            [[ -n "${GH_POST_LOG:-}" ]] && printf '%s\n' "$label" >> "$GH_POST_LOG"
            printf '[{"name":"%s"}]\n' "$label"
        else
            echo '[]'
        fi
        exit 0
        ;;
    "issue edit"|"label "*|"pr "*)
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

# --- T1: mac host refuses a fleet:needs-gl-host claim ------------------------
echo "T1: mac host refuses fleet:needs-gl-host claim"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:needs-gl-host → exit 1"
release_quiet 2001

# --- T2: linux host passes the host gate ------------------------------------
echo "T2: linux host passes the host gate (claim succeeds)"
actual=0; FLEET_TEST_HOST=linux FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "linux + fleet:needs-gl-host → exit 0"
release_quiet 2001

# --- T3: windows host passes the host gate ----------------------------------
echo "T3: windows host passes the host gate (claim succeeds)"
actual=0; FLEET_TEST_HOST=windows FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "windows + fleet:needs-gl-host → exit 0"
release_quiet 2001

# --- T4: unknown host is fail-closed ----------------------------------------
echo "T4: unknown host is fail-closed (refused)"
actual=0; FLEET_TEST_HOST=unknown FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "unknown host + fleet:needs-gl-host → exit 1"
release_quiet 2001

# --- T5: issue without the label passes on a mac host (gate opt-in) ---------
echo "T5: mac host passes an issue without fleet:needs-gl-host"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + no host label → exit 0 (opt-in)"
release_quiet 2002

# --- T6: gh failure soft-degrades to pass -----------------------------------
echo "T6: gh failure soft-degrades to pass"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2003 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "gh failure → soft-pass exit 0"
release_quiet 2003

# --- T7: mac host refuses amending-claim on a fleet:needs-gl-host PR --------
echo "T7: mac host refuses amending-claim on a fleet:needs-gl-host PR"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:needs-gl-host PR → amending-claim exit 1"

# --- T8: linux host passes amending-claim on the same PR --------------------
echo "T8: linux host passes amending-claim on a fleet:needs-gl-host PR"
actual=0; FLEET_TEST_HOST=linux FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "linux + fleet:needs-gl-host PR → amending-claim exit 0"

# --- T9: unlabeled PR passes amending-claim on a mac host -------------------
echo "T9: mac host passes amending-claim on a PR without the label"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + no host label PR → amending-claim exit 0"

# --- #2801: cross-lane review-claim gate on amending-claim ------------------
#
# fleet:amending-* and fleet:reviewing-* are disjoint label namespaces, so
# _acquire_label_on's lex-min tie-break — which filters the label list to
# startswith(prefix) — is structurally blind to a live review claim and
# grants the amend. The worker then force-pushes out from under the reviewer.

export GH_POST_LOG="$TMPROOT/gh-post.log"

assert_no_label_post() {
    local msg="$1"
    if [[ ! -s "$GH_POST_LOG" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        labels POSTed on the refuse path:"
        sed 's/^/          /' "$GH_POST_LOG"
    fi
}

# --- T10: another agent's same-host review claim refuses the amend ----------
echo "T10: amending-claim refused under another agent's fleet:reviewing-*"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3003 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:reviewing-mac-pool-9 → amending-claim exit 1"

# --- T11: ...and the refuse path mutated no labels --------------------------
# The failure mode is a mutation that outlives the refusal, so assert the
# label set, not just the exit code. _acquire_label_on POSTs before it
# decides; the gate must therefore run ahead of _cmd_pr_label_claim.
echo "T11: the refusal mutates no labels"
assert_no_label_post "refused amending-claim POSTed no label"

# --- T12: the agent's OWN review claim is a pass-through --------------------
# An agent legitimately holding both (it reviewed, then picked the feedback
# up itself) is not a cross-lane race.
echo "T12: own fleet:reviewing-<host>-<agent> does not block the amend"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + own fleet:reviewing-mac-test-agent → amending-claim exit 0"

# --- T13: a cross-host foreign review claim refuses too ---------------------
# The reviewer may be on another machine; the guard keys on the label, not on
# whether this host could have minted it.
echo "T13: another host's fleet:reviewing-* also refuses"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3005 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:reviewing-linux-pool-2 → amending-claim exit 1"
assert_no_label_post "cross-host refusal POSTed no label"

# --- T14: an unrelated PR is unaffected by the new gate ---------------------
# Guards against the gate over-refusing: 3002 carries no reviewing label.
echo "T14: PR with no review claim still amends"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + no fleet:reviewing-* → amending-claim exit 0"

# --- T15: fidelity check for T11/T13's "no POST" assertion ------------------
# assert_no_label_post is an emptiness test, so it passes for free if the
# stub's GH_POST_LOG wiring ever breaks. T14 just granted a claim, which MUST
# have POSTed the fleet:amending-* label — so a non-empty log here is what
# makes the empty log above evidence rather than a no-op.
echo "T15: the POST log records a granted claim (fidelity check)"
if grep -q '^fleet:amending-mac-test-agent$' "$GH_POST_LOG" 2>/dev/null; then
    ok "granted amending-claim POSTed fleet:amending-mac-test-agent"
else
    bad "granted amending-claim left no POST in the log — the GH_POST_LOG wiring is broken, so T11/T13 prove nothing"
fi

summarize "fleet-claim pre-acquire gates"
