#!/usr/bin/env bash
# Tests fleet-up's `--satellite` launch flag: the host-profile block it
# resolves (lifted from the script between its `# --- host profile (begin)`
# / `(end)` markers, so no tmux, git, or clone is touched) and the wiring
# that carries the profile out — the launch environment handed to
# fleet-dispatcher, the sentinel the architect panes read, and the flag's
# acceptance in both argument loops.
#
# The satellite profile is the whole point of the flag (docs/agents/
# FLEET-CROSS-HOST-SMOKE.md § "Satellite host profile"): the dispatcher
# serves only smoke-worker + worker, the worker lane elects only
# **Host:**-pinned items, and the architect panes stay up. A regression
# that dropped either knob would boot a full fleet on the ship-platform box.
#
# Hermetic: reads fleet-up's source only.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -f "$FLEET_UP" ]] || { echo "SKIP: fleet-up not found at $FLEET_UP" >&2; exit 3; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

BLOCK="$TMPROOT/host-profile-block.sh"
sed -n '/^# --- host profile (begin) ---$/,/^# --- host profile (end) ---$/p' "$FLEET_UP" > "$BLOCK"
if [[ -s "$BLOCK" ]]; then
    ok "T0: the host-profile block is delimited by its begin/end markers"
else
    bad "T0: the host-profile block is delimited by its begin/end markers"
    summarize "fleet-up --satellite"
    exit 1
fi

# resolve <HOST_PROFILE> [conf-style assignments...] — runs the lifted block
# in a fresh bash and prints the two knobs it leaves behind.
resolve() {
    local profile="$1"; shift
    env -i PATH="$PATH" HOME="$TMPROOT" "$BASH" -c '
        set -uo pipefail
        HOST_PROFILE="$1"; shift
        for kv in "$@"; do eval "$kv"; done
        source "$0" >/dev/null
        printf "roles=%s pinned=%s\n" "${FLEET_DISPATCH_ROLES:-<unset>}" "${FLEET_WORKER_HOST_PINNED_ONLY:-<unset>}"
    ' "$BLOCK" "$profile" "$@"
}

echo "T1: --satellite sets both dispatcher knobs"
assert_eq "$(resolve satellite)" "roles=smoke-worker worker pinned=1" "satellite profile knobs"

echo "T2: the default profile leaves the knobs alone"
assert_eq "$(resolve full)" "roles=<unset> pinned=<unset>" "full profile sets nothing"

echo "T3: a conf value survives a full launch and is outranked by the flag"
assert_eq "$(resolve full 'FLEET_DISPATCH_ROLES="smoke-worker"')" "roles=smoke-worker pinned=<unset>" \
    "conf-only satellite (smoke-only) stays as configured"
assert_eq "$(resolve satellite 'FLEET_DISPATCH_ROLES="merger"')" "roles=smoke-worker worker pinned=1" \
    "the flag wins over a conflicting conf"

echo "T4: the block announces the resolved profile"
out=$(env -i PATH="$PATH" HOME="$TMPROOT" "$BASH" -c 'HOST_PROFILE=satellite; source "$0"' "$BLOCK")
assert_contains "$out" "host profile: satellite" "boot line names the profile"
assert_contains "$out" "dispatch roles: smoke-worker worker" "boot line names the served roles"

echo "T5: the flag is accepted by both argument loops and documented"
src=$(<"$FLEET_UP")
assert_contains "$src" '--satellite) HOST_PROFILE="satellite" ;;' "early loop resolves the flag"
assert_contains "$src" '--satellite) ;;' "main loop accepts the flag (unknown-arg guard)"
assert_contains "$src" '#   fleet-up [mode] [--no-attach] [--satellite]' "header usage names the flag"

echo "T6: the profile reaches the dispatcher as launch environment, never an export"
assert_contains "$src" '_dispatcher_env+=(FLEET_DISPATCH_ROLES="$FLEET_DISPATCH_ROLES"' \
    "dispatcher launch env carries FLEET_DISPATCH_ROLES"
assert_contains "$src" 'FLEET_WORKER_HOST_PINNED_ONLY="$FLEET_WORKER_HOST_PINNED_ONLY")' \
    "dispatcher launch env carries FLEET_WORKER_HOST_PINNED_ONLY"
assert_absent "$src" "export FLEET_DISPATCH_ROLES" "no export of the roles knob"
assert_absent "$src" "export FLEET_WORKER_HOST_PINNED_ONLY" "no export of the pinned-only knob"

echo "T7: the sentinel the architect panes read is written beside dispatch-mode and cleared by fleet-down"
assert_contains "$src" '"$HOST_PROFILE" > "$HOME/.fleet/state/host-profile"' "fleet-up writes host-profile"
down=$(<"$SCRIPT_DIR/fleet-down")
assert_contains "$down" '"$HOME/.fleet/state/host-profile"' "fleet-down removes host-profile"

echo "T8: the bootstrap heredoc is handed the served-role list as an env prefix"
assert_contains "$src" "FLEET_DISPATCH_ROLES=\"\${FLEET_DISPATCH_ROLES:-}\" python3 - <<'PY'" \
    "heredoc sees FLEET_DISPATCH_ROLES"
assert_contains "$src" 'if served and role not in served:' "heredoc skips unserved roles"

summarize "fleet-up --satellite"
