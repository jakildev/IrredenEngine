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
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
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

# Isolate the rate-limit sentinel and the persistent-skip counter from the real
# ~/.fleet/state, and the escalation alert file from the real ~/.fleet/alerts.
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
mkdir -p "$FLEET_STATE_DIR" "$FLEET_ALERTS_DIR"

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

# Skip-counter + alert reset, so each escalation test starts from count 0.
reset_skip_counter() {
    rm -f "$FLEET_STATE_DIR"/.*-freshness-skip "$FLEET_ALERTS_DIR"/clone-freshness-* 2>/dev/null || true
}

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

# --- T5b: reviewer-scratch self-heal — clean tree parked on scratch heals ----
echo "T5b: reviewer-scratch self-heal (clean tree)"
reset_rate_limit
advance_main_clone "$CLONE" 2>/dev/null            # sync to origin first
git_q "$CLONE" checkout -B claude/opus-reviewer-scratch origin/master
push_new_commit s1
reset_rate_limit
out=$(advance_main_clone "$CLONE" 2>&1 || true)
[[ "$(git -C "$CLONE" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "clone put back on master" || fail "clone still on $(git -C "$CLONE" rev-parse --abbrev-ref HEAD)"
behind=$(clone_behind_count "$CLONE")
[[ "$behind" == "0" ]] && ok "advance continued after self-heal (behind 0)" || fail "behind=$behind after self-heal"
if git -C "$CLONE" rev-parse --verify --quiet refs/heads/claude/opus-reviewer-scratch >/dev/null; then fail "junk scratch branch not deleted"; else ok "junk scratch branch deleted"; fi
echo "$out" | grep -q "self-healed" && ok "logs the self-heal" || fail "no self-heal log: $out"

# --- T5c: reviewer-scratch + dirty tree — refuse self-heal, warn loudly ------
echo "T5c: reviewer-scratch with dirty tree is NOT self-healed"
git_q "$CLONE" checkout -B claude/sonnet-reviewer-scratch origin/master
echo "dirt" >> "$CLONE/file"
push_new_commit s2
reset_rate_limit
out=$(advance_main_clone "$CLONE" 2>&1 || true)
[[ "$(git -C "$CLONE" rev-parse --abbrev-ref HEAD)" == "claude/sonnet-reviewer-scratch" ]] && ok "dirty scratch clone left untouched" || fail "branch changed on dirty scratch clone"
echo "$out" | grep -q "FROZEN" && ok "loud frozen-master warning" || fail "no loud warning: $out"
git_q "$CLONE" checkout -- file
git_q "$CLONE" checkout master
git_q "$CLONE" branch -D claude/sonnet-reviewer-scratch

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

# --- T14: ALREADY on master, disjoint tracked dirty file -> NOT advanced -------
# The restore-side mirror of T6: a disjoint dirty tree would ff-advance fine on
# its own (the incoming commits touch a different file), so only the tracked-WIP
# guard stops it. Covers the guard firing on-master, not just on a parked
# branch. See #2378.
echo "T14: on master with disjoint tracked WIP -> master NOT advanced"
git_q "$CLONE2" checkout master
on_master_head=$(git -C "$CLONE2" rev-parse master)
echo "disjoint-wip" > "$CLONE2/other"   # tracked WIP on a file the incoming commit won't touch
git_q "$CLONE2" add other
push_new_commit t14                      # origin advances 'file' (disjoint from 'other')
out=$(restore_main_clone_to_master "$CLONE2" 2>&1 || true)
[[ "$(git -C "$CLONE2" rev-parse master)" == "$on_master_head" ]] \
    && ok "on-master disjoint-dirty clone NOT advanced" || fail "advanced master under uncommitted WIP"
grep -q "disjoint-wip" "$CLONE2/other" && ok "on-master WIP preserved" || fail "WIP lost"
echo "$out" | grep -q "live WIP wins" && ok "warns loudly on-master too" || fail "no WIP warning: $out"

# --- scratch-namespace self-heal + persistent-skip escalation (#2363) --------
# Fresh fixture again: CLONE is diverged (T7) and CLONE2 carries the T14 WIP.
CLONE3="$TMPROOT/clone3"
git clone -q "$ORIGIN" "$CLONE3"
git -C "$CLONE3" config user.email t@t
git -C "$CLONE3" config user.name test
COUNTER3="$FLEET_STATE_DIR/.$(basename "$CLONE3")-freshness-skip"
ALERT3="$FLEET_ALERTS_DIR/clone-freshness-$(basename "$CLONE3")"

# --- T15: live-Cursor-session shape -> NEVER healed --------------------------
# The regression guard for #2668's review: FLEET.md rule 1 puts Cursor / ad-hoc
# sessions on claude/<area>-<topic>, and commit-and-push leaves that session
# clean-and-pushed while the human sits on the PR. That HEAD is "recoverable"
# (contained by origin/<branch>) yet emphatically live, so recoverability must
# NOT authorize a heal — only the scratch namespace does.
echo "T15: clean, pushed claude/<area>-<topic> (live Cursor session) -> left alone"
reset_skip_counter
git_q "$CLONE3" checkout -b claude/render-glow-pulse
echo "branch-work" >> "$CLONE3/file"
git_q "$CLONE3" add file
git_q "$CLONE3" commit -m branch-work
git_q "$CLONE3" push -u origin claude/render-glow-pulse   # clean + pushed: the steady state
session_head=$(git -C "$CLONE3" rev-parse HEAD)
push_new_commit t15                                        # origin/master moves ahead
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "claude/render-glow-pulse" ]] && ok "live cursor-session park untouched" || fail "healed a live session onto $(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)"
[[ "$(git -C "$CLONE3" rev-parse HEAD)" == "$session_head" ]] && ok "session HEAD unmoved" || fail "HEAD moved under a live session"
! echo "$out" | grep -q "self-healed" && ok "no self-heal attempted" || fail "self-healed a live session: $out"
echo "$out" | grep -q "not master" && ok "warns (escalates) instead of healing" || fail "no park warning: $out"

