#!/usr/bin/env bash
# Tests for fleet-worktree-sweep: the listing, the guarded --prune, and the
# fleet-health alert that consumes the listing.
#
# Fixture: an engine clone and a nested game clone, each with an origin. The
# engine clone carries a pool worktree and a fleet-rebase scratch worktree
# (both exempt), plus three leaks — clean-on-origin, unpushed, dirty — and the
# game clone one clean-on-origin leak. A fourth engine leak is a registration
# whose directory is gone.
#
# Worktree paths are compared by basename: `git worktree list --porcelain`
# prints the realpath, which is not the mktemp spelling on macOS (/private/var)
# or under native-Windows git (C:/...).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
SWEEP="$SCRIPT_DIR/fleet-worktree-sweep"

if [[ ! -f "$SWEEP" ]]; then
    echo "SKIP: fleet-worktree-sweep absent at $SWEEP" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
export FLEET_REBASE_SCRATCH="$HOME/.fleet/rebase-scratch"
mkdir -p "$HOME"
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test
export GIT_CONFIG_NOSYSTEM=1

ENGINE="$FLEET_ENGINE_ROOT"
GAME="$ENGINE/creations/game"
LEAKS="$TMPROOT/scratch"
mkdir -p "$LEAKS"

# make_clone <dir> <label>: a clone of a fresh bare origin with one pushed commit.
make_clone() {
    local dir="$1" label="$2"
    git init -q --bare -b master "$TMPROOT/$label.git"
    git clone -q "$TMPROOT/$label.git" "$dir" 2>/dev/null
    git -C "$dir" config commit.gpgsign false
    echo init > "$dir/readme.txt"
    git -C "$dir" add readme.txt
    git -C "$dir" commit -q -m init
    git -C "$dir" push -q origin master
}

mkdir -p "$ENGINE"
make_clone "$ENGINE" engine-origin
make_clone "$GAME" game-origin

mkdir -p "$ENGINE/.claude/worktrees" "$GAME/.claude/worktrees"
git -C "$ENGINE" worktree add -q --detach "$ENGINE/.claude/worktrees/pool-1"
git -C "$GAME" worktree add -q --detach "$GAME/.claude/worktrees/pool-1"
mkdir -p "$FLEET_REBASE_SCRATCH"
git -C "$ENGINE" worktree add -q --detach "$FLEET_REBASE_SCRATCH/engine"

git -C "$ENGINE" worktree add -q --detach "$LEAKS/leak-clean"
git -C "$ENGINE" worktree add -q -b wip/unpushed "$LEAKS/leak-unpushed"
echo work > "$LEAKS/leak-unpushed/work.txt"
git -C "$LEAKS/leak-unpushed" add work.txt
git -C "$LEAKS/leak-unpushed" commit -q -m "unpushed work"
UNPUSHED_HEAD=$(git -C "$LEAKS/leak-unpushed" rev-parse HEAD)
git -C "$ENGINE" worktree add -q --detach "$LEAKS/leak-dirty"
echo scratch > "$LEAKS/leak-dirty/untracked.txt"
git -C "$ENGINE" worktree add -q --detach "$LEAKS/leak-gone"
rm -rf "$LEAKS/leak-gone"
git -C "$GAME" worktree add -q --detach "$LEAKS/game-leak-clean"

# --- T1: listing ---------------------------------------------------------------
echo "T1: listing"
set +e
listing=$(python3 "$SWEEP" --json | tr -d '\r')
rc=$?
set -e
assert_eq "$rc" "1" "T1 a non-empty listing exits 1"

