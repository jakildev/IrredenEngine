#!/usr/bin/env bash
# Tests for fleet-claim's check_host_capability gate (issue-based, #1998).
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

# Stub `gh` so check_host_capability reads canned JSON instead of hitting
# GitHub. Dispatches on the issue number passed via `gh issue view <N>`
# (PR numbers share the issues label namespace, so PRs go through the same
# arm).
#   2001 — carries fleet:needs-gl-host (GL-only task)
#   2002 — no host label (gate is opt-in)
#   2003 — gh failure (soft-degrade contract)
#   3001 — PR carrying fleet:needs-gl-host (GL-gated design-unblocked resume)
#   3002 — PR without the label
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

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
