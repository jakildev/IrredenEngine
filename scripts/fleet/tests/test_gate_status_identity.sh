#!/usr/bin/env bash
# Tests for fleet-gate-status's GitHub identity labelling.
#
# The fleet bills `gh` calls to two pools: the operator's personal account
# and, once FLEET_GH_APP_* is configured, the GitHub App's installation
# token. fleet-gate-status must say which pool each reading belongs to:
#   - a latched sample carries `identity` (scout + refusal latch write it);
#   - with the App configured, both pools' live /rate_limit readings show;
#   - unconfigured, no live call is made.
# `gh` is a PATH stub that answers by GH_TOKEN (the App token = App pool, no
# token = personal pool); `fleet-gh-token` is a PATH stub that prints a fixed
# `ghs_` installation-shaped token. A latch's identity comes from the token's
# prefix, not its presence: a personal token in GH_TOKEN still reads `user`.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
GATE_STATUS="$SCRIPT_DIR/fleet-gate-status"
SCOUT="$SCRIPT_DIR/fleet-state-scout"

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
cleanup() { [[ -n "${TMPROOT:-}" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT

export FLEET_STATE_DIR="$TMPROOT/state"
USAGE="$FLEET_STATE_DIR/usage"
BIN="$TMPROOT/bin"
mkdir -p "$USAGE" "$BIN"
export FLEET_CONF=/dev/null
export GH_STUB_LOG="$TMPROOT/gh-calls"
: > "$GH_STUB_LOG"

APP_TOKEN="ghs_synthetic-app-token"
NOW=$(date +%s)
RESET=$((NOW + 3000))

# Python stub (+ .bat twin) per tests/lib_hermetic.sh: only `api /rate_limit`
# is modelled, answered by which identity GH_TOKEN selects.
cat > "$BIN/gh" <<'STUB'
#!/usr/bin/env python3
import os, sys
open(os.environ["GH_STUB_LOG"], "a").write(" ".join(sys.argv[1:]) + "\n")
if sys.argv[1:] != ["api", "/rate_limit"]:
    sys.stderr.write("stub gh: unmodelled invocation: %s\n" % " ".join(sys.argv[1:]))
    sys.exit(1)
reset = os.environ["GH_STUB_RESET"]
token = os.environ.get("GH_TOKEN")
if token == "ghs_synthetic-app-token":
    core, graphql = (5000, 12, 4988), (5000, 7, 4993)
elif token is None:
    core, graphql = (5000, 100, 4900), (5000, 71, 4929)
else:
    sys.stderr.write("HTTP 401: Bad credentials\n")
    sys.exit(1)
sys.stdout.write(
    '{"resources":{"core":{"limit":%d,"used":%d,"remaining":%d,"reset":%s},'
    '"graphql":{"limit":%d,"used":%d,"remaining":%d,"reset":%s}}}\n'
    % (core + (reset,) + graphql + (reset,))
)
STUB
printf '@echo off\r\npython3 "%%~dp0gh" %%*\r\n' > "$BIN/gh.bat"
cat > "$BIN/fleet-gh-token" <<STUB
#!/usr/bin/env bash
printf '%s' "\${GATE_STUB_MINT-$APP_TOKEN}"
STUB
chmod +x "$BIN/gh" "$BIN/fleet-gh-token"
export GH_STUB_RESET="$RESET"

latch() {  # latch <file-stem> <json>
    printf '%s\n' "$2" > "$USAGE/$1.json"
}
status() {  # status [args...] — App knobs set unless GATE_UNCONFIGURED=1
    if [[ "${GATE_UNCONFIGURED:-}" == 1 ]]; then
        PATH="$BIN:$PATH" "$GATE_STATUS" "$@"
    else
        FLEET_GH_APP_ID=1 FLEET_GH_APP_INSTALLATION_ID=2 FLEET_GH_APP_KEY_PATH=/synthetic \
            PATH="$BIN:$PATH" "$GATE_STATUS" "$@"
    fi
}

echo "T1: a user-identity rejected latch beside a healthy App pool is labelled"
latch github-graphql.rejected "{\"rateLimitType\":\"github_graphql\",\"status\":\"rejected\",\"utilization\":1.0,\"observed_at\":$NOW,\"resetsAt\":$RESET,\"identity\":\"user\"}"
latch github-graphql "{\"rateLimitType\":\"github_graphql\",\"utilization\":0.0014,\"observed_at\":$NOW,\"resetsAt\":$RESET,\"limit\":5000,\"remaining\":4993,\"identity\":\"app\"}"
out=$(status)
assert_contains "$out" "breaching: graphql[user]" "the rejected latch reads as the user pool"
assert_contains "$out" "other:     graphql[app]" "the healthy sample reads as the App pool"
assert_contains "$out" "GitHub identities (live /rate_limit, App configured):" "live section present"
assert_contains "$out" "user  core remaining=4900/5000, graphql remaining=4929/5000" "user pool reading"
assert_contains "$out" "app   core remaining=4988/5000, graphql remaining=4993/5000" "app pool reading"

echo "T2: --json carries per-sample identity and both live pools"
json=$(status --json)
assert_eq "$(python3 -c 'import json,sys; d=json.loads(sys.argv[1]); print(sorted(o["identity"] for o in d["github"]))' "$json")" \
    "['app', 'user']" "json github[] observations carry identity"
assert_eq "$(python3 -c 'import json,sys; d=json.loads(sys.argv[1])["github_identities"]; print(sorted(d), d["app"]["graphql"]["remaining"], d["user"]["graphql"]["remaining"])' "$json")" \
    "['app', 'user'] 4993 4929" "json github_identities has both pools"

echo "T3: App configured but no token minted => app unavailable, user still shown"
out=$(GATE_STUB_MINT= status)
assert_contains "$out" "app   unavailable (fleet-gh-token minted no token)" "app marked unavailable"
assert_contains "$out" "user  core remaining=4900/5000" "user pool still read"

echo "T4: App unconfigured => no live probe, no identities section"
: > "$GH_STUB_LOG"
out=$(GATE_UNCONFIGURED=1 status)
assert_absent "$out" "GitHub identities" "no identities section"
assert_eq "$(cat "$GH_STUB_LOG")" "" "no gh call made"
assert_contains "$out" "graphql[user]" "latched identity labels still render"

echo "T5: a latch without identity (pre-field) renders unlabelled"
latch github-graphql "{\"rateLimitType\":\"github_graphql\",\"utilization\":0.1,\"observed_at\":$NOW,\"resetsAt\":$RESET,\"limit\":5000,\"remaining\":4500}"
out=$(GATE_UNCONFIGURED=1 status)
assert_contains "$out" "other:     graphql       remaining=4500/5000" "no identity suffix on an old latch"

echo "T6: the scout and the refusal latch record the identity they ran under"
rm -f "$USAGE"/*.json
ident() {  # ident <GH_TOKEN-or-"-"> [GITHUB_TOKEN] -> identity written by each writer
    SCOUT_PATH="$SCOUT" USAGE_PATH="$USAGE" TOKEN="$1" ALT_TOKEN="${2:-}" python3 - <<'PY'
import importlib.machinery, importlib.util, json, os
from pathlib import Path
os.environ.pop("GH_TOKEN", None)
os.environ.pop("GITHUB_TOKEN", None)
if os.environ["TOKEN"] != "-":
    os.environ["GH_TOKEN"] = os.environ["TOKEN"]
if os.environ["ALT_TOKEN"]:
    os.environ["GITHUB_TOKEN"] = os.environ["ALT_TOKEN"]
loader = importlib.machinery.SourceFileLoader("fleet_state_scout", os.environ["SCOUT_PATH"])
spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
mod = importlib.util.module_from_spec(spec)
loader.exec_module(mod)
usage = Path(os.environ["USAGE_PATH"])
mod.USAGE_DIR = usage
mod._latch_github_pool("core", {"rateLimitType": "github_core", "utilization": 0.1})
mod.fleet_gh_fallback.latch_refusal("synthetic", usage)
print(json.loads((usage / f"github-{mod.fleet_gh_fallback.gh_identity()}-core.json").read_text())["identity"],
      json.loads((usage / f"github-{mod.fleet_gh_fallback.gh_identity()}-graphql.rejected.json").read_text())["identity"])
PY
}
assert_eq "$(ident "$APP_TOKEN")" "app app" "installation token in GH_TOKEN => app"
assert_eq "$(ident -)" "user user" "no token => user (keychain login)"
assert_eq "$(ident ghp_synthetic-classic-pat)" "user user" "classic personal token in GH_TOKEN => user"
assert_eq "$(ident github_pat_synthetic-fine-grained)" "user user" "fine-grained personal token in GH_TOKEN => user"
assert_eq "$(ident - "$APP_TOKEN")" "app app" "GITHUB_TOKEN is gh's fallback when GH_TOKEN is unset"
assert_eq "$(ident ghp_synthetic-classic-pat "$APP_TOKEN")" "user user" "GH_TOKEN outranks GITHUB_TOKEN, as in gh"

summarize "fleet-gate-status identity labelling"
