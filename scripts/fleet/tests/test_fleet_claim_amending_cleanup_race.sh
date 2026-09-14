#!/usr/bin/env bash
# The amending-claim × `cleanup --gh` interleaving: a claim that returned
# success must still hold its fleet:amending-* label once an overlapping
# cleanup pass has finished.
#
# The two run as independent processes (the dispatcher pre-claims; the scout
# launches cleanup) and both decide on the same label from local state before
# acting on GitHub. Unsynchronised, cleanup judges a carried label dead from
# the OLD ownership record, the claim sees its own label still present and
# stamps a fresh record, and cleanup then removes the label off its earlier
# verdict — the dispatcher launches holding nothing. The per-PR amend lock
# makes each side's read-decide-act a unit; this suite pins the interleaving
# that lock exists for.
#
# Ordering is forced through the gh stub, as in test_fleet_claim_cross_lane.sh:
# cleanup's `issue edit --remove-label` parks at a gate AFTER its verdict is
# taken, the claim's `issue view` precheck marks its arrival, and the test opens
# the gate only once both have reached those points. Without the lock the
# claim finishes inside the window and its label is removed behind it; with
# the lock the claim waits for cleanup to leave, re-reads the live labels,
# and re-POSTs.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-amend-race.XXXXXX")
trap 'rm -rf "$TMPROOT"' EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_AMEND_SNAPSHOTS_DIR="$TMPROOT/amend-snapshots"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST=mac
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=3
export FLEET_PRECLAIM_DISPATCH_ID=preclaim
export CLAIM_STATE="$TMPROOT/labels"
export CLAIM_POST_LOG="$TMPROOT/posts"
export REMOVED_LOG="$TMPROOT/removed"
export CLAIM_RUN="$TMPROOT/run"
export STUB_AGE=600
PR=4301
AGENT=poolA
LABEL="fleet:amending-mac-$AGENT"
DISPATCH_DIR="$FLEET_STATE_DIR/dispatch-current"
mkdir -p "$FLEET_HEARTBEATS_DIR" "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" \
    "$FLEET_AMEND_SNAPSHOTS_DIR" "$DISPATCH_DIR" "$FLEET_ORPHANS_DIR" "$TMPROOT/bin"

# One label store, read by every gh verb the two commands use. The gate on
# `issue edit --remove-label` and the arrival marker on `issue view` are live
# only while $CLAIM_RUN exists (T1); the sequential cases run ungated.
cat > "$TMPROOT/bin/gh" <<'GH_STUB'
#!/usr/bin/env bash
set -euo pipefail

lock_state() {
    local tick
    for ((tick = 0; tick < 500; tick++)); do
        mkdir "${CLAIM_STATE}.lock" 2>/dev/null && return 0
        sleep 0.01
    done
    echo "gh stub: timed out acquiring state lock" >&2
    return 1
}
unlock_state() { rmdir "${CLAIM_STATE}.lock"; }

emit_labels() {
    local first=1 label
    printf '['
    while IFS= read -r label; do
        [[ -n "$label" ]] || continue
        [[ "$first" -eq 1 ]] || printf ','
        printf '{"name":"%s"}' "$label"
        first=0
    done < "$CLAIM_STATE"
    printf ']'
}

# await_gate <tag>: mark arrival, then block until the test opens the gate.
await_gate() {
    local tag="$1" tick
    [[ -d "$CLAIM_RUN" ]] || return 0
    : > "$CLAIM_RUN/arrived-$tag"
    for ((tick = 0; tick < 1500; tick++)); do
        [[ -f "$CLAIM_RUN/gate-$tag" ]] && return 0
        sleep 0.01
    done
    echo "gh stub: timed out waiting for $tag gate" >&2
    exit 1
}