# jq-free field access: one "repo basename dirty on_origin missing" row per leak.
rows=$(printf '%s' "$listing" | python3 -c '
import json, os, sys
for lk in json.load(sys.stdin)["leaks"]:
    print(lk["repo"], os.path.basename(lk["path"]), lk["dirty"],
          str(lk["on_origin"]).lower(), str(lk["missing"]).lower(), lk["size_bytes"] is not None)
' | tr -d '\r')
assert_contains "$rows" "engine leak-clean 0 true false True" "T1 clean-on-origin leak listed, clean, with size"
assert_contains "$rows" "engine leak-unpushed 0 false false True" "T1 unpushed leak listed as not on origin"
assert_contains "$rows" "engine leak-dirty 1 true false True" "T1 dirty leak listed with its dirty-file count"
assert_contains "$rows" "engine leak-gone 0 true true False" "T1 registration with a missing directory listed"
assert_contains "$rows" "game game-leak-clean 0 true false True" "T1 the nested game clone is swept"
assert_absent "$rows" "pool-1" "T1 pool-layout worktrees are not leaks"
assert_absent "$rows" " engine 0 true false" "T1 the fleet-rebase scratch worktree is not a leak"
assert_eq "$(printf '%s\n' "$rows" | wc -l | tr -d ' ')" "5" "T1 exactly the five leaks are listed"

text=$(python3 "$SWEEP" --no-size | tr -d '\r' || true)
assert_contains "$text" "leak-unpushed" "T1 text listing names the leak"
assert_contains "$text" "on-origin no" "T1 text listing reports origin reachability"

set +e
python3 "$SWEEP" --dirty-too >/dev/null 2>&1
assert_eq "$?" "2" "T1 --dirty-too without --prune is a usage error"
set -e

# --- T2: prune keeps what it must ----------------------------------------------
echo "T2: prune"
set +e
python3 "$SWEEP" --prune --no-size >/dev/null
rc=$?
set -e
assert_eq "$rc" "1" "T2 prune exits 1 while a dirty leak remains"
[[ ! -d "$LEAKS/leak-clean" ]] && ok "T2 clean-on-origin worktree removed" || bad "T2 clean-on-origin worktree removed"
[[ ! -d "$LEAKS/game-leak-clean" ]] && ok "T2 clean game worktree removed" || bad "T2 clean game worktree removed"
[[ ! -d "$LEAKS/leak-unpushed" ]] && ok "T2 unpushed worktree removed" || bad "T2 unpushed worktree removed"
[[ -d "$LEAKS/leak-dirty" ]] && ok "T2 dirty worktree untouched" || bad "T2 dirty worktree untouched"
[[ -f "$LEAKS/leak-dirty/untracked.txt" ]] && ok "T2 dirty file still on disk" || bad "T2 dirty file still on disk"

salvage_branch=$(git -C "$ENGINE" for-each-ref --format='%(refname:short)' 'refs/heads/salvage/*' | tr -d '\r')
assert_contains "$salvage_branch" "leak-unpushed" "T2 unpushed HEAD salvaged to a salvage/<path-slug> branch"
assert_eq "$(git -C "$ENGINE" rev-parse "$(printf '%s\n' "$salvage_branch" | head -n1)")" "$UNPUSHED_HEAD" \
    "T2 salvage branch points at the unpushed commit"
assert_eq "$(git -C "$GAME" for-each-ref 'refs/heads/salvage/*' | wc -l | tr -d ' ')" "0" \
    "T2 an on-origin HEAD is not salvaged"

registered=$(git -C "$ENGINE" worktree list --porcelain | tr -d '\r')
assert_absent "$registered" "leak-gone" "T2 the missing-directory registration is pruned"
assert_absent "$registered" "leak-clean" "T2 removed worktrees are unregistered"

# --- T3: --dirty-too -----------------------------------------------------------
echo "T3: --dirty-too"
set +e
python3 "$SWEEP" --prune --dirty-too --no-size >/dev/null
rc=$?
set -e
assert_eq "$rc" "0" "T3 prune --dirty-too leaves nothing outside the pool layout"
[[ ! -d "$LEAKS/leak-dirty" ]] && ok "T3 dirty worktree removed" || bad "T3 dirty worktree removed"

# --- T4: a clean sweep ---------------------------------------------------------
echo "T4: clean sweep"
set +e
clean=$(python3 "$SWEEP" | tr -d '\r')
rc=$?
set -e
assert_eq "$rc" "0" "T4 a clean sweep exits 0"
assert_contains "$clean" "no worktrees outside the pool layout" "T4 a clean sweep says so"
[[ -d "$ENGINE/.claude/worktrees/pool-1" && -d "$FLEET_REBASE_SCRATCH/engine" ]] \
    && ok "T4 exempt worktrees survive every prune" || bad "T4 exempt worktrees survive every prune"

# --- T5: absent clones ---------------------------------------------------------
echo "T5: absent clones"
set +e
FLEET_ENGINE_ROOT="$TMPROOT/nowhere" python3 "$SWEEP" >/dev/null
assert_eq "$?" "0" "T5 a host with no clone sweeps clean"
set -e

# --- T6: an unlistable clone ---------------------------------------------------
# The game clone's .git points at a gitdir that does not exist, so its
# `git worktree list` fails while the engine clone still lists a leak.
echo "T6: unlistable clone"
git -C "$ENGINE" worktree add -q --detach "$LEAKS/leak-late"
mv "$GAME/.git" "$GAME/.git.bak"
printf 'gitdir: %s\n' "$TMPROOT/no-such-gitdir" > "$GAME/.git"
set +e
broken=$(python3 "$SWEEP" --json --no-size 2>/dev/null | tr -d '\r')
rc=$?
stderr_text=$(python3 "$SWEEP" --no-size 2>&1 >/dev/null | tr -d '\r')
set -e
assert_eq "$rc" "3" "T6 an unlistable clone exits 3, not clean"
summary=$(printf '%s' "$broken" | python3 -c '
import json, os, sys
d = json.load(sys.stdin)
print(" ".join(os.path.basename(lk["path"]) for lk in d["leaks"]))
for e in d["errors"]:
    print(e)
' | tr -d '\r')
assert_contains "$summary" "leak-late" "T6 the readable clone's leak is still listed"
assert_contains "$summary" "[game]" "T6 the error names the unlistable clone"
assert_contains "$summary" "git worktree list failed" "T6 the error names the failed enumeration"
assert_contains "$stderr_text" "fleet-worktree-sweep: error: [game]" "T6 text mode reports the error on stderr"

set +e
python3 "$SWEEP" --prune --no-size >/dev/null 2>&1
rc=$?
set -e
assert_eq "$rc" "3" "T6 prune still exits 3 while a clone is unlistable"
[[ ! -d "$LEAKS/leak-late" ]] && ok "T6 prune clears the readable clone's leak" \
    || bad "T6 prune clears the readable clone's leak"

rm "$GAME/.git"
mv "$GAME/.git.bak" "$GAME/.git"
set +e
python3 "$SWEEP" --no-size >/dev/null
assert_eq "$?" "0" "T6 a repaired clone sweeps clean again"
set -e

summarize "fleet-worktree-sweep tests"
