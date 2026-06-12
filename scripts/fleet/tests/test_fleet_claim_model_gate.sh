#!/usr/bin/env bash
# Tests for fleet-claim's check_model_tag gate (issue-based).
#
# The gate reads model affinity from GitHub issue labels (fleet:opus /
# fleet:sonnet) with a body **Model:** field fallback. These tests stub
# `gh issue view` to drive both resolution paths without a live GitHub
# round-trip.
#
# Covers:
#   - sonnet role rejects fleet:opus-labeled issue
#   - opus role accepts fleet:opus-labeled issue
#   - sonnet role accepts fleet:sonnet-labeled issue
#   - opus role rejects fleet:sonnet-labeled issue
#   - body **Model:** field is the fallback when no label is set
#   - FLEET_ROLE_MODEL unset/empty passes any task
#   - issue with neither label nor body field passes any role
#   - gh failure passes (soft-degrade contract)
#   - legacy T-NNN input is rejected with a migration hint
#   - garbage input rejected

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

# Stub `gh` so check_model_tag reads canned JSON instead of hitting
# GitHub. The stub dispatches on the issue number passed via
# `gh issue view <N>`, returning a fixture matching the test under test.
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# Minimal `gh` stub used by check_model_tag tests. Recognized invocations:
#   gh issue view <N> --repo ... --json state,labels,body
#   gh api repos/.../issues/<N>/labels --method POST -f labels[]=...
#       → emulate the cross-host fleet:claim-* lock acquire: echo the
#         requested label as a single-element JSON array so _acquire_label_on
#         sees it as the lex-min winner.
#   gh issue edit ... (--add-label / --remove-label)  → no-op success.
case "$1 $2" in
    "issue view")
        issue_num="$3"
        case "$issue_num" in
            1001)
                echo '{"state":"OPEN","labels":[{"name":"fleet:opus"},{"name":"fleet:queued"}],"body":""}'
                ;;
            1002)
                echo '{"state":"OPEN","labels":[{"name":"fleet:sonnet"},{"name":"fleet:queued"}],"body":""}'
                ;;
            1003)
                # JSON body has escaped newlines (\\n in printf → \n literal in stdout)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Context\n\n**Model:** opus\n\nSome more text."}'
                ;;
            1004)
                printf '%s' '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Model:** sonnet (small fix)\n"}'
                ;;
            1005)
                echo '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"No model here."}'
                ;;
            1006)
                exit 1
                ;;
            *)
                echo '{"state":"OPEN","labels":[],"body":""}'
                ;;
        esac
        exit 0
        ;;
    "api "*)
        # gh api repos/.../issues/<N>/labels --method POST -f "labels[]=X"
        # Extract the requested label from the -f arg and echo it back as
        # a single-element JSON array so the lex-min winner is us.
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

# --- T1: sonnet rejects fleet:opus-labeled issue ----------------------------
echo "T1: sonnet role rejects fleet:opus-labeled issue"
actual=0; FLEET_ROLE_MODEL=sonnet "$FLEET_CLAIM" claim 1001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "sonnet rejects fleet:opus → exit 1"
release_quiet 1001

# --- T2: opus accepts fleet:opus-labeled issue ------------------------------
echo "T2: opus role accepts fleet:opus-labeled issue"
actual=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 1001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "opus accepts fleet:opus → exit 0"
release_quiet 1001

# --- T3: sonnet accepts fleet:sonnet-labeled issue --------------------------
echo "T3: sonnet role accepts fleet:sonnet-labeled issue"
actual=0; FLEET_ROLE_MODEL=sonnet "$FLEET_CLAIM" claim 1002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "sonnet accepts fleet:sonnet → exit 0"
release_quiet 1002

# --- T4: opus rejects fleet:sonnet-labeled issue ----------------------------
echo "T4: opus role rejects fleet:sonnet-labeled issue"
actual=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 1002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "opus rejects fleet:sonnet → exit 1"

# --- T5: body **Model:** opus fallback (sonnet role rejects) ----------------
echo "T5: sonnet role rejects body **Model:** opus fallback"
actual=0; FLEET_ROLE_MODEL=sonnet "$FLEET_CLAIM" claim 1003 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "body **Model:** opus, sonnet role → exit 1"

# --- T6: body **Model:** sonnet (annotated) fallback ------------------------
echo "T6: opus role rejects body **Model:** sonnet (annotated)"
actual=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 1004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "body **Model:** sonnet (annotated), opus role → exit 1"

# --- T7: FLEET_ROLE_MODEL unset passes any task -----------------------------
echo "T7: FLEET_ROLE_MODEL unset passes fleet:opus issue"
actual=0; "$FLEET_CLAIM" claim 1001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "unset FLEET_ROLE_MODEL bypasses gate → exit 0"
release_quiet 1001

# --- T8: issue with neither label nor body field passes any role ------------
echo "T8: issue with neither label nor body field passes any role"
actual=0; FLEET_ROLE_MODEL=sonnet "$FLEET_CLAIM" claim 1005 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "no signal → fall-through exit 0"
release_quiet 1005

# --- T9: gh failure soft-degrades to pass -----------------------------------
echo "T9: gh failure soft-degrades to pass"
actual=0; FLEET_ROLE_MODEL=sonnet "$FLEET_CLAIM" claim 1006 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "gh failure → soft-pass exit 0"
release_quiet 1006

# --- T10: legacy T-NNN input rejected with migration hint -------------------
echo "T10: legacy T-NNN input rejected"
actual=0; "$FLEET_CLAIM" claim T-001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 2 "T-NNN form → exit 2 (migration hint)"

# --- T11: non-numeric junk rejected -----------------------------------------
echo "T11: garbage input rejected"
actual=0; "$FLEET_CLAIM" claim "not-an-issue" test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 2 "garbage input → exit 2"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
