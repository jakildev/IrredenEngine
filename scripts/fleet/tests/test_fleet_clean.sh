#!/usr/bin/env bash
# Tests for fleet-clean: the dry-run report, the guarded --apply, --wipe-builds,
# and argument parsing.
#
# Fixture: an engine clone and a nested game clone, each with an origin. The
# engine clone carries pool worktrees in every kept state (idle, reserved,
# claimed, dispatched, heartbeating, dirty, on a feature branch), orphan dirs
# under .claude/worktrees/ (clean, holding unsaved work, just created), temp
# worktrees outside the clone (clean, dirty, HEAD on no ref), root-level build
# dirs for game worktrees, and git garbage. Ages are set from one clock that
# FLEET_CLEAN_NOW pins for the subject.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
CLEAN="$SCRIPT_DIR/fleet-clean"

if [[ ! -f "$CLEAN" ]]; then
    echo "SKIP: fleet-clean absent at $CLEAN" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
unset FLEET_GAME_ROOT FLEET_STATE_DIR FLEET_CLAIMS_DIR FLEET_RESERVATIONS_DIR \
    FLEET_HEARTBEATS_DIR FLEET_REBASE_SCRATCH
mkdir -p "$HOME"
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test
export GIT_CONFIG_NOSYSTEM=1

NOW=$(date +%s)
export FLEET_CLEAN_NOW="$NOW"

# set_age <days> <path>... — every file and dir under each path last modified
# <days> ago.
set_age() {
    local days="$1" epoch stamp
    shift
    epoch=$((NOW - days * 86400))
    stamp=$(date -u -d "@$epoch" +%Y%m%d%H%M.%S 2>/dev/null || date -u -r "$epoch" +%Y%m%d%H%M.%S)
    find "$@" -exec env TZ=UTC touch -t "$stamp" {} +
}

# make_build <dir> <days> — a build dir with one artifact, all <days> old.
make_build() {
    mkdir -p "$1/bin"
    printf 'artifact %s\n' "$1" > "$1/bin/app.exe"
    set_age "$2" "$1"
}

ENGINE="$FLEET_ENGINE_ROOT"
GAME="$ENGINE/creations/game"
POOL="$ENGINE/.claude/worktrees"

make_clone() {
    local dir="$1" label="$2"
    git init -q --bare -b master "$TMPROOT/$label.git"
    git clone -q "$TMPROOT/$label.git" "$dir" 2>/dev/null
    git -C "$dir" config commit.gpgsign false
    printf 'build*/\nsave_files/\n.claude/worktrees/\ncreations/game/\n' > "$dir/.gitignore"
    echo init > "$dir/readme.txt"
    git -C "$dir" add .gitignore readme.txt
    git -C "$dir" commit -q -m init
    git -C "$dir" push -q origin master
}

mkdir -p "$ENGINE"
make_clone "$ENGINE" engine-origin
make_clone "$GAME" game-origin

add_pool() {   # add_pool <clone> <name> [branch]
    if [[ -n "${3:-}" ]]; then
        git -C "$1" worktree add -q -b "$3" "$1/.claude/worktrees/$2"
    else
        git -C "$1" worktree add -q --detach "$1/.claude/worktrees/$2"
    fi
}

mkdir -p "$POOL" "$GAME/.claude/worktrees"
add_pool "$ENGINE" pool-1
add_pool "$ENGINE" pool-2 claude/pool-2-scratch
for n in 3 5 6 7; do add_pool "$ENGINE" "pool-$n"; done
add_pool "$ENGINE" pool-4 claude/feature-x
add_pool "$GAME" pool-1 claude/game-pool-1-scratch
add_pool "$GAME" pool-2 claude/game-pool-2-scratch

# A tracked file in a build-named dir of the main checkout, committed after the
# pools branched so only the main clone has it.
mkdir -p "$ENGINE/build-release"
echo keep > "$ENGINE/build-release/keep.txt"
git -C "$ENGINE" add -f build-release/keep.txt
git -C "$ENGINE" commit -q -m "tracked file in a build-named dir"
git -C "$ENGINE" push -q origin master

