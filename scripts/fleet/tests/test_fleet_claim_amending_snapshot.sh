#!/usr/bin/env bash
# Tests for the amend OWNERSHIP RECORD that fleet-claim writes on a won
# `amending-claim` and removes on `amending-release`.
#
# ~/.fleet/amend-snapshots/<pr>.json is what lets the cleanup sweep tell a live
# amend from one whose owning ITERATION died. The pane heartbeat alone cannot:
# that file (~/.fleet/heartbeats/<worktree>), which step 0 of five role
# docs refreshes under the same basename — so any later dispatch of any role
# into the pane renewed a dead claim indefinitely. The record therefore carries
# the claiming dispatch's FLEET_DISPATCH_ID, and the sweep compares it against
# the worktree's current dispatch.
#
# Two properties are load-bearing here:
#   - fleet-claim, not fleet-pr-claim-feedback, writes it. The mutex is the one
#     place every claim path funnels through, and a second writer would
#     overwrite the id-bearing file with an id-less copy — silently restoring
#     the pane-keyed behaviour this issue exists to remove.
#   - An UNSET FLEET_DISPATCH_ID yields an empty field, not a missing one: the
#     sweep's legacy fallback keys on "no dispatch id", and it must read the
#     same way for an architect pane (which never goes through
#     fleet-dispatch-wrap) as for a pre-upgrade claim.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2; exit 3; }

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export HOME="$TMPROOT/home"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
mkdir -p "$HOME/.fleet" "$FLEET_CLAIMS_DIR" "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR"

SNAP="$HOME/.fleet/amend-snapshots/804.json"

# gh stub. `issue view --json state,labels,body` feeds the two pre-acquire
# gates (host capability, foreign review claim) — an unlabelled OPEN PR passes
# both. The labels POST echoes only the posted label, so the claimant is the
# sole holder and wins. remove-label succeeds.
STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            view) printf '{"state":"OPEN","labels":[%s],"body":""}\n' "${STUB_VIEW_LABELS:-}"; exit 0 ;;
            edit) exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        posted=""
        for a in "$@"; do
            case "$a" in labels\[\]=*) posted="${a#labels[]=}" ;; esac
        done
        if [[ -n "$posted" ]]; then printf '[{"name":"%s"}]\n' "$posted"; else echo '[]'; fi
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

snap_field() {  # <json-path> <key>
    python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get(sys.argv[2],"<absent>"))' "$1" "$2"
}

echo "=== T1: amending-claim stamps the dispatch id into the record ==="
rm -f "$SNAP"
out=$(FLEET_DISPATCH_ID=D1 "$FLEET_CLAIM" amending-claim 804 pool-2 2>&1) || bad "amending-claim failed: $out"
if [[ -f "$SNAP" ]]; then
    ok "won claim writes ~/.fleet/amend-snapshots/<pr>.json"
    assert_eq "$(snap_field "$SNAP" pr)" "804" "record names the PR"
    assert_eq "$(snap_field "$SNAP" agent)" "pool-2" "record names the claiming agent (what the sweep matches to the label)"
    assert_eq "$(snap_field "$SNAP" dispatch_id)" "D1" "record names the claiming dispatch"
    if [[ "$(snap_field "$SNAP" acquired_epoch)" =~ ^[0-9]+$ ]]; then
        ok "record keeps acquired_epoch (R7's #1650 deferral reads this file)"
    else
        bad "acquired_epoch missing or non-numeric"
    fi
else
    bad "won claim wrote no amend-snapshot"
fi

echo "=== T2: amending-release removes the record ==="
out=$("$FLEET_CLAIM" amending-release 804 pool-2 2>&1) || true
[[ ! -f "$SNAP" ]] && ok "clean release removes the record" || bad "release left the record behind"

echo "=== T3: no FLEET_DISPATCH_ID -> the field is EMPTY, not absent ==="
# The architect pane never runs fleet-dispatch-wrap, so it has no dispatch id.
# The sweep's legacy fallback keys on an empty id; an ABSENT key would read the
# same through .get() today but is one refactor away from a KeyError, and an
# id-less record must be visibly id-less to a human reading it.
rm -f "$SNAP"
env -u FLEET_DISPATCH_ID "$FLEET_CLAIM" amending-claim 804 pool-2 >/dev/null 2>&1 || true
if [[ -f "$SNAP" ]]; then
    assert_eq "$(snap_field "$SNAP" dispatch_id)" "" "dispatch_id present and empty with no dispatch identity"
