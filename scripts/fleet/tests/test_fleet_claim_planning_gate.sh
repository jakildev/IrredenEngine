#!/usr/bin/env bash
# Tests for fleet-claim's check_planning_gates gate (#2740).
#
# The load-bearing fix for "a planning queue-block landing after fleet:queued
# is never retracted" is the scout/ingest retract round-trip. This gate is the
# belt-and-braces half: the round-trip takes a tick to converge, so a dispatch
# elected from the PREVIOUS scout generation would still be granted in that
# window. Refusing at claim time closes it independent of cache freshness.
#
# Covers:
#   - claim on a fleet:needs-plan issue    → exit 1, gate named on stderr
#   - claim on a fleet:plan-review issue   → exit 1, gate named on stderr
#   - gate-free issue                      → exit 0 (the negative control)
#   - retired human:review-plan is INERT   → exit 0 (PR #3112)
#   - gh failure soft-degrades to pass     → exit 0 (house contract)
#   - a stack containing one gated member  → whole stack refused, no claims left
#
# `gh` is stubbed to canned issue surfaces; no live GitHub round-trip.

set -euo pipefail

# This suite exercises cmd_claim against the real (possibly-stale) main clone
# but does not care about clone freshness — disable the #1810 freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

source "$(dirname "$0")/lib_assert.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 3
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected exit: $expected"
        echo "        actual exit:   $actual"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

# #2001 fleet:needs-plan          → refuse
# #2002 fleet:plan-review         → refuse
# #2003 gate-free                 → grant (negative control)
# #2004 human:review-plan only    → grant (retired label, #3112)
# #2005 gh fetch fails            → grant (soft-degrade)
STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1 $2" in
    "issue view")
        case "$3" in
            2001) echo '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:needs-plan"}],"body":""}' ;;
            2002) echo '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:plan-review"}],"body":""}' ;;
            2003) echo '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"}],"body":""}' ;;
            2004) echo '{"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"human:review-plan"}],"body":""}' ;;
            2005) exit 1 ;;
            *)    echo '{"state":"OPEN","labels":[],"body":""}' ;;
        esac
        exit 0 ;;
    "api "*)
        # Emulate the cross-host fleet:claim-* acquire: echo the requested
        # label back so _acquire_label_on sees us as the lex-min winner.
        label=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -f) shift
                    case "$1" in labels\[\]=*) label="${1#labels[]=}" ;; esac ;;
            esac
            shift || true
        done
        if [[ -n "$label" ]]; then printf '[{"name":"%s"}]\n' "$label"; else echo '[]'; fi
        exit 0 ;;
    "pr list") echo '[]'; exit 0 ;;
esac
exit 0
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

release_quiet() { "$FLEET_CLAIM" release "$1" >/dev/null 2>&1 || true; }

# --- T1: fleet:needs-plan refuses -------------------------------------------
echo "T1: claim on a fleet:needs-plan issue"
err=$(FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2001 test-agent 2>&1 >/dev/null) && rc=0 || rc=$?
assert_exit "$rc" 1 "fleet:needs-plan → exit 1"
assert_contains "$err" "fleet:needs-plan" "refusal names the gate label"
release_quiet 2001

# --- T2: fleet:plan-review refuses ------------------------------------------
echo "T2: claim on a fleet:plan-review issue"
err=$(FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2002 test-agent 2>&1 >/dev/null) && rc=0 || rc=$?
assert_exit "$rc" 1 "fleet:plan-review → exit 1"
assert_contains "$err" "fleet:plan-review" "refusal names the gate label"
release_quiet 2002

# --- T3: negative control — a gate-free issue is still claimable -------------
echo "T3: gate-free issue is granted"
rc=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2003 test-agent >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "no gate → exit 0 (the gate is not blanket-refusing)"
release_quiet 2003

# --- T4: retired human:review-plan is inert (#3112) --------------------------
echo "T4: retired human:review-plan does not gate"
rc=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2004 test-agent >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "human:review-plan → exit 0 (label retired, must not be re-armed)"
release_quiet 2004

# --- T5: gh failure soft-degrades to pass ------------------------------------
echo "T5: unfetchable issue soft-degrades"
rc=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" claim 2005 test-agent >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "gh failure → soft-pass exit 0"
release_quiet 2005

# --- T6: stack claiming is all-or-nothing over the gate ----------------------
# cmd_stack does NOT route through cmd_claim, so it needs its own call site.
echo "T6: a stack containing one gated member is refused whole"
rc=0; FLEET_ROLE_MODEL=opus "$FLEET_CLAIM" stack "2003 2002" test-agent >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "stack with a fleet:plan-review member → exit 1"
listing=$("$FLEET_CLAIM" list 2>/dev/null || true)
assert_absent "$listing" "2003" \
    "the earlier stack member was rolled back — no partial claim survives"
release_quiet 2003
release_quiet 2002

summarize "fleet-claim planning-gate refusal"
