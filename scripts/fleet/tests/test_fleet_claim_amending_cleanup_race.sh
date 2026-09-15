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
#
# The lock's own recovery path is pinned the same way (T5): a holder stalled
# past FLEET_AMEND_LOCK_STALE_SECS is stolen, and when it resumes its release
# must not remove the successor's lock. Both holders park at their POST
# (inside the lock) under a per-process GATE_TAG, so the test can back-date the
# stalled holder's lock, let the successor steal it, and then release the
# stalled holder while the successor is still inside its critical section.
#
# A stalled SWEEP is the other resumable holder, and its removal is a
# mutation nothing can recall once issued. T6 is the interleaving that
# matters across hosts: the removal is in flight, the label's own agent
# claims, the removal lands, and a claimant on another host — which sees only
# the GitHub label set — acquires before the local sweep has noticed. Exactly
# one of the two claims may return success, and it must be the one whose
# label is on the PR. T9 is the same stall earlier in the section (inside the
# age lookup), where the intent has to already exist. STUB_GATE_REMOVED and
# STUB_GATE_EVENTS add the gates those two need. T10 and T11 pin the intent
# as a fence a successor can never read as absent: it is published whole,
# and one that cannot be parsed still refuses every claim until it is old
# enough to belong to a presumed-dead writer.

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
        # Landed on GitHub's side; the caller has not yet returned.
        [[ -z "${STUB_GATE_REMOVED:-}" ]] || await_gate removed
        ;;
    "api "*)
        if printf '%s ' "$@" | grep -q 'events'; then
            [[ -z "${STUB_GATE_EVENTS:-}" ]] || await_gate events
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
        if [[ -n "$posted" && -n "${GATE_TAG:-}" ]]; then await_gate "post-$GATE_TAG"; fi
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
# plant_lock <token>: a held lock as fleet-claim writes it — the directory
# plus the holder's token file.
plant_lock() {
    mkdir "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
    : > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock/owner-$1"
}
lock_owner() {
    ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" 2>/dev/null | sed -n 's/^owner-//p' | tr '\n' ' '
}
wait_for_file() {
    local path="$1" tick
    for ((tick = 0; tick < 1500; tick++)); do
        [[ -e "$path" ]] && return 0
        sleep 0.01
    done
    return 1
}
wait_for_text() {
    local path="$1" text="$2" tick
    for ((tick = 0; tick < 1500; tick++)); do
        grep -qF "$text" "$path" 2>/dev/null && return 0
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
plant_lock live-holder
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
assert_eq "$(lock_owner)" "live-holder " "the refused claim left the holder's token in place"
rm -rf "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"

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

echo "T5: a stale holder that resumes after its lock was stolen neither releases nor overrides the successor"
reset_fixture
printf '%s\n' "fleet:wip" > "$CLAIM_STATE"
mkdir -p "$CLAIM_RUN"
: > "$CLAIM_RUN/gate-remove"
stalled_out="$TMPROOT/stalled.out"
stalled_rc_file="$TMPROOT/stalled.rc"
successor_out="$TMPROOT/successor.out"
successor_rc_file="$TMPROOT/successor.rc"
(rc=0; GATE_TAG=stalled FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" \
    > "$stalled_out" 2>&1 || rc=$?
 printf '%s\n' "$rc" > "$stalled_rc_file") &
stalled_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-post-stalled"; then
    ok "the first claimant holds the lock and is parked inside its critical section"
else
    bad "the first claimant never reached its POST"
fi
stalled_token=$(lock_owner)
assert_contains "$stalled_token" "-" "the held lock carries its holder's token"
# Age the lock past the stale threshold while its holder is still alive.
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
(rc=0; GATE_TAG=successor FLEET_DISPATCH_ID=D3 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" \
    > "$successor_out" 2>&1 || rc=$?
 printf '%s\n' "$rc" > "$successor_rc_file") &
successor_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-post-successor"; then
    ok "the successor stole the stale lock and is parked inside its own critical section"
else
    bad "the successor never reached its POST"
fi
successor_token=$(lock_owner)
if [[ -n "$successor_token" && "$successor_token" != "$stalled_token" ]]; then
    ok "the lock now carries the successor's token, not the stalled holder's"
else
    bad "lock owner after the steal: '$successor_token' (stalled holder's: '$stalled_token')"
fi
# The stalled holder resumes and finishes its POST while the successor is
# still inside its critical section.
: > "$CLAIM_RUN/gate-post-stalled"
if wait_for_text "$stalled_out" "the claim is void"; then
    ok "the resumed holder found its token gone and voided its claim"
else
    bad "the resumed holder never reported the lost lock"
fi
if [[ -d "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" ]]; then
    ok "the successor's lock survives the stalled holder's resume"
else
    bad "the stalled holder removed the successor's lock"
fi
assert_eq "$(lock_owner)" "$successor_token" "the surviving lock is still the successor's"
assert_contains "$(cat "$stalled_out")" "ownership record not stamped" \
    "the resumed holder did not stamp the ownership record over the successor's"
: > "$CLAIM_RUN/gate-post-successor"
wait "$successor_pid" || true
wait "$stalled_pid" || true
assert_eq "$(cat "$successor_rc_file" 2>/dev/null || echo "?")" "0" "the successor completes its claim"
assert_eq "$(cat "$stalled_rc_file" 2>/dev/null || echo "?")" "1" \
    "the stalled holder reports failure — it cannot vouch for an outcome the successor arbitrated"
assert_eq "$(record_dispatch)" "D3" "the ownership record is the successor's"
assert_contains "$(cat "$successor_out")" "stealing amend lock" "the theft is logged by the successor"
assert_absent "$(cat "$stalled_out")" "stealing amend lock" "the stalled holder never stole anything"
assert_absent "$(cat "$stalled_out")" "dropped this claim's label" \
    "the same agent's successor now owns the label, so the voided claim left it alone"
if [[ -d "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" ]]; then
    bad "lock left behind after both claims returned"
else
    ok "no lock left behind"
fi
if label_present; then
    ok "the amending label is on the PR after both claims"
else
    bad "the amending label is missing after both claims"
fi
rm -rf "$CLAIM_RUN"

echo "T5b: a stale holder of ANOTHER agent that resumes after the successor finished drops its label"
reset_fixture
printf '%s\n' "fleet:wip" > "$CLAIM_STATE"
mkdir -p "$CLAIM_RUN"
: > "$CLAIM_RUN/gate-remove"
OTHER=poolB
OTHER_LABEL="fleet:amending-mac-$OTHER"
(rc=0; GATE_TAG=stalled FLEET_DISPATCH_ID=D5 "$FLEET_CLAIM" amending-claim "$PR" "$OTHER" \
    > "$stalled_out" 2>&1 || rc=$?
 printf '%s\n' "$rc" > "$stalled_rc_file") &
stalled_pid=$!
wait_for_file "$CLAIM_RUN/arrived-post-stalled" || bad "poolB never reached its POST"
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
rc=0
FLEET_DISPATCH_ID=D3 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$successor_out" 2>&1 || rc=$?
assert_eq "$rc" "0" "poolA steals the stale lock and completes its claim"
: > "$CLAIM_RUN/gate-post-stalled"
wait "$stalled_pid" || true
sed 's/^/    /' "$stalled_out"
assert_eq "$(cat "$stalled_rc_file" 2>/dev/null || echo "?")" "1" "poolB's resumed claim fails"
assert_contains "$(cat "$stalled_out")" "the claim is void" "poolB reports the lost lock"
if grep -qxF "$OTHER_LABEL" "$CLAIM_STATE"; then
    bad "$OTHER_LABEL left on the PR beside poolA's label"
else
    ok "$OTHER_LABEL is not on the PR — no double hold"
fi
assert_eq "$(record_dispatch)" "D3" "the ownership record is poolA's, not overwritten by the resumed poolB"
if label_present; then ok "poolA's label is on the PR"; else bad "poolA's label is missing"; fi
rm -rf "$CLAIM_RUN"

echo "T6: removal in flight, same-agent claim, removal lands, foreign-host claim — at most one success"
# The removal has been decided and issued but has not landed; the lock goes
# stale under it; the label's own agent claims. Once the removal lands and
# before the sweep has noticed, a claimant on another host acquires. The
# local claim must have refused: had it returned success, the PR would now
# carry two owners and nothing local could revoke the first.
reset_fixture
mkdir -p "$CLAIM_RUN"
STUB_GATE_REMOVED=1 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 &
cleanup_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-remove"; then
    ok "cleanup is parked inside its gh remove-label call, lock held"
else
    bad "cleanup never reached the removal"
fi
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then
    ok "cleanup's sweep intent is beside the lock while the removal is in flight"
else
    bad "no sweep intent beside the lock while the removal is in flight"
fi
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
rc=0
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
sed 's/^/    /' "$claim_out"
local_rc=$rc
assert_eq "$rc" "1" "the label's own agent is refused while its removal is in flight"
assert_contains "$(cat "$claim_out")" "stealing amend lock" "the theft is logged"
assert_contains "$(cat "$claim_out")" "still in flight" "the refusal names the in-flight removal"
assert_eq "$(record_dispatch)" "D1" "the refused claim stamped nothing"
if label_present; then ok "the label was still on the PR when the claim was refused"; else bad "label missing before the removal landed"; fi
# The removal lands; cleanup has not returned from its gh call yet.
: > "$CLAIM_RUN/gate-remove"
if wait_for_file "$CLAIM_RUN/arrived-removed"; then
    ok "the stalled removal landed after the refused claim (the interleaving under test occurred)"
else
    bad "the removal never landed"
fi
if label_present; then bad "label still present after the removal landed"; else ok "the PR carries no amending label in the window"; fi
# Another host's claimant sees only the GitHub label set: no local intent,
# its own record and lock directories.
FOREIGN_LABEL="fleet:amending-linux-$AGENT"
foreign_snap="$TMPROOT/amend-snapshots-linux"
mkdir -p "$foreign_snap" "$TMPROOT/heartbeats-linux"
rc=0
FLEET_TEST_HOST=linux FLEET_AMEND_SNAPSHOTS_DIR="$foreign_snap" \
    FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats-linux" FLEET_DISPATCH_ID=L1 \
    "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$successor_out" 2>&1 || rc=$?
sed 's/^/    /' "$successor_out"
foreign_rc=$rc
assert_eq "$rc" "0" "the foreign-host claimant wins the window (it can see nothing but the label set)"
if grep -qxF "$FOREIGN_LABEL" "$CLAIM_STATE"; then ok "$FOREIGN_LABEL is on the PR"; else bad "$FOREIGN_LABEL missing after a successful claim"; fi
# The local sweep resumes, finds its lock gone, and must not touch the labels.
: > "$CLAIM_RUN/gate-removed"
wait "$cleanup_pid" || true
sed 's/^/    /' "$cleanup_out"
assert_contains "$(cat "$cleanup_out")" "stolen while" "cleanup detected the lost lock after its removal returned"
assert_absent "$(cat "$cleanup_out")" "restored" "nothing is re-added from local state"
assert_absent "$(cat "$cleanup_out")" "removed stale" "cleanup did not count the fenced removal as a sweep"
amending_count=$(grep -c '^fleet:amending-' "$CLAIM_STATE" || true)
assert_eq "$amending_count" "1" "exactly one amending label on the PR after the sweep resumed"
if label_present; then bad "$LABEL was re-added beside the foreign holder's — two owners"; else ok "$LABEL stays off the PR"; fi
successes=$(( (local_rc == 0) + (foreign_rc == 0) ))
assert_eq "$successes" "1" "exactly one of the two claims returned success"
assert_eq "$(record_dispatch)" "D1" "the local record is untouched (no local claim vouched for anything)"
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then bad "sweep intent left behind"; else ok "sweep intent retired"; fi
if [[ -d "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" ]]; then bad "lock left behind"; else ok "no lock left behind"; fi
# After the window the local agent's retry arbitrates on the labels alone
# and yields to the holder it can now see.
rc=0
FLEET_DISPATCH_ID=D4 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "1" "the local agent's retry yields to the foreign holder"
amending_count=$(grep -c '^fleet:amending-' "$CLAIM_STATE" || true)
assert_eq "$amending_count" "1" "still exactly one amending label after the retry"
rm -rf "$CLAIM_RUN" "$foreign_snap" "$TMPROOT/heartbeats-linux"

echo "T6b: control — the same resume with NO successor leaves the removal standing"
reset_fixture
mkdir -p "$CLAIM_RUN"
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 &
cleanup_pid=$!
wait_for_file "$CLAIM_RUN/arrived-remove" || bad "cleanup never reached the removal"
# Steal the lock with nothing behind it: the successor is a claim that fails
# its host gate before touching the record (refused on the precheck).
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
rm -rf "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
: > "$CLAIM_RUN/gate-remove"
wait "$cleanup_pid" || true
sed 's/^/    /' "$cleanup_out"
assert_contains "$(cat "$cleanup_out")" "stolen while" "cleanup detected the lost lock"
assert_absent "$(cat "$cleanup_out")" "restored" "nothing is re-added"
if label_present; then
    bad "the dead D1 label was resurrected with no successor behind it"
else
    ok "the dead D1 label stays removed"
fi
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then bad "sweep intent left behind"; else ok "sweep intent retired"; fi
rm -rf "$CLAIM_RUN"

echo "T7: a dead writer's dangling intent is retired by the next lock holder, never acted on"
reset_fixture
# The writer removed the label and died before retiring its intent.
printf '%s\n' "fleet:wip" > "$CLAIM_STATE"
dead_pid=$(bash -c 'echo $$')
printf '{"pr":%s,"repo":"jakildev/IrredenEngine","label":"%s","pid":%s}\n' \
    "$PR" "$LABEL" "$dead_pid" > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-dead-1-1"
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
sed 's/^/    /' "$cleanup_out"
if [[ -f "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-dead-1-1" ]]; then
    bad "dangling intent left behind"
else
    ok "dangling intent retired by cleanup's pass over intent-bearing PRs"
fi
if label_present; then
    bad "a dead writer's intent re-added the label"
else
    ok "the landed removal stands — nothing is re-added from local state"
fi
rc=0
FLEET_DISPATCH_ID=D4 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "0" "the label's agent recovers by POSTing afresh"
if grep -qxF "$LABEL" "$CLAIM_POST_LOG"; then ok "the recovery was a fresh POST"; else bad "the recovery did not POST"; fi
assert_eq "$(record_dispatch)" "D4" "the fresh claim stamped its record"
# The other shape: the writer died BEFORE issuing the removal, so the label
# is still there. The next holder retires the intent and re-judges the label.
reset_fixture
printf '{"pr":%s,"repo":"jakildev/IrredenEngine","label":"%s","pid":%s}\n' \
    "$PR" "$LABEL" "$dead_pid" > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-dead-1-2"
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
sed 's/^/    /' "$cleanup_out"
[[ -f "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-dead-1-2" ]] && bad "dead intent left behind" || ok "dead intent retired"
assert_contains "$(cat "$cleanup_out")" "removed stale" "the dead D1 label is re-judged and swept in the same pass"
if label_present; then bad "the dead D1 label survived the pass"; else ok "the dead D1 label is gone"; fi

echo "T8: a LIVE writer's in-flight removal refuses every claim and holds the sweep"
reset_fixture
sleep 30 &
live_pid=$!
printf '{"pr":%s,"repo":"jakildev/IrredenEngine","label":"%s","pid":%s}\n' \
    "$PR" "$LABEL" "$live_pid" > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-live-1-1"
rc=0
FLEET_DISPATCH_ID=D9 "$FLEET_CLAIM" amending-claim "$PR" "poolB" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "1" "another agent's claim is refused while the removal is in flight"
assert_contains "$(cat "$claim_out")" "still in flight" "the refusal names the in-flight removal"
if grep -qxF "fleet:amending-mac-poolB" "$CLAIM_STATE"; then
    bad "the refused claim left its label on the PR"
else
    ok "the refused claim posted nothing"
fi
STUB_AGE=2000 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
assert_contains "$(cat "$cleanup_out")" "still in flight" "cleanup skips the label for this pass"
if label_present; then ok "cleanup left the label alone"; else bad "cleanup swept under an in-flight removal"; fi
rc=0
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "1" "the label's OWN agent is refused too — the landing could take the label from a success"
assert_contains "$(cat "$claim_out")" "still in flight" "the own-agent refusal names the in-flight removal"
assert_eq "$(record_dispatch)" "D1" "the own-agent refusal stamped nothing"
if [[ -f "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-live-1-1" ]]; then
    ok "a live writer's intent is left for the writer to retire"
else
    bad "a live writer's intent was retired by someone else"
fi
kill "$live_pid" 2>/dev/null || true
wait "$live_pid" 2>/dev/null || true

echo "T9: a sweep stalled BEFORE its removal (inside the age lookup) is fenced the same way"
# The intent must exist from the section's first gh call, or a holder stalled
# there is stolen with nothing declaring its label, a claim succeeds, and the
# resumed sweep's removal lands on it.
reset_fixture
mkdir -p "$CLAIM_RUN"
STUB_GATE_EVENTS=1 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 &
cleanup_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-events"; then
    ok "cleanup is parked inside its age lookup, lock held, no removal issued"
else
    bad "cleanup never reached the age lookup"
fi
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then
    ok "the sweep declared its label on entry, before the age lookup"
else
    bad "no sweep intent while the sweep is parked before its removal"
fi
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
rc=0
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
sed 's/^/    /' "$claim_out"
assert_eq "$rc" "1" "the label's own agent is refused while the sweep is live anywhere in its section"
assert_contains "$(cat "$claim_out")" "still in flight" "the refusal names the in-flight sweep"
assert_eq "$(record_dispatch)" "D1" "the refused claim stamped nothing"
: > "$CLAIM_RUN/gate-events"
wait "$cleanup_pid" || true
sed 's/^/    /' "$cleanup_out"
assert_contains "$(cat "$cleanup_out")" "stolen before" "the resumed sweep found its lock gone before issuing the removal"
if grep -qxF "$LABEL" "$REMOVED_LOG"; then
    bad "the resumed sweep issued its removal without the lock"
else
    ok "no removal was issued once the lock was lost"
fi
if label_present; then ok "the label is still on the PR"; else bad "the label is gone"; fi
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then bad "sweep intent left behind"; else ok "sweep intent retired"; fi
rm -rf "$CLAIM_RUN"
# Nothing is stuck: the next pass re-judges the label and sweeps it.
"$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
assert_contains "$(cat "$cleanup_out")" "removed stale" "the next pass sweeps the dead D1 label"
if label_present; then bad "the dead D1 label survived the next pass"; else ok "the dead D1 label is gone on the next pass"; fi

echo "T10: a torn intent under a stolen lock is a live fence — the same-agent claim refuses, the removal lands, one owner remains"
# An in-place rewrite of the intent truncates it first, and a stealer
# scanning in that instant reads an empty file, maps it to no writer, deletes
# it, and admits a claim the resumed removal then lands on. The publish is a
# rename, so the code never produces that state; this case plants it by hand
# and pins the scan's other half — an intent it cannot read is still a fence.
reset_fixture
mkdir -p "$CLAIM_RUN"
STUB_GATE_REMOVED=1 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 &
cleanup_pid=$!
if wait_for_file "$CLAIM_RUN/arrived-remove"; then
    ok "cleanup is parked inside its gh remove-label call, lock held"
else
    bad "cleanup never reached the removal"
fi
torn_intent=$(ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* 2>/dev/null | head -1)
intent_pid=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("pid",""))' \
    "$torn_intent" 2>/dev/null || echo "(unreadable)")
