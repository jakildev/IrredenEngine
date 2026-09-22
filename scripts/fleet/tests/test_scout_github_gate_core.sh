#!/usr/bin/env bash
# Tests for fleet-state-scout's core-pool quota latch feeding the dispatcher's
# usage gate and fleet-gate-status.
#
# The core latch is written from the X-RateLimit-* headers on the scout's own
# conditional REST responses, never from `gh api /rate_limit`, which can
# report a phantom near-empty bucket for the same identity. The suite drives
# one real scout fetcher (`_rest_list`) through a fake `urlopen`, then the
# real sampler against a stub `gh`, and hands the written file to the real
# `fleet-dispatcher --gate-status` and `fleet-gate-status`.
#
# Covers:
#   - /rate_limit core used=1/5000 vs a 304 carrying used=4600/5000 => gate
#     closed at the 90% threshold
#   - the sample issues no HTTP request and no gh call beyond the search and
#     graphql reads it already made
#   - a URLError, a header-less response and a 401 each leave the latch
#     byte-identical; with no observation at all, no core file is written

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
cleanup() { [[ -n "${TMPROOT:-}" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT

export FLEET_STATE_DIR="$TMPROOT/state"
USAGE="$FLEET_STATE_DIR/usage"
LATCH="$USAGE/github-core.json"
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
# Models the two calls the sampler makes; /rate_limit reports the phantom
# core bucket. Every invocation is logged, and anything else fails closed.
cat > "$STUB_DIR/bin/gh" <<'STUB'
#!/usr/bin/env bash
d="$GH_STUB_DIR"
echo "$*" >> "$d/calls"
if [[ "$#" -eq 2 && "$1" == api && "$2" == /rate_limit ]]; then
    cat "$d/rest.json"
    exit 0
fi
if [[ "$#" -eq 4 && "$1" == api && "$2" == graphql && "$3" == -f \
      && "$4" == "query=$GH_STUB_QUERY" ]]; then
    cat "$d/graphql.json"
    exit 0
fi
echo "stub gh: unmodelled invocation: $*" | tee -a "$d/misses" >&2
exit 1
STUB
chmod +x "$STUB_DIR/bin/gh"
: > "$STUB_DIR/misses"

printf '{"resources":{"core":{"limit":5000,"used":1,"remaining":4999,"reset":%s},"graphql":{"limit":5000,"used":0,"remaining":5000,"reset":%s},"search":{"limit":30,"used":0,"remaining":30,"reset":%s}}}\n' \
    "$((NOW + 600))" "$FUTURE_RESET" "$FUTURE_RESET" > "$STUB_DIR/rest.json"
printf '{"data":{"rateLimit":{"limit":5000,"used":100,"remaining":4900,"resetAt":"%s"}}}\n' \
    "$FUTURE_RESET_ISO" > "$STUB_DIR/graphql.json"

# One scout process running a step list. `fetch=<mode>` drives one real
# `_rest_list` read through the fake urlopen; `sample` runs the sampler and
# copies github-core.json (or "<absent>") to $TMPROOT/snap.<n>; `sleep`
# waits a second so a rewrite's fresh observed_at would be visible.
# Per-sample urlopen counts land in $TMPROOT/sample-requests.
drive() {
    : > "$TMPROOT/sample-requests"
    rm -f "$TMPROOT"/snap.*
    GH_STUB_DIR="$STUB_DIR" GH_STUB_QUERY="$QUERY" PATH="$STUB_DIR/bin:$PATH" \
    RESET="$FUTURE_RESET" OUT="$TMPROOT" \
    python3 - "$SCOUT" "$USAGE" "$TMPROOT/etag" "$@" >> "$TMPROOT/scout.log" 2>&1 <<'PY'
import importlib.machinery, importlib.util, os, shutil, sys, time
import urllib.error, urllib.request
from email.message import Message
from pathlib import Path

loader = importlib.machinery.SourceFileLoader("fleet_state_scout", sys.argv[1])
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)
mod.USAGE_DIR = Path(sys.argv[2])
mod.GH_TIMEOUT_SECONDS = 1
poll = sys.modules["fleet_gh_poll"]
poll.DEFAULT_CACHE_DIR = Path(sys.argv[3])
poll.auth_token = lambda refresh=False: "synthetic-token"

URL_PREFIX = "https://api.github.com/repos/jakildev/IrredenEngine/pulls?"
RESET = os.environ["RESET"]
OUT = Path(os.environ["OUT"])
requests = []
mode = {"value": None}


def headers(limit=None, used=None, etag=None):
    msg = Message()
    if etag:
        msg["ETag"] = etag
    if limit is not None:
        msg["X-RateLimit-Limit"] = str(limit)
        msg["X-RateLimit-Used"] = str(used)
        msg["X-RateLimit-Remaining"] = str(limit - used)
        msg["X-RateLimit-Reset"] = RESET
        msg["X-RateLimit-Resource"] = "core"
    return msg


class Resp:
    def __init__(self, hdrs):
        self.headers = hdrs
        self.status = 200

    def read(self):
        return b"[]"

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def fake_urlopen(req, timeout=None):
    url = req.full_url
    requests.append(url)
    if not url.startswith(URL_PREFIX):
        raise AssertionError(f"fake urlopen: unmodelled URL {url}")
    m = mode["value"]
    if m == "200":
        return Resp(headers(5000, 4599, etag='W/"synthetic-etag"'))
    if m == "304":
        raise urllib.error.HTTPError(url, 304, "Not Modified",
                                     headers(5000, 4600), None)
    if m == "noheaders":
        return Resp(headers(etag='W/"synthetic-etag-2"'))
    if m == "urlerror":
        raise urllib.error.URLError("synthetic outage")
    if m == "401":
        raise urllib.error.HTTPError(url, 401, "Unauthorized",
                                     headers(60, 59), None)
    raise AssertionError(f"fake urlopen: unmodelled mode {m}")


urllib.request.urlopen = fake_urlopen

samples = 0
for step in sys.argv[4:]:
    if step.startswith("fetch="):
        mode["value"] = step.split("=", 1)[1]
        mod._rest_list("jakildev/IrredenEngine", "pulls", {"state": "open"})
    elif step == "sample":
        before = len(requests)
        mod.sample_github_rate_limit()
        samples += 1
        with open(OUT / "sample-requests", "a") as f:
            f.write(f"{len(requests) - before}\n")
        latch = mod.USAGE_DIR / "github-core.json"
        snap = OUT / f"snap.{samples}"
        if latch.exists():
            shutil.copyfile(latch, snap)
        else:
            snap.write_text("<absent>")
    elif step == "sleep":
        time.sleep(1)
    else:
        raise SystemExit(f"drive: unknown step {step}")
PY
}

snap() { cat "$TMPROOT/snap.$1"; }

latch_field() {
    python3 -c 'import json, sys; v = json.load(open(sys.argv[1])).get(sys.argv[2]); print("<absent>" if v is None else v)' "$LATCH" "$1"
}

gate() { "$DISPATCHER" --gate-status; }

echo "T1: phantom /rate_limit core (used=1) vs 304 headers 4600/5000 => closed"
rm -f "$USAGE"/*.json
rm -rf "$TMPROOT/etag"
: > "$STUB_DIR/calls"
drive fetch=200 fetch=304 sample
assert_eq "$(latch_field remaining)" "400" "latch remaining comes from the 304's headers"
assert_eq "$(latch_field limit)" "5000" "latch limit comes from the headers"
assert_eq "$(latch_field resetsAt)" "$FUTURE_RESET" "latch resetsAt is X-RateLimit-Reset"
assert_eq "$(latch_field rateLimitType)" "github_core" "latch type is github_core"
out=$(gate)
assert_eq "${out%% resets=*}" "closed:github_core util=92% (>= 90%)" \
    "dispatcher gate closes on the header reading, not the phantom bucket"
gs=$("$GATE_STATUS")
assert_contains "$gs" "Fleet-wide usage gate: CLOSED" "gate-status prints CLOSED"
assert_contains "$gs" "breaching: core     remaining=400/5000 (>= 90%)" \
    "gate-status core row carries the header reading"

echo "T2: the sample spends nothing beyond the reads it already made"
assert_eq "$(cat "$TMPROOT/sample-requests")" "0" "no HTTP request issued during the sample"
assert_eq "$(tr '\n' '|' < "$STUB_DIR/calls")" "api /rate_limit|api graphql -f query=$QUERY|" \
    "the sample's gh calls are the search and graphql reads alone"

echo "T3: URLError, header-less and 401 responses leave the latch byte-identical"
rm -f "$USAGE"/*.json
rm -rf "$TMPROOT/etag"
drive fetch=200 fetch=304 sample sleep fetch=urlerror sample fetch=noheaders sample fetch=401 sample
assert_eq "$(latch_field remaining)" "400" "good latch in place before the failures"
assert_eq "$(snap 2)" "$(snap 1)" "URLError leaves the latch untouched"
assert_eq "$(snap 3)" "$(snap 1)" "header-less response leaves the latch untouched"
assert_eq "$(snap 4)" "$(snap 1)" "401 leaves the latch untouched"
assert_eq "$(tr '\n' ' ' < "$TMPROOT/sample-requests")" "0 0 0 0 " "no sample issued an HTTP request"

echo "T4: no header observation => no core file, gate names no github_core"
rm -f "$USAGE"/*.json
rm -rf "$TMPROOT/etag"
drive sample fetch=urlerror fetch=noheaders fetch=401 sample
assert_eq "$(snap 1)" "<absent>" "cold start writes no core file"
assert_eq "$(snap 2)" "<absent>" "failed and 401 fetches write no core file"
out=$(gate)
assert_eq "${out%%:*}" "open" "dispatcher gate stays open"
assert_absent "$out" "github_core" "gate output names no github_core"
assert_absent "$("$GATE_STATUS")" " core " "gate-status shows no core row"

echo "T5: every stub invocation was modelled"
assert_eq "$(cat "$STUB_DIR/misses")" "" "no unmodelled gh calls"
assert_absent "$(cat "$TMPROOT/scout.log")" "unmodelled" "no unmodelled urlopen URL or mode"

summarize "fleet-state-scout github core quota latch"
