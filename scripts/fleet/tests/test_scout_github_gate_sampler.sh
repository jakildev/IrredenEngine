#!/usr/bin/env bash
# Tests for fleet-state-scout's GitHub quota sampler feeding the dispatcher's
# github_graphql gate.
#
# Sibling of test_dispatcher_github_gate.sh, which drops hand-written latch
# files and so exercises only the evaluator. This suite drives the real
# sample_github_rate_limit() against a stub `gh` on PATH, then reads the file
# the sampler wrote through the real `fleet-dispatcher --gate-status` and the
# real `fleet-gate-status`. Each tick is a fresh interpreter, so the latch
# carries state across ticks the way it does across a scout restart.
#
# Covers:
#   - a refused graphql sample latches the pool rejected => gate closed
#   - /rate_limit's phantom graphql bucket (used=0) is not the source: the
#     GraphQL self-report at 92% closes the gate
#   - a good sample after a refused one clears the latch => gate open
#   - a timeout, or a failure that is not a rate-limit refusal, leaves the
#     prior latch byte-identical

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
SCOUT="$SCRIPT_DIR/fleet-state-scout"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
GATE_STATUS="$SCRIPT_DIR/fleet-gate-status"

for _subject in "$SCOUT" "$DISPATCHER" "$GATE_STATUS"; do
    if [[ ! -f "$_subject" ]]; then
        echo "SKIP: subject not found at $_subject" >&2
        exit 3
    fi
done
unset _subject

TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_starts_with() {
    assert_eq "${1:0:${#2}}" "$2" "$3"
}

