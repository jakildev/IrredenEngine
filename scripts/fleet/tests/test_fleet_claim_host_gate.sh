#!/usr/bin/env bash
# Tests for fleet-claim's pre-acquire claim gates: check_host_capability,
# _amend_live_feedback_gate, and the generalized cross-lane gate.
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
#   - amending-claim: mac host refuses a fleet:needs-gl-host PR,
#     linux host passes it, an unlabeled PR passes on mac
#   - amending-claim: a fleet:reviewing-* claim held by ANOTHER agent
#     refuses the amend AND mutates no labels; same-host and cross-host
#     foreign claims both refuse; the claiming agent's OWN reviewing label is
#     a pass-through
#   - resolving-claim: the same four properties on the
#     semantic-conflict lane. fleet:reviewing-* is disjoint from
#     fleet:resolving-* exactly as it is from fleet:amending-*, and step 1c
#     force-pushes too, so the gate has to cover both callers
#   - fleet:needs-macos-host: claim and amending-claim refuse off mac with
#     no label POST, pass on mac; review-claim passes on windows; a PR
#     carrying both host labels refuses everywhere
#
# These arms assert the PR's label set is untouched, not just the
# exit code: _acquire_label_on POSTs the fleet:amending-* / fleet:resolving-*
# label BEFORE it decides the lex-min, so a guard placed inside it would leave
# that label stranded on a PR the worker then abandons. The gate therefore has
# to run ahead of _cmd_pr_label_claim, and "no POST happened" is the assertion
# that pins it.

set -euo pipefail

# This suite exercises cmd_claim against the real (possibly-stale) main clone but
# does not care about clone freshness — disable the freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

# PASS/FAIL counters, ok/bad, and the summarize exit idiom (scripts/fleet's
# convention — don't re-copy them). summarize's "passed: N  failed: M" line is
# also what fleet-positive-control reads to score a control run.
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
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_AMEND_SNAPSHOTS_DIR="$TMPROOT/amend-snapshots"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" "$FLEET_RESERVATIONS_DIR"

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
#   3006 — GL-gated PR under the claiming agent's incumbent amend claim
#   3102 — CONFLICTING PR, no review claim (resolving lane grant path)
#   3103 — conflicted PR under ANOTHER agent's same-host review claim
#   3104 — conflicted PR under the claiming agent's OWN review claim
#   3105 — conflicted PR under another agent's CROSS-host review claim
#   2201 — issue carrying fleet:needs-macos-host
#   3201 — PR carrying fleet:needs-macos-host (macOS-only residual)
#   3202 — PR carrying both host labels (contradictory; refused everywhere)
#   3301-3311 — live amend-tier and park-precedence fixtures
#   3312 — failed live-label fetch
# Every label-mutating `gh api ... --method POST` is appended to $GH_POST_LOG
# so a test can assert the refuse path mutated nothing.
# The `api` arm emulates the cross-host fleet:claim-* lock acquire so a
# gate-passing claim runs through to success (echo the requested label back
# as the lex-min winner). All issues carry fleet:opus so a stray ambient
# FLEET_ROLE_MODEL=opus still passes the model gate ahead of the host gate.
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

