#!/usr/bin/env bash
# Tests for fleet-edit's #2402 worktree-scope guard: under an assignment
# (FLEET_ASSIGNED_WORKTREE set) fleet-edit — an Edit-tool-equivalent CLI that
# bypasses the PreToolUse hook — must refuse a target outside the same allowlist
# the hook enforces (own worktree, $HOME/.fleet, /tmp, /private/tmp, auto-memory),
# and must be unchanged when the env is unset.
#
# Hermetic: builds a synthetic worktree layout and drives the real fleet-edit.
# The deny-case fixture is rooted under $HOME (NOT mktemp's default /tmp): /tmp is
# an unconditional allowlist entry, so a "main clone" simulated there would be
# allowed and couldn't exercise a refusal. The one temp root is trap-removed.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

FLEET_EDIT="$SCRIPT_DIR/fleet-edit"
[[ -x "$FLEET_EDIT" ]] || { echo "test setup: fleet-edit not executable at $FLEET_EDIT" >&2; exit 1; }

# Deny-case root under $HOME so a non-worktree path isn't swept up by the
# /tmp allowlist. Name avoids the ~/.fleet allowlist prefix.
ROOT=$(mktemp -d "$HOME/.ir-fleet-edit-scope-test.XXXXXX")
# Explicitly under /tmp (resolves to /private/tmp on macOS) — NOT mktemp's
# default $TMPDIR, which on macOS is /var/folders and (correctly) not on the
# allowlist. This exercises the /tmp allow rule.
TMP_ALLOWED=$(mktemp -d /tmp/fleet-edit-scope.XXXXXX)
cleanup() {
    [[ -n "${ROOT:-}" && "$ROOT" == "$HOME"/.ir-fleet-edit-scope-test.* ]] && rm -rf "$ROOT"
    [[ -n "${TMP_ALLOWED:-}" ]] && rm -rf "$TMP_ALLOWED"
}
trap cleanup EXIT

ASSIGNED="$ROOT/eng/.claude/worktrees/worker-3"
mkdir -p "$ASSIGNED/scripts" "$ROOT/eng/engine"

OLD=$(mktemp -t fe-old.XXXXXX); NEW=$(mktemp -t fe-new.XXXXXX)
printf 'alpha\n'  > "$OLD"
printf 'omega\n'  > "$NEW"
trap 'cleanup; rm -f "$OLD" "$NEW"' EXIT

# run_edit <assigned-env> <target> → prints "EXIT:<code>" then the target's content
run_edit() {
    local ec
    FLEET_ASSIGNED_WORKTREE="$1" "$FLEET_EDIT" "$2" "$OLD" "$NEW" >/dev/null 2>&1
    ec=$?
    echo "$ec"
}

# --- assignment mode: in-worktree target allowed -----------------------------
echo "assignment mode: in-worktree target is edited"
IN="$ASSIGNED/scripts/f.txt"; printf 'alpha\n' > "$IN"
assert_eq "$(run_edit "$ASSIGNED" "$IN")" "0" "in-worktree edit succeeds (exit 0)"
assert_eq "$(cat "$IN")" "omega" "in-worktree file content replaced"

# --- assignment mode: main-clone target refused ------------------------------
echo "assignment mode: main-clone target refused, file untouched"
OUT="$ROOT/eng/engine/main.cpp"; printf 'alpha\n' > "$OUT"
assert_eq "$(run_edit "$ASSIGNED" "$OUT")" "1" "main-clone edit refused (exit 1)"
assert_eq "$(cat "$OUT")" "alpha" "refused file content unchanged"

# refusal message names the guard
refuse_msg=$(FLEET_ASSIGNED_WORKTREE="$ASSIGNED" "$FLEET_EDIT" "$OUT" "$OLD" "$NEW" 2>&1 || true)
assert_contains "$refuse_msg" "REFUSING to edit" "refusal prints a corrective message"

# --- assignment mode: /tmp target allowed ------------------------------------
echo "assignment mode: /tmp target allowed"
TMPF="$TMP_ALLOWED/f.txt"; printf 'alpha\n' > "$TMPF"
assert_eq "$(run_edit "$ASSIGNED" "$TMPF")" "0" "/tmp edit succeeds under assignment"
assert_eq "$(cat "$TMPF")" "omega" "/tmp file content replaced"

# --- env unset: legacy behavior unchanged (main-clone target allowed) --------
echo "env unset: main-clone target allowed (unchanged)"
OUT2="$ROOT/eng/engine/legacy.cpp"; printf 'alpha\n' > "$OUT2"
assert_eq "$(run_edit "" "$OUT2")" "0" "unset env → no scope restriction (exit 0)"
assert_eq "$(cat "$OUT2")" "omega" "legacy edit applied"

summarize "fleet-edit scope-guard tests"