# pool-1: idle. build-game/ stale, build-release/ fresh, build/ fresh with one
# old and one new capture each of *.prof and save_files/.
make_build "$POOL/pool-1/build-game" 30
make_build "$POOL/pool-1/build-release" 0
make_build "$POOL/pool-1/build" 0
mkdir -p "$POOL/pool-1/build/demo/save_files"
echo old > "$POOL/pool-1/build/demo/old.prof"
echo old > "$POOL/pool-1/build/demo/save_files/old.png"
echo new > "$POOL/pool-1/build/demo/new.prof"
echo new > "$POOL/pool-1/build/demo/save_files/new.png"
set_age 30 "$POOL/pool-1/build/demo/old.prof" "$POOL/pool-1/build/demo/save_files/old.png"

# Every other pool is kept, each for a different reason.
for n in 2 3 4 5 6 7; do make_build "$POOL/pool-$n/build-game" 30; done
mkdir -p "$HOME/.fleet/reservations" "$HOME/.fleet/claims/issue-42" \
    "$HOME/.fleet/state/dispatch" "$HOME/.fleet/heartbeats"
echo '{"task_id": "42"}' > "$HOME/.fleet/reservations/pool-2.json"
echo untracked > "$POOL/pool-3/scratch.txt"
echo pool-5 > "$HOME/.fleet/claims/issue-42/owner"
echo '{"role":"worker","pane":"%6","agent":"pool-6"}' > "$HOME/.fleet/state/dispatch/pane-6.json"
touch "$HOME/.fleet/heartbeats/pool-7"
touch "$HOME/.fleet/heartbeats/pool-1"
set_age 3 "$HOME/.fleet/heartbeats/pool-1"

# Engine-root build dirs: the main clone's own, and game worktrees' builds.
make_build "$ENGINE/build" 30
make_build "$ENGINE/build-game-pool-1" 30
make_build "$ENGINE/build-game-pool-2" 30
make_build "$ENGINE/build-game-gone" 2
make_build "$ENGINE/build-game-game" 0

# Orphans: registered, then their admin dirs pruned away.
make_orphan() {
    git -C "$ENGINE" worktree add -q --detach "$POOL/$1"
    rm -rf "$ENGINE/.git/worktrees/$1"
}
make_orphan worker-1
make_build "$POOL/worker-1/build" 30
set_age 2 "$POOL/worker-1"
make_orphan worker-2
echo "unsaved work" > "$POOL/worker-2/notes.txt"
set_age 2 "$POOL/worker-2"
make_orphan worker-3

# Temp worktrees outside the clone.
mkdir -p "$TMPROOT/bisect"
git -C "$ENGINE" worktree add -q --detach "$TMPROOT/bisect/bwt"
git -C "$ENGINE" worktree add -q --detach "$TMPROOT/bisect/bwt-dirty"
echo edit >> "$TMPROOT/bisect/bwt-dirty/readme.txt"
git -C "$ENGINE" worktree add -q --detach "$TMPROOT/bisect/bwt-unref"
echo work > "$TMPROOT/bisect/bwt-unref/work.txt"
git -C "$TMPROOT/bisect/bwt-unref" add work.txt
git -C "$TMPROOT/bisect/bwt-unref" commit -q -m "detached work"

# Git garbage: one leftover, one a fetch is writing now.
echo junk > "$ENGINE/.git/objects/pack/tmp_pack_old"
echo junk > "$ENGINE/.git/objects/pack/tmp_pack_live"
set_age 30 "$ENGINE/.git/objects/pack/tmp_pack_old"

run_clean() {
    set +e
    out=$(bash "$CLEAN" "$@" 2>&1 | tr -d '\r')
    rc=${PIPESTATUS[0]}
    set -e
}

# assert_line <kind> <path-suffix> <msg> — a report line of <kind> whose path
# ends in <path-suffix>.
assert_line() {
    if printf '%s\n' "$out" | grep -E "^  $1 " | grep -qF "$2  ("; then
        ok "$3"
    else
        bad "$3"
        printf '%s\n' "$out" | sed 's/^/        /'
    fi
}
assert_exists() { [[ -e "$1" ]] && ok "$2" || bad "$2"; }
assert_gone() { [[ ! -e "$1" ]] && ok "$2" || bad "$2"; }