# --- T16: pool scratch ref -> healed, junk ref dropped -----------------------
# The namespace that IS junk: claude/pool-<N>-scratch (and its game twin /
# the legacy claude/<role>-reviewer-scratch) only ever belongs in a worktree.
echo "T16: claude/pool-N-scratch park -> self-healed, junk ref deleted"
reset_skip_counter
git_q "$CLONE3" checkout master
reset_rate_limit
advance_main_clone "$CLONE3" 2>/dev/null                   # sync origin/master first
git_q "$CLONE3" checkout -B claude/pool-7-scratch origin/master
push_new_commit t16                                        # origin/master moves ahead
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "pool scratch park healed to master" || fail "still on $(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)"
behind=$(clone_behind_count "$CLONE3")
[[ "$behind" == "0" ]] && ok "advance continued after the heal (behind 0)" || fail "behind=$behind after heal"
if git -C "$CLONE3" rev-parse --verify --quiet refs/heads/claude/pool-7-scratch >/dev/null; then fail "junk scratch ref not deleted"; else ok "junk scratch ref deleted"; fi
[[ "$(echo "$out" | grep -c "self-healed")" == "1" ]] && ok "exactly one self-heal line" || fail "expected 1 self-heal line, got: $out"

# --- T17: scratch ref carrying a unique commit -> untouched ------------------
# Clean + in-namespace is not enough: a scratch ref holding a commit that
# origin/master does not contain is not provably junk, so leave it.
echo "T17: claude/*-scratch with a unique local commit -> left alone"
reset_skip_counter
git_q "$CLONE3" checkout -b claude/pool-8-scratch
echo "unpushed" >> "$CLONE3/file"
git_q "$CLONE3" add file
git_q "$CLONE3" commit -m unpushed
unpushed_head=$(git -C "$CLONE3" rev-parse HEAD)
push_new_commit t17
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "claude/pool-8-scratch" ]] && ok "scratch ref with unique commit untouched" || fail "branch switched to $(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)"
[[ "$(git -C "$CLONE3" rev-parse HEAD)" == "$unpushed_head" ]] && ok "unique commit preserved" || fail "HEAD moved under a unique commit"
echo "$out" | grep -q "not master" && ok "warns about the unhealed park" || fail "no park warning: $out"

# --- T18: scratch ref with a dirty tree -> untouched -------------------------
echo "T18: claude/*-scratch with a dirty tree -> left alone"
reset_skip_counter
git_q "$CLONE3" checkout master
reset_rate_limit
advance_main_clone "$CLONE3" 2>/dev/null                   # sync origin/master first
git_q "$CLONE3" checkout -B claude/pool-9-scratch origin/master
echo "dirt" >> "$CLONE3/file"
push_new_commit t18
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "claude/pool-9-scratch" ]] && ok "dirty scratch park untouched" || fail "branch switched on a dirty scratch park"
echo "$out" | grep -q "not master" && ok "warns about the dirty park" || fail "no park warning: $out"
git_q "$CLONE3" checkout -- file

