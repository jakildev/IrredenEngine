#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-clone-freshness.sh (#1810).
#
# Exercises the three entry points against throwaway git repos:
#   clone_behind_count  — rev-parse-only behind count
#   assert_clone_fresh  — fail-loud claim gate
#   advance_main_clone  — guarded, rate-limited ff-only advance (the safety
#                         guards are the important part: never clobber an
#                         off-master / dirty / diverged shared main clone).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
HELPER="$SCRIPT_DIR/fleet-clone-freshness.sh"

if [[ ! -f "$HELPER" ]]; then
    echo "SKIP: helper not found at $HELPER" >&2
    exit 0
fi
if ! command -v git >/dev/null 2>&1; then
    echo "SKIP: git not available" >&2
    exit 0
fi

# shellcheck source=/dev/null
source "$HELPER"

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# Isolate the rate-limit sentinel from the real ~/.fleet/state.
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_STATE_DIR"

ok()   { echo "  ok: $1";   PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

git_q() { git -C "$1" "${@:2}" >/dev/null 2>&1; }

# Build an origin (bare) + a working clone on master with one commit pushed.
ORIGIN="$TMPROOT/origin.git"
CLONE="$TMPROOT/clone"
PUSHER="$TMPROOT/pusher"

git init --bare -q "$ORIGIN"
git -c init.defaultBranch=master init -q "$CLONE"
git -C "$CLONE" config user.email t@t
git -C "$CLONE" config user.name test
git -C "$CLONE" remote add origin "$ORIGIN"
echo "v1" > "$CLONE/file"
git_q "$CLONE" add file
git_q "$CLONE" commit -m v1
git_q "$CLONE" branch -M master
git_q "$CLONE" push -u origin master

# Second clone used only to push new commits to origin (simulate other merges).
git clone -q "$ORIGIN" "$PUSHER"
git -C "$PUSHER" config user.email t@t
git -C "$PUSHER" config user.name test

push_new_commit() {  # $1 = content tag
    echo "$1" >> "$PUSHER/file"
    git_q "$PUSHER" add file
    git_q "$PUSHER" commit -m "$1"
    git_q "$PUSHER" push origin master
}

reset_rate_limit() { rm -f "$FLEET_STATE_DIR"/.*-clone-advanced 2>/dev/null || true; }

# --- T1: fresh clone -> behind 0, assert passes ------------------------------
echo "T1: fresh clone (master == origin/master)"
git_q "$CLONE" fetch origin master
behind=$(clone_behind_count "$CLONE")
[[ "$behind" == "0" ]] && ok "clone_behind_count == 0 when current" || fail "behind=$behind, expected 0"
if assert_clone_fresh "$CLONE" 2>/dev/null; then ok "assert_clone_fresh exits 0 when current"; else fail "assert refused a fresh clone"; fi

# --- T2: origin ahead by 2 -> behind 2, assert refuses (no fetch in helper) ---
echo "T2: origin advanced by 2 commits, clone fetched but not merged"
push_new_commit a
push_new_commit b
git_q "$CLONE" fetch origin master   # origin/master ref now ahead; master unchanged
behind=$(clone_behind_count "$CLONE")
[[ "$behind" == "2" ]] && ok "clone_behind_count == 2" || fail "behind=$behind, expected 2"
if assert_clone_fresh "$CLONE" 2>/dev/null; then fail "assert passed a stale clone"; else ok "assert_clone_fresh refuses (exit 1) when behind"; fi

# --- T3: advance_main_clone fast-forwards a clean on-master clone -------------
echo "T3: advance_main_clone ff-only advances a clean on-master clone"
reset_rate_limit
advance_main_clone "$CLONE" 2>/dev/null
behind=$(clone_behind_count "$CLONE")
[[ "$behind" == "0" ]] && ok "clone is current after advance (behind 0)" || fail "behind=$behind after advance, expected 0"
local_head=$(git -C "$CLONE" rev-parse master)
origin_head=$(git -C "$CLONE" rev-parse origin/master)
[[ "$local_head" == "$origin_head" ]] && ok "master == origin/master after advance" || fail "heads differ after advance"

# --- T4: rate-limit — second advance within 60s skips the fetch --------------
echo "T4: rate-limit suppresses the fetch within 60s"
push_new_commit c                       # origin moves ahead again
advance_main_clone "$CLONE" 2>/dev/null # sentinel fresh (T3) -> no fetch, no advance
head_after=$(git -C "$CLONE" rev-parse master)
[[ "$head_after" == "$local_head" ]] && ok "rate-limited call did not advance" || fail "advanced despite rate-limit"
reset_rate_limit
advance_main_clone "$CLONE" 2>/dev/null # sentinel cleared -> fetches + advances
behind=$(clone_behind_count "$CLONE")
[[ "$behind" == "0" ]] && ok "advances once rate-limit window cleared" || fail "behind=$behind after rate-limit reset"

# --- T5: off-master guard — never advance a checked-out feature branch --------
echo "T5: off-master guard"
git_q "$CLONE" checkout -b feature/x
feat_head=$(git -C "$CLONE" rev-parse HEAD)
push_new_commit d
reset_rate_limit
out=$(advance_main_clone "$CLONE" 2>&1 || true)
[[ "$(git -C "$CLONE" rev-parse HEAD)" == "$feat_head" ]] && ok "feature branch HEAD untouched" || fail "advance moved a feature branch"
echo "$out" | grep -q "not master" && ok "warns about non-master clone" || fail "no off-master warning: $out"
git_q "$CLONE" checkout master
git_q "$CLONE" branch -D feature/x

# --- T6: dirty guard — never clobber uncommitted work ------------------------
echo "T6: dirty working tree guard"
reset_rate_limit
advance_main_clone "$CLONE" 2>/dev/null   # land any pending origin commits first
clean_head=$(git -C "$CLONE" rev-parse master)
echo "uncommitted" >> "$CLONE/file"
push_new_commit e
reset_rate_limit
out=$(advance_main_clone "$CLONE" 2>&1 || true)
[[ "$(git -C "$CLONE" rev-parse master)" == "$clean_head" ]] && ok "dirty clone not advanced" || fail "advanced over uncommitted changes"
echo "$out" | grep -q "uncommitted changes" && ok "warns about dirty tree" || fail "no dirty warning: $out"
git_q "$CLONE" checkout -- file

# --- T7: diverged guard — refuse a non-fast-forwardable advance --------------
echo "T7: diverged guard"
reset_rate_limit
advance_main_clone "$CLONE" 2>/dev/null   # sync to origin first
echo "local-only" >> "$CLONE/file"        # commit a local-only change -> diverge
git_q "$CLONE" add file
git_q "$CLONE" commit -m local-only
diverged_head=$(git -C "$CLONE" rev-parse master)
push_new_commit f                          # origin also moves -> true divergence
reset_rate_limit
out=$(advance_main_clone "$CLONE" 2>&1 || true)
[[ "$(git -C "$CLONE" rev-parse master)" == "$diverged_head" ]] && ok "diverged clone not advanced" || fail "advanced a diverged clone"
echo "$out" | grep -q "diverged" && ok "warns about divergence" || fail "no diverge warning: $out"

# --- T8: missing repo is a safe no-op ----------------------------------------
echo "T8: nonexistent root is a safe no-op"
behind=$(clone_behind_count "$TMPROOT/does-not-exist")
[[ "$behind" == "0" ]] && ok "clone_behind_count 0 for missing repo" || fail "behind=$behind for missing repo"
if advance_main_clone "$TMPROOT/does-not-exist" 2>/dev/null; then ok "advance no-ops on missing repo"; else fail "advance errored on missing repo"; fi

# --- restore_main_clone_to_master (fleet-up-time restore) --------------------
# Fresh fixture: CLONE is left diverged by T7, so restore tests get their own.
CLONE2="$TMPROOT/clone2"
git clone -q "$ORIGIN" "$CLONE2"
git -C "$CLONE2" config user.email t@t
git -C "$CLONE2" config user.name test

# --- T9: parked on a feature branch, clean tree -> restored + ff-advanced ----
echo "T9: parked branch, clean tree -> checkout master + ff-advance"
git_q "$CLONE2" checkout -b claude/parked-pr
push_new_commit t9
git_q "$CLONE2" fetch origin master
out=$(restore_main_clone_to_master "$CLONE2" 2>&1 || true)
branch=$(git -C "$CLONE2" rev-parse --abbrev-ref HEAD)
[[ "$branch" == "master" ]] && ok "returned to master" || fail "still on $branch"
[[ "$(git -C "$CLONE2" rev-parse master)" == "$(git -C "$CLONE2" rev-parse origin/master)" ]] \
    && ok "master ff-advanced to origin/master" || fail "master not advanced"
echo "$out" | grep -q "returned to master" && ok "logs the restore" || fail "no restore log: $out"

# --- T10: parked branch WITH tracked modification -> untouched ----------------
echo "T10: parked branch with tracked WIP -> left alone"
git_q "$CLONE2" checkout -b claude/live-wip
echo "wip" >> "$CLONE2/file"
out=$(restore_main_clone_to_master "$CLONE2" 2>&1 || true)
branch=$(git -C "$CLONE2" rev-parse --abbrev-ref HEAD)
[[ "$branch" == "claude/live-wip" ]] && ok "branch untouched" || fail "branch switched to $branch"
grep -q "wip" "$CLONE2/file" && ok "WIP preserved" || fail "WIP lost"
echo "$out" | grep -q "live WIP wins" && ok "warns loudly" || fail "no WIP warning: $out"
git_q "$CLONE2" checkout -- file   # clean up for T11

# --- T11: parked branch with only untracked junk -> restored, junk kept ------
echo "T11: parked branch with untracked junk only -> restored"
touch "$CLONE2/.review-body.md"
out=$(restore_main_clone_to_master "$CLONE2" 2>&1 || true)
branch=$(git -C "$CLONE2" rev-parse --abbrev-ref HEAD)
[[ "$branch" == "master" ]] && ok "returned to master past untracked junk" || fail "still on $branch"
[[ -f "$CLONE2/.review-body.md" ]] && ok "untracked file preserved" || fail "untracked file lost"

# --- T12: detached HEAD, clean -> restored ------------------------------------
echo "T12: detached HEAD, clean tree -> restored to master"
git_q "$CLONE2" checkout --detach origin/master
out=$(restore_main_clone_to_master "$CLONE2" 2>&1 || true)
branch=$(git -C "$CLONE2" rev-parse --abbrev-ref HEAD)
[[ "$branch" == "master" ]] && ok "detached HEAD returned to master" || fail "HEAD is $branch"

# --- T13: missing repo is a safe no-op ----------------------------------------
echo "T13: nonexistent root is a safe no-op for restore"
if restore_main_clone_to_master "$TMPROOT/does-not-exist" 2>/dev/null; then
    ok "restore no-ops on missing repo"
else
    fail "restore errored on missing repo"
fi

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
