#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-tick tmux GH_TOKEN seed (seed_tmux_gh_token),
# exercised through the --seed-tmux-token hook.
#
# fleet-up seeds the tmux server's global GH_TOKEN once; the dispatcher re-seeds
# it each tick so a pane split an hour later inherits a live installation token.
# A private tmux server (-S socket, TMUX unset) carries the whole run: tmux
# follows $TMUX ahead of TMUX_TMPDIR, so a run from inside a fleet pane would
# otherwise write into the live server's global GH_TOKEN. The suite also asserts
# the live server's value is unchanged by the run, without ever printing it.
#
#   T1: first seed -> a NEW pane reads tok-1 (printenv) and show-environment agrees
#   T2: second invocation -> a later new pane reads tok-2 (the refresh)
#   T3: an empty mint leaves the seeded value untouched (never blank, never unset)
#   T4: no session on the pinned socket -> nothing is written
#   T5: the live (default) server's GH_TOKEN is unchanged by the whole run

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2; exit 3; }
command -v tmux >/dev/null 2>&1 || { echo "SKIP: tmux not installed" >&2; exit 0; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() {
    [[ -n "$TMPROOT" && -S "$TMPROOT/sock" ]] && tmux -S "$TMPROOT/sock" kill-server 2>/dev/null
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

# Snapshot the live default server's value before anything else runs. Equality
# is checked with [[ ]] so a failure never prints the token.
live_before=$(tmux show-environment -g GH_TOKEN 2>&1 || true)

unset TMUX
export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_SESSION="fleet-test-$$"
export FLEET_TMUX_SOCKET="$TMPROOT/sock"
mkdir -p "$HOME" "$FLEET_STATE_DIR" "$TMPROOT/bin"
touch "$FLEET_CONF"
unset GH_TOKEN

# Mint stub: tok-N, N incrementing per call; GHT_STUB_EMPTY=1 prints nothing.
cat > "$TMPROOT/bin/fleet-gh-token" <<'SH'
#!/usr/bin/env bash
[[ -n "${GHT_STUB_EMPTY:-}" ]] && exit 0
n=$(cat "$GHT_STUB_COUNTER" 2>/dev/null || echo 0)
n=$((n + 1))
echo "$n" > "$GHT_STUB_COUNTER"
echo "tok-$n"
SH
chmod +x "$TMPROOT/bin/fleet-gh-token"
export GHT_STUB_COUNTER="$TMPROOT/counter"
export PATH="$TMPROOT/bin:$PATH"

seed() { "$DISPATCHER" --seed-tmux-token; }
ptmux() { tmux -S "$TMPROOT/sock" "$@"; }

# pane_token — GH_TOKEN as a freshly spawned pane process sees it.
pane_token() {
    local out="$TMPROOT/pane.out"
    rm -f "$out"
    ptmux new-window -d -t "$FLEET_SESSION" "printenv GH_TOKEN > '$out' 2>&1; true"
    local i
    for i in $(seq 1 50); do
        [[ -s "$out" ]] && break
        sleep 0.1
    done
    tr -d '\n' < "$out" 2>/dev/null
}

echo "T4: no session on the pinned socket -> nothing written"
seed
if ptmux has-session -t "$FLEET_SESSION" 2>/dev/null; then
    bad "seed created a session on the pinned socket"
else
    ok "no session appeared on the pinned socket"
fi

rm -f "$GHT_STUB_COUNTER"   # T4's mint was spent on a seed that had nowhere to land

ptmux -f /dev/null new-session -d -s "$FLEET_SESSION" "sleep 300"
sleep 0.2

echo "T1: first seed reaches a new pane"
seed
assert_eq "$(pane_token)" "tok-1" "a new pane's GH_TOKEN reads tok-1"
assert_eq "$(ptmux show-environment -g GH_TOKEN)" "GH_TOKEN=tok-1" "show-environment -g reads tok-1"

echo "T2: second invocation refreshes the value a later pane reads"
seed
assert_eq "$(pane_token)" "tok-2" "a later new pane's GH_TOKEN reads tok-2"

echo "T3: an empty mint leaves the seeded value untouched"
GHT_STUB_EMPTY=1 seed
assert_eq "$(ptmux show-environment -g GH_TOKEN)" "GH_TOKEN=tok-2" "empty mint did not blank or unset GH_TOKEN"
assert_eq "$(pane_token)" "tok-2" "a new pane after the empty mint still reads tok-2"

echo "T5: the live default server's GH_TOKEN is unchanged by the run"
live_after=$(env -u TMUX tmux show-environment -g GH_TOKEN 2>&1 || true)
if [[ "$live_before" == "$live_after" ]]; then
    ok "default server's GH_TOKEN is unchanged"
else
    bad "default server's GH_TOKEN changed during the run (value withheld)"
fi

summarize "dispatcher tmux token seed tests"
