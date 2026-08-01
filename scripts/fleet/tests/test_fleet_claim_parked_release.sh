#!/usr/bin/env bash
# Tests for the #1488 claim-lifecycle hardening in fleet-claim:
#
#   Fix A — `fleet-claim release` clears this host's `fleet:claim-<host>-*`
#           + `fleet:in-progress` labels off the issue whenever no LIVE PR
#           backs the claim: the matching open PR is parked
#           (fleet:design-blocked/-unblocked), or there is no matching PR at
#           all (the decline-after-claim path, #2732). A normal PR-open
#           release (active matching PR) keeps the labels, per
#           AUTHOR-PIPELINE.md "Claim-label lifecycle". `fleet:in-progress`
#           survives while ANOTHER host's claim label does (#2732).
#
#   Fix B — `fleet-claim cleanup --gh` open-issue claim sweep treats a parked
#           matching PR like a no-PR abandon: a stale claim whose only matching
#           PR is design-blocked/-unblocked is swept (claim label +
#           fleet:in-progress), while an active matching PR keeps the claim.
#
#   Fix C — every path that clears `fleet:in-progress` alongside a claim label
#           (`release`, the `cleanup --gh` TTL sweep, the
#           `reset-sweep-host-claims` boot sweep) keeps it while a claim the
#           pass is NOT retiring is still live. Two hosts can claim one issue
#           and go stale at different times; clearing the label under the
#           survivor advertises an owned task as free.
#
# `gh` is stubbed so the label/PR surfaces are canned JSON. Host is pinned to
# `mac` via FLEET_TEST_HOST so claim-label construction is deterministic.

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

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

assert_removed_contains() {
    if grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (not in removed log)"; fi
}
assert_removed_absent() {
    if ! grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (unexpectedly removed)"; fi
}
assert_dir_absent() {
    if [[ ! -d "$1" ]]; then ok "$2"; else bad "$2 (dir still present: $1)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
# Drive the open-issue claim sweep without waiting on the real 2h TTL: the gh
# stub reports every claim label as added long ago, and we keep the threshold
# tiny so any age clears it.
export FLEET_CLAIM_STALE_SECS_ISSUES=1
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR"

export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
REMOVED_FILE="$TMPROOT/removed.log"
: > "$REMOVED_FILE"
export REMOVED_FILE

# --- gh stub --------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# Stub gh for the #1488 parked-release / parked-sweep tests.
case "$1" in
    issue)
        case "$2" in
            list) cat "$ISSUES_JSON"; exit 0 ;;
            view)
                # `gh issue view <N> --json labels|state [--jq ...]`
                num="$3"
                want="state"
                printf '%s ' "$@" | grep -q -- '--json labels' && want="labels"
                NUM="$num" WANT="$want" python3 -c '
import json, os, sys
num = int(os.environ["NUM"])
want = os.environ["WANT"]
issues = json.load(open(os.environ["ISSUES_JSON"]))
issue = next((i for i in issues if i.get("number") == num), None)
if want == "labels":
    if issue:
        for l in issue.get("labels", []):
            print(l.get("name", ""))
else:
    print((issue or {}).get("state", "OPEN"))
'
                exit 0
                ;;
            edit)
                shift 2
                issue="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\t%s\n' "$issue" "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                exit 0
                ;;
            *) exit 0 ;;
        esac
        ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac
        ;;
    api)
        # events endpoint — report every claim label as added long ago so the
        # TTL gate in the open-issue sweep always clears, EXCEPT the labels
        # named in $FRESH_LABELS (space-separated), which report a far-future
        # timestamp. Future rather than `date +%s` because the tests pin
        # FLEET_CLAIM_STALE_SECS_ISSUES=1: a "now" stamp goes stale the moment
        # the clock ticks past the next second, which would flake.
        # The queried label rides in $FLEET_LABEL_NAME, not argv: `gh api` has
        # no --arg, so label_added_epoch passes it through the environment
        # (#2781).
        if printf '%s ' "$@" | grep -q 'events'; then
            stamp="2020-01-01T00:00:00Z"
            for fresh in ${FRESH_LABELS:-}; do
                case "${FLEET_LABEL_NAME:-}" in *"$fresh"*) stamp="2099-01-01T00:00:00Z"; break ;; esac
            done
            echo "$stamp"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

