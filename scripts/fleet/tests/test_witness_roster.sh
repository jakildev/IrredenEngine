#!/usr/bin/env bash
# Tests for witness's roster-empty detection (#2770).
#
# witness's per-role staleness thresholds only monitor a fixed roster
# (opus-architect, game-architect). If no rostered agent has ever written a
# heartbeat, `tracked` stays 0 forever and the old code unconditionally
# printed "all healthy" — the dangerous failure mode for a monitor: it does
# not error, it reports health. These tests pin the fixed behavior: a
# roster with zero tracked heartbeats gets a distinct ROSTER EMPTY warning
# and a `witness-roster.stuck` alert file instead of "all healthy".
#
# Every condition witness warns on holds until a human acts, so each warning
# must escalate-then-quiet rather than re-emit every 60 s forever (see
# scripts/fleet/CLAUDE.md §"An every-tick guard that warns must
# escalate-then-quiet"). Cases (d)-(g) pin that rate-limiting on all three
# branches: roster-empty (#2770), stale agent and dead dispatcher (#2780).
#
# Hermetic: HOME points at a temp dir per case, and the FLEET_* overrides
# witness honours are scrubbed from the caller's environment, so no live
# ~/.fleet/{heartbeats,alerts,state} is ever touched even when the suite runs
# from a fleet pane that exports them.
#
# Covers:
#   (a) empty roster (heartbeats dir exists, no rostered file in it) =>
#       ROSTER EMPTY warning, no "all healthy", witness-roster.stuck written
#   (b) a rostered agent with a fresh heartbeat => "all healthy (1 agent(s)
#       tracked)", no witness-roster.stuck
#   (c) a rostered agent with a stale heartbeat => STALE: line +
#       <agent>.stuck, no witness-roster.stuck (tracked > 0)
#   (d) a persistent empty roster => loud up to the Nth pass, silent after,
#       witness.log stops growing, witness-roster.stuck keeps refreshing its
#       count past N, and a rostered heartbeat clears counter + alert so the
#       next outage escalates from scratch
#   (e) a persistently-stale agent => same escalate-then-quiet shape on its own
#       per-agent counter, <agent>.stuck still refreshed past N, and a
#       heartbeat refresh restarts the streak
#   (f) two agents stale at once => independent counters; neither resets the
#       other, both escalate at N, and one recovering leaves the other's
#       streak intact
#   (g) a dead dispatcher pid => same shape on .witness-stale-dispatcher-skip,
#       a new dead pid restarts the streak (the pid is the subject), and a
#       live pid clears counter + alert

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# Overridable so the positive control stays reproducible after merge: point
# WITNESS at an older revision (e.g. `git show <sha>:scripts/fleet/witness`
# saved to a temp file) and the roster cases must go red.
WITNESS="${WITNESS:-$SCRIPT_DIR/witness}"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$WITNESS" ]]; then
    echo "test setup: witness not found (or not executable) at $WITNESS" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-witness-roster.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# run_witness <fake_home> [escalate_n]
# One `--once` pass with the caller's FLEET_* overrides scrubbed. Cases that
# exercise the escalation threshold pass their own N so the test doesn't have
# to run witness's ~10-minute production default that many times.
run_witness() {
    local fake_home="$1" escalate_n="${2:-}"
    mkdir -p "$fake_home/.fleet/heartbeats"
    local -a env_args=(
        -u FLEET_ALERTS_DIR -u FLEET_STATE_DIR -u FLEET_WITNESS_ESCALATE_N
        "HOME=$fake_home"
    )
    if [[ -n "$escalate_n" ]]; then
        env_args+=("FLEET_WITNESS_ESCALATE_N=$escalate_n")
    fi
    env "${env_args[@]}" "$WITNESS" --once > "$fake_home/out.txt" 2>&1
}

# --- (a) empty roster: no rostered heartbeat file at all --------------------

home_a="$TMP/home-empty"
run_witness "$home_a"
out_a=$(cat "$home_a/out.txt")

