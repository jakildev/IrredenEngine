#!/usr/bin/env bash
# Tests for fleet-rebase's retired-path conflict resolution (RETIRED_PATH_RE).
#
# master no longer carries .fleet/plans/ — a plan lives on its issue — but a
# PR opened before the deletion still edits files there, so its rebase stops
# on a modify/delete conflict per such file. Taking master's deletion is the
# only correct resolution, so tier-0 applies it and continues instead of
# handing a judgment-free conflict to the LLM pass. Anything else that
# conflicts still aborts to the LLM pass, and a PR that rebases to nothing is
# never force-pushed hollow.
#
#   T1: plan edits + a source file across two commits (one plan-only) -> the
#       plan paths are dropped, the plan-only commit is skipped, the branch
#       is pushed carrying only the source change; cleared=1, no re-arm.
#   T2: plan-only PR -> rebases to nothing; not pushed, human_remaining=1,
#       no LLM re-arm.
#   T3: plan conflict alongside a real source conflict -> abort, LLM re-arm,
#       scratch worktree left with no rebase in progress.
#   T4: --dry-run on T1's shape -> the drop is logged, nothing is pushed.
#   T5: the conflicted path still exists on master (a content conflict
#       under the retired prefix, not a deletion) -> abort, LLM re-arm.
#
# Real git throughout: a bare remote, an author clone that builds the
# branches, and the engine clone fleet-rebase carves its scratch worktree
# from. The gh stub answers only the pre-push preflight's `pr view` shape
# and fails closed on anything else.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "SKIP: fleet-rebase not executable at $REBASE" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

# --- Sandbox ------------------------------------------------------------------
TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
export GH_STUB_LOG="$TMPROOT/gh-calls.log"
TRIGGER="$FLEET_STATE_DIR/triggers/merger"
SCRATCH="$FLEET_REBASE_SCRATCH/engine"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin"
touch "$GH_STUB_LOG"

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

REMOTE="$TMPROOT/remote.git"
ENGINE="$TMPROOT/src/IrredenEngine"
AUTH="$TMPROOT/auth"

git init -q --bare -b master "$REMOTE"
git clone -q "$REMOTE" "$AUTH"
git -C "$AUTH" config commit.gpgsign false

# master before the retirement: three plan files and a source file.
mkdir -p "$AUTH/.fleet/plans"
echo "plan one" > "$AUTH/.fleet/plans/issue-1.md"
echo "plan two" > "$AUTH/.fleet/plans/issue-2.md"
echo "plan three" > "$AUTH/.fleet/plans/issue-3.md"
echo "source" > "$AUTH/readme.txt"
git -C "$AUTH" add -A
git -C "$AUTH" commit -q -m "init with plans"
git -C "$AUTH" push -q origin master

# Every PR branch forks from the pre-retirement master.
new_branch() { git -C "$AUTH" checkout -q -b "$1" master; }
commit_all() { git -C "$AUTH" add -A && git -C "$AUTH" commit -q -m "$1"; }
push_branch() { git -C "$AUTH" push -q origin "$1"; }

# Two branches of T1's shape (T4 needs a pristine one after T1 pushes).
for name in feat-mixed feat-mixed-dry; do
    new_branch "$name"
    echo "plan one edited" > "$AUTH/.fleet/plans/issue-1.md"
    echo "new source" > "$AUTH/src.txt"
    commit_all "plan edit + source"
    echo "plan two edited" > "$AUTH/.fleet/plans/issue-2.md"
    commit_all "plan-only edit"
    push_branch "$name"
done

new_branch feat-plan-only
echo "plan one edited" > "$AUTH/.fleet/plans/issue-1.md"
commit_all "plan-only edit"
push_branch feat-plan-only

new_branch feat-real-conflict
echo "plan one edited" > "$AUTH/.fleet/plans/issue-1.md"
echo "branch source" > "$AUTH/readme.txt"
commit_all "plan edit + conflicting source edit"
push_branch feat-real-conflict

new_branch feat-content
echo "plan three edited on the branch" > "$AUTH/.fleet/plans/issue-3.md"
commit_all "edit a plan master keeps"
push_branch feat-content

# master after the retirement: two plans deleted, one kept but rewritten
# (a content conflict under the retired prefix is NOT a deletion to take),
# and the source file rewritten (a real conflict for T3).
git -C "$AUTH" checkout -q master
git -C "$AUTH" rm -q .fleet/plans/issue-1.md .fleet/plans/issue-2.md
echo "plan three rewritten on master" > "$AUTH/.fleet/plans/issue-3.md"
echo "master source" > "$AUTH/readme.txt"
commit_all "retire plan files"
push_branch master

git clone -q "$REMOTE" "$ENGINE"
git -C "$ENGINE" config commit.gpgsign false

# gh stub: models the pre-push preflight only — `gh pr view <N> --repo <slug>
# --json state,baseRefName,labels --jq <program>` — and fails closed on any
# other call shape, so nothing the script might grow can slip past unmodelled.
cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
echo "$*" >> "$GH_STUB_LOG"
if [[ $# -eq 9 && "$1" == "pr" && "$2" == "view" && "$3" =~ ^[0-9]+$ \
      && "$4" == "--repo" && "$6" == "--json" \
      && "$7" == "state,baseRefName,labels" && "$8" == "--jq" ]]; then
    printf 'OPEN\tmaster\tfleet:approved\n'
    exit 0
fi
echo "gh stub: unmodelled call: $*" >&2
exit 1
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

