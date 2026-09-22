#!/usr/bin/env bash
# Tests for fleet-dispatcher's 5-hour-window usage gate.
#
# Covers:
#   - empty usage dir => gate open
#   - utilization >= threshold + fresh + future reset => gate closed
#   - threshold override via env var (FLEET_DISPATCHER_USAGE_GATE)
#   - stale observation (older than USAGE_STALE_SECONDS) w/o resetsAt => ignored
#   - stale observation but future resetsAt => still closed (window authoritative)
#   - resetsAt well in the past (past grace) => observation ignored
#   - resetsAt recently in the past, within RESET_GRACE_SECONDS => still active
#   - RESET_GRACE_SECONDS=0 reverts to instant open at resetsAt
#   - epoch-int resetsAt within grace => still active (parity with ISO branch)
#   - non-numeric RESET_GRACE_SECONDS override falls back to default
#   - utilization < threshold => gate open with util reported
#   - worst-of across multiple types when only one is over threshold
#   - defensive percent path (utilization > 1.5 treated as percent)
#   - the wall: a rejected observation closes, is named, binds until its own
#     resetsAt + grace, and (written by the real fleet-claude-stream) is not
#     reopened by a later below-threshold warning from another pane
#   - model-scoped windows: the Fable-only weekly wall (latched by the real
#     stream with the observing model) closes only `claude <fable model>` and
#     `scoped`, never `all` or a model-less `claude`; the scope override (read
#     from the conf) and `seven_day_<family>` types; an unlisted `seven_day*`
#     type takes the weekly threshold and any other unlisted type names the
#     fallback; fleet-gate-status reports the scoped wall apart from the
#     fleet-wide verdict

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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