# --- T19: persistent identical skip escalates once, then goes quiet ----------
echo "T19: persistent skip -> one ESCALATION + alert file, then silence"
reset_skip_counter
git_q "$CLONE3" checkout master
git_q "$CLONE3" checkout -b feature/persistent   # non-claude/*: never healed
export FLEET_FRESHNESS_SKIP_ESCALATE_N=3
reset_rate_limit; esc1=$(advance_main_clone "$CLONE3" 2>&1 || true)
reset_rate_limit; esc2=$(advance_main_clone "$CLONE3" 2>&1 || true)
reset_rate_limit; esc3=$(advance_main_clone "$CLONE3" 2>&1 || true)
# Snapshot the alert AT the escalation tick: ticks past N deliberately refresh
# it (T22), so its count= advances from here on.
alert_at_n=$(cat "$ALERT3" 2>/dev/null || true)
reset_rate_limit; esc4=$(advance_main_clone "$CLONE3" 2>&1 || true)
echo "$esc1" | grep -q "not master" && ! echo "$esc1" | grep -q "ESCALATION" && ok "tick 1 warns normally" || fail "tick 1 wrong: $esc1"
echo "$esc2" | grep -q "not master" && ! echo "$esc2" | grep -q "ESCALATION" && ok "tick 2 warns normally" || fail "tick 2 wrong: $esc2"
echo "$esc3" | grep -q "ESCALATION" && ok "tick 3 (== N) escalates" || fail "tick 3 did not escalate: $esc3"
[[ -n "$alert_at_n" ]] && ok "alert file written" || fail "no alert file at $ALERT3"
echo "$alert_at_n" | grep -q "reason=parked" && ok "alert records reason=parked" || fail "alert missing reason: $alert_at_n"
echo "$alert_at_n" | grep -q "count=3" && ok "alert records count=3" || fail "alert missing count: $alert_at_n"
echo "$alert_at_n" | grep -q "branch=feature/persistent" && ok "alert records the parked branch" || fail "alert missing branch: $alert_at_n"
[[ -z "$esc4" ]] && ok "tick 4 (> N) is silent" || fail "tick 4 still warned: $esc4"

# --- T20: a different skip key restarts the count ---------------------------
echo "T20: changing the skip condition re-arms the warning"
git_q "$CLONE3" checkout -b feature/other
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
echo "$out" | grep -q "not master" && ok "new key warns again (not suppressed)" || fail "new key stayed suppressed: $out"
! echo "$out" | grep -q "ESCALATION" && ok "new key does not re-escalate immediately" || fail "re-escalated on a fresh key: $out"

# --- T21: clearing the condition removes the counter and the alert ----------
echo "T21: healthy pass clears the counter + alert file"
git_q "$CLONE3" checkout master
push_new_commit t21
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
behind=$(clone_behind_count "$CLONE3")
[[ "$behind" == "0" ]] && ok "clone advanced once back on master" || fail "behind=$behind after restoring master"
[[ ! -f "$COUNTER3" ]] && ok "skip counter removed on the healthy pass" || fail "counter survived: $(cat "$COUNTER3" 2>/dev/null)"
[[ ! -f "$ALERT3" ]] && ok "alert file removed on the healthy pass" || fail "alert survived: $(cat "$ALERT3" 2>/dev/null)"