# slice_for <number> <head>
slice_for() {
    printf '{"prs": [{"repo":"engine","number":%s,"headRefName":"%s","baseRefName":"master","mergeable":"CONFLICTING","labels":["fleet:approved"]}]}\n' \
        "$1" "$2" > "$FLEET_STATE_DIR/projections/merger.json"
}
reset_run() { rm -f "$TRIGGER"; : > "$GH_STUB_LOG"; }
remote_sha() { git -C "$AUTH" fetch -q origin && git -C "$AUTH" rev-parse "origin/$1"; }
assert_no_rebase_in_progress() {
    if [[ -d "$(git -C "$SCRATCH" rev-parse --git-path rebase-merge)" ]]; then
        bad "$1 (rebase-merge dir still present in the scratch worktree)"
    else
        ok "$1"
    fi
}

# === T1 ======================================================================
echo "T1: plan edits + source change -> deletions taken, plan-only commit skipped, pushed"
reset_run
slice_for 400 feat-mixed
T1=$("$REBASE" --auto --rearm-trigger 2>&1 || true)
assert_contains "$T1" "engine#400: took origin/master's deletion of retired path(s) [.fleet/plans/issue-1.md .fleet/plans/issue-2.md] to finish the rebase" \
    "T1 logs every dropped path"
assert_contains "$T1" "engine#400: rebased onto origin/master and pushed" "T1 pushed the rebased branch"
assert_contains "$T1" "attempted=1 cleared=1 llm_remaining=0 human_remaining=0" "T1 counted as cleared"
assert_contains "$(cat "$GH_STUB_LOG")" "pr view 400 --repo jakildev/IrredenEngine" "T1 pre-push preflight ran"
remote_sha feat-mixed >/dev/null
assert_eq "$(git -C "$AUTH" diff --name-only origin/master origin/feat-mixed)" "src.txt" \
    "T1 the pushed branch differs from master by the source file only"
assert_eq "$(git -C "$AUTH" rev-list --count origin/master..origin/feat-mixed)" "1" \
    "T1 the plan-only commit was skipped, not replayed empty"
assert_eq "$(git -C "$AUTH" show origin/feat-mixed:src.txt)" "new source" "T1 the source change survived"
if [[ -e "$TRIGGER" ]]; then bad "T1 no trigger written"; else ok "T1 no trigger written"; fi

# === T2 ======================================================================
echo "T2: plan-only PR -> rebases to nothing; not pushed, human's call"
reset_run
before=$(remote_sha feat-plan-only)
slice_for 401 feat-plan-only
T2=$("$REBASE" --auto --rearm-trigger 2>&1 || true)
assert_contains "$T2" "engine#401: every commit touched only retired paths; nothing left to push" \
    "T2 names the hollow-PR outcome"
assert_contains "$T2" "attempted=1 cleared=0 llm_remaining=0 human_remaining=1" \
    "T2 counted as human_remaining, not LLM work"
assert_eq "$(remote_sha feat-plan-only)" "$before" "T2 remote branch untouched"
assert_absent "$(cat "$GH_STUB_LOG")" "pr view 401" "T2 never reached the pre-push preflight"
if [[ -e "$TRIGGER" ]]; then bad "T2 no LLM re-arm"; else ok "T2 no LLM re-arm"; fi

# === T3 ======================================================================
echo "T3: plan conflict + real source conflict -> abort to the LLM pass"
reset_run
before=$(remote_sha feat-real-conflict)
slice_for 402 feat-real-conflict
T3=$("$REBASE" --auto --rearm-trigger 2>&1 || true)
assert_contains "$T3" "engine#402: rebase onto origin/master conflicts; leaving for LLM" "T3 aborted"
assert_absent "$T3" "took origin/master's deletion" "T3 a mixed conflict set takes nothing"
assert_contains "$T3" "llm_remaining=1" "T3 counted as LLM work"
assert_eq "$(remote_sha feat-real-conflict)" "$before" "T3 remote branch untouched"
assert_no_rebase_in_progress "T3 scratch worktree left clean for the next PR"
if [[ -f "$TRIGGER" && "$(cat "$TRIGGER")" == "llm" ]]; then ok "T3 LLM pass re-armed"; else bad "T3 LLM pass re-armed"; fi

# === T4 ======================================================================
echo "T4: --dry-run on T1's shape -> drop logged, nothing pushed"
reset_run
before=$(remote_sha feat-mixed-dry)
slice_for 403 feat-mixed-dry
T4=$("$REBASE" --auto --dry-run 2>&1 || true)
assert_contains "$T4" "engine#403: took origin/master's deletion of retired path(s)" "T4 drop logged"
assert_contains "$T4" "engine#403: clean rebase onto origin/master (dry-run; not pushed)" "T4 dry-run outcome"
assert_eq "$(remote_sha feat-mixed-dry)" "$before" "T4 remote branch untouched"
assert_absent "$(cat "$GH_STUB_LOG")" "pr view 403" "T4 no preflight in dry-run"

# === T5 ======================================================================
echo "T5: conflicted retired path still exists on master -> not a deletion, abort"
reset_run
before=$(remote_sha feat-content)
slice_for 404 feat-content
T5=$("$REBASE" --auto --rearm-trigger 2>&1 || true)
assert_contains "$T5" "engine#404: rebase onto origin/master conflicts; leaving for LLM" "T5 aborted"
assert_absent "$T5" "took origin/master's deletion" "T5 a content conflict is never resolved by deletion"
assert_contains "$T5" "llm_remaining=1" "T5 counted as LLM work"
assert_eq "$(remote_sha feat-content)" "$before" "T5 remote branch untouched"
assert_no_rebase_in_progress "T5 scratch worktree left clean"

summarize "fleet-rebase retired-path conflict tests"