assert_eq "$intent_pid" "$cleanup_pid" \
    "the re-published intent is whole and names the sweep's own pid"
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.intent-"*.tmp >/dev/null 2>&1; then
    bad "a staged intent was left beside the published one"
else
    ok "the publish left no staged file behind"
fi
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock"
# The pre-rename publish's truncate instant, frozen.
: > "$torn_intent"
rc=0
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
sed 's/^/    /' "$claim_out"
local_rc=$rc
assert_eq "$rc" "1" "the label's own agent is refused on an intent it cannot read"
assert_contains "$(cat "$claim_out")" "stealing amend lock" "the theft is logged"
assert_contains "$(cat "$claim_out")" "unreadable intent" "the refusal names the unreadable intent"
assert_eq "$(record_dispatch)" "D1" "the refused claim stamped nothing"
if [[ -f "$torn_intent" ]]; then
    ok "the unreadable intent was left for its writer, not deleted"
else
    bad "the scan deleted the unreadable intent"
fi
if label_present; then ok "the label was still on the PR when the claim was refused"; else bad "label missing before the removal landed"; fi
: > "$CLAIM_RUN/gate-remove"
if wait_for_file "$CLAIM_RUN/arrived-removed"; then
    ok "the stalled removal landed after the refused claim"
else
    bad "the removal never landed"
