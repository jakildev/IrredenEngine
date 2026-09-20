#!/usr/bin/env bash
# Tests for fleet-babysit's campaign re-entry gate
# (retire_cross_fleet_campaign_session), exercised through the
# FLEET_BABYSIT_PRINT_LAUNCH=1 inspection hook.
#
# The invariant under test: a campaign pane resumes WITHIN a fleet session but
# never ACROSS one. Resume carries no prompt, so a resumed campaign never
# re-reads the tree — after a restart it would continue against a picture of
# the world that predates whatever merged, superseded or collided while it was
# down. Its durable state is the campaign doc plus the PR list, and only the
# fresh path (the /role-campaign slash command) re-reads those.
#
# The boundary is the babysit process itself: fleet-up spawns one per pane, so
# a sidecar older than this process belongs to a previous fleet. The suite pins
# that clock through FLEET_BABYSIT_STARTED_AT rather than sleeping, so both arms
# of the comparison are reachable without a wall-clock wait.
#
# Architects are the control: their conversation IS their state and a human
# re-orients them on sight, so a stale architect sidecar must still resume. A
# gate that caught architects too would silently reset every design
# conversation on every fleet-up.
#
# Covers:
#   - T1: campaign + sidecar older than this babysit -> fresh /role-campaign
#   - T2: the retired sidecar is renamed, not deleted (transcript stays
#         reachable by uuid) and the new session-id is a different one
#   - T3: campaign + sidecar newer than this babysit -> --resume, no prompt
#   - T4: architect + sidecar older than this babysit -> --resume (unchanged)
#   - T5: campaign with no sidecar at all -> fresh, and nothing is retired

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
BABYSIT="$SCRIPT_DIR/fleet-babysit"

if [[ ! -x "$BABYSIT" ]]; then
    echo "SKIP: fleet-babysit not found at $BABYSIT" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

# Normalized through cd/pwd: TMPDIR carries a trailing slash on macOS, and
# the doubled separator survives in the literal string while the subshell's
# own `pwd` collapses it — the two spellings then slug to different
# transcript directories and every resume arm reads as a dead pointer.
TMPROOT=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/babysit-campaign.XXXXXX")" && pwd)

mkdir -p "$TMPROOT/bin"
for c in claude tmux gh; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$TMPROOT/bin/$c"
    chmod +x "$TMPROOT/bin/$c"
done

# Mirrors fleet-babysit's project_transcripts_dir(): every non-alnum byte to
# '-', runs not collapsed. Kept in sync by hand — a bash script cannot be
# imported function-only without executing it.
slug_for() { printf '%s' "$1" | sed 's/[^A-Za-z0-9]/-/g'; }

PROJECT_CWD="$TMPROOT/campaign-cwd"
mkdir -p "$PROJECT_CWD"
PROJECT_SLUG=$(slug_for "$PROJECT_CWD")

make_transcript() {
    local home="$1" session_id="$2"
    mkdir -p "$home/.claude/projects/$PROJECT_SLUG"
    touch "$home/.claude/projects/$PROJECT_SLUG/$session_id.jsonl"
}

# started_at pins BABYSIT_STARTED_AT: "older" puts the babysit process after
# the sidecar's mtime (a previous fleet's sidecar), "newer" puts it before
# (this fleet wrote it).
launch_for() {
    local home="$1" role="$2" started_at="$3"
    ( cd "$PROJECT_CWD" && env HOME="$home" PATH="$TMPROOT/bin:$PATH" \
        FLEET_BABYSIT_STARTED_AT="$started_at" FLEET_BABYSIT_PRINT_LAUNCH=1 \
        "$BABYSIT" fable "$role" live 2>/dev/null | grep '^claude ' )
}

now=$(date +%s)
older_than_sidecar=$(( now - 600 ))   # babysit started before the sidecar
newer_than_sidecar=$(( now + 600 ))   # babysit started after the sidecar