assert_contains "$out_a" "ROSTER EMPTY" "empty roster: distinct warning emitted"
assert_absent   "$out_a" "all healthy"  "empty roster: does not report all healthy"
assert_contains "$out_a" "opus-architect" "empty roster: warning names a rostered agent"
assert_contains "$out_a" "game-architect" "empty roster: warning names the other rostered agent"
[[ -f "$home_a/.fleet/alerts/witness-roster.stuck" ]] \
    && ok "empty roster: witness-roster.stuck written" \
    || bad "empty roster: witness-roster.stuck written"
assert_contains "$(cat "$home_a/.fleet/logs/witness.log" 2>/dev/null || true)" \
    "ROSTER EMPTY" "empty roster: warning also logged to witness.log"

# --- (b) rostered agent, fresh heartbeat ------------------------------------

home_b="$TMP/home-fresh"
mkdir -p "$home_b/.fleet/heartbeats"
touch "$home_b/.fleet/heartbeats/opus-architect"
run_witness "$home_b"
out_b=$(cat "$home_b/out.txt")

assert_contains "$out_b" "all healthy (1 agent(s) tracked)" \
    "fresh heartbeat: reports all healthy with tracked=1"
assert_absent "$out_b" "ROSTER EMPTY" "fresh heartbeat: no roster-empty warning"
[[ -f "$home_b/.fleet/alerts/witness-roster.stuck" ]] \
    && bad "fresh heartbeat: witness-roster.stuck NOT written" \
    || ok "fresh heartbeat: witness-roster.stuck NOT written"

# --- (c) rostered agent, stale heartbeat ------------------------------------

home_c="$TMP/home-stale"
mkdir -p "$home_c/.fleet/heartbeats"
touch -t 202401010000 "$home_c/.fleet/heartbeats/game-architect"
run_witness "$home_c"
out_c=$(cat "$home_c/out.txt")

assert_contains "$out_c" "STALE: game-architect" "stale heartbeat: STALE line emitted"
assert_absent "$out_c" "ROSTER EMPTY" "stale heartbeat: no roster-empty warning (tracked>0)"
[[ -f "$home_c/.fleet/alerts/game-architect.stuck" ]] \
    && ok "stale heartbeat: game-architect.stuck written" \
    || bad "stale heartbeat: game-architect.stuck written"
[[ -f "$home_c/.fleet/alerts/witness-roster.stuck" ]] \
    && bad "stale heartbeat: witness-roster.stuck NOT written" \
    || ok "stale heartbeat: witness-roster.stuck NOT written"

# --- (d) persistent empty roster: escalate, then quiet ----------------------
# N=3 keeps the case fast; the production default is 10 (~10 min of passes).

home_d="$TMP/home-escalate"
esc_n=3
counter_d="$home_d/.fleet/state/.witness-roster-skip"
alert_d="$home_d/.fleet/alerts/witness-roster.stuck"
log_d="$home_d/.fleet/logs/witness.log"

# Passes 1..N-1: the ordinary loud warn, not yet escalated.
run_witness "$home_d" "$esc_n"
out_d1=$(cat "$home_d/out.txt")
assert_contains "$out_d1" "ROSTER EMPTY" "pass 1 (< N): still warns loudly"
assert_absent   "$out_d1" "ESCALATION"   "pass 1 (< N): has not escalated yet"
assert_contains "$(cat "$alert_d" 2>/dev/null || true)" "count=1" \
    "pass 1 (< N): alert records count=1"
[[ -f "$counter_d" ]] \
    && ok "pass 1 (< N): consecutive-skip counter written" \
    || bad "pass 1 (< N): consecutive-skip counter written"

run_witness "$home_d" "$esc_n"
out_d2=$(cat "$home_d/out.txt")
assert_contains "$out_d2" "ROSTER EMPTY" "pass 2 (< N): still warns loudly"
assert_absent   "$out_d2" "ESCALATION"   "pass 2 (< N): has not escalated yet"

