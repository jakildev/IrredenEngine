#!/usr/bin/env bash
# Credentials must not survive in tmux global state. The private server
# isolates this test from the live fleet, whose token is compared but never printed.

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
    rm -f "$out" "$out.done"
    ptmux new-window -d -t "$FLEET_SESSION" "printenv GH_TOKEN > '$out' 2>&1; echo done > '$out.done'"
    local i
    for i in $(seq 1 50); do
        [[ -f "$out.done" ]] && break
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

echo "T1: a stale App token is removed before a new pane"
ptmux set-environment -g GH_TOKEN ghs_synthetic-old
seed
assert_eq "$(pane_token)" "" "a new pane inherits no token"
assert_eq "$(ptmux show-environment -g GH_TOKEN 2>/dev/null || true)" "" "global GH_TOKEN removed"

echo "T2: another credential cannot survive the next clear"
ptmux set-environment -g GH_TOKEN synthetic-user-credential
ptmux set-environment -g GITHUB_TOKEN ghs_synthetic-other
seed
assert_eq "$(pane_token)" "" "a later pane inherits no credential"
assert_eq "$(ptmux show-environment -g GITHUB_TOKEN 2>/dev/null || true)" "" "fallback token removed"

echo "T3: clearing credentials does not mint a token"
[[ ! -f "$GHT_STUB_COUNTER" ]] && ok "no token mint" || bad "unneeded token mint"

echo "T5: the live default server's GH_TOKEN is unchanged by the run"
live_after=$(env -u TMUX tmux show-environment -g GH_TOKEN 2>&1 || true)
if [[ "$live_before" == "$live_after" ]]; then
    ok "default server's GH_TOKEN is unchanged"
else
    bad "default server's GH_TOKEN changed during the run (value withheld)"
fi

summarize "dispatcher tmux token seed tests"
