#!/usr/bin/env bash
# Tests for fleet-campaign-status, the campaign lane's re-entry oracle.
#
# The invariant under test is the verdict, and specifically that it is decided
# on CONTENT, not on commit identity. Campaign PRs are squash-merged: a stack's
# several commits per PR collapse into one commit with a fresh patch-id, so
# `git cherry` reports a fully merged branch as almost entirely unmatched
# upstream and `git rebase` replays work that already landed. T2 builds exactly
# that shape — a branch whose content is on master under different commits —
# and requires `superseded`, with the cherry count reported as the trap it is.
#
# T3 is the other side: a branch whose content really never landed must NOT be
# called superseded, because --apply resets a superseded branch. Getting T3
# wrong throws away work.
#
# Fixtures are local throwaway repos with an `origin/master` remote-tracking
# ref, and PR rows come from --pr-json, so the suite touches neither GitHub nor
# ~/.fleet.
#
# Covers:
#   - T1: a branch level with the default branch is `clean`
#   - T2: content-identical-but-squashed is `superseded`, and the cherry
#         count is reported (patch-id disagreeing with content is the trap)
#   - T3: a branch carrying an unlanded file is `stranded`, and names the file
#   - T4: a branch with an open PR on its head is `live` regardless of content
#   - T5: --apply on a superseded branch resets it, carries the in-flight slice
#         across, keeps a recovery ref, and clears a merged cursor-stack-base
#   - T6: --apply refuses a `stranded` branch
#   - T7: exit codes — 0 ready, 1 attention, 2 no worktree
#   - T8: a file another lane changed on the default branch is reported as
#         foreign activity on the campaign's surface

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
TOOL="$SCRIPT_DIR/fleet-campaign-status"

if [[ ! -x "$TOOL" ]]; then
    echo "SKIP: fleet-campaign-status not found at $TOOL" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

# cd/pwd-normalized: TMPDIR carries a trailing slash on macOS and the doubled
# separator rides into every path comparison below.
TMPROOT=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/campaign-status.XXXXXX")" && pwd)

SLUG="demo-objective"
BRANCH="claude/$SLUG-slice"

g() { git -C "$1" "${@:2}"; }

# A fixture repo with a real origin/master remote-tracking ref (the tool
# measures against a ref, not a branch name) and the campaign doc in place.
# Sets FIXTURE rather than printing the path: a stray line of git output on
# stdout would otherwise ride into the captured path and every later -C.
FIXTURE=""
new_fixture() {
    local name="$1"
    local wt="$TMPROOT/$name"
    mkdir -p "$wt"
    g "$wt" init --quiet --initial-branch=master
    g "$wt" config user.email fixture@example.invalid
    g "$wt" config user.name Fixture
    mkdir -p "$wt/docs/design/campaigns" "$wt/engine"
    cat > "$wt/docs/design/campaigns/$SLUG.md" <<'DOC'
# demo objective

## Ledger

| Date | Entry |
|---|---|
| 2026-01-02 | Checkpoint 1: the first slice landed |

## Now

- **In flight:** the slice under test.
DOC
    echo "base" > "$wt/engine/base.txt"
    g "$wt" add -A
    g "$wt" commit --quiet -m "base"
    # origin/master as a plain remote-tracking ref — no network, no remote.
    g "$wt" update-ref refs/remotes/origin/master HEAD
    FIXTURE="$wt"
}

# Advance the fixture's origin/master by one commit, off the current worktree
# state, without moving the checked-out branch.
advance_origin() {
    local wt="$1"
    local file="$2"
    local content="$3"
    local msg="$4"
    g "$wt" stash push --quiet --include-untracked >/dev/null 2>&1 || true
    local here
    here=$(g "$wt" rev-parse --abbrev-ref HEAD)
    g "$wt" checkout --quiet -B __origin refs/remotes/origin/master
    mkdir -p "$(dirname "$wt/$file")"
    printf '%s\n' "$content" > "$wt/$file"
    g "$wt" add -A
    g "$wt" commit --quiet -m "$msg"
    g "$wt" update-ref refs/remotes/origin/master HEAD
    g "$wt" checkout --quiet "$here"
    g "$wt" branch --quiet -D __origin
    g "$wt" stash pop --quiet >/dev/null 2>&1 || true
}

PRJSON_EMPTY="$TMPROOT/pr-empty.json"
printf '{"open": [], "merged": []}\n' > "$PRJSON_EMPTY"