# Pass N: the single loud escalation line.
run_witness "$home_d" "$esc_n"
out_d3=$(cat "$home_d/out.txt")
assert_contains "$out_d3" "ROSTER EMPTY ESCALATION" "pass N: escalates once"
assert_contains "$out_d3" "3 consecutive passes"    "pass N: names the streak length"
assert_contains "$(cat "$alert_d" 2>/dev/null || true)" "count=3" \
    "pass N: alert records count=3"
log_lines_at_n=$(wc -l < "$log_d" 2>/dev/null || echo 0)

# Past N: silent on stdout and in the log, but the alert keeps refreshing —
# it is the only standing signal once the loud channel goes quiet.
run_witness "$home_d" "$esc_n"
out_d4=$(cat "$home_d/out.txt")
assert_absent "$out_d4" "ROSTER EMPTY" "pass N+1: quiet on stdout"
assert_absent "$out_d4" "all healthy"  "pass N+1: quiet does not become a health claim"
assert_contains "$(cat "$alert_d" 2>/dev/null || true)" "count=4" \
    "pass N+1: alert still refreshed past N"

run_witness "$home_d" "$esc_n"
assert_contains "$(cat "$alert_d" 2>/dev/null || true)" "count=5" \
    "pass N+2: alert count keeps advancing (not frozen at N)"
log_lines_after=$(wc -l < "$log_d" 2>/dev/null || echo 0)
assert_eq "$log_lines_after" "$log_lines_at_n" \
    "past N: witness.log stops growing (no unbounded append)"

# A healthy pass clears both, so the next outage escalates from scratch.
touch "$home_d/.fleet/heartbeats/opus-architect"
run_witness "$home_d" "$esc_n"
out_d_ok=$(cat "$home_d/out.txt")
assert_contains "$out_d_ok" "all healthy (1 agent(s) tracked)" \
    "healthy pass: reports health again"
[[ -f "$alert_d" ]] \
    && bad "healthy pass: alert cleared" \
    || ok "healthy pass: alert cleared"
[[ -f "$counter_d" ]] \
    && bad "healthy pass: counter cleared" \
    || ok "healthy pass: counter cleared"

rm -f "$home_d/.fleet/heartbeats/opus-architect"
run_witness "$home_d" "$esc_n"
out_d_again=$(cat "$home_d/out.txt")
assert_contains "$out_d_again" "ROSTER EMPTY" "after clear: warns loudly again"
assert_absent   "$out_d_again" "ESCALATION"   "after clear: streak restarted, not resumed"
assert_contains "$(cat "$alert_d" 2>/dev/null || true)" "count=1" \
    "after clear: count restarts at 1"

# --- (e) persistently-stale agent: escalate, then quiet ---------------------
# Same rule as (d), on the other unbounded-append branch (#2780). The agent
# stays stale until a human acts, so the loud line must stop while
# <agent>.stuck keeps being refreshed every pass.

home_e="$TMP/home-stale-escalate"
mkdir -p "$home_e/.fleet/heartbeats"
touch -t 202401010000 "$home_e/.fleet/heartbeats/opus-architect"
counter_e="$home_e/.fleet/state/.witness-stale-opus-architect-skip"
alert_e="$home_e/.fleet/alerts/opus-architect.stuck"
log_e="$home_e/.fleet/logs/witness.log"

run_witness "$home_e" 3
assert_contains "$(cat "$home_e/out.txt")" "STALE: opus-architect" \
    "stale pass 1 (< N): still warns loudly"
assert_absent "$(cat "$home_e/out.txt")" "ESCALATION" \
    "stale pass 1 (< N): has not escalated yet"
[[ -f "$counter_e" ]] \
    && ok "stale pass 1 (< N): per-agent counter written" \
    || bad "stale pass 1 (< N): per-agent counter written"

run_witness "$home_e" 3
run_witness "$home_e" 3
out_e3=$(cat "$home_e/out.txt")
assert_contains "$out_e3" "STALE ESCALATION" "stale pass N: escalates once"
assert_contains "$out_e3" "3 consecutive passes" "stale pass N: names the streak length"
log_lines_at_n_e=$(wc -l < "$log_e" 2>/dev/null || echo 0)

