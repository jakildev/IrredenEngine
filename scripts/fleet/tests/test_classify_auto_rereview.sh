#!/usr/bin/env bash
# Tests for scripts/fleet/classify-auto-rereview.sh (engine #2290 / game #229).
#
# Each case builds a throwaway git repo mirroring a real synchronize-event
# graph, invokes the classify script with the event's BEFORE/AFTER/BASE_SHA,
# and asserts the emitted rebase_only decision:
#
#   T1  stacked child retargeted onto master after the parent SQUASH-merges
#       -> mechanical (the headline bug: this must NOT strip fleet:approved)
#   T2  in-place catch-up rebase onto an advanced master, child diff unchanged
#       -> mechanical (must stay correct — the case the old logic handled)
#   T3  real content change (amended tip commit) -> re-review
#   T4  BEFORE commit unavailable (orphaned, unfetchable) -> re-review (safe)
#   T5  plain force-push, identical tree, new SHA -> mechanical
#
# Hermetic: no live GitHub, no origin remote, no ~/.fleet. The script's
# best-effort `git fetch origin` fails closed to the local objects we build.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$SCRIPT_DIR/classify-auto-rereview.sh"

if [[ ! -f "$SCRIPT" ]]; then
    echo "SKIP: script not found at $SCRIPT" >&2
    exit 0
fi
if ! command -v git >/dev/null 2>&1; then
    echo "SKIP: git not available" >&2
    exit 0
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

# Deterministic commit identity + dates so SHAs don't depend on wall-clock.
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=t@t
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=t@t
export GIT_AUTHOR_DATE="2026-01-01T00:00:00 +0000"
export GIT_COMMITTER_DATE="2026-01-01T00:00:00 +0000"
# Never let rebase/commit open an editor.
export GIT_EDITOR=true GIT_SEQUENCE_EDITOR=true

new_repo() {  # $1 = name -> echoes path
    local d="$TMPROOT/$1"
    git -c init.defaultBranch=master init -q "$d"
    ( cd "$d" && echo base > base.txt && git add base.txt && git commit -qm M0 )
    echo "$d"
}

add() {  # $1 = repo  $2 = file  $3 = msg
    ( cd "$1" && echo "$2" > "$2" && git add "$2" && git commit -qm "$3" )
}

# Run the classifier in $1 with the given event SHAs; echoes the decision line.
classify() {  # $1 = repo  BEFORE AFTER BASE_SHA
    ( cd "$1" && BEFORE="$2" AFTER="$3" BASE_SHA="$4" bash "$SCRIPT" 2>/dev/null )
}

expect() {  # $1 = label  $2 = got  $3 = want
    if [[ "$2" == "$3" ]]; then ok "$1 -> $2"; else fail "$1: got '$2' want '$3'"; fi
}

# --- T1: stacked child retargeted after parent SQUASH-merge ------------------
echo "T1: retarget-after-squash-merge (the bug) -> rebase_only=true"
R=$(new_repo t1)
git -C "$R" checkout -q -b parent
add "$R" parent1.txt p1
add "$R" parent2.txt p2
PARENT_TIP=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q -b child
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)                 # child on the parent branch
git -C "$R" checkout -q master
git -C "$R" merge --squash -q parent >/dev/null      # squash parent's changes...
git -C "$R" commit -qm "squash parent (#PR)"         # ...as one commit; p1/p2 gone
BASE_SHA=$(git -C "$R" rev-parse HEAD)               # new master tip
git -C "$R" rebase -q --onto master "$PARENT_TIP" child >/dev/null
AFTER=$(git -C "$R" rev-parse child)                 # child replayed onto master
expect "T1" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

# --- T2: in-place catch-up rebase onto advanced master -----------------------
echo "T2: in-place catch-up rebase, child diff unchanged -> rebase_only=true"
R=$(new_repo t2)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q master
add "$R" master_extra.txt m1                         # master advances (disjoint file)
BASE_SHA=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q feature
git -C "$R" rebase -q master >/dev/null
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T2" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

# --- T3: real content change (amended tip) -----------------------------------
echo "T3: real content change -> rebase_only=false"
R=$(new_repo t3)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
( cd "$R" && echo "edited content" > child2.txt && git add child2.txt \
    && git commit -q --amend -m c2 )                 # genuine edit to the tip
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T3" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=false"

# --- T4: BEFORE unavailable (orphaned, unfetchable) --------------------------
echo "T4: before commit unavailable -> rebase_only=false (conservative)"
R=$(new_repo t4)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
AFTER=$(git -C "$R" rev-parse HEAD)
BEFORE=0000000000000000000000000000000000000000       # not a real object
expect "T4" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=false"

# --- T5: plain force-push, identical tree, new SHA ---------------------------
echo "T5: no-op force-push (same tree, new SHA) -> rebase_only=true"
R=$(new_repo t5)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
# Re-commit the identical tip content with a later committer date -> new SHA,
# same tree (mirrors a bare `git push --force` with no edits).
GIT_COMMITTER_DATE="2026-02-02T00:00:00 +0000" \
    git -C "$R" commit -q --amend --no-edit
AFTER=$(git -C "$R" rev-parse HEAD)
[[ "$AFTER" != "$BEFORE" ]] || fail "T5 setup: AFTER SHA should differ from BEFORE"
expect "T5" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

echo
echo "PASS=$PASS FAIL=$FAIL"
[[ "$FAIL" -eq 0 ]]