else
    bad "claim with no dispatch id wrote no record at all"
fi

echo "=== T4: fleet-pr-claim-feedback does not write the record ==="
# One owner per artifact: a second writer in the wrapper would overwrite the
# id-bearing file with an id-less one and silently re-key liveness to the pane.
# Match on CODE lines only (a comment mention is the intended documentation).
_snap_code=$(grep -n 'amend-snapshots' "$SCRIPT_DIR/fleet-pr-claim-feedback" \
    | grep -v '^[0-9]\{1,\}:[[:space:]]*#' || true)
if [[ -z "$_snap_code" ]]; then
    ok "no code line in fleet-pr-claim-feedback touches the amend-snapshot"
else
    bad "fleet-pr-claim-feedback still writes the amend-snapshot: $_snap_code"
fi

echo "=== T5: the dispatcher's PRE-CLAIM records the sentinel, the role's re-acquire replaces it ==="
# fleet-dispatcher takes a feedback target's claim before fleet-dispatch-wrap
# mints the iteration's id, so it passes FLEET_PRECLAIM_DISPATCH_ID instead of
# leaving the variable unset. Unset would write the EMPTY id of T3 — correct
# for an architect pane, wrong here: it routes the dispatcher's own fresh claim
# onto the pane-heartbeat fallback at exactly the moment that pane is idle and
# its heartbeat stale, so a label carried past TTL is swept out from under it
# (test_fleet_claim_amending_sweep.sh phase 5). The window closes when the role
# re-runs fleet-pr-claim-feedback at its step a and this same code path
# overwrites the record with the minted id.
rm -f "$SNAP"
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim 804 pool-2 >/dev/null 2>&1 || true
assert_eq "$(snap_field "$SNAP" dispatch_id)" "preclaim" \
    "pre-claim records the sentinel, not an empty id"
FLEET_DISPATCH_ID=D9 "$FLEET_CLAIM" amending-claim 804 pool-2 >/dev/null 2>&1 || true
assert_eq "$(snap_field "$SNAP" dispatch_id)" "D9" \
    "the role's step-a re-acquire replaces the sentinel with its minted id"
assert_eq "$(snap_field "$SNAP" agent)" "pool-2" "re-acquire keeps the owning agent"
"$FLEET_CLAIM" amending-release 804 pool-2 >/dev/null 2>&1 || true

echo "=== T5b: the re-acquire still replaces the sentinel when the label is already held ==="
# T5's stub reports an unlabelled PR, so its re-acquire POSTs afresh. In the
# fleet the dispatcher's pre-claim label is still on the PR at step a, and an
# already-held claim takes the no-POST incumbent path instead — which must
# write the record too, or the sentinel outlives its grace and the claim falls
# back onto the pane heartbeat for the rest of the iteration.
rm -f "$SNAP"
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim 804 pool-2 >/dev/null 2>&1 || true
_held_out=$(STUB_VIEW_LABELS='{"name":"fleet:amending-mac-pool-2"}' FLEET_DISPATCH_ID=D10 \
    "$FLEET_CLAIM" amending-claim 804 pool-2 2>&1) || true
assert_contains "$_held_out" "already held" "the re-acquire took the incumbent path"
assert_eq "$(snap_field "$SNAP" dispatch_id)" "D10" \
    "the incumbent re-acquire replaces the sentinel with its minted id"
"$FLEET_CLAIM" amending-release 804 pool-2 >/dev/null 2>&1 || true

echo "=== T6: the sentinel is the one fleet-common owns ==="
# The dispatcher passes FLEET_PRECLAIM_DISPATCH_ID by name and fleet-claim
# compares against it by name, so both read the same definition — but the value
# is also spelled literally in this suite and in the sweep suite's fixture. Pin
# it so a change to the constant cannot leave those fixtures asserting a value
# nothing produces any more.
_sentinel=$(source "$SCRIPT_DIR/fleet-common.sh" >/dev/null 2>&1; printf '%s' "${FLEET_PRECLAIM_DISPATCH_ID:-}")
assert_eq "$_sentinel" "preclaim" "fleet-common.sh defines the pre-claim sentinel this suite pins"

summarize
