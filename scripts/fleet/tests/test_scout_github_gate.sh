#!/usr/bin/env bash
# Tests for fleet-state-scout's GitHub quota sampler feeding the dispatcher's
# usage gate and fleet-gate-status.
#
# test_dispatcher_github_gate.sh drops hand-written latch files, so it proves
# only the evaluator. This suite drives the real sampler against a stub `gh`
# and hands whatever file it wrote to the real `fleet-dispatcher
# --gate-status` and `fleet-gate-status`.
#
# Covers:
#   - a GraphQL sample refused by the rate limiter => gate closed, REJECTED
#   - /rate_limit reporting a phantom empty graphql bucket while GraphQL's
#     own self-report reads 92% => gate closed at the 90% threshold
#   - a refused sample followed by a good one => latch recovers, gate open
#   - a timeout or a non-rate-limit failure => prior latch byte-identical
#   - the refused latch carries the last good reset only while it is ahead
#   - refusal logging is transition-only
#   - a refused GraphQL call in run_capture latches github-graphql.rejected.json
#     at the sampled reset, and a later good self-report leaves it untouched

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
SCOUT="$SCRIPT_DIR/fleet-state-scout"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
GATE_STATUS="$SCRIPT_DIR/fleet-gate-status"

for subject in "$SCOUT" "$DISPATCHER" "$GATE_STATUS"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject not found at $subject" >&2
        exit 3
    fi
done

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
cleanup() { [[ -n "${TMPROOT:-}" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT

export FLEET_STATE_DIR="$TMPROOT/state"
USAGE="$FLEET_STATE_DIR/usage"
LATCH="$USAGE/github-graphql.json"
STUB_DIR="$TMPROOT/stub"
mkdir -p "$USAGE" "$STUB_DIR/bin"

# Isolate from the operator's fleet-up.conf and any per-type threshold
# exports, as test_dispatcher_github_gate.sh does.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

NOW=$(date +%s)
FUTURE_RESET=$((NOW + 3600))
FUTURE_RESET_ISO=$(python3 -c "import sys, time; print(time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(int(sys.argv[1]))))" "$FUTURE_RESET")
QUERY='{rateLimit{limit used remaining resetAt}}'

# --- stub gh ---------------------------------------------------------------
# Models exactly the two calls the sampler makes; anything else fails closed
# and is recorded so the suite can assert no call went unmodelled.
cat > "$STUB_DIR/bin/gh" <<'STUB'
#!/usr/bin/env bash
d="$GH_STUB_DIR"
if [[ "$#" -eq 2 && "$1" == api && "$2" == /rate_limit ]]; then
    cat "$d/rest.json"
    exit 0
fi
if [[ "$#" -eq 4 && "$1" == api && "$2" == graphql && "$3" == -f \
      && "$4" == "query=$GH_STUB_QUERY" ]]; then
    case "$(cat "$d/graphql-mode")" in
        good)
            cat "$d/graphql.json"; exit 0 ;;
        refused)
            printf '%s\n' '{"errors":[{"type":"RATE_LIMIT","code":"graphql_rate_limit","message":"API rate limit already exceeded for user ID 1234567."}]}'
            echo "gh: API rate limit already exceeded for user ID 1234567." >&2
            exit 1 ;;
        timeout)
            sleep 5; exit 0 ;;
        error)
            echo "HTTP 502: Bad Gateway (https://api.github.com/graphql)" >&2
            exit 1 ;;
    esac
fi
if [[ "$1" == pr && "$2" == list ]]; then
    echo "GraphQL: API rate limit already exceeded for user ID 1234567." >&2
    exit 1
fi
echo "stub gh: unmodelled invocation: $*" | tee -a "$d/misses" >&2
exit 1
STUB
chmod +x "$STUB_DIR/bin/gh"
: > "$STUB_DIR/misses"

rest_fixture() {
    # $1 = graphql used as /rate_limit reports it (the phantom reads 0).
    printf '{"resources":{"core":{"limit":5000,"used":100,"remaining":4900,"reset":%s},"graphql":{"limit":5000,"used":%s,"remaining":%s,"reset":%s},"search":{"limit":30,"used":0,"remaining":30,"reset":%s}}}\n' \
        "$FUTURE_RESET" "$1" "$((5000 - $1))" "$((NOW + 3600))" "$FUTURE_RESET" > "$STUB_DIR/rest.json"
}

