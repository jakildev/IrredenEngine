#!/usr/bin/env bash
# Positive-fire coverage for tier-0's unlinked merged-parent repair.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
export EVENT_LOG="$TMPROOT/events.log"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin"
touch "$EVENT_LOG"

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

REMOTE="$TMPROOT/remote.git"
AUTH="$TMPROOT/auth"
git init -q --bare -b master "$REMOTE"
git clone -q "$REMOTE" "$AUTH"
git -C "$AUTH" config commit.gpgsign false
printf 'base\n' > "$AUTH/base.txt"
printf 'common\n' > "$AUTH/conflict.txt"
git -C "$AUTH" add -A
git -C "$AUTH" commit -q -m base
git -C "$AUTH" push -q origin master

git -C "$AUTH" checkout -q -b parent
printf 'parent\n' > "$AUTH/parent.txt"
git -C "$AUTH" add parent.txt
git -C "$AUTH" commit -q -m parent
PARENT_HEAD=$(git -C "$AUTH" rev-parse HEAD)
git -C "$AUTH" push -q origin parent

make_child() {
    local branch="$1" kind="$2"
    git -C "$AUTH" checkout -q -b "$branch" parent
    if [[ "$kind" == conflict ]]; then
        printf 'child\n' > "$AUTH/conflict.txt"
        git -C "$AUTH" add conflict.txt
    else
        printf '%s\n' "$branch" > "$AUTH/$branch.txt"
        git -C "$AUTH" add "$branch.txt"
    fi
    git -C "$AUTH" commit -q -m "$branch"
    git -C "$AUTH" push -q origin "$branch"
}

make_child child-clean clean
make_child child-open clean
make_child child-stack clean
make_child child-both clean
make_child child-conflict conflict
make_child child-offmaster clean
make_child child-unreadable clean
make_child child-dry clean
make_child child-retarget-fail clean

git -C "$AUTH" checkout -q master
git -C "$AUTH" merge -q --squash parent
git -C "$AUTH" commit -q -m "squash parent"
PARENT_MERGE=$(git -C "$AUTH" rev-parse HEAD)
printf 'master\n' > "$AUTH/conflict.txt"
git -C "$AUTH" add conflict.txt
git -C "$AUTH" commit -q -m "master conflict"
git -C "$AUTH" push -q origin master
git clone -q "$REMOTE" "$FLEET_ENGINE_ROOT"
git -C "$FLEET_ENGINE_ROOT" config commit.gpgsign false

cat > "$REMOTE/hooks/post-receive" <<'HOOK'
#!/usr/bin/env bash
echo push >> "$EVENT_LOG"
HOOK
chmod +x "$REMOTE/hooks/post-receive"

export PARENT_HEAD PARENT_MERGE
cat > "$TMPROOT/bin/gh" <<'GHSTUB'
#!/usr/bin/env python3
import json
import os
import sys

args = sys.argv[1:]
mode = os.environ.get("MODE", "orphan")
log = os.environ["EVENT_LOG"]

if args[:2] == ["pr", "list"]:
    merged = {
        "number": 90, "state": "MERGED", "headRefName": "parent",
        "headRefOid": os.environ["PARENT_HEAD"],
        "mergeCommit": {"oid": os.environ["PARENT_MERGE"]},
    }
    rows = [merged]
    if mode in {"open", "both"}:
        rows.append(dict(merged, number=91, state="OPEN", mergeCommit=None))
    if mode == "offmaster":
        rows[0]["mergeCommit"] = {"oid": os.environ["OFF_MASTER_SHA"]}
    print(json.dumps(rows))
    raise SystemExit(0)

if args[:1] == ["api"]:
    if mode == "unreadable":
        raise SystemExit(1)
    child = int(os.environ["CHILD_PR"])
    stacks = [{"open": True, "pull_requests": [{"number": child}]}] \
        if mode == "stack" else []
    print(json.dumps(stacks))
    raise SystemExit(0)

if args[:2] == ["pr", "view"]:
    print("OPEN\tparent\tfleet:approved")
    raise SystemExit(0)

if args[:2] == ["pr", "edit"]:
    if "--base" in args:
        with open(log, "a", encoding="utf-8") as handle:
            handle.write("retarget\n")
        if mode == "retarget-fail":
            raise SystemExit(1)
    elif "--add-label" in args:
        with open(log, "a", encoding="utf-8") as handle:
            handle.write("label\n")
    raise SystemExit(0)

if args[:2] == ["pr", "comment"]:
    with open(log, "a", encoding="utf-8") as handle:
        handle.write("comment\n")
    raise SystemExit(0)

