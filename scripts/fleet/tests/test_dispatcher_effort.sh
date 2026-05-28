#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-role effort resolution.
#
# Covers the FLEET_EFFORT_<ROLE> override mechanism that mirrors the
# concurrency-cap pattern:
#   - built-in defaults (heavy roles xhigh, the rest high)
#   - per-role env var override
#   - per-role conf-file override
#   - env var beats conf
#   - global FLEET_EFFORT fallback when no per-role value is set
#   - per-role value beats the global FLEET_EFFORT
#
# Exercised through `fleet-dispatcher --print-effort <role>`, the inspection
# subcommand that prints a role's resolved effort and exits before the
# daemon loop.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

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

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
# Point at a guaranteed non-existent tmux session so the dispatcher never
# touches a real fleet running on the dev machine.
export FLEET_SESSION="fleet-test-$$"
mkdir -p "$FLEET_STATE_DIR"

# Start from a clean slate: no conf, no per-role env, no global override.
unset FLEET_EFFORT \
      FLEET_EFFORT_OPUS_WORKER FLEET_EFFORT_OPUS_REVIEWER FLEET_EFFORT_MERGER \
      FLEET_EFFORT_SONNET_AUTHOR FLEET_EFFORT_SONNET_REVIEWER \
      FLEET_EFFORT_SMOKE_WORKER 2>/dev/null || true

# --- Test 1: built-in defaults ---------------------------------------------
echo "T1: effort defaults (no conf, no env)"
rm -f "$FLEET_CONF"
assert_eq "$("$DISPATCHER" --print-effort opus-worker)"     "xhigh" "opus-worker defaults to xhigh"
assert_eq "$("$DISPATCHER" --print-effort opus-reviewer)"   "high"  "opus-reviewer defaults to high"
assert_eq "$("$DISPATCHER" --print-effort merger)"          "high"  "merger defaults to high"
assert_eq "$("$DISPATCHER" --print-effort sonnet-author)"   "high"  "sonnet-author defaults to high"
assert_eq "$("$DISPATCHER" --print-effort smoke-worker)"    "high"  "smoke-worker defaults to high"

# --- Test 2: per-role env var override -------------------------------------
echo "T2: env var overrides default"
assert_eq "$(FLEET_EFFORT_OPUS_REVIEWER=xhigh "$DISPATCHER" --print-effort opus-reviewer)" \
    "xhigh" "FLEET_EFFORT_OPUS_REVIEWER promotes opus-reviewer to xhigh"

# --- Test 3: conf file override --------------------------------------------
echo "T3: conf file overrides default"
cat >"$FLEET_CONF" <<'CONFEOF'
FLEET_EFFORT_MERGER=xhigh
CONFEOF
assert_eq "$("$DISPATCHER" --print-effort merger)"        "xhigh" "conf sets merger to xhigh"
assert_eq "$("$DISPATCHER" --print-effort opus-reviewer)" "high"  "untouched role keeps its default"

# --- Test 4: env var beats conf --------------------------------------------
echo "T4: env var has higher priority than conf"
cat >"$FLEET_CONF" <<'CONFEOF'
FLEET_EFFORT_OPUS_WORKER=high
CONFEOF
assert_eq "$(FLEET_EFFORT_OPUS_WORKER=xhigh "$DISPATCHER" --print-effort opus-worker)" \
    "xhigh" "env FLEET_EFFORT_OPUS_WORKER beats conf value"
rm -f "$FLEET_CONF"

# --- Test 5: global FLEET_EFFORT fallback ----------------------------------
echo "T5: global FLEET_EFFORT applies when no per-role value is set"
assert_eq "$(FLEET_EFFORT=xhigh "$DISPATCHER" --print-effort opus-reviewer)" \
    "xhigh" "global FLEET_EFFORT lifts opus-reviewer"
assert_eq "$(FLEET_EFFORT=high "$DISPATCHER" --print-effort opus-worker)" \
    "high"  "global FLEET_EFFORT lowers opus-worker"

# --- Test 6: per-role beats global -----------------------------------------
echo "T6: per-role value wins over global FLEET_EFFORT"
assert_eq "$(FLEET_EFFORT=high FLEET_EFFORT_OPUS_WORKER=xhigh "$DISPATCHER" --print-effort opus-worker)" \
    "xhigh" "FLEET_EFFORT_OPUS_WORKER beats global FLEET_EFFORT=high"

# --- Test 7: usage error on missing role arg -------------------------------
echo "T7: --print-effort with no role → usage error"
if "$DISPATCHER" --print-effort >/dev/null 2>&1; then
    FAIL=$((FAIL + 1)); echo "  FAIL: missing role arg should exit non-zero"
else
    PASS=$((PASS + 1)); echo "  ok: missing role arg exits non-zero"
fi

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
