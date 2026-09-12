#!/usr/bin/env bash
# Tests for the host-qualified amending-sweep in `fleet-claim cleanup --gh`'s
# PR-label pass (#2099).
#
# The amending-sweep skips removing a past-TTL fleet:amending-<host>-<agent>
# label when the owning agent's heartbeat is fresh — an active worker is mid
# amend and sweeping its claim would trigger a duplicate dispatch (#1650). But
# heartbeats are HOST-LOCAL: ~/.fleet/heartbeats/<agent> is touched only by the
# worker on its own host. The old code keyed the freshness check by bare
# basename, so when the sweep ran on host B (mac) over a host-A (windows)
# amending label, it read *mac*'s heartbeat for the same basename. With a
# same-basename mac worker alive, the sweep saw a fresh heartbeat and skipped
# the stale cross-host label forever — the 32h #2089 deadlock.
#
# Fix: host-qualify the freshness skip. Only honor a fresh heartbeat for a
# SAME-HOST amending label; a cross-host label ages out on pure TTL.
#
# Every label here is reported as added long ago (events stub → 2020), so the
# age gate is always satisfied — the heartbeat host-qualification alone decides
# what survives.

set -euo pipefail

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
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }
assert_removed_contains() {
    if grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (not swept)"; fi
}
assert_removed_absent() {
    if ! grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (unexpectedly swept)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
# Heartbeats live under $HOME/.fleet/heartbeats/<agent> (host-local, no env
# override) — isolate them in the sandbox.
export HOME="$TMPROOT/home"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" \
    "$FLEET_ORPHANS_DIR" "$HOME/.fleet/heartbeats"

export PRS_JSON="$TMPROOT/prs.json"
export ISSUES_JSON="$TMPROOT/issues.json"
echo '[]' > "$ISSUES_JSON"   # no claim/steward labels to sweep in the later passes
REMOVED_FILE="$TMPROOT/removed.log"; : > "$REMOVED_FILE"; export REMOVED_FILE

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    issue)
        case "$2" in
            list) cat "$ISSUES_JSON"; exit 0 ;;
            edit)
                shift 2; issue="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\t%s\n' "$issue" "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        # events endpoint — label age. Default: added long ago, so the TTL is
        # always elapsed and the liveness predicate alone decides (phase 1).
        # STUB_AGE pins the age in seconds instead, which is what lets a case
        # sit INSIDE the 30-min TTL but past the 120 s orphan grace.
        if printf '%s ' "$@" | grep -q 'events'; then
            if [[ -n "${STUB_AGE:-}" ]]; then
                python3 -c "import time;print(time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(time.time()-${STUB_AGE})))"
            else
                echo "2020-01-01T00:00:00Z"
            fi
        fi
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# Live same-host workers. worker-1 collides (by basename) with the dead
# cross-host windows-worker-1 amending label below — the exact spoof geometry.
touch "$HOME/.fleet/heartbeats/worker-1"
touch "$HOME/.fleet/heartbeats/worker-2"
# (no heartbeat for worker-3 → its same-host claim is genuinely abandoned)