# --- T1: dry run ---------------------------------------------------------------
echo "T1: dry run"
run_clean
assert_eq "$rc" "0" "T1 dry run exits 0"
assert_line stale-build "pool-1/build-game" "T1 a stale build-game/ in an idle pool is reclaimable"
assert_absent "$out" "pool-1/build-release" "T1 a fresh build-release/ is not listed"
assert_line old-capture "pool-1/build" "T1 old captures in a kept build/ are reclaimable"
assert_contains "$out" "2 file(s) older than 14d" "T1 only the old .prof and save_files file are counted"
assert_line kept "pool-2" "T1 a reserved pool is kept"
assert_contains "$out" "busy: reserved" "T1 the reservation is named"
assert_contains "$out" "1 dirty file(s)" "T1 a dirty pool is kept"
assert_contains "$out" "holds branch claude/feature-x" "T1 a pool on a feature branch is kept"
assert_contains "$out" "busy: claim issue-42" "T1 a pool owning a claim is kept"
assert_contains "$out" "busy: in-flight dispatch" "T1 a pool named by a dispatch record is kept"
assert_contains "$out" "busy: heartbeat within the hour" "T1 a heartbeating pool is kept"
assert_line stale-build "IrredenEngine/build" "T1 the main clone's stale build/ is reclaimable"
assert_line kept "IrredenEngine/build-release" "T1 a build dir holding a tracked file is kept"
assert_line stale-build "build-game-pool-1" "T1 an idle game worktree's stale root build is reclaimable"
assert_line kept "build-game-pool-2" "T1 a busy game worktree's root build is kept"
assert_line stale-build "build-game-gone" "T1 a root build whose game worktree is gone is reclaimable"
assert_absent "$out" "build-game-game" "T1 the game main clone's fresh build is not listed"
assert_line orphan "worktrees/worker-1" "T1 a clean orphan is reclaimable"
assert_contains "$out" "worker-2  (1 file(s) hold content the clone has no object for)" \
    "T1 an orphan holding unsaved work is kept"
assert_contains "$out" "worker-3  (created within the hour)" "T1 a just-created orphan is kept"
assert_line temp-worktree "bisect/bwt" "T1 a clean temp worktree is reclaimable"
assert_contains "$out" "bwt-dirty  (1 dirty file(s))" "T1 a dirty temp worktree is kept"
assert_contains "$out" "is on no ref" "T1 a temp worktree whose HEAD is on no ref is kept"
assert_line git-garbage ".git/objects" "T1 old git garbage is reclaimable"
assert_contains "$out" "rerun with --apply" "T1 the dry run says how to apply"
assert_exists "$POOL/pool-1/build-game" "T1 the dry run deletes nothing (build)"
assert_exists "$POOL/worker-1" "T1 the dry run deletes nothing (orphan)"
assert_exists "$TMPROOT/bisect/bwt" "T1 the dry run deletes nothing (temp worktree)"
assert_exists "$ENGINE/.git/objects/pack/tmp_pack_old" "T1 the dry run deletes nothing (garbage)"

run_clean --days=60
assert_absent "$out" "pool-1/build-game" "T1 --days=60 keeps a 30-day-old build"
assert_line stale-build "build-game-gone" "T1 a gone worktree's build goes at any age"

# --- T2: apply -----------------------------------------------------------------
echo "T2: apply"
run_clean --apply
assert_eq "$rc" "0" "T2 apply exits 0"
assert_contains "$out" "reclaimed:" "T2 apply reports what it reclaimed"
assert_gone "$POOL/pool-1/build-game" "T2 stale pool build removed"
assert_gone "$POOL/pool-1/build/demo/old.prof" "T2 old .prof removed"
assert_gone "$POOL/pool-1/build/demo/save_files/old.png" "T2 old save_files output removed"
assert_exists "$POOL/pool-1/build/demo/new.prof" "T2 new .prof kept"
assert_exists "$POOL/pool-1/build/demo/save_files/new.png" "T2 new save_files output kept"
assert_exists "$POOL/pool-1/build/bin/app.exe" "T2 a pool's build/ is kept without --wipe-builds"
assert_exists "$POOL/pool-1/build-release" "T2 fresh build-release/ kept"
for n in 2 3 4 5 6 7; do
    assert_exists "$POOL/pool-$n/build-game" "T2 kept pool-$n's build untouched"