run_tool() {
    local wt="$1"; shift
    "$TOOL" "$SLUG" --worktree "$wt" --no-fetch --pr-json "$PRJSON_EMPTY" "$@"
}

# --- T1: level with the default branch --------------------------------------
echo "T1: a branch level with origin/master is clean"
new_fixture t1; WT1="$FIXTURE"
g "$WT1" checkout --quiet -b "$BRANCH"
out=$(run_tool "$WT1" || true)
assert_contains "$out" "verdict    CLEAN" "a branch with no commits past master is clean"
assert_contains "$out" "## Now" "the campaign doc's Now section is reported"
assert_contains "$out" "2026-01-02" "the last ledger row is reported"

# --- T2: squash-merged content is superseded --------------------------------
echo "T2: content already on master under a different commit is superseded"
new_fixture t2; WT2="$FIXTURE"
g "$WT2" checkout --quiet -b "$BRANCH"
# Two commits on the branch, the shape a stacked slice leaves behind.
printf 'slice one\n' > "$WT2/engine/slice.txt"
g "$WT2" add -A && g "$WT2" commit --quiet -m "slice part one"
printf 'slice one\nslice two\n' > "$WT2/engine/slice.txt"
g "$WT2" add -A && g "$WT2" commit --quiet -m "slice part two"
# The same final content lands on master as ONE commit — the squash merge.
advance_origin "$WT2" engine/slice.txt "$(printf 'slice one\nslice two')" "slice (#1)"
out=$(run_tool "$WT2" || true)
assert_contains "$out" "verdict    SUPERSEDED" \
    "a branch whose content is all on master is superseded, not stranded"
assert_contains "$out" "RESET" "the superseded verdict prescribes a reset"
assert_contains "$out" "unmatched by \`git cherry\`" \
    "the patch-id count is reported, because it disagrees with the content verdict"
if echo "$out" | grep -q "2 of 2 commits unmatched"; then
    ok "git cherry matches neither squashed commit — the trap is quantified"
else
    bad "expected both commits unmatched by patch-id: $(echo "$out" | grep cherry || true)"
fi

# --- T3: unlanded content is stranded ---------------------------------------
echo "T3: a branch carrying content master never took is stranded"
new_fixture t3; WT3="$FIXTURE"
g "$WT3" checkout --quiet -b "$BRANCH"
printf 'landed\n' > "$WT3/engine/slice.txt"
printf 'never landed\n' > "$WT3/engine/orphan.txt"
g "$WT3" add -A && g "$WT3" commit --quiet -m "slice plus an orphan"
advance_origin "$WT3" engine/slice.txt landed "slice (#1)"
out=$(run_tool "$WT3" || true)
assert_contains "$out" "verdict    STRANDED" "an unlanded file makes the branch stranded"
assert_contains "$out" "never landed: engine/orphan.txt" "the unlanded file is named"

# --- T4: an open PR on the head is live -------------------------------------
echo "T4: a branch with an open PR on its head is live"
new_fixture t4; WT4="$FIXTURE"
g "$WT4" checkout --quiet -b "$BRANCH"
printf 'wip\n' > "$WT4/engine/slice.txt"
g "$WT4" add -A && g "$WT4" commit --quiet -m "slice"
PRJSON_LIVE="$TMPROOT/pr-live.json"
cat > "$PRJSON_LIVE" <<JSON
{"open": [{"number": 42, "headRefName": "$BRANCH", "title": "slice",
           "labels": [{"name": "fleet:wip"}]}], "merged": []}
JSON
out=$("$TOOL" "$SLUG" --worktree "$WT4" --no-fetch --pr-json "$PRJSON_LIVE" || true)
assert_contains "$out" "verdict    LIVE" "an open PR on the head means continue it"
assert_contains "$out" "#42" "the open campaign PR is listed"