graphql_good() {
    # $1 = used out of 5000.
    printf '{"data":{"rateLimit":{"limit":5000,"used":%s,"remaining":%s,"resetAt":"%s"}}}\n' \
        "$1" "$((5000 - $1))" "$FUTURE_RESET_ISO" > "$STUB_DIR/graphql.json"
    echo good > "$STUB_DIR/graphql-mode"
}

graphql_mode() { echo "$1" > "$STUB_DIR/graphql-mode"; }

# One scout sample: the real module, with only its usage dir and gh timeout
# repointed. Scout log lines accumulate in $TMPROOT/scout.log.
sample() {
    GH_STUB_DIR="$STUB_DIR" GH_STUB_QUERY="$QUERY" PATH="$STUB_DIR/bin:$PATH" \
    python3 - "$SCOUT" "$USAGE" >> "$TMPROOT/scout.log" 2>&1 <<'PY'
import importlib.machinery, importlib.util, sys
from pathlib import Path
loader = importlib.machinery.SourceFileLoader("fleet_state_scout", sys.argv[1])
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)
mod.USAGE_DIR = Path(sys.argv[2])
mod.GH_TIMEOUT_SECONDS = 1
mod.sample_github_rate_limit()
PY
}

latch_field() {
    python3 -c 'import json, sys; v = json.load(open(sys.argv[1])).get(sys.argv[2]); print("<absent>" if v is None else v)' "$LATCH" "$1"
}

gate() { "$DISPATCHER" --gate-status; }

