#!/usr/bin/env bash

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
source "$SCRIPT_DIR/fleet-common.sh"
if ! declare -F fleet_worktree_registered >/dev/null; then
    FLEET_WORKTREE_ERROR="registration predicate unavailable"
    fleet_worktree_registered() { return 1; }
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(cd "$(mktemp -d)" && pwd -P)
export HOME="$TMPROOT/home"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
mkdir -p "$HOME" "$FLEET_ALERTS_DIR"
FLEET_WORKTREE_HEAL_AVAILABLE=1

eval "$(sed -n '/^heal_worktree_registration()/,/^# migrate_legacy_worker_worktrees/p' "$SCRIPT_DIR/fleet-up" | sed '$d')"
if ! declare -F ensure_worktree >/dev/null; then
    ensure_worktree() { return 127; }
fi

tree_hash() {
    find "$1" -type f ! -name .git -print | LC_ALL=C sort | while IFS= read -r file; do
        shasum "$file"
    done | shasum | awk '{print $1}'
}

make_repo() {
    local repo="$1"
    mkdir -p "$repo"
    git -C "$repo" init -q
    git -C "$repo" config user.email test@example.invalid
    git -C "$repo" config user.name Test
    printf 'base\n' > "$repo/tracked"
    git -C "$repo" add tracked
    git -C "$repo" commit -qm base
    git -C "$repo" branch -M master
    git -C "$repo" remote add origin "$repo"
    git -C "$repo" update-ref refs/remotes/origin/master HEAD
}

echo "T1: a dirty pane is re-registered without changing working-tree bytes"
REPO="$TMPROOT/repo"
make_repo "$REPO"
WT="$REPO/.claude/worktrees/pool-1"
mkdir -p "$(dirname "$WT")"
git -C "$REPO" worktree add -q -b claude/pool-1-scratch "$WT" master
fleet_worktree_registered "$WT" \
    && ok "shared predicate accepts a healthy worktree" || bad "shared predicate refused a healthy worktree"
printf 'edit\n' >> "$WT/tracked"
printf 'new\n' > "$WT/untracked"
before_hash=$(tree_hash "$WT")
before_status=$(git -C "$WT" status --porcelain)
before_head=$(git -C "$WT" rev-parse HEAD)
ADMIN=$(sed -n 's/^gitdir: //p' "$WT/.git")
rm -rf "$ADMIN"
if fleet_worktree_registered "$WT"; then
    bad "shared predicate accepted a missing admin directory"
else
    ok "shared predicate rejects a missing admin directory"
fi
assert_contains "$FLEET_WORKTREE_ERROR" "not a git repository" "shared predicate keeps Git's first error line"
ensure_worktree "$REPO" .claude/worktrees/pool-1 fleet/pool-1 claude/pool-1-scratch
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$before_head" "heal keeps the scratch ref at its prior commit"
assert_eq "$(tree_hash "$WT")" "$before_hash" "heal preserves working-tree bytes"
assert_eq "$(git -C "$WT" status --porcelain)" "$before_status" "heal preserves tracked and untracked status"
assert_contains "$(git -C "$REPO" worktree list --porcelain)" "$WT" "healed pane returns to worktree list"
assert_eq "$(git -C "$REPO" worktree prune --dry-run -v)" "" "healed pane is not prunable"
[[ -f "$FLEET_ALERTS_DIR/fleet-up-healed-dirty-pool-1" ]] \
    && ok "dirty heal writes its human alert" || bad "dirty heal alert missing"

echo "T2: an absent scratch branch is created at origin/master"
WT2="$REPO/.claude/worktrees/pool-2"
git -C "$REPO" worktree add -q -b temporary-pool-2 "$WT2" master
ADMIN2=$(sed -n 's/^gitdir: //p' "$WT2/.git")
rm -rf "$ADMIN2"
ensure_worktree "$REPO" .claude/worktrees/pool-2 fleet/pool-2 claude/pool-2-scratch
assert_eq "$(git -C "$WT2" symbolic-ref --short HEAD)" "claude/pool-2-scratch" \
    "missing scratch branch is created and checked out"
assert_eq "$(git -C "$WT2" rev-parse HEAD)" "$(git -C "$REPO" rev-parse origin/master)" \
    "new scratch branch starts at origin/master"

echo "T3: an empty pane directory uses git worktree add"
WT3="$REPO/.claude/worktrees/pool-3"
mkdir -p "$WT3"
ensure_worktree "$REPO" .claude/worktrees/pool-3 fleet/pool-3 claude/pool-3-scratch
assert_eq "$(git -C "$WT3" rev-parse --show-toplevel)" "$WT3" "empty directory becomes a worktree"

echo "T4: a gitdir from another clone is left untouched"
FOREIGN="$TMPROOT/foreign"
make_repo "$FOREIGN"
WT4="$REPO/.claude/worktrees/pool-4"
mkdir -p "$WT4"
printf 'gitdir: %s/.git/worktrees/pool-4\n' "$FOREIGN" > "$WT4/.git"
set +e
out=$(ensure_worktree "$REPO" .claude/worktrees/pool-4 fleet/pool-4 claude/pool-4-scratch 2>&1)
rc=$?
set -e
assert_eq "$rc" "0" "foreign gitdir is a non-fatal fleet-up skip"
assert_contains "$out" "belongs to another clone" "foreign gitdir is explained"
[[ ! -e "$FOREIGN/.git/worktrees/pool-4" ]] \
    && ok "foreign admin dir is not created" || bad "foreign admin dir was created"

summarize "fleet-up worktree registration heal"
