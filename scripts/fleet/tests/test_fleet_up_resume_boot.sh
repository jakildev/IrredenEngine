#!/usr/bin/env bash
# Tests for fleet-up's boot-time resume preservation:
#
#   reset_worktree — must NOT strip a clean-but-unpushed claude/<N>-* task
#   branch (crash after commit, before push): resetting it to scratch
#   strands the commits on a dangling ref and breaks fleet-dispatch-wrap's
#   branch-keyed in-flight check, so the interrupted session never resumes.
#   Scratch branches and fully-pushed branches still reset.
#
#   seed_sidecar_resume_triggers — a surviving *.session.json must seed a
#   dispatcher trigger for its role at boot, or a hard-killed session
#   strands until an organic projection change; unknown roles seed nothing.
#
# Both are exercised by sed-extracting the function from fleet-up (they are
# self-contained: git + $HOME/.fleet paths + FLEET_* env overrides only).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -x "$FLEET_UP" ]] || { echo "test setup: fleet-up not found at $FLEET_UP" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/reservations"

extract_fn() {  # extract_fn <name> — print the function body from fleet-up
    sed -n "/^$1() {/,/^}/p" "$FLEET_UP"
}
eval "$(extract_fn reset_worktree)"
eval "$(extract_fn seed_sidecar_resume_triggers)"
[[ "$(type -t reset_worktree)" == "function" ]] || { echo "test setup: reset_worktree not extracted" >&2; exit 1; }
[[ "$(type -t seed_sidecar_resume_triggers)" == "function" ]] || { echo "test setup: seed_sidecar_resume_triggers not extracted" >&2; exit 1; }

# --- git fixtures ---------------------------------------------------------
ORIGIN="$TMPROOT/origin.git"
git init --quiet --bare "$ORIGIN"
SEED="$TMPROOT/seed"
git clone --quiet "$ORIGIN" "$SEED" 2>/dev/null
(cd "$SEED" \
    && git -c user.email=t@t -c user.name=t commit --allow-empty -m init --quiet \
    && git branch -M master \
    && git push --quiet origin master)

new_checkout() {  # new_checkout <name> -> clone with origin/master fetched
    local dir="$TMPROOT/$1"
    git clone --quiet "$ORIGIN" "$dir" 2>/dev/null
    echo "$dir"
}
current_branch() { git -C "$1" rev-parse --abbrev-ref HEAD; }

echo "T1: clean-but-unpushed claude/<N>-* branch is preserved"
WT1=$(new_checkout wt1)
(cd "$WT1" \
    && git checkout -q -b claude/42-topic \
    && git -c user.email=t@t -c user.name=t commit --allow-empty -m work --quiet)
out=$(reset_worktree "$WT1" claude/pool-1-scratch)
assert_contains "$out" "preserving for resumption" "reports preservation"
assert_eq "$(current_branch "$WT1")" "claude/42-topic" "branch untouched"

echo "T2: fully-pushed claude/<N>-* branch still resets (no squash-merge pin)"
WT2=$(new_checkout wt2)
(cd "$WT2" \
    && git checkout -q -b claude/43-topic \
    && git -c user.email=t@t -c user.name=t commit --allow-empty -m work --quiet \
    && git push --quiet -u origin claude/43-topic 2>/dev/null)
reset_worktree "$WT2" claude/pool-2-scratch >/dev/null
assert_eq "$(current_branch "$WT2")" "claude/pool-2-scratch" "pushed branch reset to scratch"

echo "T3: scratch branch resets even with local commits (throwaway by definition)"
WT3=$(new_checkout wt3)
(cd "$WT3" \
    && git checkout -q -b claude/pool-3-scratch \
    && git -c user.email=t@t -c user.name=t commit --allow-empty -m scratchwork --quiet)
reset_worktree "$WT3" claude/pool-3-scratch >/dev/null
assert_eq "$(git -C "$WT3" rev-parse HEAD)" "$(git -C "$WT3" rev-parse origin/master)" \
    "scratch branch reset to origin/master"

echo "T4: reservation preserves regardless of branch state"
WT4=$(new_checkout wt4)
printf '{"task_id":"7"}\n' > "$HOME/.fleet/reservations/wt4.json"
out=$(reset_worktree "$WT4" claude/pool-4-scratch)
assert_contains "$out" "has reservation" "reservation guard fired"
assert_eq "$(current_branch "$WT4")" "master" "branch untouched under reservation"

echo "T5: sidecar seeds a trigger for its role at boot; unknown roles do not"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_SESSIONS_DIR"
printf '{"session_id":"S1","role":"worker"}\n'   > "$FLEET_SESSIONS_DIR/pool-1.session.json"
printf '{"session_id":"S2","role":"mystery"}\n'  > "$FLEET_SESSIONS_DIR/pool-2.session.json"
out=$(seed_sidecar_resume_triggers)
assert_contains "$out" "bootstrap-triggered worker" "worker trigger reported"
if [[ -f "$FLEET_STATE_DIR/triggers/worker" ]]; then
    ok "worker trigger file created"
else
    bad "worker trigger file missing"
fi
if [[ -f "$FLEET_STATE_DIR/triggers/mystery" ]]; then
    bad "unknown role seeded a trigger"
else
    ok "unknown role seeded nothing"
fi

summarize "fleet-up resume-boot preservation"
