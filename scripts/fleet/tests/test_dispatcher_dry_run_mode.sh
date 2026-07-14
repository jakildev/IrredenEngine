#!/usr/bin/env bash
# Tests for fleet-dispatcher's boot-mode (dry-run) gate — issue #2022.
#
# The dispatcher reads the boot mode fleet-up writes to
# $FLEET_STATE_DIR/dispatch-mode each tick. In live / review-only it is inert:
# dispatch proceeds as before and the dispatched role command carries that mode.
# In dry-run it threads `dry-run` into the role command (so a dispatched worker
# runs the standby path, not a live task-pickup iteration) — the part exercised
# here through two inspection subcommands that exit before the daemon loop:
#   --print-mode                          → the resolved boot mode
#   --print-dispatch-command <role> <key> → the command sent into a pane
#
# The "dispatch once then idle" loop gating is verified by inspection (it needs
# the live daemon loop + mocked panes); these tests pin the mode-resolution and
# mode-threading mechanism the gating rides on.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
# Point at a guaranteed non-existent tmux session so the dispatcher never
# touches a real fleet running on the dev machine.
export FLEET_SESSION="fleet-test-$$"
mkdir -p "$FLEET_STATE_DIR"
SENTINEL="$FLEET_STATE_DIR/dispatch-mode"

# --- Test 1: --print-mode resolves the sentinel ----------------------------
echo "T1: --print-mode resolves the dispatch-mode sentinel"
rm -f "$SENTINEL"
assert_eq "$("$DISPATCHER" --print-mode)" "live" "absent sentinel → live (backward-compatible)"
printf 'dry-run\n'     > "$SENTINEL"; assert_eq "$("$DISPATCHER" --print-mode)" "dry-run"     "dry-run sentinel → dry-run"
printf 'live\n'        > "$SENTINEL"; assert_eq "$("$DISPATCHER" --print-mode)" "live"        "live sentinel → live"
printf 'review-only\n' > "$SENTINEL"; assert_eq "$("$DISPATCHER" --print-mode)" "review-only" "review-only sentinel → review-only"
printf 'bogus\n'       > "$SENTINEL"; assert_eq "$("$DISPATCHER" --print-mode)" "live"        "unrecognized value → live"

# --- Test 2: dispatch command threads the mode into the role command -------
echo "T2: --print-dispatch-command carries the boot mode as the trailing arg"
printf 'live\n' > "$SENTINEL"
cmd_live=$("$DISPATCHER" --print-dispatch-command worker pane-3)
assert_contains "$cmd_live" "fleet-dispatch-wrap" "live: dispatch goes through fleet-dispatch-wrap"
assert_eq "${cmd_live##* }" "live" "live: trailing mode arg is live (prior hard-coded behavior)"

printf 'dry-run\n' > "$SENTINEL"
cmd_dry=$("$DISPATCHER" --print-dispatch-command worker pane-3)
assert_eq "${cmd_dry##* }" "dry-run" "dry-run: trailing mode arg is dry-run"

rm -f "$SENTINEL"
cmd_absent=$("$DISPATCHER" --print-dispatch-command merger pane-1)
assert_eq "${cmd_absent##* }" "live" "absent sentinel: trailing mode arg defaults to live"

# --- Test 3: usage error on missing args -----------------------------------
echo "T3: --print-dispatch-command with missing pane_key → usage error"
if "$DISPATCHER" --print-dispatch-command worker >/dev/null 2>&1; then
    bad "missing pane_key should exit non-zero"
else
    ok "missing pane_key exits non-zero"
fi

# --- Test 4: fleet-dispatch-wrap runs /role-<role> <mode> ------------------
# Stub `claude` on PATH so the wrap's invocation is observable without a real
# model call. The stub echoes its final positional arg (the slash command) to a
# capture file. fleet-claude-stream is also stubbed (pass-through) so the pipe
# resolves. The wrap validates an unknown mode down to live.
echo "T4: fleet-dispatch-wrap invokes the role slash command with the passed mode"
if [[ -x "$WRAP" ]]; then
    BIN="$TMPROOT/bin"
    mkdir -p "$BIN"
    CAP="$TMPROOT/slash.txt"
    cat >"$BIN/claude" <<EOF
#!/usr/bin/env bash
# Last positional arg is the "/role-<role> <mode>" slash command.
for a in "\$@"; do last="\$a"; done
printf '%s\n' "\$last" > "$CAP"
EOF
    cat >"$BIN/fleet-claude-stream" <<'EOF'
#!/usr/bin/env bash
cat >/dev/null
EOF
    chmod +x "$BIN/claude" "$BIN/fleet-claude-stream"

    # Run from a hermetic cwd so the worker auto-retrigger (basename "$PWD" →
    # fleet-claim reservation-of + trigger touch) can't reach the real fleet.
    ( cd "$TMPROOT" && PATH="$BIN:$PATH" "$WRAP" pane-9 sonnet high worker "" dry-run ) >/dev/null 2>&1 || true
    assert_eq "$(cat "$CAP" 2>/dev/null)" "/role-worker dry-run" "wrap passes /role-worker dry-run"

    : > "$CAP"
    ( cd "$TMPROOT" && PATH="$BIN:$PATH" "$WRAP" pane-9 sonnet high reviewer "" ) >/dev/null 2>&1 || true
    assert_eq "$(cat "$CAP" 2>/dev/null)" "/role-reviewer live" "wrap defaults missing mode (5 args) to live"

    : > "$CAP"
    ( cd "$TMPROOT" && PATH="$BIN:$PATH" "$WRAP" pane-9 sonnet high merger "" bogus ) >/dev/null 2>&1 || true
    assert_eq "$(cat "$CAP" 2>/dev/null)" "/role-merger live" "wrap validates an unknown mode down to live"
else
    echo "  skip: fleet-dispatch-wrap not found at $WRAP"
fi

summarize