mk_claim() {
    local slug="$1" owner="$2"
    mkdir -p "$FLEET_CLAIMS_DIR/$slug"
    echo "$owner" > "$FLEET_CLAIMS_DIR/$slug/owner"
    echo "$slug"  > "$FLEET_CLAIMS_DIR/$slug/title"
    date +%s      > "$FLEET_CLAIMS_DIR/$slug/created"
}

# =========================================================================
echo "=== Phase 1: release clears labels only for a PARKED matching PR (Fix A) ==="
# =========================================================================
# #700 parked (design-blocked PR) -> release clears claim + in-progress.
# #701 active  (plain wip PR)      -> release keeps claim + in-progress.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":700,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":701,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[
  {"number":800,"headRefName":"claude/700-parked","labels":[{"name":"fleet:wip"},{"name":"fleet:design-blocked"}]},
  {"number":801,"headRefName":"claude/701-live","labels":[{"name":"fleet:wip"}]}
]
JSON
mk_claim 700 opus-worker-1
mk_claim 701 opus-worker-1

REL700=$("$FLEET_CLAIM" release 700 2>&1); echo "$REL700" | sed 's/^/    /'
assert_dir_absent "$FLEET_CLAIMS_DIR/700" "release dropped FS claim #700"
assert_removed_contains $'700\tfleet:claim-mac-opus-worker-1' "parked release cleared #700 claim label"
assert_removed_contains $'700\tfleet:in-progress' "parked release cleared #700 fleet:in-progress"

REL701=$("$FLEET_CLAIM" release 701 2>&1); echo "$REL701" | sed 's/^/    /'
assert_dir_absent "$FLEET_CLAIMS_DIR/701" "release dropped FS claim #701"
assert_removed_absent $'701\tfleet:claim-mac-opus-worker-1' "active release KEPT #701 claim label"
assert_removed_absent $'701\tfleet:in-progress' "active release KEPT #701 fleet:in-progress"

# =========================================================================
echo "=== Phase 1b: release with NO matching PR clears labels too (#2732) ==="
# =========================================================================
# The decline-after-claim path: a worker claims, finds the task unworkable,
# comments, and releases without ever opening a PR. `issue_pr_state` returns
# "none", which its own docstring says keep-vs-release callers must treat like
# "parked" — otherwise the issue sits in the scout's in_progress[] bucket
# (invisible to every pane's queue walk) until the 2h cleanup --gh TTL.
#
# #702 no PR                     -> release clears claim + in-progress.
# #703 no PR, foreign-host claim -> our claim label goes, in-progress STAYS.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":702,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":703,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:claim-linux-pool-2"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[
  {"number":802,"headRefName":"claude/999-unrelated","labels":[{"name":"fleet:wip"}]}
]
JSON
mk_claim 702 opus-worker-1
mk_claim 703 opus-worker-1

REL702=$("$FLEET_CLAIM" release 702 2>&1); echo "$REL702" | sed 's/^/    /'
assert_dir_absent "$FLEET_CLAIMS_DIR/702" "release dropped FS claim #702"
assert_removed_contains $'702\tfleet:claim-mac-opus-worker-1' "no-PR release cleared #702 claim label"
assert_removed_contains $'702\tfleet:in-progress' "no-PR release cleared #702 fleet:in-progress"

REL703=$("$FLEET_CLAIM" release 703 2>&1); echo "$REL703" | sed 's/^/    /'
assert_dir_absent "$FLEET_CLAIMS_DIR/703" "release dropped FS claim #703"
assert_removed_contains $'703\tfleet:claim-mac-opus-worker-1' "no-PR release cleared #703 own-host claim label"
assert_removed_absent  $'703\tfleet:claim-linux-pool-2' "no-PR release left #703 foreign-host claim label"
assert_removed_absent  $'703\tfleet:in-progress' "no-PR release KEPT #703 fleet:in-progress (foreign claim live)"