fi
FOREIGN_LABEL="fleet:amending-linux-$AGENT"
foreign_snap="$TMPROOT/amend-snapshots-linux"
mkdir -p "$foreign_snap" "$TMPROOT/heartbeats-linux"
rc=0
FLEET_TEST_HOST=linux FLEET_AMEND_SNAPSHOTS_DIR="$foreign_snap" \
    FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats-linux" FLEET_DISPATCH_ID=L1 \
    "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$successor_out" 2>&1 || rc=$?
foreign_rc=$rc
assert_eq "$rc" "0" "the foreign-host claimant wins the empty-label window"
: > "$CLAIM_RUN/gate-removed"
wait "$cleanup_pid" || true
sed 's/^/    /' "$cleanup_out"
assert_contains "$(cat "$cleanup_out")" "stolen while" "cleanup detected the lost lock after its removal returned"
assert_absent "$(cat "$cleanup_out")" "restored" "nothing is re-added from local state"
amending_count=$(grep -c '^fleet:amending-' "$CLAIM_STATE" || true)
assert_eq "$amending_count" "1" "exactly one amending label on the PR after the sweep resumed"
if grep -qxF "$FOREIGN_LABEL" "$CLAIM_STATE"; then ok "the one label is the foreign holder's"; else bad "$FOREIGN_LABEL missing"; fi
successes=$(( (local_rc == 0) + (foreign_rc == 0) ))
assert_eq "$successes" "1" "exactly one of the two claims returned success"
assert_eq "$(record_dispatch)" "D1" "the local record is untouched"
if ls "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-"* >/dev/null 2>&1; then bad "sweep intent left behind"; else ok "the resumed sweep retired its torn intent"; fi
if [[ -d "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.lock" ]]; then bad "lock left behind"; else ok "no lock left behind"; fi
rm -rf "$CLAIM_RUN" "$foreign_snap" "$TMPROOT/heartbeats-linux"

