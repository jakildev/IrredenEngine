#!/usr/bin/env bash
# Tests fleet-up's pane env seed (seed_tmux_pane_env plus the new-session
# block that calls it, both lifted from the script): ir-run's launch knobs
# (FLEET_UNATTENDED, FLEET_RUN_DEFAULT_TIMEOUT, FLEET_WINDOW_MODE) reach every
# pane even when the tmux server predates the fleet-up run.
#
# fleet-down kills the session, not the server, and a pane's shell inherits
# the server's global env — not the env of the client that created it. An
# export in fleet-up alone therefore leaves panes unmarked, and their direct
# ir-run launches take the host's screen.
#
#   T0: the helper and the new-session block are extracted
#   T1: control — on a pre-existing server, a session made by a marked client
#       without the block sees no marker (the failure the block fixes)
#   T2: the block's initial pane (pool-1) sees the set knobs
#   T3: a later split pane sees them too
#   T4: a stale knob from a prior session is evicted when the conf unsets it
#   T5: with no server running, the server the block starts carries the knobs
#
# A private tmux server (-S socket, TMUX unset) carries the whole run, so a
# run from inside a fleet pane never writes the live server's env.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -f "$FLEET_UP" ]] || { echo "SKIP: fleet-up not found at $FLEET_UP" >&2; exit 3; }
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
unset TMUX

BLOCK="$TMPROOT/pane-launch-env-block.sh"
{
    sed -n '/^seed_tmux_pane_env() {$/,/^}$/p' "$FLEET_UP"
    sed -n '/^if tmux list-sessions >\/dev\/null 2>&1; then$/,/^seed_tmux_pane_env$/p' "$FLEET_UP"
} > "$BLOCK"
if grep -q '^seed_tmux_pane_env() {$' "$BLOCK" && grep -q 'tmux new-session -d -s "$SESSION"' "$BLOCK"; then
    ok "T0: the helper and the new-session block are extracted"
else
    bad "T0: the helper and the new-session block are extracted"
    summarize "fleet-up pane launch env"
    exit 1
fi

ptmux() { tmux -S "$TMPROOT/sock" "$@"; }

# probe_cmd <tag> — a pane command that records the knobs its shell inherited.
# POSIX-only: tmux runs it under the host's default-shell, which may be zsh.
probe_cmd() {
    local out="$TMPROOT/$1.out"
    printf 'printf "FLEET_UNATTENDED=%%s FLEET_RUN_DEFAULT_TIMEOUT=%%s FLEET_WINDOW_MODE=%%s\\n" "${FLEET_UNATTENDED:-unset}" "${FLEET_RUN_DEFAULT_TIMEOUT:-unset}" "${FLEET_WINDOW_MODE:-unset}" > %q.tmp; mv %q.tmp %q; exec sleep 60' \
        "$out" "$out" "$out"
}

# read_probe <tag> — waits for the pane's record and prints it on one line.
read_probe() {
    local out="$TMPROOT/$1.out" i
    for i in $(seq 1 50); do
        [[ -s "$out" ]] && break
        sleep 0.1
    done
    tr '\n' ' ' < "$out" 2>/dev/null
}

# A server started with none of the knobs but a stale FLEET_WINDOW_MODE, as a
# prior fleet-up session whose conf set it would leave behind.
env -u FLEET_UNATTENDED -u FLEET_RUN_DEFAULT_TIMEOUT -u FLEET_WINDOW_MODE \
    tmux -S "$TMPROOT/sock" new-session -d -s keeper "exec sleep 60"
ptmux set-environment -g FLEET_WINDOW_MODE hidden

echo "T1: control — a marked client's export alone does not reach the pane"
FLEET_UNATTENDED=1 ptmux new-session -d -s control "$(probe_cmd control)"
assert_contains "$(read_probe control)" "FLEET_UNATTENDED=unset" "pre-existing server drops the client's marker"

mkdir -p "$TMPROOT/engine/.claude/worktrees/pool-1"

# run_block <socket> <pool-1 probe tag> — runs the lifted block as fleet-up
# would with the conf setting the marker and a timeout but no window mode.
run_block() {
    FLEET_UNATTENDED=1 FLEET_RUN_DEFAULT_TIMEOUT=90 SOCK="$1" \
        IR_FLEET_WORKERS=4 FLEET_FABLE_FALLBACK=opus _gh_app_token= \
        SESSION=fleet-test ENGINE="$TMPROOT/engine" TRANSIENT_PANE_CMD="$(probe_cmd "$2")" \
        "$BASH" -c '
            set -euo pipefail
            unset FLEET_WINDOW_MODE
            tmux() { command tmux -S "$SOCK" "$@"; }
            source "$0"
        ' "$BLOCK"
}

run_block "$TMPROOT/sock" pool1
assert_eq "$?" "0" "the block runs clean under set -euo pipefail"

echo "T2: the initial pool-1 pane sees the set knobs"
pool1=$(read_probe pool1)
assert_contains "$pool1" "FLEET_UNATTENDED=1" "pool-1 carries the marker"
assert_contains "$pool1" "FLEET_RUN_DEFAULT_TIMEOUT=90" "pool-1 carries the conf timeout"

echo "T3: a later split pane sees them too"
ptmux split-window -d -t fleet-test "$(probe_cmd split)"
split=$(read_probe split)
assert_contains "$split" "FLEET_UNATTENDED=1" "split pane carries the marker"
assert_contains "$split" "FLEET_RUN_DEFAULT_TIMEOUT=90" "split pane carries the conf timeout"

echo "T4: an unset conf knob evicts the prior session's value"
assert_contains "$pool1" "FLEET_WINDOW_MODE=unset" "pool-1 drops the stale window mode"
assert_contains "$split" "FLEET_WINDOW_MODE=unset" "split pane drops the stale window mode"

echo "T5: with no server yet, the server the block starts carries the knobs"
run_block "$TMPROOT/fresh-sock" fresh
assert_eq "$?" "0" "the block runs clean with no server running"
assert_contains "$(read_probe fresh)" "FLEET_UNATTENDED=1 FLEET_RUN_DEFAULT_TIMEOUT=90 FLEET_WINDOW_MODE=unset" "fresh-server pool-1 sees exactly the conf's knobs"
tmux -S "$TMPROOT/fresh-sock" kill-server 2>/dev/null

summarize "fleet-up pane launch env"
