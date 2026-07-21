#!/usr/bin/env bash
# Tests for fleet-guard-worktree-edit — the PreToolUse Edit|Write|MultiEdit hook
# that refuses edits escaping the agent's worktree.
#
# The guard reads PreToolUse JSON on stdin and (on a policy deny) emits a
# hookSpecificOutput deny form on stdout; an allow is exit 0 with no deny JSON,
# and any parse failure fails OPEN. This test drives the script directly with
# synthetic hook JSON (built with jq so paths are escaped safely) across both
# modes — hermetic: no live GitHub, no live ~/.fleet, and the paths are pure
# string stand-ins the guard never touches on disk.
#
# Covers:
#   ASSIGNMENT mode (FLEET_ASSIGNED_WORKTREE set) — identity from the assignment,
#     never cwd: a main-clone absolute write is denied even when the hook-input
#     cwd IS the main clone; a sibling agent's worktree is denied; the same-
#     basename game worktree is allowed; ~/.fleet, /tmp, /private/tmp, and the
#     auto-memory dir are allowed; a relative path is resolved against the drifted
#     cwd then tested; the sibling-prefix (worker-3-foo vs worker-3) trap; a
#     '..'-bearing target (relative or absolute) is lexically collapsed before the
#     containment tests, so it can't pass the literal match while resolving out.
#   LEGACY mode (env unset) — the original cwd-derived behavior, unchanged.
#   Fail-open — malformed JSON and an empty file field never wedge (exit 0, allow).

set -uo pipefail

# Native-Windows (Git Bash / MSYS2): stop the shell from path-converting the
# synthetic POSIX stand-ins (`/private/tmp/...` -> `C:/Program Files/Git/...`)
# as they pass through native jq.exe — the conversion corrupts the pure-string
# fixtures before the guard ever sees them. Inert on macOS/Linux.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

GUARD="$SCRIPT_DIR/fleet-guard-worktree-edit"
if [[ ! -x "$GUARD" ]]; then
    echo "test setup: guard not executable at $GUARD" >&2
    exit 1
fi

# Synthetic layout (never created on disk — the guard is pure string logic).
ASSIGNED="/eng/.claude/worktrees/worker-3"           # this pane's engine worktree
GAME_WT="/game/.claude/worktrees/worker-3"           # same pane, game clone
SIBLING="/eng/.claude/worktrees/worker-1"            # another agent
PREFIX_TRAP="/eng/.claude/worktrees/worker-3-foo"    # sibling-prefix decoy
MAIN="/eng"                                          # shared main clone

# verdict <assigned-env> <cwd> <file> → prints DENY or allow.
verdict() {
    local input out
    input=$(jq -n --arg cwd "$2" --arg f "$3" '{cwd:$cwd, tool_input:{file_path:$f}}')
    out=$(printf '%s' "$input" | FLEET_ASSIGNED_WORKTREE="$1" bash "$GUARD" 2>/dev/null || true)
    if printf '%s' "$out" | grep -q '"permissionDecision": *"deny"'; then
        echo DENY
    else
        echo allow
    fi
}

echo "ASSIGNMENT mode (FLEET_ASSIGNED_WORKTREE=$ASSIGNED):"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "$MAIN/engine/foo.cpp")" DENY \
    "abs main-clone write denied even when cwd IS the main clone (drifted-cwd hole)"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$ASSIGNED/scripts/x.sh")" allow \
    "abs write inside the assigned worktree allowed"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$SIBLING/x.sh")" DENY \
    "sibling agent's worktree denied"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$GAME_WT/src/x.cpp")" allow \
    "same-basename game worktree allowed (one rule covers the engine+game pair)"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$PREFIX_TRAP/x.sh")" DENY \
    "sibling-prefix worker-3-foo cannot prefix-match worker-3"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "$HOME/.fleet/handoff/worker-3.md")" allow \
    "\$HOME/.fleet write allowed"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "/tmp/scratch")" allow \
    "/tmp write allowed"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "/private/tmp/scratch")" allow \
    "/private/tmp write allowed (macOS canonical form)"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "$HOME/.claude/projects/xyz/memory/m.md")" allow \
    "auto-memory dir write allowed"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "engine/foo.cpp")" DENY \
    "relative path resolved against a drifted main-clone cwd is denied"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "scripts/x.sh")" allow \
    "relative path resolved against an in-worktree cwd is allowed"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "../../../engine/foo.cpp")" DENY \
    "relative ..-escape from the assigned worktree cwd denied (resolves to main clone)"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$ASSIGNED/../../../engine/foo.cpp")" DENY \
    "abs target embedding the assigned worktree + ..-escape denied"
