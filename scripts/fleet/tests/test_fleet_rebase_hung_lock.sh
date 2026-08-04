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
#
# Escalate-then-quiet (#2795), T5-T8: the wedged holder persists until a human
# acts and the dispatcher keeps re-invoking this one-shot script, so the loud
# line must stop after N while the alert keeps refreshing.
#   N+1 contended invocations           -> loud line once, alert refreshed after
#   a different wedged holder pid       -> key changed, escalates again
#   healthy pass (we get the lock)      -> counter + alert cleared
#   counter wiped every tick            -> positive control: loud line every tick

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "SKIP: fleet-rebase not found/executable at $REBASE" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
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
COUNTER="$SDIR/.fleet-rebase-hung-lock-skip"
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

run_rebase_n() {  # as run_rebase, but with an explicit FLEET_REBASE_HUNG_LOCK_ESCALATE_N.
    # Kept separate so run_rebase keeps exercising the *unset* defaulting arm —
    # passing an empty override there would shadow it (scripts/fleet/CLAUDE.md
    # "Exercise every arm").
    FLEET_STATE_DIR="$SDIR" \
    FLEET_ALERTS_DIR="$ADIR" \
    FLEET_REBASE_HUNG_LOCK_ESCALATE_N="$1" \
    "$REBASE" --auto 2>&1 || true
}