# Shared fixture bodies, exported so the quoted stub heredoc reads them from
# the environment (it cannot interpolate). SYMMETRIC_BODY is the drift pin:
# `test_scout_gl_host_backstop.py` asserts the SAME text against the
# python `_body_backend_symmetric`, so the bash and python halves of one
# predicate are pinned to one input rather than two hand-kept copies. Keep both
# ASCII and quote-free — they are spliced into the stub's JSON.
export SYMMETRIC_BODY="The CAST bridge drops the sub-cell frac in c_resolve_per_axis_screen_depth.glsl; the Metal twin c_resolve_per_axis_screen_depth.metal is identical."
export GL_ONLY_BODY="The regression lives in src/opengl/opengl_render_impl.cpp and reproduces only under the GL backend."

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
            3006)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-gl-host"},{"name":"fleet:amending-mac-test-agent"}],"body":""}'
                ;;
            3102)
                echo '{"state":"OPEN","labels":[{"name":"fleet:semantic-conflict"}],"body":""}'
                ;;
            3103)
                echo '{"state":"OPEN","labels":[{"name":"fleet:semantic-conflict"},{"name":"fleet:reviewing-mac-pool-9"}],"body":""}'
                ;;
            3104)
                echo '{"state":"OPEN","labels":[{"name":"fleet:semantic-conflict"},{"name":"fleet:reviewing-mac-test-agent"}],"body":""}'
                ;;
            3105)
                echo '{"state":"OPEN","labels":[{"name":"fleet:semantic-conflict"},{"name":"fleet:reviewing-linux-pool-2"}],"body":""}'
                ;;
            2004)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-gl-host"},{"name":"fleet:backend-symmetric"},{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":""}'
                ;;
            2005)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-gl-host"},{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":"'"$SYMMETRIC_BODY"'"}'
                ;;
            2006)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-gl-host"},{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":"'"$GL_ONLY_BODY"'"}'
                ;;
            2007)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"},{"name":"fleet:needs-gl-host"},{"name":"fleet:backend-symmetric"}],"body":""}'
                ;;
            2201)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-macos-host"},{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":""}'
                ;;
            3201)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"},{"name":"fleet:needs-macos-host"}],"body":""}'
                ;;
            3202)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"},{"name":"fleet:needs-gl-host"},{"name":"fleet:needs-macos-host"}],"body":""}'
                ;;
            3301)
                echo '{"state":"OPEN","labels":[{"name":"fleet:wip"}],"body":""}'
                ;;
            3302)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-fix"}],"body":""}'
                ;;
            3303)
                echo '{"state":"OPEN","labels":[{"name":"human:needs-fix"},{"name":"fleet:design-blocked"}],"body":""}'
                ;;
            3304)
                echo '{"state":"OPEN","labels":[{"name":"human:blocker"},{"name":"fleet:awaiting-infra"}],"body":""}'
                ;;
            3305)
                echo '{"state":"OPEN","labels":[{"name":"fleet:design-unblocked"},{"name":"fleet:wip"}],"body":""}'
                ;;
            3306)
                echo '{"state":"OPEN","labels":[{"name":"human:re-review"}],"body":""}'
                ;;
            3307)
                echo '{"state":"OPEN","labels":[{"name":"fleet:needs-fix"},{"name":"fleet:design-blocked"}],"body":""}'
                ;;
            3308)
                echo '{"state":"OPEN","labels":[{"name":"human:needs-fix"},{"name":"human:wip"}],"body":""}'
                ;;
            3309)
                echo '{"state":"OPEN","labels":[{"name":"fleet:human-amending"}],"body":""}'
                ;;
            3310)
                echo '{"state":"OPEN","labels":[{"name":"fleet:human-amending"},{"name":"fleet:amending-mac-test-agent"}],"body":""}'
                ;;
            3311)
                echo '{"state":"OPEN","labels":[{"name":"fleet:amending-mac-test-agent"}],"body":""}'
                ;;
            3312)
                exit 1
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
            printf '[[{"name":"%s"}]]\n' "${FLEET_CLAIM_CANDIDATE:-}"
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

# --- cross-lane review-claim gate on amending-claim ------------------------
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

# --- the backend-symmetric narrowing -----------------------------------------
# The gate's premise justifies refusing the GL half, not a whole task whose
# OTHER half is natively Metal-verifiable.

# --- T10: mac claims a backend-symmetric GL task -----------------------------
echo "T16: mac host claims a backend-symmetric fleet:needs-gl-host task"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + gl-host + backend-symmetric label → exit 0"
release_quiet 2004

# --- T11: bash body backstop, no discriminator label -------------------------
# Kills the claim->refuse->release churn: the scout infers backend_symmetric
# from the body, so dispatch would offer this task; without the same backstop
# here the claim would refuse it. Shares its body text with the scout suite's
# python-side case so the two implementations of one predicate cannot drift.
echo "T17: mac host claims on the body backstop alone (no discriminator label)"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2005 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + gl-host + body cites .glsl and .metal → exit 0"
release_quiet 2005

# --- T12: opposite-direction lock at the claim layer --------------------------
# The false-negative direction must not reopen: a body naming a
# GL-only source path is NOT backend-symmetric, so mac still refuses.
echo "T18: mac still refuses a GL-only-bodied task (opposite-direction lock)"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2006 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + gl-host + GL-only body → exit 1"
release_quiet 2006

# --- T13: PR path is deliberately NOT narrowed -------------------------------
# Pins the asymmetry itself. On a PR the gl-host label describes the REMAINING
# work, so a task-axis discriminator says nothing about it. Without this, the
# apparent inconsistency invites a future hand-fix that silently reopens it.
echo "T19: mac still refuses amending-claim on a PR carrying BOTH labels"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 2007 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + gl-host + backend-symmetric PR → amending-claim exit 1"

# --- T14: fail-closed unknown host survives the narrowing --------------------
# The discriminator opens only the mac door — asserted, not merely commented.
echo "T20: unknown host still refused despite backend-symmetric"
actual=0; FLEET_TEST_HOST=freebsd FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "freebsd + gl-host + backend-symmetric → exit 1"
release_quiet 2004

