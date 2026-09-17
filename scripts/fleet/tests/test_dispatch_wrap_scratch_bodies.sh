#!/usr/bin/env bash
# Tests for fleet-dispatch-wrap's pre-launch removal of the gitignored
# per-iteration scratch bodies (.review-body.md and its siblings).
#
# A fresh launch — any runtime, any role — starts with none of them in the
# pool worktree; a resume leaves them alone. Checked through the
# FLEET_DISPATCH_PRINT_LAUNCH hook, which exits after the launch decision and
# before any agent spawns, so the suite never runs claude or codex.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"
[[ -x "$WRAP" ]] || { echo "test setup: $WRAP not found"; exit 1; }

# Hermeticity: a planning-assigned dispatch exports these into every child
# shell; the wrap only ever sets them from its own argv, never clears them.
unset FLEET_PLAN_ISSUE FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
  FLEET_DISPATCH_REPO FLEET_DISPATCH_NUMBER FLEET_DISPATCH_REASON

# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_SESSIONS_DIR" "$FLEET_STATE_DIR"

# --- stubs ---------------------------------------------------------------
BIN="$TMPROOT/bin"; mkdir -p "$BIN"
for tool in claude codex fleet-claude-stream tmux; do
  printf '#!/usr/bin/env bash\nexit 0\n' > "$BIN/$tool"
done
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
[[ "$1" == "reservation-of" ]] && exit 0
exit 0
STUB
cat > "$BIN/git" <<'STUB'
#!/usr/bin/env bash
case "$*" in
  *"rev-parse --abbrev-ref HEAD"*) echo "master" ;;
  *"rev-parse --verify --quiet refs/remotes/origin/"*) exit 1 ;;
  *"status --porcelain"*) : ;;
  *"rev-list --count"*) echo 0 ;;
  *) : ;;
esac
exit 0
STUB
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

WT="$TMPROOT/pool-4"; mkdir -p "$WT"
SIDECAR="$FLEET_SESSIONS_DIR/pool-4.session.json"

BODIES=(.review-body.md .review-body-3421.md .pr-body.md .merger-body.md .coding-improvement-body.md)

seed_bodies() {
  local f
  for f in "${BODIES[@]}"; do echo "stale" > "$WT/$f"; done
  echo "keep" > "$WT/notes.txt"
}

# bodies_present → 0 when every scratch body still exists, 1 otherwise
bodies_present() {
  local f
  for f in "${BODIES[@]}"; do [[ -f "$WT/$f" ]] || return 1; done
  return 0
}

# bodies_absent → 0 when no scratch body remains, 1 otherwise
bodies_absent() {
  local f
  for f in "${BODIES[@]}"; do [[ -e "$WT/$f" ]] && return 1; done
  return 0
}

launch() {  # args: model effort role [fallback] [mode] [target] [runtime] [class]
  ( cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-4 "$@" 2>>"$TMPROOT/stderr.log" )
}

# =========================================================================
echo "T1: fresh claude worker launch removes every scratch body, keeps other files"
rm -f "$SIDECAR"; seed_bodies
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T1: fresh launch decision" || bad "T1: launch decision: $out"
bodies_absent && ok "T1: all five scratch bodies removed" || bad "T1: a scratch body survived: $(ls -A "$WT" | tr '\n' ' ')"
[[ -f "$WT/notes.txt" ]] && ok "T1: unrelated untracked file kept" || bad "T1: notes.txt removed"

echo "T2: fresh codex reviewer launch removes them too"
rm -f "$SIDECAR"; seed_bodies
out=$(launch gpt-5.6-terra medium sonnet-reviewer "" live target=review:engine:3421 codex sonnet)
[[ "$out" == resumed=0* ]] && ok "T2: fresh codex launch decision" || bad "T2: launch decision: $out"
bodies_absent && ok "T2: scratch bodies removed before the codex launch" || bad "T2: a scratch body survived"

echo "T3: a resume keeps them (the prior session may be mid-draft)"
printf '{"session_id":"SID-1","role":"worker","model":"sonnet","effort":"high","runtime":"claude","created_epoch":1}\n' > "$SIDECAR"
seed_bodies
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=1* ]] && ok "T3: resume decision" || bad "T3: launch decision: $out"
bodies_present && ok "T3: scratch bodies untouched on resume" || bad "T3: a scratch body was removed on resume"
rm -f "$SIDECAR"

echo "T4: a sidecar from another role launches fresh, so the bodies go"
printf '{"session_id":"SID-2","role":"sonnet-reviewer","model":"sonnet","effort":"high","runtime":"claude","created_epoch":1}\n' > "$SIDECAR"
seed_bodies
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T4: cross-role sidecar launches fresh" || bad "T4: launch decision: $out"
bodies_absent && ok "T4: scratch bodies removed on the cross-role fresh launch" || bad "T4: a scratch body survived"
rm -f "$SIDECAR"

echo "T5: non-dispatched role (queue-manager) is a fresh launch as well"
seed_bodies
out=$(launch sonnet high queue-manager "" live)
[[ "$out" == resumed=0* ]] && ok "T5: queue-manager launch decision" || bad "T5: launch decision: $out"
bodies_absent && ok "T5: scratch bodies removed" || bad "T5: a scratch body survived"

summarize "fleet-dispatch-wrap scratch-body cleanup"
