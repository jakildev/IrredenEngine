#!/usr/bin/env bash
# Tests for fleet-rebase's inherited-prefix drop (issue #1690).
#
# Reproduces the squash-merged-base stack shape that burned three Opus
# semantic-conflict iterations in 24h (#1631, #1638, #1626): a stacked
# child PR whose branch still carries its base PR's pre-squash commits.
# After the base squash-merges and its branch is deleted, the merger
# retargets the child to master. A plain `git rebase origin/master` then
# replays the inherited commits onto the squash that already holds their
# content — empty-or-conflicting — and even when git drops them the
# changed-file-set guard reads the vanished inherited files as drift, so
# tier-0 bails to the LLM pass. The fix rebases --onto master from the
# inherited boundary (the base PR's pre-merge head), replaying only the
# child's own commits.
#
#   T1: inherited-prefix conflicts with the squash, child's own commit is
#       clean → tier-0 drops the prefix and clears it (cleared=1).
#   T2: child's OWN commit conflicts with master → still bails to the LLM
#       pass (cleared=0, llm_remaining=1) — genuine conflicts unaffected.
#   T3: parent EDITED during review after the child forked, so its recorded
#       pre-merge head is a sibling of the child head and the #1690
#       is-ancestor gate fails (#1824 / #1789-on-#1782). The fork-point
#       fallback derives the boundary from merge-base(child, parent-head) and
#       still drops the inherited prefix (cleared=1).
#
# All run --auto --dry-run, so the rebase + changed-file-set guard run
# fully but nothing is pushed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "test setup: fleet-rebase not executable at $REBASE" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""

cleanup() {
    # Prune the linked scratch worktree before nuking the tree so git
    # doesn't leave a dangling worktree registration in any shared object
    # store (the sandbox repo is self-contained, but be tidy).
    if [[ -n "$TMPROOT" && -d "$TMPROOT" ]]; then
        rm -rf "$TMPROOT"
    fi
}
trap cleanup EXIT

# --- Sandbox ---------------------------------------------------------------
TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"          # fleet-rebase derives ENGINE from $HOME
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin" "$TMPROOT/merged" "$TMPROOT/log"

# No global git identity under the fresh HOME — give the sandbox one.
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

git_q() { git "$@" >/dev/null 2>&1; }

REMOTE="$TMPROOT/remote.git"
AUTH="$TMPROOT/auth"
ENGINE="$TMPROOT/src/IrredenEngine"

git_q init --bare -b master "$REMOTE"

# Authoring clone: build the topology and push it to the bare remote.
git_q clone "$REMOTE" "$AUTH"
git -C "$AUTH" config commit.gpgsign false

# init commit on master.
echo init > "$AUTH/readme.txt"
git -C "$AUTH" add readme.txt
git -C "$AUTH" commit -q -m "init"
INIT=$(git -C "$AUTH" rev-parse HEAD)
git_q -C "$AUTH" push origin master

# T1 child: init -> B1 (inherited: add base.txt "v1") -> C1 (own: add child.txt)
git_q -C "$AUTH" checkout -b feat-child "$INIT"
echo "v1" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "B1 inherited add base.txt"
B1=$(git -C "$AUTH" rev-parse HEAD)
echo "child work" > "$AUTH/child.txt"
git -C "$AUTH" add child.txt
git -C "$AUTH" commit -q -m "C1 own add child.txt"
git_q -C "$AUTH" push origin feat-child

# T2 child: init -> B1b (inherited: add base.txt "v1") -> C2 (own: EDIT base.txt)
git_q -C "$AUTH" checkout -b feat-child2 "$INIT"
echo "v1" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "B1b inherited add base.txt"
B1B=$(git -C "$AUTH" rev-parse HEAD)
echo "own conflicting edit" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "C2 own edit base.txt"
git_q -C "$AUTH" push origin feat-child2

# T3 (#1789 shape — parent EDITED during review AFTER the child forked).
# P_FORK (inherited: add base.txt "v1") is the SHARED fork commit; the child
# adds child3.txt on top, while the parent gains a SECOND commit P_EDIT only
# after the child forked. So the recorded parent head (P_EDIT) is a sibling
# of — not an ancestor of — the child head, defeating the #1690 is-ancestor
# gate; the fork-point fallback derives merge-base(child, P_EDIT) == P_FORK.
git_q -C "$AUTH" checkout -b feat-base3 "$INIT"
echo "v1" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "P_fork inherited add base.txt"
P_FORK=$(git -C "$AUTH" rev-parse HEAD)
git_q -C "$AUTH" checkout -b feat-child3 "$P_FORK"
echo "child3 work" > "$AUTH/child3.txt"
git -C "$AUTH" add child3.txt
git -C "$AUTH" commit -q -m "C3 own add child3.txt"
git_q -C "$AUTH" push origin feat-child3
# Parent's review edit, layered on the fork point AFTER the child forked. The
# branch is never pushed as a head (it is "merged + deleted"), but GitHub
# retains its pre-merge head as refs/pull/<n>/head — push that so the fix's
# `git fetch origin refs/pull/<n>/head` can realize the otherwise-absent SHA.
git_q -C "$AUTH" checkout feat-base3
echo "v1 amended during review" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "P_edit parent amended during review"
P_EDIT=$(git -C "$AUTH" rev-parse HEAD)
git_q -C "$AUTH" push origin "$P_EDIT:refs/pull/197/head"