# Past N: silent on stdout and in the log. Delete the alert first so its
# recreation proves witness still writes the durable signal past the
# escalation instead of freezing it there.
rm -f "$alert_e"
run_witness "$home_e" 3
out_e4=$(cat "$home_e/out.txt")
assert_absent "$out_e4" "STALE: opus-architect" "stale pass N+1: quiet on stdout"
assert_absent "$out_e4" "all healthy" "stale pass N+1: quiet does not become a health claim"
[[ -f "$alert_e" ]] \
    && ok "stale pass N+1: <agent>.stuck still refreshed past N" \
    || bad "stale pass N+1: <agent>.stuck still refreshed past N"
log_lines_after_e=$(wc -l < "$log_e" 2>/dev/null || echo 0)
assert_eq "$log_lines_after_e" "$log_lines_at_n_e" \
    "past N: witness.log stops growing on the stale branch"

# A heartbeat refresh clears that subject's counter, so a later re-stale
# escalates from scratch rather than resuming the quieted streak.
touch "$home_e/.fleet/heartbeats/opus-architect"
run_witness "$home_e" 3
assert_contains "$(cat "$home_e/out.txt")" "all healthy" "stale: fresh heartbeat reports health"
[[ -f "$counter_e" ]] \
    && bad "stale: fresh heartbeat clears the counter" \
    || ok "stale: fresh heartbeat clears the counter"

touch -t 202401010000 "$home_e/.fleet/heartbeats/opus-architect"
run_witness "$home_e" 3
out_e_again=$(cat "$home_e/out.txt")
assert_contains "$out_e_again" "STALE: opus-architect" "stale after clear: warns loudly again"
assert_absent "$out_e_again" "ESCALATION" "stale after clear: streak restarted, not resumed"

# --- (f) two agents stale at once: independent counters ---------------------
# The discriminating case for the per-subject <tag>. On one shared counter the
# two warns would each see the other's key, reset to 1 every pass, and never
# reach N at all.

home_f="$TMP/home-stale-two"
mkdir -p "$home_f/.fleet/heartbeats"
touch -t 202401010000 "$home_f/.fleet/heartbeats/opus-architect"
touch -t 202401010000 "$home_f/.fleet/heartbeats/game-architect"

run_witness "$home_f" 3
run_witness "$home_f" 3
run_witness "$home_f" 3
out_f=$(cat "$home_f/out.txt")
assert_contains "$out_f" "STALE: opus-architect" "two stale: first agent still warned at N"
assert_contains "$out_f" "STALE: game-architect" "two stale: second agent still warned at N"
assert_eq "$(grep -c "STALE ESCALATION" <<< "$out_f")" "2" \
    "two stale: both escalate on the same pass (neither reset the other)"
for a in opus-architect game-architect; do
    c=$(awk '{print $1}' "$home_f/.fleet/state/.witness-stale-$a-skip" 2>/dev/null || echo "")
    assert_eq "$c" "3" "two stale: $a kept its own streak"
done

# Clearing one must not touch the other's streak.
touch "$home_f/.fleet/heartbeats/opus-architect"
run_witness "$home_f" 3
[[ -f "$home_f/.fleet/state/.witness-stale-opus-architect-skip" ]] \
    && bad "two stale: recovered agent's counter cleared" \
    || ok "two stale: recovered agent's counter cleared"
assert_eq "$(awk '{print $1}' "$home_f/.fleet/state/.witness-stale-game-architect-skip" 2>/dev/null || echo "")" \
    "4" "two stale: still-stale agent's streak untouched by the other's recovery"

# --- (g) dead dispatcher pid: escalate, then quiet --------------------------
# A rostered heartbeat keeps tracked>0 so this exercises the dispatcher branch
# alone, not roster-empty.

