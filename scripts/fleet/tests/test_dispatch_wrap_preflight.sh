#!/usr/bin/env bash

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
mkdir -p "$HOME" "$FLEET_STATE_DIR/dispatch-current" "$FLEET_SESSIONS_DIR" "$FLEET_ALERTS_DIR"

BIN="$TMPROOT/bin"
mkdir -p "$BIN"
export CLAUDE_ARGV_LOG="$TMPROOT/claude.log"
export CLAIM_LOG="$TMPROOT/claim.log"
: > "$CLAUDE_ARGV_LOG"
: > "$CLAIM_LOG"
for tool in codex fleet-claude-stream fleet-gh-token fleet-runtime tmux; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$BIN/$tool"
done
cat > "$BIN/claude" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAUDE_ARGV_LOG"
exit 0
STUB
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAIM_LOG"
exit 0
STUB
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

REPO="$TMPROOT/repo"
mkdir -p "$REPO"
git -C "$REPO" init -q
git -C "$REPO" config user.email test@example.invalid
git -C "$REPO" config user.name Test
printf 'seed\n' > "$REPO/seed"
git -C "$REPO" add seed
git -C "$REPO" commit -qm seed
git -C "$REPO" branch -M master

echo "T1: a missing worktree admin dir refuses before wrapper state or launch"
BROKEN="$REPO/pool-broken"
git -C "$REPO" worktree add -q -b fleet/pool-broken "$BROKEN" master
ADMIN=$(sed -n 's/^gitdir: //p' "$BROKEN/.git")
rm -rf "$ADMIN"
set +e
(cd "$BROKEN" && "$WRAP" pane-1 opus high worker "" live target=plan:engine:3823 codex opus) \
    >"$TMPROOT/broken.out" 2>"$TMPROOT/broken.err"
rc=$?
set -e
assert_eq "$rc" "1" "broken worktree exits 1"
[[ -f "$FLEET_ALERTS_DIR/dispatch-wrap-pool-broken" ]] \
    && ok "broken worktree writes its alert" || bad "broken worktree alert missing"
assert_contains "$(<"$FLEET_ALERTS_DIR/dispatch-wrap-pool-broken")" "not a git repository" \
    "alert carries Git's error"
[[ ! -s "$CLAUDE_ARGV_LOG" ]] && ok "no role process starts" || bad "role process started"
[[ ! -e "$FLEET_SESSIONS_DIR/pool-broken.session.json" ]] \
    && ok "no sidecar is written" || bad "sidecar was written"
[[ ! -e "$FLEET_STATE_DIR/dispatch-current/pool-broken" ]] \
    && ok "no dispatch-current record is written" || bad "dispatch-current was written"
assert_contains "$(<"$CLAIM_LOG")" "planning-release 3823 pool-broken" \
    "assigned planning claim is released"

echo "T2: a directory inherited from its enclosing clone is refused"
NESTED="$REPO/pool-nested"
mkdir -p "$NESTED"
set +e
(cd "$NESTED" && "$WRAP" pane-2 opus high worker "" live) \
    >"$TMPROOT/nested.out" 2>"$TMPROOT/nested.err"
rc=$?
set -e
assert_eq "$rc" "1" "nested non-worktree exits 1"
assert_contains "$(<"$FLEET_ALERTS_DIR/dispatch-wrap-pool-nested")" "resolves to" \
    "nested non-worktree reports the wrong root"

echo "T3: a healthy worktree launches and clears a stale alert"
HEALTHY="$REPO/pool-healthy"
git -C "$REPO" worktree add -q -b fleet/pool-healthy "$HEALTHY" master
printf 'stale\n' > "$FLEET_ALERTS_DIR/dispatch-wrap-pool-healthy"
printf '2\n' > "$FLEET_ALERTS_DIR/.dispatch-wrap-pool-healthy.count"
out=$(cd "$HEALTHY" && FLEET_DISPATCH_PRINT_LAUNCH=1 \
    "$WRAP" pane-3 opus high worker "" live "" codex opus 2>/dev/null)
assert_contains "$out" "resumed=0" "healthy worktree reaches launch selection"
[[ ! -e "$FLEET_ALERTS_DIR/dispatch-wrap-pool-healthy" ]] \
    && ok "healthy worktree clears the alert" || bad "healthy alert survived"

echo "T4: a staged wrapper without fleet-common refuses identity-ambiguous launch"
STAGED="$TMPROOT/staged"
mkdir -p "$STAGED"
cp "$WRAP" "$STAGED/fleet-dispatch-wrap"
chmod +x "$STAGED/fleet-dispatch-wrap"
set +e
out=$(cd "$HEALTHY" && FLEET_DISPATCH_PRINT_LAUNCH=1 \
    "$STAGED/fleet-dispatch-wrap" pane-4 opus high worker "" live 2>&1)
rc=$?
set -e
assert_eq "$rc" "1" "missing identity helper refuses launch"
assert_absent "$out" "resumed=0" "no ambiguous-auth role starts"

summarize "fleet-dispatch-wrap worktree preflight"
