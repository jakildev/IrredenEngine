#!/usr/bin/env bash
# Tests for fleet-dispatch-wrap's pre-launch clean-pane arm: a role launch
# starts from a clean pane or does not start. Uses a real temporary git
# worktree (not a git stub), since the arm's own `git diff`/`checkout` must
# actually run; only claude/codex/tmux/fleet-claude-stream/fleet-claim are
# stubbed. Checked through the FLEET_DISPATCH_PRINT_LAUNCH hook, which exits
# after the launch decision and before any agent spawns.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"
[[ -x "$WRAP" ]] || { echo "test setup: $WRAP not found"; exit 1; }

unset FLEET_PLAN_ISSUE FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
  FLEET_DISPATCH_REPO FLEET_DISPATCH_NUMBER FLEET_DISPATCH_REASON

# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_LEFTOVERS_DIR="$TMPROOT/leftovers"
mkdir -p "$FLEET_SESSIONS_DIR" "$FLEET_STATE_DIR" "$FLEET_LEFTOVERS_DIR"

# --- stubs: everything except git, which must run for real -----------------
BIN="$TMPROOT/bin"; mkdir -p "$BIN"
for tool in claude codex fleet-claude-stream tmux; do
  printf '#!/usr/bin/env bash\nexit 0\n' > "$BIN/$tool"
done
export FLEET_CLAIM_RESV_FILE="$TMPROOT/resv.txt"
: > "$FLEET_CLAIM_RESV_FILE"
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
[[ "$1" == "reservation-of" ]] && { cat "${FLEET_CLAIM_RESV_FILE:-/dev/null}" 2>/dev/null; exit 0; }
exit 0
STUB
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

# --- a real origin + worktree clone -----------------------------------------
ORIGIN="$TMPROOT/origin.git"
git init --quiet --bare "$ORIGIN"
SEED="$TMPROOT/seed"
git init --quiet "$SEED"
git -C "$SEED" config user.email "test@example.com"
git -C "$SEED" config user.name "test"
git -C "$SEED" checkout --quiet -b master
echo "original" > "$SEED/tracked.txt"
printf '\x00\x01\xff\xfe\x00binary\x00' > "$SEED/image.bin"
git -C "$SEED" add tracked.txt image.bin
git -C "$SEED" commit --quiet -m "seed"
git -C "$SEED" remote add origin "$ORIGIN"
git -C "$SEED" push --quiet origin master

WT="$TMPROOT/pool-4"
git clone --quiet "$ORIGIN" "$WT"
git -C "$WT" config user.email "test@example.com"
git -C "$WT" config user.name "test"
SIDECAR="$FLEET_SESSIONS_DIR/pool-4.session.json"

reset_pane() {
  git -C "$WT" checkout --quiet master
  git -C "$WT" reset --quiet --hard origin/master
  git -C "$WT" clean --quiet -fd
  : > "$FLEET_CLAIM_RESV_FILE"
  rm -f "$SIDECAR"
}

seed_dirty() {
  echo "modified" > "$WT/tracked.txt"
  printf '\x00\x01\xff\xfe\x00CHANGED\x00' > "$WT/image.bin"
  echo "loop" > "$WT/.retry-verdict.sh"
}

tracked_dirty_present() {
  [[ -n "$(git -C "$WT" status --porcelain | grep -v '^??' || true)" ]]
}

latest_patch() { ls "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch 2>/dev/null | head -1; }

launch() {  # args: model effort role [fallback] [mode] [target] [runtime] [class]
  ( cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-4 "$@" 2>>"$TMPROOT/stderr.log" )
}

# =============================================================================
echo "T1: positive-fire — tracked mods + retry script, no reservation, no sidecar"
reset_pane; seed_dirty
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T1: fresh launch decision" || bad "T1: launch decision: $out"
tracked_dirty_present && bad "T1: tracked modifications survived" || ok "T1: worktree clean"
[[ "$(git -C "$WT" rev-parse HEAD)" == "$(git -C "$WT" rev-parse origin/master)" ]] \
  && ok "T1: HEAD reset to origin/master" || bad "T1: HEAD did not reset"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "claude/pool-4-scratch" ]] \
  && ok "T1: branch reset to claude/pool-4-scratch" || bad "T1: branch is $(git -C "$WT" rev-parse --abbrev-ref HEAD)"
[[ -f "$WT/.retry-verdict.sh" ]] && bad "T1: retry script survived" || ok "T1: retry script removed"
patch=$(latest_patch)
[[ -n "$patch" ]] && ok "T1: leftover patch written" || bad "T1: no leftover patch found"
if [[ -n "$patch" ]]; then
  grep -q "tracked.txt" "$patch" && ok "T1: patch covers the text file" || bad "T1: patch missing tracked.txt"
  grep -q "GIT binary patch" "$patch" && ok "T1: patch covers the binary file" || bad "T1: patch missing binary hunk"
fi
grep -q "backed up to" "$TMPROOT/stderr.log" && ok "T1: stderr names the patch" || bad "T1: stderr silent on the backup"

echo "T2: reservation exemption — a live lock on this pane leaves it untouched"
reset_pane; seed_dirty
echo "9001" > "$FLEET_CLAIM_RESV_FILE"
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T2: fresh launch decision" || bad "T2: launch decision: $out"
tracked_dirty_present && ok "T2: tracked modifications survive under a live reservation" || bad "T2: tracked modifications were discarded"
[[ -f "$WT/.retry-verdict.sh" ]] && ok "T2: retry script survives" || bad "T2: retry script was removed"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "T2: branch untouched" || bad "T2: branch changed under reservation"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T2: no patch written" || bad "T2: a patch was written despite the reservation"

echo "T3: resume exemption — a role-matching sidecar leaves it untouched"
reset_pane; seed_dirty
printf '{"session_id":"SID-1","role":"worker","model":"sonnet","effort":"high","runtime":"claude","created_epoch":1}\n' > "$SIDECAR"
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=1* ]] && ok "T3: resume decision" || bad "T3: launch decision: $out"
tracked_dirty_present && ok "T3: tracked modifications survive a resume" || bad "T3: tracked modifications were discarded on resume"
[[ -f "$WT/.retry-verdict.sh" ]] && ok "T3: retry script survives" || bad "T3: retry script was removed on resume"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T3: no patch written on resume" || bad "T3: a patch was written on resume"
rm -f "$SIDECAR"

echo "T4: clean pane is a no-op"
reset_pane
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T4: fresh launch decision" || bad "T4: launch decision: $out"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T4: no patch written on a clean pane" || bad "T4: a patch was written on a clean pane"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "T4: branch unchanged on a clean pane" || bad "T4: branch changed on a clean pane"
[[ -s "$TMPROOT/stderr.log" ]] && bad "T4: spurious log line on a clean pane: $(cat "$TMPROOT/stderr.log")" || ok "T4: no spurious log line"

summarize "fleet-dispatch-wrap clean-pane pre-launch arm"
