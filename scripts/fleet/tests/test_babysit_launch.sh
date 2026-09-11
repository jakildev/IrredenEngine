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
# T5/T6 cover the dead-session fallback (#3004): a saved session-id whose
# transcript no longer resolves (pruned after ~30 days idle, or otherwise
# unusable) must fall back to a fresh session instead of crash-looping
# forever on a pointer that can never succeed.
#
# Covers:
#   - resume launch carries --resume <id> and NO prompt argument
#   - resume launch does NOT carry the old nudge text
#   - first-ever launch carries --session-id and the /role-<role> command
#   - first-ever launch persists the session-id file
#   - architect launches export the fleet-session-track hook gate vars
#     (FLEET_SESSION_FILE/ROLE/MODE) so /clear repoints the sidecar
#   - T5: a session-id with no transcript on disk falls back to a fresh
#     session (no --resume), deletes the stale sidecar, and logs why
#   - T6: N consecutive immediate exit-1 resumes (transcript present, but
#     every resume still exits 1) also condemns the pointer, end-to-end
#     through the real relaunch loop

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
BABYSIT="$SCRIPT_DIR/fleet-babysit"

if [[ ! -x "$BABYSIT" ]]; then
    echo "test setup: fleet-babysit not found at $BABYSIT" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
BABYSIT_BG_PID=""