assert_eq "$(verdict "$ASSIGNED" "$ASSIGNED" "$ASSIGNED/scripts/../src/x.cpp")" allow \
    "..-bearing target that stays inside the worktree allowed (no over-block)"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "/tmp/../eng/engine/foo.cpp")" DENY \
    "/tmp/..-escape cannot ride the scratch-dir allowlist"

echo "ASSIGNMENT mode (native-Windows path spellings):"
# The file tools on a Windows host hand the hook drive-letter absolute paths
# (backslashed `C:\...` or forward `C:/...`); un-normalized, the guard's POSIX
# `/*` test reads them as RELATIVE and denies EVERY edit — including
# correctly-routed in-worktree ones (observed on the first native-Windows
# fleet boot). win_norm maps them to the MSYS `/c/...` form. Pure-string
# fixtures — these cases run on any host.
WIN_ASSIGNED='C:/eng/.claude/worktrees/worker-3'
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng\.claude\worktrees\worker-3' 'C:\eng\.claude\worktrees\worker-3\scripts\x.sh')" allow \
    "backslashed drive-letter target inside the assigned worktree allowed"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:/eng/.claude/worktrees/worker-3' 'C:/eng/.claude/worktrees/worker-3/scripts/x.sh')" allow \
    "forward-slash drive-letter target inside the assigned worktree allowed"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng' 'C:\eng\engine\foo.cpp')" DENY \
    "backslashed main-clone target denied"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng' 'C:\eng\.claude\worktrees\worker-1\x.sh')" DENY \
    "backslashed sibling worktree denied"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng' 'C:\msys64\tmp\claude\sess\scratch.txt')" allow \
    "native-Windows harness scratchpad (C:\msys64\tmp) allowed"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng\.claude\worktrees\worker-3' 'scripts\fleet\x.sh')" allow \
    "backslashed relative target resolved against an in-worktree cwd allowed"
assert_eq "$(verdict "$WIN_ASSIGNED" 'C:\eng' 'engine\foo.cpp')" DENY \
    "backslashed relative target from a drifted main-clone cwd denied"

echo "LEGACY mode (env unset):"
assert_eq "$(verdict "" "$ASSIGNED" "$MAIN/engine/foo.cpp")" DENY \
    "cwd in worktree + abs path to main clone denied"
assert_eq "$(verdict "" "$ASSIGNED" "$ASSIGNED/scripts/x.sh")" allow \
    "cwd in worktree + abs path in worktree allowed"
assert_eq "$(verdict "" "$MAIN" "$MAIN/engine/foo.cpp")" allow \
    "cwd in main clone → unrestricted (human / non-fleet session)"
assert_eq "$(verdict "" "$ASSIGNED" "engine/foo.cpp")" allow \
    "cwd in worktree + relative path allowed (resolves in-worktree)"

echo "fail-open:"
malformed_ec() {
    printf '%s' '{not json' | FLEET_ASSIGNED_WORKTREE="$ASSIGNED" bash "$GUARD" >/dev/null 2>&1
    echo $?
}
assert_eq "$(malformed_ec)" "0" "malformed JSON fails open (exit 0)"
assert_eq "$(verdict "$ASSIGNED" "$MAIN" "")" allow \
    "empty file field allowed (nothing to constrain)"

summarize "fleet-guard-worktree-edit tests"