# =========================================================================
echo "=== Phase 2: cleanup --gh sweeps a PARKED claim like a no-PR abandon (Fix B) ==="
# =========================================================================
: > "$REMOVED_FILE"
rm -rf "$FLEET_CLAIMS_DIR"/*   # Pass 2 operates on GH labels, not FS claims.
# #710 parked  (design-unblocked PR) -> swept.
# #711 active  (plain wip PR)        -> kept.
# #712 no PR                          -> swept (pre-existing behavior, unchanged).
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":710,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":711,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-2"},{"name":"fleet:in-progress"}]},
  {"number":712,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[
  {"number":810,"headRefName":"claude/710-parked","labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"}]},
  {"number":811,"headRefName":"claude/711-live","labels":[{"name":"fleet:wip"}]}
]
JSON

CLEAN_OUT=$("$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1); echo "$CLEAN_OUT" | sed 's/^/    /'

assert_removed_contains $'710\tfleet:claim-mac-opus-worker-1' "parked-PR sweep removed #710 claim label"
assert_removed_contains $'710\tfleet:in-progress' "parked-PR sweep removed #710 fleet:in-progress"
assert_removed_absent  $'711\tfleet:claim-mac-opus-worker-2' "active-PR claim #711 retained"
assert_removed_absent  $'711\tfleet:in-progress' "active-PR #711 fleet:in-progress retained"
assert_removed_contains $'712\tfleet:claim-mac-opus-worker-1' "no-PR sweep removed #712 claim label (unchanged)"
assert_removed_contains $'712\tfleet:in-progress' "no-PR sweep removed #712 fleet:in-progress (unchanged)"

# =========================================================================
echo "=== Phase 2b: cleanup --gh keeps fleet:in-progress while a 2nd claim is live ==="
# =========================================================================
: > "$REMOVED_FILE"
# Two hosts can claim one issue and cross the TTL at different times, because
# the age gate is evaluated per label row. Only the row retiring the LAST live
# claim may clear fleet:in-progress — otherwise the sweep advertises a task
# another host is actively working as free, which is the double-claim race the
# release-side guard (the #703 case) closes.
#
# #720 stale mac claim + FRESH linux claim -> mac swept, in-progress STAYS.
# #721 two stale claims                    -> both swept, in-progress cleared.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":720,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:claim-linux-pool-2"},{"name":"fleet:in-progress"}]},
  {"number":721,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:claim-linux-pool-3"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[]
JSON
export FRESH_LABELS="fleet:claim-linux-pool-2"
CLEAN_OUT_2B=$("$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1)
echo "$CLEAN_OUT_2B" | sed 's/^/    /'
export FRESH_LABELS=""

assert_removed_contains $'720\tfleet:claim-mac-opus-worker-1' "2-claim sweep removed #720 stale mac claim"
assert_removed_absent  $'720\tfleet:claim-linux-pool-2' "2-claim sweep left #720 fresh foreign claim"
assert_removed_absent  $'720\tfleet:in-progress' "2-claim sweep KEPT #720 fleet:in-progress (foreign claim live)"
assert_removed_contains $'721\tfleet:claim-mac-opus-worker-1' "all-stale sweep removed #721 mac claim"
assert_removed_contains $'721\tfleet:claim-linux-pool-3' "all-stale sweep removed #721 foreign claim"
assert_removed_contains $'721\tfleet:in-progress' "all-stale sweep cleared #721 fleet:in-progress (no claim left)"

# =========================================================================
echo "=== Phase 2c: reset-sweep-host-claims keeps fleet:in-progress for a foreign claim ==="
# =========================================================================
: > "$REMOVED_FILE"
# This pass removes only fleet:claim-<this-host>-*, so any other host's claim
# is live by construction — a local boot says nothing about remote work.
#
# #730 mac claim + foreign linux claim -> mac swept, in-progress STAYS.
# #731 mac claim only                  -> mac swept, in-progress cleared.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":730,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:claim-linux-pool-2"},{"name":"fleet:in-progress"}]},
  {"number":731,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[]
JSON
SWEEP_OUT=$("$FLEET_CLAIM" reset-sweep-host-claims 2>&1); echo "$SWEEP_OUT" | sed 's/^/    /'

assert_removed_contains $'730\tfleet:claim-mac-opus-worker-1' "host sweep removed #730 own-host claim"
assert_removed_absent  $'730\tfleet:claim-linux-pool-2' "host sweep left #730 foreign-host claim"
assert_removed_absent  $'730\tfleet:in-progress' "host sweep KEPT #730 fleet:in-progress (foreign claim live)"
assert_removed_contains $'731\tfleet:claim-mac-opus-worker-1' "host sweep removed #731 own-host claim"
assert_removed_contains $'731\tfleet:in-progress' "host sweep cleared #731 fleet:in-progress (no claim left)"

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
