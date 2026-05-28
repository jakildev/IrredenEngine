#!/usr/bin/env bash
# Tests for fleet-babysit's per-role effort resolution.
#
# fleet-babysit launches the architect panes (and, for manual runs, any
# role). Its effort resolution is the consumer side of the FLEET_EFFORT_<ROLE>
# mechanism: a role->varname map (tr 'a-z-' 'A-Z_') + indirect expansion,
# with precedence per-role FLEET_EFFORT_<ROLE> > global FLEET_EFFORT >
# model-derived default. fleet-up exports FLEET_EFFORT_OPUS_ARCHITECT /
# FLEET_EFFORT_GAME_ARCHITECT for it to read.
#
# Exercised through the FLEET_BABYSIT_PRINT_EFFORT=1 inspection hook, which
# prints the resolved effort and exits before any claude launch.
#
# Covers:
#   - model-derived defaults (opus -> xhigh, sonnet -> high) for manual runs
#   - the full 1m model name still hits the opus glob
#   - per-role FLEET_EFFORT_<ROLE> override
#   - role->varname mapping for a hyphenated role (game-architect)
#   - global FLEET_EFFORT fallback when no per-role value is set
#   - per-role value beats the global FLEET_EFFORT

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BABYSIT="$SCRIPT_DIR/fleet-babysit"

if [[ ! -x "$BABYSIT" ]]; then
    echo "test setup: fleet-babysit not found at $BABYSIT" >&2
    exit 1
fi

PASS=0
FAIL=0

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

# Resolve effort for <model> <role> with a clean env (no FLEET_EFFORT*).
# Extra `KEY=VAL` pairs are exported into the subshell for override tests.
effort_for() {
    local model="$1" role="$2"; shift 2
    env -u FLEET_EFFORT -u FLEET_EFFORT_OPUS_ARCHITECT -u FLEET_EFFORT_GAME_ARCHITECT \
        -u FLEET_EFFORT_OPUS_REVIEWER -u FLEET_EFFORT_OPUS_WORKER \
        -u FLEET_EFFORT_SONNET_AUTHOR \
        FLEET_BABYSIT_PRINT_EFFORT=1 "$@" \
        "$BABYSIT" "$model" "$role"
}

# --- Test 1: model-derived defaults (manual run, no env) -------------------
echo "T1: model-derived defaults"
assert_eq "$(effort_for opus opus-architect)"   "xhigh" "opus model -> xhigh default"
assert_eq "$(effort_for sonnet sonnet-author)"  "high"  "sonnet model -> high default"
# Manual run of a dispatched role keys on the model, not the dispatcher's
# per-role policy — opus -> xhigh even for opus-reviewer (documented; the
# real fleet launches reviewers via the dispatcher, not babysit).
assert_eq "$(effort_for opus opus-reviewer)"    "xhigh" "manual opus opus-reviewer -> xhigh (model-derived)"

# --- Test 2: full 1m model name still hits the opus glob -------------------
echo "T2: full model name hits opus glob"
assert_eq "$(effort_for 'claude-opus-4-8[1m]' opus-architect)" "xhigh" "claude-opus-4-8[1m] -> xhigh"

# --- Test 3: per-role override ---------------------------------------------
echo "T3: per-role FLEET_EFFORT_<ROLE> overrides the default"
assert_eq "$(effort_for opus opus-architect FLEET_EFFORT_OPUS_ARCHITECT=high)" \
    "high" "FLEET_EFFORT_OPUS_ARCHITECT=high lowers opus-architect"

# --- Test 4: role->varname mapping for a hyphenated role -------------------
echo "T4: tr maps game-architect -> FLEET_EFFORT_GAME_ARCHITECT"
assert_eq "$(effort_for opus game-architect FLEET_EFFORT_GAME_ARCHITECT=high)" \
    "high" "FLEET_EFFORT_GAME_ARCHITECT resolves for game-architect"

# --- Test 5: global FLEET_EFFORT fallback ----------------------------------
echo "T5: global FLEET_EFFORT applies when no per-role value is set"
assert_eq "$(effort_for opus opus-architect FLEET_EFFORT=high)" \
    "high" "global FLEET_EFFORT=high lowers opus-architect"

# --- Test 6: per-role beats global -----------------------------------------
echo "T6: per-role value wins over global FLEET_EFFORT"
assert_eq "$(effort_for opus opus-architect FLEET_EFFORT=high FLEET_EFFORT_OPUS_ARCHITECT=xhigh)" \
    "xhigh" "FLEET_EFFORT_OPUS_ARCHITECT beats global FLEET_EFFORT=high"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
