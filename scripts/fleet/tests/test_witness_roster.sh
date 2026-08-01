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
# Hermetic: HOME points at a temp dir per case, so no live
# ~/.fleet/heartbeats or ~/.fleet/alerts is ever touched.
#
# Covers:
#   (a) empty roster (heartbeats dir exists, no rostered file in it) =>
#       ROSTER EMPTY warning, no "all healthy", witness-roster.stuck written
#   (b) a rostered agent with a fresh heartbeat => "all healthy (1 agent(s)
#       tracked)", no witness-roster.stuck
#   (c) a rostered agent with a stale heartbeat => STALE: line +
#       <agent>.stuck, no witness-roster.stuck (tracked > 0)

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
WITNESS="$SCRIPT_DIR/witness"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$WITNESS" ]]; then
    echo "test setup: witness not found (or not executable) at $WITNESS" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-witness-roster.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

run_witness() {
    local fake_home="$1"
    mkdir -p "$fake_home/.fleet/heartbeats"
    HOME="$fake_home" "$WITNESS" --once > "$fake_home/out.txt" 2>&1
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

summarize "witness roster tests"