seed_lock() {   # $1 = pid, $2 = started epoch ("" = omit the started file)
    # The streak counter is cleared too, so each subtest escalates from scratch;
    # T5+ deliberately seed once and then re-invoke without re-seeding.
    rm -rf "$LOCK" "$ADIR" "$COUNTER"
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

# --- T5: N+1 contended invocations -> loud once, alert keeps refreshing ------
# The dispatcher re-invokes this one-shot script while the wedge persists, so an
# unguarded `log` would repeat identically forever. Past N stderr goes quiet and
# the alert file becomes the only standing signal — hence the delete-and-reappear
# assertion: a human triaging ~/.fleet/alerts must not be able to silence a
# still-live wedge permanently.
echo "T5: N+1 contended runs -> one loud HUNG-LOCK, then quiet, alert refreshed"
spawn_live; hp=$HOLDER_PID
seed_lock "$hp" "$(( now - 4000 ))"
t5a=$(run_rebase)
alert_at_n=$(cat "$ALERT" 2>/dev/null || true)
rm -f "$ALERT"                                    # human clears the inbox mid-wedge
t5b=$(run_rebase)
t5c=$(run_rebase)
echo "$t5a" | grep -q "HUNG-LOCK" && ok "tick 1 (== N) escalates loudly" || fail "tick 1 did not escalate: $t5a"
echo "$t5a" | grep -q "suppressing further identical lines" && ok "tick 1 announces the suppression" || fail "no suppression notice: $t5a"
echo "$alert_at_n" | grep -q "count=1" && ok "alert at N records count=1" || fail "wrong count at N: $alert_at_n"
echo "$t5b" | grep -q "HUNG-LOCK" && fail "tick 2 (> N) still loud: $t5b" || ok "tick 2 (> N) is quiet"
echo "$t5c" | grep -q "HUNG-LOCK" && fail "tick 3 (> N) still loud: $t5c" || ok "tick 3 (> N) is quiet"
echo "$t5b" | grep -q "deferring to the LLM pass" && ok "quiet ticks still defer normally" || fail "quiet tick stopped deferring: $t5b"
[[ -f "$ALERT" ]] && ok "alert re-created past N (cleared inbox re-arms)" || fail "alert stayed gone past N — wedge permanently silent"
grep -q "count=3" "$ALERT" 2>/dev/null && ok "refreshed alert carries the current count" || fail "stale count past N: $(cat "$ALERT" 2>/dev/null)"

# --- T6: a different wedged holder is news again -----------------------------
echo "T6: new holder pid restarts the streak and re-escalates"
spawn_live; hp2=$HOLDER_PID
rm -rf "$LOCK"; mkdir -p "$LOCK"                  # new holder, counter deliberately kept
echo "$hp2" > "$LOCK/pid"
echo "$(( now - 4000 ))" > "$LOCK/started"
out=$(run_rebase)
echo "$out" | grep -q "HUNG-LOCK" && ok "re-escalates for a different holder" || fail "suppressed a new wedged holder: $out"
grep -q "holder_pid=$hp2" "$ALERT" 2>/dev/null && ok "alert names the new holder" || fail "alert kept the old holder: $(cat "$ALERT" 2>/dev/null)"
grep -q "count=1" "$ALERT" 2>/dev/null && ok "streak restarted at 1 on the new key" || fail "count did not restart: $(cat "$ALERT" 2>/dev/null)"

# --- T7: a healthy pass clears the counter and the alert ---------------------
# Acquiring the lock means no holder is wedged. Without the all-clear the alert
# outlives the outage it reported and the streak never re-arms the loud line.
echo "T7: healthy pass (lock acquired) clears counter + alert"
dead2=$(sh -c 'echo $$')
rm -rf "$LOCK"; mkdir -p "$LOCK"
echo "$dead2" > "$LOCK/pid"
echo "$(( now - 4000 ))" > "$LOCK/started"
[[ -f "$COUNTER" && -f "$ALERT" ]] && ok "fixture starts with a standing counter + alert" || fail "fixture precondition missing"
out=$(run_rebase)
echo "$out" | grep -q "breaking stale lock" && ok "acquires by breaking the dead holder's lock" || fail "did not acquire: $out"
[[ ! -f "$COUNTER" ]] && ok "counter removed on the healthy pass" || fail "counter survived: $(cat "$COUNTER" 2>/dev/null)"
[[ ! -f "$ALERT" ]] && ok "alert removed on the healthy pass" || fail "alert survived: $(cat "$ALERT" 2>/dev/null)"

# --- T8: positive control -- without the counter the quiet assertions fail ----
# Discriminates on a runtime condition, not on whether the fix is present at some
# ref: wiping the counter between ticks reproduces exactly the pre-#2795 state
# (no persisted streak), so it can never invert once this change is committed.
# If T5's "tick 2 is quiet" still passed here, T5 would be proving nothing.
echo "T8: positive control -- counter wiped each tick -> loud EVERY tick"
spawn_live; hp3=$HOLDER_PID
seed_lock "$hp3" "$(( now - 4000 ))"
pc_loud=0
for _ in 1 2 3; do
    rm -f "$COUNTER"                              # pre-fix: nothing persists the streak
    out=$(run_rebase)
    if echo "$out" | grep -q "HUNG-LOCK"; then
        pc_loud=$((pc_loud + 1))
    fi
done
(( pc_loud == 3 )) && ok "pre-fix shape is loud on all 3 ticks (control is meaningful)" || fail "control only loud on $pc_loud/3 — T5 may pass vacuously"

# --- T9: the N override arm (default 1 is only one of the two arms) ----------
echo "T9: FLEET_REBASE_HUNG_LOCK_ESCALATE_N=3 -> warn, warn, escalate, quiet"
spawn_live; hp4=$HOLDER_PID
seed_lock "$hp4" "$(( now - 4000 ))"
n1=$(run_rebase_n 3); n2=$(run_rebase_n 3); n3=$(run_rebase_n 3); n4=$(run_rebase_n 3)
echo "$n1" | grep -q "HUNG-LOCK" && ! echo "$n1" | grep -q "suppressing further" && ok "tick 1 warns without escalating" || fail "tick 1 wrong: $n1"
echo "$n2" | grep -q "HUNG-LOCK" && ! echo "$n2" | grep -q "suppressing further" && ok "tick 2 warns without escalating" || fail "tick 2 wrong: $n2"
echo "$n3" | grep -q "suppressing further" && ok "tick 3 (== N) escalates" || fail "tick 3 did not escalate: $n3"
echo "$n4" | grep -q "HUNG-LOCK" && fail "tick 4 (> N) still loud: $n4" || ok "tick 4 (> N) is quiet"
grep -q "count=4" "$ALERT" 2>/dev/null && ok "alert still refreshes past a non-default N" || fail "stale count: $(cat "$ALERT" 2>/dev/null)"

# --- T10: a garbage / out-of-range N falls back to 1, never to "never" -------
# A bad override must not disable the escalation entirely — that would turn a
# typo into a silent wedge.
echo "T10: invalid N falls back to 1"
for bad in "abc" "0" "-2" ""; do
    spawn_live; hpb=$HOLDER_PID
    seed_lock "$hpb" "$(( now - 4000 ))"
    b1=$(run_rebase_n "$bad"); b2=$(run_rebase_n "$bad")
    echo "$b1" | grep -q "suppressing further" && ok "N='$bad' escalates on tick 1" || fail "N='$bad' did not escalate: $b1"
    echo "$b2" | grep -q "HUNG-LOCK" && fail "N='$bad' still loud on tick 2: $b2" || ok "N='$bad' quiets on tick 2"
done

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