done
assert_exists "$POOL/pool-3/scratch.txt" "T2 the dirty pool's file untouched"
assert_gone "$ENGINE/build" "T2 the main clone's stale build/ removed"
assert_exists "$ENGINE/build-release/keep.txt" "T2 the tracked file untouched"
assert_eq "$(git -C "$ENGINE" status --porcelain | wc -l | tr -d ' ')" "0" "T2 the main clone is still clean"
assert_gone "$ENGINE/build-game-pool-1" "T2 idle game worktree's root build removed"
assert_exists "$ENGINE/build-game-pool-2" "T2 busy game worktree's root build kept"
assert_gone "$ENGINE/build-game-gone" "T2 gone game worktree's root build removed"
assert_exists "$ENGINE/build-game-game" "T2 the game main clone's fresh build kept"
assert_gone "$POOL/worker-1" "T2 clean orphan removed"
assert_exists "$POOL/worker-2/notes.txt" "T2 the orphan's unsaved work kept"
assert_exists "$POOL/worker-3" "T2 the just-created orphan kept"
assert_gone "$TMPROOT/bisect/bwt" "T2 clean temp worktree removed"
assert_exists "$TMPROOT/bisect/bwt-dirty" "T2 dirty temp worktree kept"
assert_exists "$TMPROOT/bisect/bwt-unref/work.txt" "T2 the HEAD-on-no-ref temp worktree kept"
registered=$(git -C "$ENGINE" worktree list --porcelain | tr -d '\r')
assert_eq "$(printf '%s\n' "$registered" | grep -c '/bisect/bwt$' || true)" "0" \
    "T2 the removed temp worktree is unregistered"
assert_contains "$registered" "pool-1" "T2 pool registrations survive"
assert_gone "$ENGINE/.git/objects/pack/tmp_pack_old" "T2 old git garbage removed"
assert_exists "$ENGINE/.git/objects/pack/tmp_pack_live" "T2 live git garbage kept"

run_clean
assert_absent "$out" "stale-build" "T2 a second dry run finds no stale builds"

# --- T3: --wipe-builds ---------------------------------------------------------
echo "T3: --wipe-builds"
run_clean --wipe-builds --apply
assert_eq "$rc" "0" "T3 wipe exits 0"
assert_gone "$POOL/pool-1/build" "T3 the idle pool's build/ wiped"
assert_gone "$POOL/pool-1/build-release" "T3 the idle pool's fresh build-release/ wiped"
assert_exists "$POOL/pool-2/build-game" "T3 a busy pool's build survives the wipe"
assert_exists "$POOL/pool-3/build-game" "T3 a dirty pool's build survives the wipe"
assert_exists "$ENGINE/build-game-game" "T3 the game main clone's build is not a pool build"

# --- T5: state that changes between discovery and deletion ---------------------
# Every target below is reclaimable when discovery runs. FLEET_CLEAN_APPLY_HOOK
# fires before the first --apply re-check and changes all of them at once, so
# each later deletion has to see a change made after discovery and after
# earlier deletions; race-idle and old2.prof are the untouched controls.
echo "T5: apply-time re-check"
for n in busy claim hb dirty branch lock unreg idle cap; do
    add_pool "$ENGINE" "race-$n"
    make_build "$POOL/race-$n/build-game" 30
done
mkdir -p "$POOL/race-cap/build"
echo old > "$POOL/race-cap/build/old.prof"
echo old > "$POOL/race-cap/build/old2.prof"
set_age 30 "$POOL/race-cap/build"
make_orphan race-orphan
set_age 2 "$POOL/race-orphan"
git -C "$ENGINE" worktree add -q --detach "$TMPROOT/bisect/bwt-race"
make_build "$ENGINE/build" 30
make_build "$ENGINE/build-game-race-gone" 2
add_pool "$GAME" race-game claude/game-race-game-scratch
make_build "$ENGINE/build-game-race-game" 30