# 800 cross-host (windows) amending, basename collides with live mac-worker-1
#       -> swept (cannot observe windows's heartbeat; TTL governs) [the bug]
# 801 same-host (mac) amending, owner worker-2 heartbeat fresh
#       -> kept (live same-host owner)
# 802 same-host (mac) amending, owner worker-3 has no heartbeat
#       -> swept (abandoned)
# 803 cross-host (windows) reviewing — no heartbeat semantics
#       -> swept (pure TTL, unchanged)
cat > "$PRS_JSON" <<'JSON'
[
  {"number":800,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-windows-worker-1"}]},
  {"number":801,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-worker-2"}]},
  {"number":802,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-worker-3"}]},
  {"number":803,"labels":[{"name":"fleet:reviewing-windows-worker-1"}]}
]
JSON

echo "=== phase 1: cleanup --gh host-qualified amending sweep (#2099) ==="
OUT=$("$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 || true)
echo "$OUT" | sed 's/^/    /'

assert_removed_contains $'800\tfleet:amending-windows-worker-1' \
    "cross-host amending label swept despite a fresh same-basename local heartbeat (#2099)"
assert_removed_absent   $'801\tfleet:amending-mac-worker-2' \
    "same-host amending label with a fresh owner heartbeat kept"
assert_removed_contains $'802\tfleet:amending-mac-worker-3' \
    "same-host amending label with no owner heartbeat swept (abandoned)"
assert_removed_contains $'803\tfleet:reviewing-windows-worker-1' \
    "cross-host reviewing label swept on pure TTL (no heartbeat semantics)"

# ===========================================================================
# Dispatch-keyed amend ownership.
#
# The phase-1 predicate above vouches for a same-host amending label on the
# PANE heartbeat (~/.fleet/heartbeats/<agent>). Step 0 of FIVE role docs
# touches that file with the same worktree basename, so any later dispatch of
# any role into the pane renews the liveness of a claim whose owning ITERATION
# died — indefinitely, and never past the age gate either. Such a
# claim becomes simultaneously un-reapable and
# un-claimable, and the reviewer is held off by REVIEW_SKIP_PREFIXES too.
#
# Ownership is therefore keyed on the dispatch: fleet-dispatch-wrap mints a
# FLEET_DISPATCH_ID per launch, records it at
# $FLEET_STATE_DIR/dispatch-current/<worktree>, and fleet-claim stamps it into
# the amend-snapshot at claim time. A snapshot whose dispatch_id is NOT the
# worktree's current one is a confirmed orphan — the dispatcher never launches
# into an occupied pane, so a newer id there proves the owner has ended.
#
# STUB_AGE=600 for the confirmed-orphan phase: past the 120 s orphan grace and
# INSIDE the 30-min TTL. A pure age gate keeps every label in that window, so
# whatever is swept there was swept on the dispatch comparison and nothing else.
SNAP_DIR="$HOME/.fleet/amend-snapshots"
DISPATCH_DIR="$FLEET_STATE_DIR/dispatch-current"
mkdir -p "$SNAP_DIR" "$DISPATCH_DIR"

# <pr> <agent> <dispatch-id> [<acquired-epoch>]  — the record fleet-claim
# writes on amending-claim. The epoch defaults to now; phase 5 overrides it to
# age a pre-claim past its grace without sleeping.
write_snap() {
    printf '{"pr":%s,"agent":"%s","acquired_epoch":%s,"dispatch_id":"%s"}\n' \
        "$1" "$2" "${4:-$(date +%s)}" "$3" > "$SNAP_DIR/$1.json"
}

# Every owner below is a live PANE: a fresh heartbeat under its basename is
# exactly what phase 1's predicate reads, and what a later dispatch renews.
for _a in pool-2 pool-3 pool-4 pool-5 pool-6 pool-7; do
    touch "$HOME/.fleet/heartbeats/$_a"
done
# (pool-8 deliberately has NO heartbeat — the legacy stale path, phase 4.)

# 810 owner dispatch D1, worktree now on D2  -> SWEPT (the dispatch-keyed miss)
# 811 owner dispatch D1, worktree still D1   -> kept (live owner)
# 812 owner dispatch D1, no dispatch-current -> kept (fallback: fresh heartbeat)
# 813 no snapshot at all                     -> kept (legacy fallback)
# 814 snapshot names a DIFFERENT agent       -> kept (fallback; record not ours)
write_snap 810 pool-2 D1; printf 'D2\n' > "$DISPATCH_DIR/pool-2"
write_snap 811 pool-3 D1; printf 'D1\n' > "$DISPATCH_DIR/pool-3"
write_snap 812 pool-4 D1; rm -f "$DISPATCH_DIR/pool-4"
rm -f "$SNAP_DIR/813.json"
write_snap 814 pool-9 D1; printf 'D2\n' > "$DISPATCH_DIR/pool-6"

: > "$REMOVED_FILE"
cat > "$PRS_JSON" <<'JSON'
[
  {"number":810,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-2"}]},
  {"number":811,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-3"}]},
  {"number":812,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-4"}]},
  {"number":813,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-5"}]},
  {"number":814,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-6"}]}
]
JSON

echo "=== phase 2: dispatch-keyed ownership, past grace + inside TTL (STUB_AGE=600) ==="
OUT=$(STUB_AGE=600 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 || true)
echo "$OUT" | sed 's/^/    /'

assert_removed_contains $'810\tfleet:amending-mac-pool-2' \
    "amending label whose owning dispatch was superseded is swept despite a fresh pane heartbeat (#2973)"
if grep -q 'superseded by D2' <<< "$OUT"; then
    ok "the removal names the superseding dispatch as its evidence"
else
    bad "the removal log does not name the superseding dispatch"
fi
if [[ ! -f "$SNAP_DIR/810.json" ]]; then
    ok "confirmed-dead reap deletes the amend-snapshot (unblocks the R7 heal)"
else
    bad "confirmed-dead reap left the amend-snapshot behind"
fi
assert_removed_absent $'811\tfleet:amending-mac-pool-3' \
    "amending label whose owning dispatch is still the worktree's current one is kept"
assert_removed_absent $'812\tfleet:amending-mac-pool-4' \
    "no dispatch-current record -> legacy heartbeat rule keeps the claim"
assert_removed_absent $'813\tfleet:amending-mac-pool-5' \
    "no amend-snapshot -> legacy heartbeat rule keeps the claim"
assert_removed_absent $'814\tfleet:amending-mac-pool-6' \
    "amend-snapshot naming another agent is not ours to judge -> legacy rule keeps the claim"

# --- phase 3: the orphan grace still applies to a confirmed-dead claim -----
# STUB_AGE=30 < FLEET_CLAIM_PRLABEL_ORPHAN_GRACE_SECS (120): the window between
# the label POST and the snapshot write must never read as an orphan.
write_snap 815 pool-7 D1; printf 'D2\n' > "$DISPATCH_DIR/pool-7"
: > "$REMOVED_FILE"
cat > "$PRS_JSON" <<'JSON'
[ {"number":815,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-7"}]} ]
JSON
echo "=== phase 3: confirmed-dead but inside the 120s orphan grace (STUB_AGE=30) ==="
STUB_AGE=30 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 | sed 's/^/    /'
assert_removed_absent $'815\tfleet:amending-mac-pool-7' \
    "confirmed-dead claim inside the orphan grace is spared (claim/snapshot race)"

# --- phase 4: the legacy TTL path is untouched -----------------------------
# pool-8 has no heartbeat and an empty dispatch_id (a pre-upgrade or
# architect-pane claim): swept on pure TTL as before, and its snapshot is LEFT
# in place — the "swept-but-alive" R7 deferral stands where liveness is
# unknown.
write_snap 816 pool-8 ""
: > "$REMOVED_FILE"
cat > "$PRS_JSON" <<'JSON'
[ {"number":816,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-8"}]} ]
JSON
echo "=== phase 4: legacy TTL path (no heartbeat, no dispatch id) ==="
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 | sed 's/^/    /'
assert_removed_contains $'816\tfleet:amending-mac-pool-8' \
    "same-host amending label with no heartbeat still swept on pure TTL"
if [[ -f "$SNAP_DIR/816.json" ]]; then
    ok "legacy TTL reap leaves the amend-snapshot in place (#1650 deferral)"
else
    bad "legacy TTL reap deleted the amend-snapshot"
fi

# --- phase 5: the dispatcher's PRE-CLAIM window ----------------------------
# fleet-dispatcher takes a `feedback` target's amending-claim BEFORE
# fleet-dispatch-wrap mints the iteration's FLEET_DISPATCH_ID, so the claim it
# writes carries the FLEET_PRECLAIM_DISPATCH_ID sentinel rather than a minted
# id. The pane it is launching into is idle by construction, so the PANE
# heartbeat there is the PREVIOUS iteration's and is typically stale — and when
# the label is one carried over past TTL from that earlier iteration (a
# re-dispatch of the same PR into the same pane re-acquires the existing label
# without resetting its created_at), deferring to the heartbeat lets this sweep
# delete the dispatcher's own fresh pre-claim and admit a second feedback
# worker: the double-amend the pre-claim exists to prevent.
#
# pool-10/11/12 deliberately have NO heartbeat — that IS the reviewer's
# scenario. Default stub age (2020) keeps every case far past the TTL, so a
# label kept here was kept on the pre-claim verdict and nothing else.
rm -f "$HOME/.fleet/heartbeats/pool-10" "$HOME/.fleet/heartbeats/pool-11" \
      "$HOME/.fleet/heartbeats/pool-12"
_now=$(date +%s)
# 817 fresh pre-claim, stale pane             -> kept (the window under test)
# 818 pre-claim aged past the 300s grace      -> swept (falls back to the
#                                                pane rule, which is stale)
# 819 fresh pre-claim, dispatch-current holds
#     the PREVIOUS iteration's real id        -> kept: the sentinel is the
#                                                ABSENCE of an id, never a
#                                                superseded one
write_snap 817 pool-10 preclaim "$_now"
write_snap 818 pool-11 preclaim "$(( _now - 900 ))"
write_snap 819 pool-12 preclaim "$_now"; printf 'D7\n' > "$DISPATCH_DIR/pool-12"
: > "$REMOVED_FILE"
cat > "$PRS_JSON" <<'JSON'
[
  {"number":817,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-10"}]},
  {"number":818,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-11"}]},
  {"number":819,"labels":[{"name":"fleet:wip"},{"name":"fleet:amending-mac-pool-12"}]}
]
JSON
echo "=== phase 5: dispatcher pre-claim window (stale pane heartbeat, past TTL) ==="
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 | sed 's/^/    /'
assert_removed_absent $'817\tfleet:amending-mac-pool-10' \
    "a fresh pre-claim survives a stale pane heartbeat on a past-TTL label"
assert_removed_contains $'818\tfleet:amending-mac-pool-11' \
    "a pre-claim past its grace falls back to the pane rule and is swept"
assert_removed_absent $'819\tfleet:amending-mac-pool-12' \
    "the pre-claim sentinel is never read as a dispatch superseded by the pane's last id"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