case "${1:-} ${2:-}" in
    "pr list")
        lock_state
        printf '[{"number":%s,"labels":' "${STUB_PR:-4301}"
        emit_labels
        printf '}]\n'
        unlock_state
        ;;
    "issue list") echo '[]' ;;
    "issue view")
        if [[ -d "$CLAIM_RUN" ]]; then : > "$CLAIM_RUN/arrived-view"; fi
        lock_state
        printf '{"state":"OPEN","labels":'
        emit_labels
        printf ',"body":""}\n'
        unlock_state
        ;;
    "issue edit")
        remove=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == "--remove-label" ]]; then
                shift
                remove="${1:-}"
            fi
            shift || true
        done
        [[ -n "$remove" ]] || exit 0
        await_gate remove
        lock_state
        : > "${CLAIM_STATE}.next"
        while IFS= read -r label; do
            [[ "$label" == "$remove" ]] || printf '%s\n' "$label" >> "${CLAIM_STATE}.next"
        done < "$CLAIM_STATE"
        mv "${CLAIM_STATE}.next" "$CLAIM_STATE"
        printf '%s\n' "$remove" >> "$REMOVED_LOG"
        unlock_state
        ;;
    "api "*)
        if printf '%s ' "$@" | grep -q 'events'; then
            python3 -c "import time;print(time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(time.time()-${STUB_AGE})))"
            exit 0
        fi
        posted=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                labels\[\]=*) posted="${1#labels[]=}" ;;
            esac
            shift || true
        done
        lock_state
        if [[ -n "$posted" ]]; then
            present=0
            while IFS= read -r label; do
                [[ "$label" == "$posted" ]] && present=1
            done < "$CLAIM_STATE"
            [[ "$present" -eq 1 ]] || printf '%s\n' "$posted" >> "$CLAIM_STATE"
            printf '%s\n' "$posted" >> "$CLAIM_POST_LOG"
        fi
        emit_labels
        printf '\n'
        unlock_state
        ;;
    *) ;;
esac
GH_STUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

