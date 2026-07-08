#!/usr/bin/env bash
# Tests for fleet-babysit's architect launch command (launch_architect_first),
# exercised through the FLEET_BABYSIT_PRINT_LAUNCH=1 inspection hook — it prints
# the resolved `claude ...` argv and exits before spawning claude.
#
# The invariant under test: an architect resume (a persisted session-id exists,
# the fleet-down -> fleet-up case) launches `claude --resume <id>` with NO
# trailing prompt, so the conversation reloads without generating a model turn.
# The old behaviour fired a "tell me what you were last working on" prompt here,
# costing one full model turn per architect on every fleet-up and feeding the
# cold-start request burst. First-ever launch (no session-id yet) still
# bootstraps via the role slash command.
#
# Covers:
#   - resume launch carries --resume <id> and NO prompt argument
#   - resume launch does NOT carry the old nudge text
#   - first-ever launch carries --session-id and the /role-<role> command
#   - first-ever launch persists the session-id file
#   - architect launches export the fleet-session-track hook gate vars
#     (FLEET_SESSION_FILE/ROLE/MODE) so /clear repoints the sidecar

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BABYSIT="$SCRIPT_DIR/fleet-babysit"

if [[ ! -x "$BABYSIT" ]]; then
    echo "test setup: fleet-babysit not found at $BABYSIT" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        '$needle' not found in: $haystack"
    fi
}

assert_not_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        '$needle' unexpectedly present in: $haystack"
    fi
}

TMPROOT=$(mktemp -d)
# Stub claude/tmux/gh so nothing real launches even if a path slips past the
# inspection hook (the hook exits first; this is belt-and-suspenders).
mkdir -p "$TMPROOT/bin"
for c in claude tmux gh; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$TMPROOT/bin/$c"
    chmod +x "$TMPROOT/bin/$c"
done

# Run launch resolution for <model> <role> against an isolated HOME (so the
# session-id file at $HOME/.fleet/sessions/<role>.session-id is sandboxed).
# Prints the resolved `claude ...` argv line only (drops babysit's log lines).
launch_for() {
    local home="$1" model="$2" role="$3"
    env HOME="$home" PATH="$TMPROOT/bin:$PATH" FLEET_BABYSIT_PRINT_LAUNCH=1 \
        "$BABYSIT" "$model" "$role" live 2>/dev/null | grep '^claude '
}

# --- T1: resume launch has --resume <id> and NO prompt ----------------------
echo "T1: architect resume launches --resume with no prompt"
H1="$TMPROOT/h1"; mkdir -p "$H1/.fleet/sessions"
SID="11111111-2222-3333-4444-555555555555"
echo "$SID" > "$H1/.fleet/sessions/opus-architect.session-id"
out=$(launch_for "$H1" 'claude-opus-4-8[1m]' opus-architect)
assert_eq "$out" "claude --model claude-opus-4-8[1m] --effort xhigh --resume $SID" \
    "resume argv is exactly --model/--effort/--resume <id>, no trailing prompt"
assert_not_contains "$out" "last working on" "resume carries no 'last working on' nudge"
assert_not_contains "$out" "resume our previous" "resume carries no 'resume our previous' nudge"

# --- T2: first-ever launch bootstraps via the role command ------------------
echo "T2: first-ever architect launch uses --session-id + role command"
H2="$TMPROOT/h2"; mkdir -p "$H2"
out=$(launch_for "$H2" 'claude-opus-4-8[1m]' opus-architect)
assert_contains "$out" "--session-id " "first launch passes --session-id"
assert_contains "$out" "/role-opus-architect live" "first launch fires the role slash command"
# the session-id file is now persisted for the next fleet-up to resume
[[ -f "$H2/.fleet/sessions/opus-architect.session-id" ]] \
    && { PASS=$((PASS + 1)); echo "  ok: first launch persisted the session-id file"; } \
    || { FAIL=$((FAIL + 1)); echo "  FAIL: session-id file not written on first launch"; }

# --- T3: game-architect resume path resolves the same way -------------------
echo "T3: game-architect resume also drops the prompt"
H3="$TMPROOT/h3"; mkdir -p "$H3/.fleet/sessions"
GSID="99999999-8888-7777-6666-555555555555"
echo "$GSID" > "$H3/.fleet/sessions/game-architect.session-id"
out=$(launch_for "$H3" 'claude-opus-4-8[1m]' game-architect)
assert_eq "$out" "claude --model claude-opus-4-8[1m] --effort xhigh --resume $GSID" \
    "game-architect resume argv carries no prompt"

# --- T4: architect launch exports the session-track hook gate ----------------
# The fleet-session-track SessionStart hook only acts in sessions whose env
# carries FLEET_SESSION_FILE — that's how a /clear repoints the sidecar so
# the next fleet-up resumes the post-/clear session. Assert the architect
# launch exports the gate (surfaced via the PRINT_LAUNCH inspection line).
echo "T4: architect launch exports fleet-session-track gate vars"
track=$(env HOME="$H1" PATH="$TMPROOT/bin:$PATH" FLEET_BABYSIT_PRINT_LAUNCH=1 \
    "$BABYSIT" 'claude-opus-4-8[1m]' opus-architect live 2>/dev/null \
    | grep '^session-track: ')
assert_eq "$track" \
    "session-track: file=$H1/.fleet/sessions/opus-architect.session-id role=opus-architect mode=live" \
    "architect exports sidecar path, role, and mode for the hook"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