# --- T5: --apply repairs a superseded branch --------------------------------
echo "T5: --apply resets a superseded branch and carries the slice across"
new_fixture t5; WT5="$FIXTURE"
g "$WT5" checkout --quiet -b "$BRANCH"
printf 'slice\n' > "$WT5/engine/slice.txt"
g "$WT5" add -A && g "$WT5" commit --quiet -m "slice part one"
advance_origin "$WT5" engine/slice.txt slice "slice (#1)"
# Master then moves on with an unrelated commit the branch has never seen.
advance_origin "$WT5" engine/other.txt other "another lane (#2)"
# The in-flight slice: one tracked edit and one untracked file.
printf 'in flight\n' > "$WT5/engine/base.txt"
printf 'brand new\n' > "$WT5/engine/inflight.txt"
g "$WT5" config "branch.$BRANCH.cursor-stack-base" "claude/$SLUG-merged-parent"
old_head=$(g "$WT5" rev-parse HEAD)
out=$(run_tool "$WT5" --apply || true)
assert_contains "$out" "reset $BRANCH onto origin/master" "the branch was reset"
assert_eq "$(g "$WT5" rev-parse HEAD)" "$(g "$WT5" rev-parse refs/remotes/origin/master)" \
    "HEAD now equals origin/master"
assert_eq "$(cat "$WT5/engine/base.txt")" "in flight" "the tracked in-flight edit survived"
assert_eq "$(cat "$WT5/engine/inflight.txt")" "brand new" "the untracked in-flight file survived"
assert_eq "$(cat "$WT5/engine/other.txt")" "other" "the other lane's commit is now present"
if g "$WT5" config --get "branch.$BRANCH.cursor-stack-base" >/dev/null 2>&1; then
    bad "the merged cursor-stack-base was left in place — the next PR would target a dead base"
else
    ok "the merged cursor-stack-base was cleared"
fi
recovery=$(g "$WT5" for-each-ref --format='%(refname)' 'refs/campaign-reentry/**' | head -1)
if [[ -n "$recovery" ]]; then
    assert_eq "$(g "$WT5" rev-parse "$recovery")" "$old_head" \
        "the pre-reset head is recoverable from $recovery"
else
    bad "no recovery ref was written — the reset would be unrecoverable after reflog expiry"
fi

# --- T6: --apply refuses a stranded branch ----------------------------------
echo "T6: --apply refuses to reset a stranded branch"
new_fixture t6; WT6="$FIXTURE"
g "$WT6" checkout --quiet -b "$BRANCH"
printf 'never landed\n' > "$WT6/engine/orphan.txt"
g "$WT6" add -A && g "$WT6" commit --quiet -m "orphan"
advance_origin "$WT6" engine/unrelated.txt unrelated "unrelated (#1)"
set +e
run_tool "$WT6" --apply >/dev/null 2>"$TMPROOT/t6.err"
rc=$?
set -e
assert_eq "$rc" "2" "--apply on a stranded branch exits 2"
assert_contains "$(cat "$TMPROOT/t6.err")" "only repairs a \`superseded\` branch" \
    "the refusal names the verdict it requires"
assert_eq "$(g "$WT6" rev-parse HEAD)" "$(g "$WT6" rev-parse "$BRANCH")" \
    "the stranded branch was not moved"

# --- T7: exit codes ----------------------------------------------------------
echo "T7: exit codes distinguish ready, attention and no-worktree"
set +e
run_tool "$WT1" >/dev/null 2>&1; rc_clean=$?
run_tool "$WT2" >/dev/null 2>&1; rc_superseded=$?
"$TOOL" "$SLUG" --worktree "$TMPROOT/does-not-exist" --no-fetch >/dev/null 2>&1; rc_missing=$?
set -e
assert_eq "$rc_clean" "0" "a clean branch exits 0 (ready)"
assert_eq "$rc_superseded" "1" "a superseded branch exits 1 (attention)"
assert_eq "$rc_missing" "2" "a missing worktree exits 2 (usage)"

# --- T8: foreign activity on the campaign's surface -------------------------
echo "T8: another lane's commit on a campaign file is reported"
new_fixture t8; WT8="$FIXTURE"
g "$WT8" checkout --quiet -b "$BRANCH"
# The campaign's own file, uncommitted — the in-flight slice defines surface too.
printf 'campaign edit\n' > "$WT8/engine/owned.txt"
g "$WT8" add -A && g "$WT8" commit --quiet -m "campaign takes ownership of owned.txt"
advance_origin "$WT8" engine/owned.txt "campaign edit" "campaign slice (#1)"
advance_origin "$WT8" engine/owned.txt "another lane rewrote this" "other lane rewrite (#99)"
out=$(run_tool "$WT8" || true)
assert_contains "$out" "OTHER LANES TOUCHED THIS CAMPAIGN'S FILES" \
    "foreign activity on the surface is surfaced"
assert_contains "$out" "other lane rewrite" "the foreign commit is named"
assert_contains "$out" "engine/owned.txt" "the collided file is named"

summarize "fleet-campaign-status tests"