# Squash the base PR's content onto master with a DIFFERENT body than the
# inherited commit — this is the "merged with amendments" case that
# defeats git's patch-id empty-commit dropping and forces the conflict.
git_q -C "$AUTH" checkout master
echo "v2 squashed-with-amendments" > "$AUTH/base.txt"
git -C "$AUTH" add base.txt
git -C "$AUTH" commit -q -m "S squash-merge of base PR (amended)"
git_q -C "$AUTH" push origin master

# feat-base / feat-base2 are never pushed: they are "merged + deleted", so
# fleet-rebase sees the base branch as gone and takes the retarget path.

# Engine clone fleet-rebase operates on (origin -> bare remote).
git_q clone "$REMOTE" "$ENGINE"
git -C "$ENGINE" config commit.gpgsign false

# Merged-base lookups, keyed by head branch. The stub gh emulates the
# net output of `gh pr list ... --json number,headRefOid --jq '<tsv>'`.
printf '199\t%s\n' "$B1"  > "$TMPROOT/merged/feat-base.tsv"
printf '198\t%s\n' "$B1B" > "$TMPROOT/merged/feat-base2.tsv"
# T3: the merged parent reports its EDITED head P_EDIT (not the fork commit),
# which is a sibling of the child head — the #1690 gate fails on it.
printf '197\t%s\n' "$P_EDIT" > "$TMPROOT/merged/feat-base3.tsv"

# --- Stub gh ---------------------------------------------------------------
# Only the merged-base lookup is exercised in --dry-run (the retarget
# `gh pr edit` and the pre-push `gh pr view` are gated behind non-dry-run).
export MERGED_DIR="$TMPROOT/merged"
cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
# Test stub: emulate `gh pr list --head <h> --state merged` by echoing the
# pre-baked "<number>\t<headRefOid>" tsv for that head. Everything else is
# a silent success so set -e in fleet-rebase never trips on the stub.
args="$*"
if [[ "$1" == "pr" && "$2" == "list" ]]; then
    head=""
    prev=""
    for a in "$@"; do
        [[ "$prev" == "--head" ]] && head="$a"
        prev="$a"
    done
    f="$MERGED_DIR/$head.tsv"
    [[ -f "$f" ]] && cat "$f"
    exit 0
fi
exit 0
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

write_slice() {
    # $1 = JSON array literal of PR records
    printf '{"prs": %s}\n' "$1" > "$FLEET_STATE_DIR/projections/merger.json"
}

run_rebase() {
    # Echo combined stdout+stderr (fleet-rebase logs to stderr).
    "$REBASE" --auto --dry-run 2>&1 || true
}

# === T1: inherited-prefix drop resolves a squash-merged stacked child =====
echo "T1: inherited prefix conflicts with squash, own commit clean -> tier-0 drops + clears"
write_slice '[{"repo":"engine","number":200,"headRefName":"feat-child","baseRefName":"feat-base","mergeable":"CONFLICTING","labels":["fleet:approved"]}]'
T1=$(run_rebase)
assert_contains "$T1" "retargeting PR to master" "T1 took the retarget path"
assert_contains "$T1" "dropping inherited prefix at ${B1:0:12}" "T1 dropped the inherited prefix at the boundary"
assert_contains "$T1" "attempted=1 cleared=1 merged=0 llm_remaining=0" "T1 tier-0 cleared the PR (no LLM bail)"
assert_absent   "$T1" "changed-file set drifted" "T1 no spurious file-set drift"
assert_absent   "$T1" "conflicts; leaving for LLM" "T1 no conflict bail"

# === T2: a genuine own-commit conflict still goes to the LLM pass ==========
echo "T2: child's own commit conflicts with master -> still bails to LLM"
write_slice '[{"repo":"engine","number":201,"headRefName":"feat-child2","baseRefName":"feat-base2","mergeable":"CONFLICTING","labels":["fleet:approved"]}]'
T2=$(run_rebase)
assert_contains "$T2" "dropping inherited prefix at ${B1B:0:12}" "T2 still attempts the prefix drop"
assert_contains "$T2" "(inherited-prefix drop) conflicts; leaving for LLM" "T2 own-commit conflict bails to LLM"
assert_contains "$T2" "attempted=1 cleared=0 merged=0 llm_remaining=1" "T2 left for the LLM pass"

# === T3: parent edited after the child forked -> fork-point fallback drops ==
echo "T3: parent amended post-fork (recorded head is a sibling) -> fork-point fallback drops + clears"
write_slice '[{"repo":"engine","number":202,"headRefName":"feat-child3","baseRefName":"feat-base3","mergeable":"CONFLICTING","labels":["fleet:approved"]}]'
T3=$(run_rebase)
assert_contains "$T3" "retargeting PR to master" "T3 took the retarget path"
assert_contains "$T3" "parent edited post-fork; deriving inherited boundary from child fork point at ${P_FORK:0:12}" "T3 used the fork-point fallback at the boundary"
assert_absent   "$T3" "dropping inherited prefix at" "T3 did NOT take the is-ancestor fast path"
assert_contains "$T3" "attempted=1 cleared=1 merged=0 llm_remaining=0" "T3 tier-0 cleared the PR (no LLM bail)"
assert_absent   "$T3" "changed-file set drifted" "T3 no spurious file-set drift"
assert_absent   "$T3" "conflicts; leaving for LLM" "T3 no conflict bail"

# --- Summary ---------------------------------------------------------------
summarize "fleet-rebase inherited-drop tests"
