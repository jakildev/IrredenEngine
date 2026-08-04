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
# The roster-empty condition holds until a human acts, so the warning must
# escalate-then-quiet rather than re-emit every 60 s forever (see
# scripts/fleet/CLAUDE.md §"An every-tick guard that warns must
# escalate-then-quiet"). Case (d) pins that rate-limiting.
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
        -u FLEET_ALERTS_DIR -u FLEET_STATE_DIR -u FLEET_WITNESS_ROSTER_ESCALATE_N
        "HOME=$fake_home"
    )
    if [[ -n "$escalate_n" ]]; then
        env_args+=("FLEET_WITNESS_ROSTER_ESCALATE_N=$escalate_n")
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

summarize "witness roster tests"