assert_line_matches() {
    local haystack="$1" pattern="$2" msg="$3"
    if grep -Eq -- "$pattern" <<<"$haystack"; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected a line matching /$pattern/ in:"
        sed 's/^/          /' <<<"$haystack"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
USAGE="$FLEET_STATE_DIR/usage"
LATCH="$USAGE/github-graphql.json"
mkdir -p "$USAGE" "$TMPROOT/bin"

# Isolate from the operator's fleet-up.conf and any per-type gate override,
# same rationale as test_dispatcher_github_gate.sh.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

NOW=$(date +%s)
RESET_EPOCH=$((NOW + 1800))
RESET_ISO=$(python3 -c "import datetime,sys; print(datetime.datetime.fromtimestamp(int(sys.argv[1]), datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))" "$RESET_EPOCH")

# /rate_limit as it reads during the phantom: graphql used=0 with a reset one
# hour out, whatever the real bucket holds.
cat > "$TMPROOT/rest.json" <<EOF
{"resources":{
  "core":{"limit":5000,"used":100,"remaining":4900,"reset":$RESET_EPOCH},
  "graphql":{"limit":5000,"used":0,"remaining":5000,"reset":$((NOW + 3600))},
  "search":{"limit":30,"used":0,"remaining":30,"reset":$((NOW + 60))}}}
EOF

gql_ok() {
    printf '{"data":{"rateLimit":{"limit":5000,"used":%s,"remaining":%s,"resetAt":"%s"}}}\n' \
        "$1" "$((5000 - $1))" "$RESET_ISO" > "$TMPROOT/gql.out"
    : > "$TMPROOT/gql.err"
    echo 0 > "$TMPROOT/gql.rc"
}

gql_refused() {
    # Synthetic user ID; the wording is GitHub's primary-limit refusal.
    printf '%s\n' '{"errors":[{"type":"RATE_LIMIT","code":"graphql_rate_limit","message":"API rate limit already exceeded for user ID 1."}]}' \
        > "$TMPROOT/gql.out"
    printf '%s\n' 'gh: API rate limit already exceeded for user ID 1.' > "$TMPROOT/gql.err"
    echo 1 > "$TMPROOT/gql.rc"
}

gql_fail() {
    : > "$TMPROOT/gql.out"
    printf '%s\n' "$1" > "$TMPROOT/gql.err"
    echo 1 > "$TMPROOT/gql.rc"
}

# The stub models exactly the two invocations the sampler makes and fails
# closed on anything else, so a new gh call in the sampler cannot fall
# through to the network.
cat > "$TMPROOT/bin/gh" <<'STUB'
#!/usr/bin/env bash
if [[ $# -eq 2 && "$1" == api && "$2" == /rate_limit ]]; then
    cat "$STUB_DIR/rest.json"
    exit 0
fi
if [[ $# -eq 4 && "$1" == api && "$2" == graphql && "$3" == -f && "$4" == query=* ]]; then
    [[ -f "$STUB_DIR/gql.sleep" ]] && sleep "$(cat "$STUB_DIR/gql.sleep")"
    cat "$STUB_DIR/gql.out"
    cat "$STUB_DIR/gql.err" >&2
    exit "$(cat "$STUB_DIR/gql.rc")"
fi
echo "gh stub: unmodelled invocation: $*" >&2
exit 97
STUB
chmod +x "$TMPROOT/bin/gh"
export STUB_DIR="$TMPROOT"

cat > "$TMPROOT/drive.py" <<'PY'
import importlib.machinery
import importlib.util
import os
import sys
from pathlib import Path

loader = importlib.machinery.SourceFileLoader("fleet_state_scout", sys.argv[1])
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)
mod.USAGE_DIR = Path(os.environ["FLEET_STATE_DIR"]) / "usage"
mod.GH_TIMEOUT_SECONDS = 1
mod.sample_github_rate_limit()
PY

# One scout tick's quota sample against the stub. The scout log goes to a
# file so assertions can read how many lines a run of ticks produced.
tick() {
    PATH="$TMPROOT/bin:$PATH" python3 "$TMPROOT/drive.py" "$SCOUT" >> "$TMPROOT/scout.log"
}

latch_field() {
    python3 -c "import json,sys; v=json.load(open(sys.argv[1])).get(sys.argv[2]); print('' if v is None else v)" "$LATCH" "$1"
}

echo "T1: a refused graphql sample closes the gate"
gql_ok 2058
tick
gql_refused
tick
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:github_graphql rejected util=100%" "dispatcher reads the sampler's latch as a rejected wall"
status_out=$("$GATE_STATUS")
assert_line_matches "$status_out" '^Fleet-wide usage gate: CLOSED$' "fleet-gate-status reports the gate CLOSED"
assert_line_matches "$status_out" '^  breaching: graphql +remaining=0/5000 REJECTED \(>= 90%\)' "graphql row carries REJECTED"
assert_eq "$(latch_field resetsAt)" "$RESET_EPOCH" "rejected latch carries the last good sample's future reset"

echo "T2: repeated refusals log the transition once"
tick
tick
n=$(grep -c 'rate-limit refusal:' "$TMPROOT/scout.log" || true)
assert_eq "$n" 1 "one refusal line across three refused ticks"

echo "T3: recovery — a good sample after a refused one reopens the gate"
gql_ok 2058
tick
assert_eq "$(latch_field status)" "" "recovered latch carries no status"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "open" "dispatcher gate reopens"
status_out=$("$GATE_STATUS")
assert_line_matches "$status_out" '^  other: +graphql +remaining=2942/5000 \(< 90%\)' "fleet-gate-status shows the recovered bucket"
n=$(grep -c 'rate-limit refusal cleared' "$TMPROOT/scout.log" || true)
assert_eq "$n" 1 "one recovery line"

echo "T4: the phantom /rate_limit graphql bucket is not the source"
gql_ok 4600
tick
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:github_graphql util=92% (>= 90%)" "GraphQL self-report at 92% closes the gate under a used=0 /rate_limit"
assert_eq "$(latch_field resetsAt)" "$RESET_EPOCH" "resetsAt is the self-report's reset as an epoch int"
assert_eq "$(ls "$USAGE" | tr '\n' ' ')" "github-core.json github-graphql.json github-search.json " \
    "core and search are still latched from /rate_limit"

echo "T5: a failure that is not a rate-limit refusal leaves the latch untouched"
cp "$LATCH" "$TMPROOT/prior.json"
gql_fail 'HTTP 502: Bad Gateway (https://api.github.com/graphql)'
tick
assert_eq "$(cmp -s "$LATCH" "$TMPROOT/prior.json" && echo same || echo differs)" same "rc=1 non-refusal leaves the latch byte-identical"

echo "T6: a timed-out sample leaves the latch untouched"
gql_ok 10
echo 3 > "$TMPROOT/gql.sleep"
tick
rm -f "$TMPROOT/gql.sleep"
assert_eq "$(cmp -s "$LATCH" "$TMPROOT/prior.json" && echo same || echo differs)" same "timeout leaves the latch byte-identical"

echo "T7: a refusal with no future reset on record omits resetsAt"
python3 - "$LATCH" "$((NOW - 60))" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["resetsAt"] = int(sys.argv[2])
json.dump(d, open(p, "w"))
PY
gql_refused
tick
assert_eq "$(latch_field resetsAt)" "" "past reset is not carried into the rejected latch"
out=$("$DISPATCHER" --gate-status)
assert_starts_with "$out" "closed:github_graphql rejected util=100%" "latch without resetsAt still closes the gate on observed_at"

echo "T8: stub fidelity — an unmodelled gh call fails closed"
if PATH="$TMPROOT/bin:$PATH" gh api graphql --jq . >/dev/null 2>&1; then
    bad "stub rejects an unmodelled invocation"
else
    ok "stub rejects an unmodelled invocation"
fi

summarize "scout github gate sampler"