# --- T1: a previous fleet's campaign sidecar is not resumed -----------------
echo "T1: campaign sidecar predating this babysit starts a fresh session"
H1="$TMPROOT/h1"; mkdir -p "$H1/.fleet/sessions"
SID1="aaaaaaaa-1111-2222-3333-444444444444"
SIDECAR1="$H1/.fleet/sessions/campaign-million-entity-render.session-id"
echo "$SID1" > "$SIDECAR1"
make_transcript "$H1" "$SID1"
out=$(launch_for "$H1" campaign-million-entity-render "$newer_than_sidecar")
assert_contains "$out" "--session-id" "cross-fleet re-entry launches a new session"
assert_contains "$out" "/role-campaign million-entity-render live" \
    "cross-fleet re-entry fires the role command so startup re-reads the tree"
assert_absent "$out" "--resume" "cross-fleet re-entry does not resume"

# --- T2: the old pointer is retired, not destroyed --------------------------
echo "T2: the retired sidecar is kept and the new session-id differs"
retired=$(ls "$H1/.fleet/sessions/" | grep 'session-id.prev-' || true)
if [[ -n "$retired" ]]; then
    ok "old sidecar retired to $retired"
else
    bad "old sidecar was deleted rather than retired"
fi
assert_eq "$(cat "$H1/.fleet/sessions/$retired" 2>/dev/null)" "$SID1" \
    "the retired file still holds the old uuid, so the transcript stays reachable"
new_sid=$(cat "$SIDECAR1" 2>/dev/null || echo "")
if [[ -n "$new_sid" && "$new_sid" != "$SID1" ]]; then
    ok "a fresh session-id was persisted ($new_sid)"
else
    bad "expected a new session-id in the sidecar, got '$new_sid'"
fi

# --- T3: within one fleet session a campaign still resumes ------------------
echo "T3: campaign sidecar written by this fleet resumes with no prompt"
H3="$TMPROOT/h3"; mkdir -p "$H3/.fleet/sessions"
SID3="bbbbbbbb-1111-2222-3333-444444444444"
echo "$SID3" > "$H3/.fleet/sessions/campaign-million-entity-render.session-id"
make_transcript "$H3" "$SID3"
out=$(launch_for "$H3" campaign-million-entity-render "$older_than_sidecar")
assert_eq "$out" "claude --model fable --effort xhigh --resume $SID3" \
    "an in-fleet crash resumes the slice, argv unchanged and prompt-free"
assert_eq "$(cat "$H3/.fleet/sessions/campaign-million-entity-render.session-id")" \
    "$SID3" "the in-fleet sidecar still points at the same session"
if ls "$H3/.fleet/sessions/" | grep -q 'session-id.prev-'; then
    bad "an in-fleet sidecar was retired — a mid-slice crash would lose its context"
else
    ok "an in-fleet sidecar is left alone"
fi

# --- T4: architects are untouched by the gate -------------------------------
echo "T4: an architect sidecar predating this babysit still resumes"
H4="$TMPROOT/h4"; mkdir -p "$H4/.fleet/sessions"
SID4="cccccccc-1111-2222-3333-444444444444"
echo "$SID4" > "$H4/.fleet/sessions/opus-architect.session-id"
make_transcript "$H4" "$SID4"
out=$(launch_for "$H4" opus-architect "$newer_than_sidecar")
assert_eq "$out" "claude --model fable --effort xhigh --resume $SID4" \
    "the architect's cross-restart resume is not caught by the campaign gate"
assert_eq "$(cat "$H4/.fleet/sessions/opus-architect.session-id")" "$SID4" \
    "the architect sidecar still points at the same session"
if ls "$H4/.fleet/sessions/" | grep -q 'session-id.prev-'; then
    bad "an architect sidecar was retired — design conversations would reset"
else
    ok "the architect sidecar is left alone"
fi

# --- T5: no sidecar at all ---------------------------------------------------
echo "T5: a campaign with no sidecar starts fresh and retires nothing"
H5="$TMPROOT/h5"; mkdir -p "$H5/.fleet/sessions"
out=$(launch_for "$H5" campaign-million-entity-render "$newer_than_sidecar")
assert_contains "$out" "/role-campaign million-entity-render live" \
    "a first-ever campaign launch fires the role command"
if ls "$H5/.fleet/sessions/" | grep -q 'session-id.prev-'; then
    bad "a first-ever launch retired a file that never existed"
else
    ok "nothing retired on a first-ever launch"
fi

summarize "fleet-babysit campaign re-entry tests"