echo "T11: an unreadable intent fails closed only inside the presumed-dead bound; past it the next holder retires it"
reset_fixture
printf '%s\n' "fleet:wip" > "$CLAIM_STATE"
: > "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-torn-1-1"
rc=0
FLEET_DISPATCH_ID=D4 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "1" "a fresh unreadable intent refuses the claim"
assert_contains "$(cat "$claim_out")" "unreadable intent" "the refusal names it"
if grep -qxF "$LABEL" "$CLAIM_STATE"; then bad "the refused claim posted its label"; else ok "the refused claim posted nothing"; fi
[[ -f "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-torn-1-1" ]] && ok "the fresh unreadable intent is kept" || bad "the fresh unreadable intent was deleted"
printf '%s\n' "fleet:wip" "$LABEL" > "$CLAIM_STATE"
STUB_AGE=2000 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine > "$cleanup_out" 2>&1 || true
assert_contains "$(cat "$cleanup_out")" "still in flight" "cleanup skips the label under an unreadable intent"
if label_present; then ok "cleanup left the label alone"; else bad "cleanup swept under an unreadable intent"; fi
printf '%s\n' "fleet:wip" > "$CLAIM_STATE"
touch -t 202001010000 "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-torn-1-1"
rc=0
FLEET_DISPATCH_ID=D4 "$FLEET_CLAIM" amending-claim "$PR" "$AGENT" > "$claim_out" 2>&1 || rc=$?
assert_eq "$rc" "0" "an unreadable intent older than FLEET_AMEND_LOCK_STALE_SECS no longer blocks the claim"
[[ -f "$FLEET_AMEND_SNAPSHOTS_DIR/$PR.sweep-torn-1-1" ]] && bad "the aged unreadable intent was left behind" || ok "the aged unreadable intent was retired"
if grep -qxF "$LABEL" "$CLAIM_POST_LOG"; then ok "the claim POSTed afresh"; else bad "the claim did not POST"; fi

summarize "fleet-claim amending × cleanup race"