raise SystemExit(99)
GHSTUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

slice_for() {
    local number="$1" head="$2"
    export CHILD_PR="$number"
    printf '{"prs":[{"repo":"engine","number":%s,"headRefName":"%s","baseRefName":"parent","mergeable":"MERGEABLE","labels":["fleet:approved"]}]}\n' \
        "$number" "$head" > "$FLEET_STATE_DIR/projections/merger.json"
    : > "$EVENT_LOG"
    rm -f "$FLEET_STATE_DIR/triggers/merger"
}

remote_sha() { git -C "$AUTH" fetch -q origin && git -C "$AUTH" rev-parse "origin/$1"; }

echo "T1: orphan clean repair pushes before retarget"
export MODE=orphan
slice_for 101 child-clean
T1=$("$REBASE" --auto --rearm-trigger 2>&1)
assert_contains "$T1" "repaired orphaned base" "clean orphan repaired"
assert_eq "$(sed -n '1p' "$EVENT_LOG")" push "push is the first remote mutation"
assert_eq "$(sed -n '2p' "$EVENT_LOG")" retarget "retarget follows the push"
assert_eq "$(git -C "$AUTH" diff --name-only origin/master "$(remote_sha child-clean)")" \
    "child-clean.txt" "repaired head contains only the child change"

echo "T2: open parent is not reported"
export MODE=open
slice_for 102 child-open
T2=$("$REBASE" --auto 2>&1)
assert_absent "$T2" "orphaned merged base" "open parent suppresses orphan repair"
assert_eq "$(cat "$EVENT_LOG")" "" "open parent causes no remote mutation"

echo "T3: open stack membership is not reported"
export MODE=stack
slice_for 103 child-stack
T3=$("$REBASE" --auto 2>&1)
assert_absent "$T3" "orphaned merged base" "stack membership suppresses orphan repair"
assert_eq "$(cat "$EVENT_LOG")" "" "stacked child causes no remote mutation"

echo "T4: merged plus open parent prefers the open parent"
export MODE=both
slice_for 104 child-both
T4=$("$REBASE" --auto 2>&1)
assert_absent "$T4" "orphaned merged base" "open parent wins"

echo "T5: child conflict parks without push or LLM target"
export MODE=orphan
slice_for 105 child-conflict
T5=$("$REBASE" --auto --rearm-trigger 2>&1)
assert_contains "$T5" "parked for human repair" "conflicting child parked"
assert_eq "$(cat "$EVENT_LOG")" $'label\ncomment' "park emits one label and one comment"
if [[ -e "$FLEET_STATE_DIR/triggers/merger" ]]; then bad "park emits no merge target"; else ok "park emits no merge target"; fi

echo "T6: parent merge not on master parks"
export MODE=offmaster OFF_MASTER_SHA="$(remote_sha child-offmaster)"
slice_for 106 child-offmaster
T6=$("$REBASE" --auto --rearm-trigger 2>&1)
assert_contains "$T6" "parked for human repair" "off-master parent parked"
assert_absent "$(cat "$EVENT_LOG")" push "off-master parent does not push"

echo "T7: unreadable stacks defer without mutation or label"
export MODE=unreadable
slice_for 107 child-unreadable
T7=$("$REBASE" --auto --rearm-trigger 2>&1)
assert_contains "$T7" "orphan classification unreadable" "unreadable stacks defer"
assert_eq "$(cat "$EVENT_LOG")" "" "unreadable stacks do not mutate"
assert_contains "$T7" "unknown_skipped=1" "unreadable stacks count as unknown"

echo "T8: dry-run mutates nothing"
export MODE=orphan
slice_for 108 child-dry
before=$(remote_sha child-dry)
T8=$("$REBASE" --auto --dry-run 2>&1)
assert_contains "$T8" "would rebase, push, then retarget" "dry-run reports repair"
assert_eq "$(remote_sha child-dry)" "$before" "dry-run leaves remote head unchanged"
assert_eq "$(cat "$EVENT_LOG")" "" "dry-run emits no remote mutation"

echo "T9: retarget failure parks the already-rebased head"
export MODE=retarget-fail
slice_for 109 child-retarget-fail
T9=$("$REBASE" --auto --rearm-trigger 2>&1)
assert_contains "$T9" "parked for human repair" "retarget failure parks"
assert_eq "$(cat "$EVENT_LOG")" $'push\nretarget\nlabel\ncomment' \
    "retarget failure records push, failed retarget, then park"
if [[ -e "$FLEET_STATE_DIR/triggers/merger" ]]; then bad "retarget failure emits no merge target"; else ok "retarget failure emits no merge target"; fi

summarize