# --- the same cross-lane gate on resolving-claim ----------------------------
#
# cmd_resolving_claim is a bare delegation to _cmd_pr_label_claim, so the
# cross-lane gate on cmd_amending_claim does not automatically cover it.
# fleet:reviewing-* is disjoint from fleet:resolving-* just as it is from
# fleet:amending-*, so the lex-min tie-break
# grants the resolve, role-worker step 1c rebases, and fleet-pr-amend-push
# force-pushes out from under the in-flight review. These mirror T10-T15
# one-for-one on the other lane — kept as a parallel block rather than a
# shared helper so a future change to one lane cannot silently re-scope the
# other's coverage.

# --- T21: another agent's same-host review claim refuses the resolve --------
echo "T21: resolving-claim refused under another agent's fleet:reviewing-*"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" resolving-claim 3103 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:reviewing-mac-pool-9 → resolving-claim exit 1"

# --- T22: ...and the refuse path mutated no labels -------------------------
# Same load-bearing property as T11: a stranded fleet:resolving-* label on a
# PR the worker then abandons blocks the lane until the orphan sweep runs.
echo "T22: the refusal mutates no labels"
assert_no_label_post "refused resolving-claim POSTed no label"

# --- T23: the agent's OWN review claim is a pass-through -------------------
# An agent that reviewed and then resolved the conflict itself is not a
# cross-lane race — it holds both labels legitimately, and it is the only
# claimant, so there is no unread head. This is the deliberate asymmetry with
# the projection side, which is agent-blind by design.
echo "T23: own fleet:reviewing-<host>-<agent> does not block the resolve"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" resolving-claim 3104 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + own fleet:reviewing-mac-test-agent → resolving-claim exit 0"

# --- T24: a cross-host foreign review claim refuses too --------------------
echo "T24: another host's fleet:reviewing-* also refuses the resolve"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" resolving-claim 3105 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + fleet:reviewing-linux-pool-2 → resolving-claim exit 1"
assert_no_label_post "cross-host resolve refusal POSTed no label"

# --- T25: a conflicted PR with no review claim still resolves --------------
# Guards against the gate over-refusing — the whole lane would deadlock.
echo "T25: conflicted PR with no review claim still resolves"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" resolving-claim 3102 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + no fleet:reviewing-* → resolving-claim exit 0"

# --- T26: fidelity check for T22/T24's "no POST" assertion -----------------
# assert_no_label_post is an emptiness test, so it passes for free if the
# stub's GH_POST_LOG wiring breaks. T25 just granted a resolve, which MUST
# have POSTed fleet:resolving-mac-test-agent.
echo "T26: the POST log records a granted resolve (fidelity check)"
if grep -q '^fleet:resolving-mac-test-agent$' "$GH_POST_LOG" 2>/dev/null; then
    ok "granted resolving-claim POSTed fleet:resolving-mac-test-agent"
else
    bad "granted resolving-claim left no POST in the log — the GH_POST_LOG wiring is broken, so T22/T24 prove nothing"
fi

# --- T27: an incumbent amend claim still honors the host gate --------------
echo "T27: incumbent amending-claim remains subject to fleet:needs-gl-host"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3006 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "mac + incumbent amend + fleet:needs-gl-host → amending-claim exit 1"
assert_no_label_post "host-refused incumbent amending-claim POSTed no label"
assert_absent "$output" "acquired" "host-refused incumbent amending-claim reports no acquisition"

# --- fleet:needs-macos-host: the one-OS residual pin ------------------------
# Refuses every host but mac on claim and amending-claim, ahead of any label
# POST; review stays host-agnostic.

echo "T28: windows and linux refuse amending-claim on a fleet:needs-macos-host PR"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=windows FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3201 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "windows + fleet:needs-macos-host PR → amending-claim exit 1"
assert_contains "$output" "fleet:needs-macos-host" "refusal names the label"
actual=0; FLEET_TEST_HOST=linux FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3201 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "linux + fleet:needs-macos-host PR → amending-claim exit 1"
assert_no_label_post "host-refused macOS amending-claim POSTed no label"

echo "T29: mac passes amending-claim on a fleet:needs-macos-host PR"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3201 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + fleet:needs-macos-host PR → amending-claim exit 0"

echo "T30: windows review-claim on a fleet:needs-macos-host PR is allowed"
actual=0; FLEET_TEST_HOST=windows FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" review-claim 3201 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "windows + fleet:needs-macos-host PR → review-claim exit 0"

echo "T31: issue claim honors fleet:needs-macos-host"
actual=0; FLEET_TEST_HOST=windows FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2201 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "windows + fleet:needs-macos-host issue → claim exit 1"
release_quiet 2201
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2201 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "mac + fleet:needs-macos-host issue → claim exit 0"
release_quiet 2201