# The carried label: owned by dispatch D1, whose worktree has since launched
# D2 — a confirmed orphan (verdict 2), past the 120 s grace at STUB_AGE=600
# and inside the 30-min TTL. No heartbeat for the agent, so nothing but the
# ownership record can vouch for the label.
reset_fixture() {
    rm -rf "$CLAIM_RUN" "${CLAIM_STATE}.lock" "$FLEET_AMEND_SNAPSHOTS_DIR"/* \
        "$FLEET_HEARTBEATS_DIR"/* "$FLEET_ORPHANS_DIR"/*
    printf '%s\n' "fleet:wip" "$LABEL" > "$CLAIM_STATE"
    : > "$CLAIM_POST_LOG"
    : > "$REMOVED_LOG"
    printf '{"pr":%s,"agent":"%s","acquired_epoch":%s,"dispatch_id":"D1"}\n' \
        "$PR" "$AGENT" "$(( $(date +%s) - 3600 ))" > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.json"
    printf 'D2\n' > "$DISPATCH_DIR/$AGENT"
}

label_present() { grep -qxF "$LABEL" "$CLAIM_STATE"; }
record_dispatch() {
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("dispatch_id",""))' \
        "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.json" 2>/dev/null || echo "(no record)"
}
wait_for_file() {
    local path="$1" tick
    for ((tick = 0; tick < 1500; tick++)); do
        [[ -e "$path" ]] && return 0
        sleep 0.01
    done
    return 1
}

echo "T1: cleanup's verdict is taken, the pre-claim lands in its window, cleanup then removes"
reset_fixture
mkdir -p "$CLAIM_RUN"
cleanup_out="$TMPROOT/cleanup.out"
claim_out="$TMPROOT/claim.out"
claim_rc_file="$TMPROOT/claim.rc"
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 &
cleanup_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-remove"; then
    ok "cleanup reached its removal off the D1 record (verdict taken, label not yet removed)"
else
    bad "cleanup never reached the removal gate"
fi
(rc=0; FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
 printf '%s\n' "$rc" > "$claim_rc_file") &
claim_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-view"; then
    ok "pre-claim's precheck read the live labels while cleanup was parked at the removal"
else
    bad "pre-claim never reached its precheck"
fi
# Both sides are past their unlocked reads. Let cleanup act.
: > "$CLAIM_RUN/gate-remove"
wait "$cleanup_pid" || true
wait "$claim_pid" || true
claim_rc=$(cat "$claim_rc_file" 2>/dev/null || echo "?")
echo "  cleanup: $(grep -c . "$cleanup_out") line(s); claim rc=$claim_rc"
sed 's/^/    /' "$cleanup_out"
sed 's/^/    /' "$claim_out"
assert_eq "$claim_rc" "0" "pre-claim returned success"
if label_present; then
    ok "the successful pre-claim still holds $LABEL after the overlapping cleanup pass"
else
    bad "cleanup removed $LABEL from under a pre-claim that had already returned success"
fi
assert_eq "$(record_dispatch)" "preclaim" \
    "ownership record carries the pre-claim sentinel, not the dead D1 dispatch"
if grep -qxF "$LABEL" "$REMOVED_LOG"; then
    ok "cleanup did reap the dead D1 claim (the removal is legitimate — its verdict was on the old record)"
else
    bad "cleanup never removed the D1 label; the interleaving under test did not occur"
fi
if grep -qxF "$LABEL" "$CLAIM_POST_LOG"; then
    ok "the pre-claim re-POSTed after re-reading the live labels inside the lock"
else
    bad "the pre-claim never re-POSTed; it must have stamped an incumbent label that was then removed"
fi
assert_absent "$(cat "$claim_out")" "still held after" \
    "the claim waited for the lock rather than timing out on it"
rm -rf "$CLAIM_RUN"

echo "T2: the pre-claim lands first; cleanup's verdict inside the lock sees it live"
reset_fixture
rc=0
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "0" "pre-claim on the carried label succeeds (incumbent path)"
assert_eq "$(record_dispatch)" "preclaim" "incumbent re-acquire rewrote the record"
# Past the TTL, so nothing but the liveness verdict can keep the label.
STUB_AGE=2000 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
sed 's/^/    /' "$cleanup_out"
assert_contains "$(cat "$cleanup_out")" "same-host owning dispatch still live" \
    "cleanup's locked verdict reads the fresh pre-claim as live on a past-TTL label"
if label_present; then
    ok "label kept"
else
    bad "label removed despite a fresh pre-claim record"
fi

echo "T3: a held lock is waited on, never bypassed — cleanup leaves the label for its next pass"
reset_fixture
mkdir "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
FLEET_AMEND_LOCK_WAIT_SECS=1 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine \
    > "$cleanup_out" 2>&1 || true
assert_contains "$(cat "$cleanup_out")" "amend lock held" \
    "cleanup reports the skipped label"
if label_present; then
    ok "the orphaned label survives this pass (fail closed)"
else
    bad "cleanup removed the label without the lock"
fi
rc=0
FLEET_AMEND_LOCK_WAIT_SECS=1 FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" \
    > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "1" "amending-claim refuses while the lock is held (exit 1)"
assert_eq "$(record_dispatch)" "D1" "a refused claim leaves the ownership record untouched"
rmdir "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"

echo "T4: a lock older than FLEET_AMEND_LOCK_STALE_SECS is a crashed holder and is stolen"
reset_fixture
mkdir "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
rc=0
FLEET_AMEND_LOCK_WAIT_SECS=2 FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" \
    > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "0" "claim succeeds through the stolen lock"
assert_contains "$(cat "$claim_out")" "stealing amend lock" "the theft is logged"
if [[ -d "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" ]]; then
    bad "lock left behind after the claim returned"
else
    ok "lock released on return"
fi

summarize "fleet-claim amending × cleanup race"