home_g="$TMP/home-dispatcher"
mkdir -p "$home_g/.fleet/heartbeats" "$home_g/.fleet/state"
touch "$home_g/.fleet/heartbeats/opus-architect"
echo 999999 > "$home_g/.fleet/state/dispatcher.pid"
counter_g="$home_g/.fleet/state/.witness-stale-dispatcher-skip"
alert_g="$home_g/.fleet/alerts/fleet-dispatcher.stuck"

run_witness "$home_g" 3
assert_contains "$(cat "$home_g/out.txt")" "STALE: fleet-dispatcher" \
    "dead dispatcher pass 1 (< N): still warns loudly"
assert_absent "$(cat "$home_g/out.txt")" "all healthy" \
    "dead dispatcher: a dead dispatcher is not health"

run_witness "$home_g" 3
run_witness "$home_g" 3
assert_contains "$(cat "$home_g/out.txt")" "STALE ESCALATION" \
    "dead dispatcher pass N: escalates once"

rm -f "$alert_g"
run_witness "$home_g" 3
out_g4=$(cat "$home_g/out.txt")
assert_absent "$out_g4" "STALE: fleet-dispatcher" "dead dispatcher pass N+1: quiet on stdout"
assert_absent "$out_g4" "all healthy" "dead dispatcher pass N+1: quiet is not a health claim"
[[ -f "$alert_g" ]] \
    && ok "dead dispatcher pass N+1: fleet-dispatcher.stuck still refreshed past N" \
    || bad "dead dispatcher pass N+1: fleet-dispatcher.stuck still refreshed past N"

# A relaunched-but-also-dead dispatcher is a NEW outage: the pid is the
# subject, so the streak restarts rather than staying quiet under the old one.
echo 999998 > "$home_g/.fleet/state/dispatcher.pid"
run_witness "$home_g" 3
out_g_newpid=$(cat "$home_g/out.txt")
assert_contains "$out_g_newpid" "STALE: fleet-dispatcher (pid 999998" \
    "dead dispatcher: a new dead pid warns loudly again"
assert_absent "$out_g_newpid" "ESCALATION" \
    "dead dispatcher: new pid restarts the streak, not resumes it"

# A live pid clears the counter, so a later death escalates from scratch.
echo $$ > "$home_g/.fleet/state/dispatcher.pid"
run_witness "$home_g" 3
assert_contains "$(cat "$home_g/out.txt")" "all healthy" "live dispatcher: reports health"
[[ -f "$counter_g" ]] \
    && bad "live dispatcher: counter cleared" \
    || ok "live dispatcher: counter cleared"
[[ -f "$alert_g" ]] \
    && bad "live dispatcher: alert cleared" \
    || ok "live dispatcher: alert cleared"

# A removed pidfile clears both artifacts too. Nothing asserts a dead
# dispatcher without one, so a streak and a .stuck alert raised while it
# existed must not outlive it — the alert especially, since past N it is the
# only standing signal and no later pass could rewrite or clear it.
echo 999997 > "$home_g/.fleet/state/dispatcher.pid"
run_witness "$home_g" 3
assert_contains "$(cat "$home_g/out.txt")" "STALE: fleet-dispatcher (pid 999997" \
    "removed pidfile: precondition — a dead pid raises the streak and the alert"
[[ -f "$counter_g" && -f "$alert_g" ]] \
    && ok "removed pidfile: precondition — counter and alert both on disk" \
    || bad "removed pidfile: precondition — counter and alert both on disk"

rm -f "$home_g/.fleet/state/dispatcher.pid"
run_witness "$home_g" 3
out_g_nopid=$(cat "$home_g/out.txt")
assert_absent "$out_g_nopid" "STALE: fleet-dispatcher" \
    "removed pidfile: no dead-dispatcher claim without a recorded pid"
[[ -f "$counter_g" ]] \
    && bad "removed pidfile: counter cleared" \
    || ok "removed pidfile: counter cleared"
[[ -f "$alert_g" ]] \
    && bad "removed pidfile: alert cleared" \
    || ok "removed pidfile: alert cleared"

summarize "witness roster tests"