echo "T32: a PR carrying both host labels refuses on every host"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3202 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "mac + both host labels → amending-claim exit 1"
actual=0; FLEET_TEST_HOST=linux FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3202 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "linux + both host labels → amending-claim exit 1"

# --- live worker-feedback gate ---------------------------------------------

echo "T33: a stale no-tier dispatch is refused before mutation"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3301 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "fleet:wip without a worker tier → amending-claim exit 1"
assert_contains "$output" "no live worker feedback tier" "no-tier refusal has a stable reason"
assert_no_label_post "no-tier refusal POSTed no label"

echo "T34: fleet:needs-fix remains claimable"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3302 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "fleet:needs-fix → amending-claim exit 0"
if grep -q '^fleet:amending-mac-test-agent$' "$GH_POST_LOG" 2>/dev/null; then
    ok "fleet:needs-fix grant POSTed the amend label"
else
    bad "fleet:needs-fix grant did not POST the amend label"
fi

echo "T35: human feedback outranks suppressible parks"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3303 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "human:needs-fix + fleet:design-blocked → amending-claim exit 0"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3304 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "human:blocker + fleet:awaiting-infra → amending-claim exit 0"

echo "T36: design resume remains claimable through fleet:wip"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3305 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "fleet:design-unblocked + fleet:wip → amending-claim exit 0"

echo "T37: reviewer-only cue is not worker feedback"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3306 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "human:re-review alone → amending-claim exit 1"
assert_contains "$output" "no live worker feedback tier" "reviewer-only refusal uses the no-tier reason"
assert_no_label_post "reviewer-only refusal POSTed no label"

echo "T38: suppressible parks refuse fleet-owned tiers"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3307 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "fleet:needs-fix + fleet:design-blocked → amending-claim exit 1"
assert_contains "$output" "worker feedback is parked by fleet:design-blocked" "park refusal names the stable park label"
assert_no_label_post "parked fleet-tier refusal POSTed no label"

echo "T39: absolute parks refuse human feedback"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3308 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "human:needs-fix + human:wip → amending-claim exit 1"
assert_contains "$output" "worker feedback is parked by human:wip" "absolute-park refusal names the stable park label"
assert_no_label_post "absolute-park refusal POSTed no label"

echo "T40: fleet:human-amending resumes as live worker work"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3309 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "fleet:human-amending fresh claim → exit 0"
if grep -q '^fleet:amending-mac-test-agent$' "$GH_POST_LOG" 2>/dev/null; then
    ok "human-amend resume POSTed the amend label"
else
    bad "human-amend resume did not POST the amend label"
fi

echo "T41: a valid human-amend incumbent refreshes ownership"
rm -f "$FLEET_AMEND_SNAPSHOTS_DIR/3310.json"
: > "$GH_POST_LOG"
actual=0; FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus FLEET_DISPATCH_ID=refresh-3310 "$FLEET_CLAIM" amending-claim 3310 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "fleet:human-amending incumbent → exit 0"
assert_no_label_post "valid incumbent refreshed without a duplicate POST"
assert_contains "$(cat "$FLEET_AMEND_SNAPSHOTS_DIR/3310.json" 2>/dev/null || true)" '"dispatch_id":"refresh-3310"' "valid incumbent refreshed its ownership record"

echo "T42: a cleared incumbent cannot refresh ownership"
printf '%s\n' "sentinel" > "$FLEET_AMEND_SNAPSHOTS_DIR/3311.json"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus FLEET_DISPATCH_ID=stale-3311 "$FLEET_CLAIM" amending-claim 3311 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "incumbent with no live tier → exit 1"
assert_contains "$output" "no live worker feedback tier" "cleared incumbent uses the no-tier reason"
assert_eq "$(cat "$FLEET_AMEND_SNAPSHOTS_DIR/3311.json")" "sentinel" "cleared incumbent did not refresh ownership"
assert_no_label_post "cleared incumbent POSTed no label"

echo "T43: failed live-label fetch has a distinct stable reason"
: > "$GH_POST_LOG"
actual=0; output=$(FLEET_TEST_HOST=mac FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" amending-claim 3312 test-agent 2>&1) || actual=$?
assert_exit "$actual" 1 "failed live-label fetch → exit 1"
assert_contains "$output" "live labels unavailable" "fetch failure is distinct from a stale no-tier dispatch"
assert_absent "$output" "no live worker feedback tier" "fetch failure does not masquerade as no-tier"
assert_no_label_post "failed live-label fetch POSTed no label"

summarize "fleet-claim pre-acquire gates"