HOOK="$TMPROOT/apply-hook.sh"
cat > "$HOOK" <<EOF
[[ -e "$TMPROOT/hook-fired" ]] && exit 0
touch "$TMPROOT/hook-fired"
echo '{"task_id": "77"}' > "$HOME/.fleet/reservations/race-busy.json"
mkdir -p "$HOME/.fleet/claims/issue-77"
echo race-claim > "$HOME/.fleet/claims/issue-77/owner"
touch "$HOME/.fleet/heartbeats/race-hb"
echo wip > "$POOL/race-dirty/wip.txt"
git -C "$POOL/race-branch" checkout -q -b claude/race-feature
git -C "$ENGINE" worktree lock "$POOL/race-lock"
rm -rf "$ENGINE/.git/worktrees/race-unreg"
touch "$POOL/race-cap/build/old.prof"
echo "fresh unsaved work" > "$POOL/race-orphan/notes.txt"
echo edit >> "$TMPROOT/bisect/bwt-race/readme.txt"
echo fresh > "$ENGINE/build/bin/new.o"
git -C "$GAME" worktree add -q --detach "$GAME/.claude/worktrees/race-gone"
git -C "$GAME/.claude/worktrees/race-game" checkout -q -b claude/game-race-feature
EOF
FLEET_CLEAN_APPLY_HOOK="$HOOK" run_clean --apply
assert_exists "$TMPROOT/hook-fired" "T5 the apply hook ran"
# assert_skipped <path-suffix> <reason> <msg> — discovery listed the item, then
# --apply skipped it for <reason> and left it on disk.
assert_skipped() {
    if printf '%s\n' "$out" | grep -qF "$1  (" \
        && printf '%s\n' "$out" | grep -F "skipped: " | grep -F "$1 (" | grep -qF "$2"; then
        ok "$3"
    else
        bad "$3"
        printf '%s\n' "$out" | sed 's/^/        /'
    fi
}
assert_skipped "race-busy/build-game" "busy: reserved" "T5 a pool reserved after discovery is skipped"
assert_skipped "race-claim/build-game" "busy: claim issue-77" "T5 a pool that took a claim is skipped"
assert_skipped "race-hb/build-game" "busy: heartbeat within the hour" "T5 a pool that heartbeat is skipped"
assert_skipped "race-dirty/build-game" "1 dirty file(s)" "T5 a pool that went dirty is skipped"
assert_skipped "race-branch/build-game" "holds branch claude/race-feature" "T5 a pool that left its scratch branch is skipped"
assert_skipped "race-lock/build-game" "locked" "T5 a pool locked after discovery is skipped"
assert_skipped "race-unreg/build-game" "is no longer registered" "T5 a pool unregistered after discovery is skipped"
assert_skipped "race-orphan" "created within the hour" "T5 an orphan written after discovery is skipped"
assert_skipped "bisect/bwt-race" "1 dirty file(s)" "T5 a temp worktree that went dirty is skipped"
assert_skipped "IrredenEngine/build" "written since discovery" "T5 a stale build written after discovery is skipped"
assert_skipped "build-game-race-gone" "game worktree 'race-gone' exists now" \
    "T5 a gone worktree's build is skipped once the worktree is back"
assert_skipped "build-game-race-game" "holds branch claude/game-race-feature" \
    "T5 a root game build is skipped when its game worktree leaves its scratch branch"
for n in busy claim hb dirty branch lock unreg; do
    assert_exists "$POOL/race-$n/build-game/bin/app.exe" "T5 race-$n's build untouched"
done
assert_exists "$POOL/race-orphan/notes.txt" "T5 the orphan's new work untouched"
assert_exists "$TMPROOT/bisect/bwt-race/readme.txt" "T5 the dirty temp worktree untouched"
assert_exists "$ENGINE/build/bin/new.o" "T5 the rewritten main build untouched"
assert_exists "$ENGINE/build-game-race-gone" "T5 the reclaimed worktree's root build untouched"
assert_exists "$ENGINE/build-game-race-game" "T5 the busy game worktree's root build untouched"
assert_exists "$POOL/race-cap/build/old.prof" "T5 a capture rewritten after discovery is kept"
assert_gone "$POOL/race-cap/build/old2.prof" "T5 the untouched capture beside it is removed"
assert_gone "$POOL/race-idle/build-game" "T5 the untouched idle pool's build is removed"
assert_eq "$rc" "0" "T5 skips are not failures"

# --- T4: arguments -------------------------------------------------------------
echo "T4: arguments"
for args in "--days" "--days=" "--days abc" "--root" "--root=" "--bogus" "--root $TMPROOT/nowhere"; do
    # shellcheck disable=SC2086
    run_clean $args
    assert_eq "$rc" "2" "T4 '$args' is a usage error"
done
FLEET_CLEAN_NOW=soon run_clean
assert_eq "$rc" "2" "T4 a non-numeric FLEET_CLEAN_NOW is a usage error"
run_clean --help
assert_eq "$rc" "0" "T4 --help exits 0"
assert_contains "$out" "fleet-clean [--days N]" "T4 --help prints the usage block"
assert_absent "$out" "set -euo pipefail" "T4 --help stops at the header"

summarize "fleet-clean tests"