assert_starts_with() {
    local actual="$1" prefix="$2" msg="$3"
    if [[ "$actual" == "$prefix"* ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected prefix: $prefix"
        echo "        actual:          $actual"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_STATE_DIR/usage"

# Isolate from the operator's ~/.fleet/fleet-up.conf — the dispatcher sources
# it on startup, so a host that sets e.g. FLEET_DISPATCHER_USAGE_GATE_FIVE_HOUR
# would clobber the thresholds these cases assert (per-type beats the global
# override T3 passes). Point FLEET_CONF at an empty file and clear any gate
# vars already in the caller env so the suite tests the baked defaults.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

NOW=$(date +%s)
# Future ISO-8601 timestamp; portable across BSD/GNU date.
RESETS=$(python3 -c "import datetime,time; print(datetime.datetime.fromtimestamp(time.time()+3600,tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")

echo "T1: empty usage dir => open"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "empty state reports open"

echo "T2: utilization 0.85, fresh, future reset => closed"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "85% trips default 80% threshold"

echo "T3: same fixture, threshold 0.99 => open"
out=$(FLEET_DISPATCHER_USAGE_GATE=0.99 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:five_hour util=85%" "raised threshold opens gate"

echo "T4: stale observation (2h old) with NO resetsAt => open"
# Staleness cutoff only applies when there's no reset boundary to trust.
printf '{"rateLimitType":"five_hour","utilization":0.85,"observed_at":%s}\n' "$((NOW - 7200))" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "stale observation without resetsAt ages out"

echo "T4b: stale observation (2h old) but future resetsAt => still closed"
# Regression guard: a future resetsAt is authoritative. Utilization can't
# fall below the wall until the window resets, so an old-but-still-bound
# reading must keep the gate closed. Previously the stale cutoff ran first
# and blinded the gate mid-window, re-triggering a worker into the wall.
printf '{"rateLimitType":"five_hour","utilization":1,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$((NOW - 7200))" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=100%" "future resetsAt overrides stale observed_at"

echo "T5: resetsAt in the past (well past grace) => open"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"2020-01-01T00:00:00Z","observed_at":%s}\n' "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "expired window ignored"

echo "T5b: resetsAt 60s in the past, default 600s grace => still closed"
RECENT_RESET=$(python3 -c "import datetime,time; print(datetime.datetime.fromtimestamp(time.time()-60,tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RECENT_RESET" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "grace window keeps gate closed past resetsAt"

echo "T5c: same fixture, grace=0 => open immediately"
out=$(FLEET_DISPATCHER_RESET_GRACE_SECONDS=0 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "grace=0 reverts to instant open at resetsAt"

echo "T5d: epoch-int resetsAt 60s in the past, default grace => still closed"
RECENT_RESET_EPOCH=$((NOW - 60))
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":%s,"observed_at":%s}\n' "$RECENT_RESET_EPOCH" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "epoch-int resetsAt also honors grace"

echo "T5e: non-numeric RESET_GRACE_SECONDS falls back to default (still closed)"
# Re-use the T5b ISO fixture (60s past reset) so a working grace keeps the
# gate closed. Pass a typo'd value; the dispatcher must clamp to 600 and
# stay closed. Without the validate-and-clamp guard the inline-Python int()
# raises, the heredoc dies, stdout is empty, and usage_gate_open treats
# empty as "not closed:" — silent gate-open.
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RECENT_RESET" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$(FLEET_DISPATCHER_RESET_GRACE_SECONDS="600s" "$DISPATCHER" --gate-status 2>/dev/null)
assert_starts_with "$out" "closed:five_hour util=85%" "non-numeric grace override falls back to default"

echo "T6: utilization 0.50 => open with util reported"
printf '{"rateLimitType":"five_hour","utilization":0.50,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:five_hour util=50%" "below-threshold reports util"

echo "T7: worst-of across two types (one over)"
printf '{"rateLimitType":"five_hour","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
printf '{"rateLimitType":"output_tokens","utilization":0.30,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/output_tokens.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "worst observation wins"
rm -f "$FLEET_STATE_DIR/usage/output_tokens.json"

echo "T8: defensive percent path (raw 85, not 0.85) => closed"
printf '{"rateLimitType":"five_hour","utilization":85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour util=85%" "percent value treated as percent"

# --- Per-type thresholds -----------------------------------------------------

echo "T9: seven_day at 92% with builtin 0.95 default => open"
printf '{"rateLimitType":"seven_day","utilization":0.92,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
rm -f "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:seven_day util=92%" "seven_day defaults to 0.95"

echo "T10: seven_day at 96% with builtin 0.95 default => closed"
printf '{"rateLimitType":"seven_day","utilization":0.96,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:seven_day util=96% (>= 95%)" "seven_day trips at 96%"

echo "T11: per-type env override beats builtin default"
out=$(FLEET_DISPATCHER_USAGE_GATE_SEVEN_DAY=0.50 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:seven_day util=96% (>= 50%)" "per-type override wins"

echo "T12: global env override applies to types without per-type override"
# unknown type — falls through per-type lookup -> global override -> 0.50
printf '{"rateLimitType":"daily_tokens","utilization":0.60,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/daily_tokens.json"
rm -f "$FLEET_STATE_DIR/usage/seven_day.json"
out=$(FLEET_DISPATCHER_USAGE_GATE=0.50 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:daily_tokens util=60% (>= 50%)" "global override applies to unknown types"

echo "T13: per-type override beats global override"
# Same daily_tokens at 60%, but global set to 0.50, daily_tokens-specific set to 0.99 -> open
out=$(FLEET_DISPATCHER_USAGE_GATE=0.50 FLEET_DISPATCHER_USAGE_GATE_DAILY_TOKENS=0.99 "$DISPATCHER" --gate-status)
assert_starts_with "$out" "open:daily_tokens util=60%" "per-type beats global"

# --- The wall itself ------------------------------------------------------------

echo "T14: a latched rejected observation closes the gate and reads as rejected"
rm -f "$FLEET_STATE_DIR/usage/daily_tokens.json"
# What fleet-claude-stream writes for a status:"rejected" rate_limit_event
# (utilization synthesized at 1.0 — the event itself carries none).
printf '{"rateLimitType":"five_hour","utilization":1.0,"resetsAt":"%s","observed_at":%s,"status":"rejected"}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:five_hour rejected util=100% (>= 80%)" "rejected observation closes, named as such"

echo "T15: a rejected observation stays binding past the observed_at cutoff while resetsAt is ahead"
printf '{"rateLimitType":"seven_day","utilization":1.0,"resetsAt":"%s","observed_at":%s,"status":"rejected"}\n' "$RESETS" "$(( NOW - 7200 ))" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
rm -f "$FLEET_STATE_DIR/usage/five_hour.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:seven_day rejected util=100%" "two-hour-old rejection with a future reset still closes"

echo "T16: a rejected observation ages out once resetsAt + grace has passed"
printf '{"rateLimitType":"seven_day","utilization":1.0,"resetsAt":%s,"observed_at":%s,"status":"rejected"}\n' "$(( NOW - 1200 ))" "$(( NOW - 1300 ))" \
    > "$FLEET_STATE_DIR/usage/seven_day.json"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "past the window the wall no longer binds"
rm -f "$FLEET_STATE_DIR/usage/seven_day.json"

echo "T17: --gate-status scopes: claude sees the Anthropic window, shared only the GitHub pools"
printf '{"rateLimitType":"five_hour","utilization":1.0,"resetsAt":"%s","observed_at":%s,"status":"rejected"}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/five_hour.json"
printf '{"rateLimitType":"github_core","utilization":0.10,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/github-core.json"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "closed:five_hour rejected" "claude scope: closed on the wall"
assert_starts_with "$("$DISPATCHER" --gate-status shared)" "open:github_core util=10%" "shared scope: the GitHub pool alone, open"
assert_starts_with "$("$DISPATCHER" --gate-status all)" "closed:five_hour rejected" "all: closed"
out=$("$DISPATCHER" --gate-status bogus 2>&1 || true)
assert_starts_with "$out" "usage: fleet-dispatcher --gate-status" "an unknown scope is a usage error"
rm -f "$FLEET_STATE_DIR/usage/five_hour.json" "$FLEET_STATE_DIR/usage/github-core.json"

# --- Rejection dominance across panes -------------------------------------------
# Observations are written by the real fleet-claude-stream here, one event per
# invocation, the way two panes' streams write the shared usage dir: the pane
# that hit the wall emits `rejected`, a pane still mid-turn emits a later
# `allowed_warning` below threshold for the same window.
STREAM="$SCRIPT_DIR/fleet-claude-stream"
feed_stream() {  # $1 = one rate_limit_info JSON object
    printf '{"type":"rate_limit_event","rate_limit_info":%s}\n' "$1" \
        | python3 "$STREAM" >/dev/null 2>&1
}
RESETS_EPOCH=$(( NOW + 3600 ))

echo "T18: a later allowed_warning from another pane cannot reopen a latched rejection"
rm -f "$FLEET_STATE_DIR/usage"/*.json
feed_stream "{\"status\":\"rejected\",\"resetsAt\":$RESETS_EPOCH,\"rateLimitType\":\"seven_day\"}"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "closed:seven_day rejected util=100%" "the wall closes the gate"
feed_stream "{\"status\":\"allowed_warning\",\"utilization\":0.85,\"resetsAt\":$RESETS_EPOCH,\"rateLimitType\":\"seven_day\"}"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "closed:seven_day rejected util=100%" "a late 85% warning (below the 95% weekly threshold) leaves it closed"
[[ -f "$FLEET_STATE_DIR/usage/seven_day.rejected.json" && -f "$FLEET_STATE_DIR/usage/seven_day.json" ]] \
    && { PASS=$((PASS + 1)); echo "  ok: the rejection is its own record beside the warning"; } \
    || { FAIL=$((FAIL + 1)); echo "  FAIL: expected seven_day.rejected.json beside seven_day.json: $(ls "$FLEET_STATE_DIR/usage")"; }

echo "T19: the rejection releases on its own reset, and the standing warning is what remains"
# Same two records, the rejection's window now past reset + grace while the
# warning's (a later observation of the next window) is still ahead.
if python3 - "$FLEET_STATE_DIR/usage/seven_day.rejected.json" "$(( NOW - 1200 ))" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1]); d = json.loads(p.read_text()); d["resetsAt"] = int(sys.argv[2]); p.write_text(json.dumps(d))
PY
then
    assert_starts_with "$("$DISPATCHER" --gate-status claude)" "open:seven_day util=85%" "past the rejection's window the gate reopens on the live warning"
else
    FAIL=$((FAIL + 1)); echo "  FAIL: no rejection record to age (T18's fixture missing)"
fi
rm -f "$FLEET_STATE_DIR/usage"/*.json

echo "T20: a result-only wall (no rate_limit_event) closes the claude gate on its own record"
# The CLI's wall result text without the rejected event that normally
# precedes it: the stream latches wall.rejected.json with no resetsAt, so the
# record binds on the observed_at cutoff (USAGE_STALE_SECONDS) and ages out.
printf '{"type":"result","subtype":"success","is_error":true,"api_error_status":429,"result":"You'"'"'ve hit your limit · resets 4:40pm"}\n' \
    | python3 "$STREAM" >/dev/null 2>&1
[[ -f "$FLEET_STATE_DIR/usage/wall.rejected.json" && ! -f "$FLEET_STATE_DIR/usage/five_hour.rejected.json" ]] \
    && { PASS=$((PASS + 1)); echo "  ok: the fallback is its own record, not a forged event record"; } \
    || { FAIL=$((FAIL + 1)); echo "  FAIL: expected wall.rejected.json alone: $(ls "$FLEET_STATE_DIR/usage")"; }
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "closed:five_hour rejected util=100%" "the result-only wall closes the gate"
assert_starts_with "$("$DISPATCHER" --gate-status shared)" "open" "the shared (GitHub) scope is untouched"
if python3 - "$FLEET_STATE_DIR/usage/wall.rejected.json" "$(( NOW - 4000 ))" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1]); d = json.loads(p.read_text()); d["observed_at"] = int(sys.argv[2]); p.write_text(json.dumps(d))
PY
then
    assert_starts_with "$("$DISPATCHER" --gate-status claude)" "open" \
        "with no resetsAt the fallback ages out on the observed_at cutoff (3600s)"
else
    FAIL=$((FAIL + 1)); echo "  FAIL: no fallback record to age"
fi
rm -f "$FLEET_STATE_DIR/usage"/*.json

check_contains() {  # $1 = haystack, $2 = needle, $3 = message
    if [[ "$1" == *"$2"* ]]; then
        PASS=$((PASS + 1)); echo "  ok: $3"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $3"; echo "        missing: $2"; echo "        in:      $1"
    fi
}

feed_session() {  # $1 = model, $2 = one rate_limit_info JSON object
    printf '%s\n' \
        "{\"type\":\"system\",\"subtype\":\"init\",\"model\":\"$1\",\"cwd\":\"/w\",\"session_id\":\"s\"}" \
        "{\"type\":\"rate_limit_event\",\"rate_limit_info\":$2}" \
        | python3 "$STREAM" >/dev/null 2>&1
}

echo "T21: the Fable-only weekly wall gates only launches on a Fable model"
feed_session 'claude-fable-5-1[1m]' \
    "{\"status\":\"rejected\",\"resetsAt\":$RESETS_EPOCH,\"rateLimitType\":\"seven_day_overage_included\"}"
assert_starts_with "$("$DISPATCHER" --gate-status)" "open" "all: the fleet-wide gate stays open"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "open" "claude with no model: open"
assert_starts_with "$("$DISPATCHER" --gate-status claude 'fable[1m]')" \
    "closed:seven_day_overage_included rejected util=100% (>= 95%) scope=fable" \
    "claude fable[1m]: closed, weekly threshold, scope named"
assert_starts_with "$("$DISPATCHER" --gate-status claude claude-fable-5-1)" \
    "closed:seven_day_overage_included rejected" "claude on a full Fable model id: closed"
assert_starts_with "$("$DISPATCHER" --gate-status claude 'opus[1m]')" "open" \
    "claude opus[1m]: open"
assert_starts_with "$("$DISPATCHER" --gate-status scoped)" \
    "closed:seven_day_overage_included rejected" "scoped: closed"
assert_starts_with "$("$DISPATCHER" --gate-status scoped 'opus[1m]')" "open" \
    "scoped opus[1m]: open"
assert_starts_with "$("$DISPATCHER" --gate-status scoped gpt-5.6-sol)" "open" \
    "a model outside every family matches no scoped window"
check_contains "$(cat "$FLEET_STATE_DIR/usage/seven_day_overage_included.rejected.json")" \
    '"model": "claude-fable-5-1[1m]"' "the stream stamps the observing session's model"

echo "T22: a scope override read from the conf makes the wall account-wide"
SCOPE_CONF="$TMPROOT/scope.conf"
echo 'FLEET_DISPATCHER_USAGE_SCOPE_SEVEN_DAY_OVERAGE_INCLUDED=account' > "$SCOPE_CONF"
out=$(FLEET_CONF="$SCOPE_CONF" "$DISPATCHER" --gate-status claude)
assert_starts_with "$out" "closed:seven_day_overage_included rejected util=100% (>= 95%)" \
    "claude with no model: closed"
[[ "$out" != *"scope="* ]] \
    && { PASS=$((PASS + 1)); echo "  ok: an account-wide line names no scope"; } \
    || { FAIL=$((FAIL + 1)); echo "  FAIL: account-wide line names a scope: $out"; }
assert_starts_with "$(FLEET_CONF="$SCOPE_CONF" "$DISPATCHER" --gate-status)" \
    "closed:seven_day_overage_included rejected" "all: closed"
rm -f "$FLEET_STATE_DIR/usage"/*.json

echo "T23: seven_day_<family> is scoped to that family without a table row"
feed_stream "{\"status\":\"rejected\",\"resetsAt\":$RESETS_EPOCH,\"rateLimitType\":\"seven_day_opus\"}"
assert_starts_with "$("$DISPATCHER" --gate-status claude 'claude-opus-4-8[1m]')" \
    "closed:seven_day_opus rejected util=100% (>= 95%) scope=opus" "claude on opus: closed"
assert_starts_with "$("$DISPATCHER" --gate-status claude sonnet)" "open" "claude sonnet: open"
assert_starts_with "$("$DISPATCHER" --gate-status)" "open" "all: open"
rm -f "$FLEET_STATE_DIR/usage"/*.json

echo "T24: unlisted types — weekly threshold for seven_day*, a named fallback otherwise"
printf '{"rateLimitType":"seven_day_foo","utilization":0.89,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day_foo.json"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "open:seven_day_foo util=89% (< 95%)" \
    "an unlisted weekly type at 89% stays open"
printf '{"rateLimitType":"seven_day_foo","utilization":0.96,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/seven_day_foo.json"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" "closed:seven_day_foo util=96% (>= 95%)" \
    "an unlisted weekly type at 96% closes"
rm -f "$FLEET_STATE_DIR/usage"/*.json
printf '{"rateLimitType":"daily_tokens","utilization":0.85,"resetsAt":"%s","observed_at":%s}\n' "$RESETS" "$NOW" \
    > "$FLEET_STATE_DIR/usage/daily_tokens.json"
assert_starts_with "$("$DISPATCHER" --gate-status claude)" \
    "closed:daily_tokens util=85% (>= 80%, unlisted type, fallback threshold)" \
    "a closed line resting on the generic fallback says so"
rm -f "$FLEET_STATE_DIR/usage"/*.json

echo "T25: fleet-gate-status keeps the scoped wall out of the fleet-wide verdict"
GATE_STATUS_TOOL="$SCRIPT_DIR/fleet-gate-status"
feed_session 'claude-fable-5-1[1m]' \
    "{\"status\":\"rejected\",\"resetsAt\":$RESETS_EPOCH,\"rateLimitType\":\"seven_day_overage_included\"}"
json=$("$GATE_STATUS_TOOL" --json)
summary=$(printf '%s' "$json" | python3 -c '
import json, sys
d = json.load(sys.stdin)
s = d.get("scoped_breaching", [])
print(d["gate"], len(d["breaching"]), [(o["rateLimitType"], o.get("scope"), o.get("model")) for o in s])')
assert_starts_with "$summary" \
    "open 0 [('seven_day_overage_included', 'fable', 'claude-fable-5-1[1m]')]" \
    "--json: gate open, the wall listed under scoped_breaching with scope and model"
text=$("$GATE_STATUS_TOOL")
check_contains "$text" "Fleet-wide usage gate: OPEN" "text: fleet-wide gate OPEN"
check_contains "$text" "Model-scoped usage gate: CLOSED for fable" "text: the scoped gate names fable"
rm -f "$FLEET_STATE_DIR/usage"/*.json

echo "T26: fleet-gate-status's mirror of the gate tables agrees with the dispatcher's"
# Both heredocs define the tables and helpers inline (a heredoc cannot import).
# Load only the literal tables and function defs from each and compare their
# answers over a type x override matrix.
if drift=$(python3 - "$DISPATCHER" "$GATE_STATUS_TOOL" <<'PY'
import ast, os, re, sys

def load(path, marker):
    src = open(path).read()
    for body in re.findall(r"python3 - <<'PY'\n(.*?)\nPY\n", src, re.S):
        if marker in body:
            tree = ast.parse(body)
            keep = [n for n in tree.body if isinstance(n, (ast.FunctionDef, ast.Assign))
                    and not (isinstance(n, ast.Assign) and not isinstance(
                        n.value, (ast.Dict, ast.Tuple, ast.Constant)))]
            ns = {"os": os}
            exec(compile(ast.Module(keep, []), path, "exec"), ns)
            return ns
    sys.exit(f"no heredoc containing {marker} in {path}")

d = load(sys.argv[1], "def scope_of")
g = load(sys.argv[2], "def scope_of")
bad = []
for name in ("BUILTIN_PER_TYPE_DEFAULTS", "BUILTIN_FALLBACK", "MODEL_FAMILIES", "BUILTIN_MODEL_SCOPES"):
    if d[name] != g[name]:
        bad.append(f"{name}: {d[name]!r} != {g[name]!r}")
types = ["five_hour", "seven_day", "seven_day_overage_included", "seven_day_opus",
         "seven_day_sonnet", "seven_day_foo", "daily_tokens", "github_core", "github_search"]
envs = [{}, {"FLEET_DISPATCHER_USAGE_SCOPE_SEVEN_DAY_OVERAGE_INCLUDED": "account"},
        {"FLEET_DISPATCHER_USAGE_SCOPE_DAILY_TOKENS": "opus"},
        {"FLEET_DISPATCHER_USAGE_GATE_SEVEN_DAY": "0.5"}, {"FLEET_DISPATCHER_USAGE_GATE": "0.7"}]
for env in envs:
    saved = dict(os.environ)
    os.environ.update(env)
    try:
        for t in types:
            if d["scope_of"](t) != g["scope_of"](t):
                bad.append(f"scope_of({t}) {env}: {d['scope_of'](t)} != {g['scope_of'](t)}")
            if d["threshold_for"](t) != g["threshold_source"](t):
                bad.append(f"threshold({t}) {env}: {d['threshold_for'](t)} != {g['threshold_source'](t)}")
    finally:
        os.environ.clear(); os.environ.update(saved)
print("\n".join(bad) if bad else "agree")
PY
); then
    assert_starts_with "$drift" "agree" "tables and helpers agree across the type x override matrix"
else
    FAIL=$((FAIL + 1)); echo "  FAIL: drift guard could not load a copy: $drift"
fi

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