echo "T1: refused GraphQL sample => gate closed, graphql REJECTED"
rm -f "$USAGE"/*.json
rest_fixture 0
graphql_good 2058
sample
graphql_mode refused
sample
assert_eq "$(latch_field status)" "rejected" "refused sample latches status=rejected"
assert_eq "$(latch_field resetsAt)" "$FUTURE_RESET" "refused latch carries the last good reset"
assert_eq "$(latch_field limit)" "5000" "refused latch carries the last good limit"
assert_eq "$(latch_field remaining)" "0" "refused latch reports remaining=0"
out=$(gate)
assert_eq "$out" "closed:github_graphql rejected util=100% (>= 90%) resets=$FUTURE_RESET" \
    "dispatcher gate closes on the refused latch"
gs=$("$GATE_STATUS")
assert_contains "$gs" "Fleet-wide usage gate: CLOSED" "gate-status prints CLOSED"
assert_contains "$gs" "breaching: graphql  remaining=0/5000 REJECTED (>= 90%)" \
    "gate-status graphql row carries REJECTED"

echo "T2: refused with no prior latch => closed, no resetsAt, no limit"
rm -f "$USAGE"/*.json
graphql_mode refused
sample
assert_eq "$(latch_field resetsAt)" "<absent>" "no prior reset => resetsAt omitted"
assert_eq "$(latch_field limit)" "<absent>" "no prior limit => limit omitted"
out=$(gate)
assert_eq "${out%% *}" "closed:github_graphql" "gate closes on observed_at alone"

echo "T3: refused after a latch whose reset has passed => resetsAt omitted"
printf '{"rateLimitType":"github_graphql","utilization":0.4,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":3000}\n' \
    "$((NOW - 60))" "$((NOW - 120))" > "$LATCH"
graphql_mode refused
sample
assert_eq "$(latch_field resetsAt)" "<absent>" "past reset is not carried into the refused latch"
assert_eq "$(latch_field limit)" "5000" "limit still carried"
out=$(gate)
assert_eq "${out%% *}" "closed:github_graphql" "gate still closes"

echo "T4: phantom /rate_limit graphql (used=0) vs self-report 4600/5000 => closed"
rm -f "$USAGE"/*.json
rest_fixture 0
graphql_good 4600
sample
out=$(gate)
assert_eq "$out" "closed:github_graphql util=92% (>= 90%) resets=$FUTURE_RESET" \
    "gate reads the self-report, not the phantom bucket"
assert_eq "$(latch_field remaining)" "400" "latch remaining from the self-report"

echo "T5: refused then good => latch recovers, gate open"
rm -f "$USAGE"/*.json
: > "$TMPROOT/scout.log"
rest_fixture 0
graphql_mode refused
sample
sample
graphql_good 2058
sample
assert_eq "$(latch_field status)" "<absent>" "good sample clears status"
out=$(gate)
assert_eq "${out%%:*}" "open" "dispatcher gate re-opens"
gs=$("$GATE_STATUS")
assert_contains "$gs" "other:     graphql  remaining=2942/5000 (< 90%)" "gate-status shows the true bucket"
assert_absent "$gs" "REJECTED" "no REJECTED marker after recovery"
log=$(cat "$TMPROOT/scout.log")
assert_eq "$(grep -c 'github graphql quota refused' <<<"$log" || true)" "1" \
    "two refused ticks log one refusal line"
assert_contains "$log" "already exceeded for user ID 1234567" "refusal line carries the refusal text"
assert_eq "$(grep -c 'github graphql quota recovered' <<<"$log" || true)" "1" \
    "recovery logs one line"

echo "T6: timeout and non-rate-limit failure leave the prior latch byte-identical"
rm -f "$USAGE"/*.json
graphql_good 2058
sample
before=$(cat "$LATCH")
# A rewrite would refresh observed_at; one second makes it visible.
sleep 1
graphql_mode timeout
sample
assert_eq "$(cat "$LATCH")" "$before" "timeout leaves the latch untouched"
graphql_mode error
sample
assert_eq "$(cat "$LATCH")" "$before" "HTTP 502 leaves the latch untouched"

echo "T7: /rate_limit latches search only — neither core nor graphql"
rm -f "$USAGE"/*.json
rest_fixture 4900
graphql_mode error
sample
assert_eq "$(ls "$USAGE" | tr '\n' ' ')" "github-search.json " \
    "/rate_limit half writes search only"

echo "T9: a refused pr list in run_capture latches github-graphql.rejected.json"
REJECTED="$USAGE/github-graphql.rejected.json"
rm -f "$USAGE"/*.json
: > "$TMPROOT/scout.log"
rest_fixture 0
graphql_good 1100
sample
GH_STUB_DIR="$STUB_DIR" GH_STUB_QUERY="$QUERY" PATH="$STUB_DIR/bin:$PATH" \
python3 - "$SCOUT" "$USAGE" >> "$TMPROOT/scout.log" 2>&1 <<'PY'
import importlib.machinery, importlib.util, sys
from pathlib import Path
loader = importlib.machinery.SourceFileLoader("fleet_state_scout", sys.argv[1])
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)
mod.USAGE_DIR = Path(sys.argv[2])
mod.GH_TIMEOUT_SECONDS = 5
for _ in range(2):
    assert mod.run_capture(["gh", "pr", "list", "--json", "number"]) is None
PY
rejected_field() {
    python3 -c 'import json, sys; v = json.load(open(sys.argv[1])).get(sys.argv[2]); print("<absent>" if v is None else v)' "$REJECTED" "$1" 2>/dev/null \
        || echo "<no latch>"
}
assert_eq "$(rejected_field status)" "rejected" "refused pr list latches status=rejected"
assert_eq "$(rejected_field resetsAt)" "$FUTURE_RESET" "latch carries the sampled future reset"
assert_contains "$(rejected_field reason)" "gh pr list: GraphQL: API rate limit already exceeded" \
    "latch reason names the refused call"
assert_eq "$(latch_field utilization)" "0.22" "the self-report latch is not touched"
log=$(cat "$TMPROOT/scout.log")
assert_eq "$(grep -c 'usage gate latched closed' <<<"$log" || true)" "1" \
    "two refused calls log one latch line"
before=$(cat "$REJECTED" 2>/dev/null || echo "<no latch>")
sleep 1
graphql_good 1100
sample
assert_eq "$(cat "$REJECTED" 2>/dev/null || echo "<no latch>")" "$before" "a later good self-report leaves the refusal latch byte-unchanged"
assert_eq "$(gate)" "closed:github_graphql rejected util=100% (>= 90%) resets=$FUTURE_RESET" \
    "dispatcher gate stays closed while the self-report reads 22%"

echo "T8: every gh invocation was modelled by the stub"
assert_eq "$(cat "$STUB_DIR/misses")" "" "no unmodelled gh calls"

summarize "fleet-state-scout github quota sampler"