# --- T22: the alert is refreshed past N, not stamped once --------------------
# Past N stderr goes quiet, so the alert file is the ONLY standing signal. If it
# were written once at count == N, a human triaging ~/.fleet/alerts would
# silence a still-live condition forever, and count= would freeze at the
# escalation instant. Delete it mid-outage and assert it comes back, current.
echo "T22: alert refreshes past N (cleared inbox re-arms, count stays honest)"
reset_skip_counter
git_q "$CLONE3" checkout -b feature/refresh
export FLEET_FRESHNESS_SKIP_ESCALATE_N=2
reset_rate_limit; advance_main_clone "$CLONE3" 2>/dev/null || true   # count=1
reset_rate_limit; advance_main_clone "$CLONE3" 2>/dev/null || true   # count=2 == N
[[ -f "$ALERT3" ]] && ok "alert written at N" || fail "no alert at N"
rm -f "$ALERT3"                                                      # human clears the inbox
reset_rate_limit; out=$(advance_main_clone "$CLONE3" 2>&1 || true)   # count=3 > N
[[ -f "$ALERT3" ]] && ok "alert re-created past N" || fail "alert stayed gone past N — condition permanently silent"
grep -q "count=3" "$ALERT3" 2>/dev/null && ok "refreshed alert carries the current count" || fail "stale count: $(cat "$ALERT3" 2>/dev/null)"
[[ -z "$out" ]] && ok "refresh stays silent on stderr past N" || fail "tick past N warned: $out"
git_q "$CLONE3" checkout master
unset FLEET_FRESHNESS_SKIP_ESCALATE_N

# --- T23: a refused `branch -D` is reported honestly, not as a delete --------
# The delete is refusable two ways: another worktree holds the ref (the live
# pool worktree owning claude/pool-<N>-scratch, reachable through the
# checkout-master→delete window or a stale worktree admin entry), or a
# refs/heads lock survives a killed git process — the fixture here. The heal
# itself still succeeded, so the line must say the ref was LEFT: an operator
# reading it mid-outage acts on that claim. See #2668.
echo "T23: heal with a refused branch -D -> reports the ref as left, not deleted"
reset_skip_counter
git_q "$CLONE3" checkout master
reset_rate_limit
advance_main_clone "$CLONE3" 2>/dev/null                   # sync origin/master first
git_q "$CLONE3" checkout -B claude/pool-6-scratch origin/master
push_new_commit t23                                        # origin/master moves ahead
: > "$CLONE3/.git/refs/heads/claude/pool-6-scratch.lock"   # killed-git-process leftover
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "healed to master despite the refused delete" || fail "still on $(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)"
if git -C "$CLONE3" rev-parse --verify --quiet refs/heads/claude/pool-6-scratch >/dev/null; then ok "ref survives the refusal (fixture reproduces it)"; else fail "ref deleted — fixture did not reproduce the refusal"; fi
echo "$out" | grep -q "left the junk branch in place" && ok "line reports the ref as left" || fail "wrong heal line: $out"
! echo "$out" | grep -q "deleted the junk branch" && ok "no false delete claim" || fail "claimed a delete git refused: $out"
behind=$(clone_behind_count "$CLONE3")
[[ "$behind" == "0" ]] && ok "advance continued past the refusal (behind 0)" || fail "behind=$behind after the refused delete"
rm -f "$CLONE3/.git/refs/heads/claude/pool-6-scratch.lock"

# --- T24: the namespace conjunct, isolated from the containment one ----------
# T15's session carries a commit origin/master does not, so the containment
# check alone already refuses it — leaving the scratch-namespace half of the
# predicate unproven. A session branched at origin/master's tip with nothing
# committed yet is clean AND contained, so only the namespace glob can refuse
# it. That shape is the common one (branch, get pulled away, come back), and
# healing it switches the human's branch out from under them: recoverable is
# not idle. See #2668.
echo "T24: clean claude/<area>-<topic> at origin/master's tip -> left alone"
reset_skip_counter
git_q "$CLONE3" checkout master
reset_rate_limit
advance_main_clone "$CLONE3" 2>/dev/null                   # sync origin/master first
git_q "$CLONE3" checkout -B claude/editor-timeline-polish origin/master
session_head=$(git -C "$CLONE3" rev-parse HEAD)
push_new_commit t24                                        # origin/master moves ahead
reset_rate_limit
out=$(advance_main_clone "$CLONE3" 2>&1 || true)
[[ "$(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)" == "claude/editor-timeline-polish" ]] && ok "contained-HEAD session park untouched" || fail "healed a contained session onto $(git -C "$CLONE3" rev-parse --abbrev-ref HEAD)"
[[ "$(git -C "$CLONE3" rev-parse HEAD)" == "$session_head" ]] && ok "session HEAD unmoved" || fail "HEAD moved under a contained session"
! echo "$out" | grep -q "self-healed" && ok "namespace refuses it (containment alone would not)" || fail "self-healed a contained session: $out"
echo "$out" | grep -q "not master" && ok "warns (escalates) instead of healing" || fail "no park warning: $out"
git_q "$CLONE3" checkout master

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