cleanup() {
    if [[ -n "$BABYSIT_BG_PID" ]]; then
        kill "$BABYSIT_BG_PID" 2>/dev/null || true
        wait "$BABYSIT_BG_PID" 2>/dev/null || true
    fi
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
# Stub claude/tmux/gh so nothing real launches even if a path slips past the
# inspection hook (the hook exits first; this is belt-and-suspenders).
mkdir -p "$TMPROOT/bin"
for c in claude tmux gh; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$TMPROOT/bin/$c"
    chmod +x "$TMPROOT/bin/$c"
done

# Every architect launch resolves its transcript directory from `pwd` —
# fleet-babysit's project_transcripts_dir() maps every non-alnum byte to
# '-' (Claude Code's own project-dir munging; runs are not collapsed). Kept
# in sync with that function by hand since a bash script can't be imported
# function-only without executing it.
slug_for() {
    printf '%s' "$1" | sed 's/[^A-Za-z0-9]/-/g'
}

PROJECT_CWD="$TMPROOT/architect-cwd"
mkdir -p "$PROJECT_CWD"
PROJECT_SLUG=$(slug_for "$PROJECT_CWD")

# Pre-create a transcript file so a resume launch's session-id "resolves"
# (the T1/T3/T4 fixtures all want this — they're testing the resume-argv
# shape, not the dead-pointer fallback).
make_transcript() {
    local home="$1" session_id="$2"
    mkdir -p "$home/.claude/projects/$PROJECT_SLUG"
    touch "$home/.claude/projects/$PROJECT_SLUG/$session_id.jsonl"
}

# Run launch resolution for <model> <role> against an isolated HOME (so the
# session-id file at $HOME/.fleet/sessions/<role>.session-id is sandboxed),
# from PROJECT_CWD (so the transcript-dir slug is deterministic). Prints the
# resolved `claude ...` argv line only (drops babysit's log lines).
launch_for() {
    local home="$1" model="$2" role="$3"
    ( cd "$PROJECT_CWD" && env HOME="$home" PATH="$TMPROOT/bin:$PATH" FLEET_BABYSIT_PRINT_LAUNCH=1 \
        "$BABYSIT" "$model" "$role" live 2>/dev/null | grep '^claude ' )
}

# --- T1: resume launch has --resume <id> and NO prompt ----------------------
echo "T1: architect resume launches --resume with no prompt"
H1="$TMPROOT/h1"; mkdir -p "$H1/.fleet/sessions"
SID="11111111-2222-3333-4444-555555555555"
echo "$SID" > "$H1/.fleet/sessions/opus-architect.session-id"
make_transcript "$H1" "$SID"
out=$(launch_for "$H1" 'claude-opus-4-8[1m]' opus-architect)
assert_eq "$out" "claude --model claude-opus-4-8[1m] --effort xhigh --resume $SID" \
    "resume argv is exactly --model/--effort/--resume <id>, no trailing prompt"
assert_absent "$out" "last working on" "resume carries no 'last working on' nudge"
assert_absent "$out" "resume our previous" "resume carries no 'resume our previous' nudge"

# --- T2: first-ever launch bootstraps via the role command ------------------
echo "T2: first-ever architect launch uses --session-id + role command"
H2="$TMPROOT/h2"; mkdir -p "$H2"
out=$(launch_for "$H2" 'claude-opus-4-8[1m]' opus-architect)
assert_contains "$out" "--session-id " "first launch passes --session-id"
assert_contains "$out" "/role-opus-architect live" "first launch fires the role slash command"
# the session-id file is now persisted for the next fleet-up to resume
[[ -f "$H2/.fleet/sessions/opus-architect.session-id" ]] \
    && ok "first launch persisted the session-id file" \
    || bad "session-id file not written on first launch"

# --- T3: game-architect resume path resolves the same way -------------------
echo "T3: game-architect resume also drops the prompt"
H3="$TMPROOT/h3"; mkdir -p "$H3/.fleet/sessions"
GSID="99999999-8888-7777-6666-555555555555"
echo "$GSID" > "$H3/.fleet/sessions/game-architect.session-id"
make_transcript "$H3" "$GSID"
out=$(launch_for "$H3" 'claude-opus-4-8[1m]' game-architect)
assert_eq "$out" "claude --model claude-opus-4-8[1m] --effort xhigh --resume $GSID" \
    "game-architect resume argv carries no prompt"

# --- T4: architect launch exports the session-track hook gate ----------------
# The fleet-session-track SessionStart hook only acts in sessions whose env
# carries FLEET_SESSION_FILE — that's how a /clear repoints the sidecar so
# the next fleet-up resumes the post-/clear session. Assert the architect
# launch exports the gate (surfaced via the PRINT_LAUNCH inspection line).
echo "T4: architect launch exports fleet-session-track gate vars"
track=$(cd "$PROJECT_CWD" && env HOME="$H1" PATH="$TMPROOT/bin:$PATH" FLEET_BABYSIT_PRINT_LAUNCH=1 \
    "$BABYSIT" 'claude-opus-4-8[1m]' opus-architect live 2>/dev/null \
    | grep '^session-track: ')
assert_eq "$track" \
    "session-track: file=$H1/.fleet/sessions/opus-architect.session-id role=opus-architect mode=live" \
    "architect exports sidecar path, role, and mode for the hook"

# --- T5: dead session-id (no transcript) falls back to a fresh session ------
# A saved session-id whose transcript was pruned still resolves
# `-f "$SESSION_FILE"` but has nothing under ~/.claude/projects/ — babysit
# must detect that BEFORE building --resume, not after crash-looping on it.
echo "T5: dead session-id (missing transcript) falls back to fresh session"
H5="$TMPROOT/h5"; mkdir -p "$H5/.fleet/sessions"
DEAD_SID="deaddead-0000-0000-0000-deaddeaddead"
echo "$DEAD_SID" > "$H5/.fleet/sessions/opus-architect.session-id"
# Deliberately no make_transcript call — the transcript directory tree
# under $H5/.claude/projects/ doesn't exist at all, let alone the file.
out=$(launch_for "$H5" 'claude-opus-4-8[1m]' opus-architect)
assert_absent "$out" "--resume" "dead pointer does not launch with --resume"
assert_contains "$out" "--session-id " "dead pointer falls back to a fresh --session-id launch"
assert_contains "$out" "/role-opus-architect live" "fresh fallback still fires the role slash command"
new_sid=$(cat "$H5/.fleet/sessions/opus-architect.session-id" 2>/dev/null || echo "")
[[ -n "$new_sid" && "$new_sid" != "$DEAD_SID" ]] \
    && ok "stale sidecar replaced with a new session-id" \
    || bad "sidecar still holds the dead session-id ($new_sid)"
assert_contains "$(cat "$H5/.fleet/logs/opus-architect.log" 2>/dev/null || echo '')" \
    "stale architect session $DEAD_SID" \
    "logs the stale session-id it condemned"

# --- T6: N consecutive immediate exit-1 resumes condemns the pointer too ----
# Fallback signal for whatever the filesystem check misses: the transcript
# exists (so T5's check passes every time) but every --resume launch still
# exits 1 immediately — without the streak signal babysit reads each exit as
# an ordinary crash and retries the same dead pointer forever.
# Exercises the real relaunch loop, not just the inspection hook — the streak
# is loop-local state that FLEET_BABYSIT_PRINT_LAUNCH's early-exit never
# reaches.
echo "T6: consecutive immediate exit-1 resumes condemns the pointer"
H6="$TMPROOT/h6"; mkdir -p "$H6/.fleet/sessions"
SID6="66666666-6666-6666-6666-666666666666"
echo "$SID6" > "$H6/.fleet/sessions/opus-architect.session-id"
make_transcript "$H6" "$SID6"

T6_BIN="$TMPROOT/t6-bin"; mkdir -p "$T6_BIN"
T6_CALLS="$TMPROOT/t6-claude-calls.log"
: > "$T6_CALLS"
# Records every invocation, then simulates a dead-but-transcript-present
# pointer: exit 1 whenever --resume is in argv, exit 0 (a "successful" fresh
# boot) otherwise — so the loop naturally terminates each cycle instead of
# crash-looping through the whole suite run.
cat > "$T6_BIN/claude" <<'STUB'
#!/usr/bin/env bash
echo "$@" >> "$CLAUDE_CALLS_LOG"
for a in "$@"; do
    [[ "$a" == "--resume" ]] && exit 1
done
exit 0
STUB
chmod +x "$T6_BIN/claude"

(
    cd "$PROJECT_CWD" && env HOME="$H6" PATH="$T6_BIN:$PATH" \
        CLAUDE_CALLS_LOG="$T6_CALLS" \
        FLEET_CRASH_DELAY=1 FLEET_CLEAN_DELAY=1 FLEET_RESUME_FAIL_THRESHOLD=2 \
        FLEET_MAX_ATTEMPTS=6 \
        "$BABYSIT" 'claude-opus-4-8[1m]' opus-architect live \
        >"$TMPROOT/t6-babysit.log" 2>&1
) &
BABYSIT_BG_PID=$!

condemn_line=""
for _ in $(seq 1 50); do
    if [[ -f "$H6/.fleet/logs/opus-architect.log" ]]; then
        condemn_line=$(grep "stale architect session $SID6" "$H6/.fleet/logs/opus-architect.log" || true)
        [[ -n "$condemn_line" ]] && break
    fi
    sleep 0.3
done
assert_contains "$condemn_line" "2 consecutive immediate exit-1 resumes" \
    "streak reaches FLEET_RESUME_FAIL_THRESHOLD and condemns the pointer"

new_sid6=""
for _ in $(seq 1 30); do
    if [[ -f "$H6/.fleet/sessions/opus-architect.session-id" ]]; then
        new_sid6=$(cat "$H6/.fleet/sessions/opus-architect.session-id")
        [[ -n "$new_sid6" && "$new_sid6" != "$SID6" ]] && break
    fi
    sleep 0.3
done
[[ -n "$new_sid6" && "$new_sid6" != "$SID6" ]] \
    && ok "post-streak relaunch wrote a fresh session-id" \
    || bad "sidecar did not get replaced after the streak condemned it (saw: $new_sid6)"

kill "$BABYSIT_BG_PID" 2>/dev/null || true
wait "$BABYSIT_BG_PID" 2>/dev/null || true
BABYSIT_BG_PID=""

resume_calls=$(grep -c -- '--resume' "$T6_CALLS" 2>/dev/null || echo 0)
[[ "$resume_calls" -eq 2 ]] \
    && ok "exactly threshold-many --resume attempts were made before falling back" \
    || bad "expected exactly 2 --resume attempts, saw $resume_calls (log: $T6_CALLS)"

summarize
