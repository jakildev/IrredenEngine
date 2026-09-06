#!/usr/bin/env bash
# Tests for fleet-up's probe_model RESOLVED_MODEL_ID extraction.
#
# probe_model's sed pulls the first key of the `claude --output-format
# json` result's "modelUsage" object as the concrete model id the CLI
# resolved an alias to (`sed -n 's/.*"modelUsage":{"\([^"]*\)".*/\1/p'`).
# That parsing has no coverage: this suite stubs `claude` to emit fixture
# JSON and asserts the extraction on a realistic success blob, a
# multi-key "modelUsage" object (first key wins), and that it degrades to
# empty — never an error — on output missing the key or not JSON at all.
#
# Exercised via the extract_fn pattern (test_fleet_up_resume_boot.sh):
# sed-extract probe_model from fleet-up and eval it standalone.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -x "$FLEET_UP" ]] || { echo "test setup: fleet-up not found at $FLEET_UP" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

extract_fn() {  # extract_fn <name> — print the function body from fleet-up
    sed -n "/^$1() {/,/^}/p" "$FLEET_UP"
}
eval "$(extract_fn probe_model)"
[[ "$(type -t probe_model)" == "function" ]] || { echo "test setup: probe_model not extracted" >&2; exit 1; }

# Keep the watchdog floor low; the stub below always resolves the
# backgrounded `claude` call instantly, so this only bounds a genuinely
# hung test run, not the normal path.
export FLEET_MODEL_PROBE_TIMEOUT=5

echo "T1: realistic success fixture — extracts the billed model id"
claude() {
    printf '%s' '{"type":"result","subtype":"success","is_error":false,"result":"ok","session_id":"abc-123","modelUsage":{"claude-sonnet-5-20260315":{"inputTokens":10,"outputTokens":3}}}'
}
probe_model "sonnet"
rc=$?
assert_eq "$rc" "0" "probe_model reports success on a clean claude exit"
assert_eq "$RESOLVED_MODEL_ID" "claude-sonnet-5-20260315" "extracts the modelUsage key"

echo "T2: multiple modelUsage keys — first key wins"
claude() {
    printf '%s' '{"type":"result","result":"ok","modelUsage":{"claude-opus-5-20260201":{"inputTokens":1},"claude-haiku-4-5-20251001":{"inputTokens":1}}}'
}
probe_model "opus"
assert_eq "$RESOLVED_MODEL_ID" "claude-opus-5-20260201" "first modelUsage key extracted, not the second"

echo "T3: claude succeeds but the result has no modelUsage key — degrades to empty"
claude() {
    printf '%s' '{"type":"result","subtype":"success","is_error":false,"result":"ok","session_id":"abc-123"}'
}
probe_model "sonnet"
rc=$?
assert_eq "$rc" "0" "probe_model still reports success (claude itself exited clean)"
assert_eq "$RESOLVED_MODEL_ID" "" "RESOLVED_MODEL_ID degrades to empty, not an error"

echo "T4: claude emits non-JSON output — degrades to empty"
claude() {
    printf '%s' 'error: model not found'
}
probe_model "nonexistent-model"
assert_eq "$RESOLVED_MODEL_ID" "" "malformed (non-JSON) output degrades to empty"

echo "T5: claude itself fails — probe_model returns non-zero, RESOLVED_MODEL_ID unset"
claude() {
    return 1
}
if probe_model "sonnet"; then
    bad "probe_model should report failure when claude exits non-zero"
else
    ok "probe_model reports failure when claude exits non-zero"
fi
assert_eq "$RESOLVED_MODEL_ID" "" "RESOLVED_MODEL_ID stays empty when claude fails"

summarize "fleet-up probe_model extraction"
