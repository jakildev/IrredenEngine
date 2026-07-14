#!/usr/bin/env bash
# Tests for fleet-rebase's hung-lock escalation (#2362).
#
# Drives the real `fleet-rebase --auto` against a pre-seeded lock directory in a
# temp FLEET_STATE_DIR. The lock check runs before the merger-slice check, so no
# slice is needed. The EXIT trap that removes the lock is registered only AFTER
# acquire succeeds, so on the defer path our seeded lock survives for assertions.
#
# Matrix:
#   alive holder + backdated `started`  -> loud HUNG-LOCK + alert file, still defers
#   alive holder + fresh `started`      -> benign defer only, no alert
#   dead holder                         -> stale-break acquires (no escalation)
#   alive holder + missing `started`    -> no escalation (skip the age check)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "SKIP: fleet-rebase not found/executable at $REBASE" >&2
    exit 0
fi

PASS=0
FAIL=0
TMPROOT=""
LIVE_PIDS=()
cleanup() {
    local p
    for p in "${LIVE_PIDS[@]:-}"; do
        [[ -n "$p" ]] && kill "$p" 2>/dev/null
        true  # never let a kill of an already-dead holder fail the EXIT trap (set -e)
    done
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
    return 0
}
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

SDIR="$TMPROOT/state"
ADIR="$TMPROOT/alerts"
LOCK="$SDIR/fleet-rebase.lock.d"
ALERT="$ADIR/fleet-rebase-hung-lock"
mkdir -p "$SDIR"

# Spawn a fresh long-lived process to stand in for an "alive holder"; sets
# HOLDER_PID. A dedicated holder per subtest avoids the job-control reaping that
# would silently kill a single shared background pid across command
# substitutions. Sets a global (not echoed via $(...)) so the `sleep &` runs in
# THIS shell — a subshell background job would leak past cleanup's tracking.
HOLDER_PID=""
spawn_live() {
    sleep 600 &
    HOLDER_PID=$!
    LIVE_PIDS+=("$HOLDER_PID")
}

run_rebase() {  # runs the real script against the temp dirs; captures combined output
    FLEET_STATE_DIR="$SDIR" \
    FLEET_ALERTS_DIR="$ADIR" \
    "$REBASE" --auto 2>&1 || true
}

seed_lock() {   # $1 = pid, $2 = started epoch ("" = omit the started file)
    rm -rf "$LOCK" "$ADIR"
    mkdir -p "$LOCK"
    echo "$1" > "$LOCK/pid"
    [[ -n "$2" ]] && echo "$2" > "$LOCK/started"
    return 0
}

now=$(date +%s)

# --- T1: alive holder held past the ceiling -> escalate ----------------------
echo "T1: alive holder + backdated started -> HUNG-LOCK + alert, still defers"
spawn_live; hp=$HOLDER_PID
seed_lock "$hp" "$(( now - 4000 ))"   # 4000s > default 1800s ceiling
out=$(run_rebase)
echo "$out" | grep -q "HUNG-LOCK" && ok "loud HUNG-LOCK logged" || fail "no HUNG-LOCK: $out"
echo "$out" | grep -q "deferring to the LLM pass" && ok "still defers (does not break an alive lock)" || fail "did not defer: $out"
[[ -f "$ALERT" ]] && ok "alert file written" || fail "no alert file at $ALERT"
grep -q "holder_pid=$hp" "$ALERT" 2>/dev/null && ok "alert names the holder pid" || fail "alert missing holder pid"
[[ -f "$LOCK/pid" ]] && ok "seeded lock left intact (not broken)" || fail "lock was removed on the defer path"

# --- T2: alive holder, fresh -> benign defer only ----------------------------
echo "T2: alive holder + fresh started -> benign defer, no escalation"
spawn_live; hp=$HOLDER_PID
seed_lock "$hp" "$now"
out=$(run_rebase)
echo "$out" | grep -q "deferring to the LLM pass" && ok "defers" || fail "did not defer: $out"
echo "$out" | grep -q "HUNG-LOCK" && fail "escalated a fresh holder: $out" || ok "no HUNG-LOCK for a fresh holder"
[[ -f "$ALERT" ]] && fail "wrote an alert for a fresh holder" || ok "no alert file for a fresh holder"

# --- T3: dead holder -> stale-break acquires, no escalation ------------------
echo "T3: dead holder -> breaks the stale lock, no escalation"
# A pid that has already exited and been reaped: the subshell running `echo $$`
# is collected by the command substitution, so this pid is dead (kill -0 fails)
# with no lingering holder to clean up and no job-control `wait` warning.
dead=$(sh -c 'echo $$')
seed_lock "$dead" "$(( now - 4000 ))"     # old, but the holder is dead
out=$(run_rebase)
echo "$out" | grep -q "breaking stale lock" && ok "breaks a dead holder's lock" || fail "did not break dead lock: $out"
echo "$out" | grep -q "HUNG-LOCK" && fail "escalated a dead holder: $out" || ok "no escalation for a dead holder"

# --- T4: missing started stamp -> no escalation (skip the age check) ----------
echo "T4: alive holder, missing started -> no escalation"
spawn_live; hp=$HOLDER_PID
seed_lock "$hp" ""                         # pid but no started file
out=$(run_rebase)
echo "$out" | grep -q "deferring to the LLM pass" && ok "defers" || fail "did not defer: $out"
echo "$out" | grep -q "HUNG-LOCK" && fail "escalated without a started stamp: $out" || ok "no escalation when started is absent"
[[ -f "$ALERT" ]] && fail "wrote an alert without a started stamp" || ok "no alert without a started stamp"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
