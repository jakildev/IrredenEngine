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
#         foreign activity on the campaign's surface, and the campaign's own
#         merge is excluded from that section
#   - T9: THE destructive case — unlanded work INSIDE a file another lane
#         also changed. A file-name comparison cancels the path and calls it
#         superseded; --apply would then delete committed work.
#   - T10: `behind` is distinct from `clean`, and --apply fast-forwards it
#   - T11: a detached HEAD is refused before anything is written
#   - T12: a locally-ignored file the default branch tracks is refused
#   - T13: the campaign's own merge is excluded from the other-lanes section
#          even when it lands INSIDE the reported window
#   - T14: an open PR from another lane intersecting the surface is reported
#          against its OWN merge base, and an unfetchable head is reported as
#          unknown rather than dropped
#   - T15: the `since` window starts at the campaign's most recently LANDED
#          merge, chosen by distance to the tip rather than row order
#   - T16: the surface includes files that only a merged campaign PR touched
#   - T17: an unresolvable default ref is `unknown`, never `clean`
#   - T18: an unreadable open-PR list is `unknown`, never a classification

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
# force=1 adds a path .gitignore excludes (the T12 fixture needs the default
# branch to track a path that is ignored locally).
advance_origin() {
    local wt="$1"
    local file="$2"
    local content="$3"
    local msg="$4"
    local force="${5:-0}"
    g "$wt" stash push --quiet --include-untracked >/dev/null 2>&1 || true
    local here
    here=$(g "$wt" rev-parse --abbrev-ref HEAD)
    g "$wt" checkout --quiet -B __origin refs/remotes/origin/master
    mkdir -p "$(dirname "$wt/$file")"
    printf '%s\n' "$content" > "$wt/$file"
    g "$wt" add -A
    [[ "$force" == "1" ]] && g "$wt" add -f "$file"
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
assert_contains "$(cat "$TMPROOT/t6.err")" "this branch is \`stranded\`" \
    "the refusal names the verdict it found"
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

# --- T9: unlanded work inside a shared file ---------------------------------
# The case that makes the verdict load-bearing. The branch's commit changes a
# file another lane ALSO changed, and the branch's own change never landed. A
# path-set comparison cancels the file (both sides name it) and reads
# `superseded`, so --apply deletes committed campaign work. Only the merge
# distinguishes the two.
echo "T9: unlanded work inside a file another lane also changed is stranded"
new_fixture t9; WT9="$FIXTURE"
g "$WT9" checkout --quiet -b "$BRANCH"
printf 'one\ntwo\nthree\nfour\nfive\nsix\nseven\nUNLANDED CAMPAIGN WORK\n' \
    > "$WT9/engine/shared.txt"
g "$WT9" add -A && g "$WT9" commit --quiet -m "campaign edits the end of shared.txt"
# Another lane edits the SAME file far from the campaign's hunk, so the merge is
# CLEAN and still changes master — a conflict would make the case easy.
advance_origin "$WT9" engine/shared.txt \
    "$(printf 'ANOTHER LANE\ntwo\nthree\nfour\nfive\nsix\nseven')" "other lane (#99)"
out=$(run_tool "$WT9" || true)
assert_contains "$out" "verdict    STRANDED" \
    "unlanded work inside a shared file is stranded, not superseded"
assert_contains "$out" "never landed: engine/shared.txt" \
    "the shared file is named in the at-risk inventory"
set +e
run_tool "$WT9" --apply >"$TMPROOT/t9.out" 2>"$TMPROOT/t9.err"; rc9=$?
set -e
assert_eq "$rc9" "2" "--apply refuses it"
assert_contains "$(g "$WT9" show HEAD:engine/shared.txt)" "UNLANDED CAMPAIGN WORK" \
    "the campaign's committed work is still on the branch"

# --- T10: behind is not clean ------------------------------------------------
echo "T10: a branch with no commits of its own but a moved master is behind"
new_fixture t10; WT10="$FIXTURE"
g "$WT10" checkout --quiet -b "$BRANCH"
advance_origin "$WT10" engine/other.txt moved "another lane (#5)"
out=$(run_tool "$WT10" || true)
assert_contains "$out" "verdict    BEHIND" "0 ahead with a moved master is behind, not clean"
assert_contains "$out" "stale base" "the line warns that a slice here starts stale"
printf 'in flight\n' > "$WT10/engine/base.txt"
out=$(run_tool "$WT10" --apply || true)
assert_eq "$(g "$WT10" rev-parse HEAD)" "$(g "$WT10" rev-parse refs/remotes/origin/master)" \
    "--apply fast-forwards a behind branch"
assert_eq "$(cat "$WT10/engine/base.txt")" "in flight" "the in-flight slice survived the fast-forward"
assert_contains "$out" "verdict    CLEAN" \
    "the post-apply report is re-measured, not patched"

# --- T11: detached HEAD ------------------------------------------------------
echo "T11: a detached HEAD is refused before anything is written"
new_fixture t11; WT11="$FIXTURE"
g "$WT11" checkout --quiet -b "$BRANCH"
printf 'slice\n' > "$WT11/engine/slice.txt"
g "$WT11" add -A && g "$WT11" commit --quiet -m "slice"
advance_origin "$WT11" engine/slice.txt slice "slice (#1)"
g "$WT11" checkout --quiet --detach HEAD
set +e
run_tool "$WT11" --apply >"$TMPROOT/t11.out" 2>"$TMPROOT/t11.err"; rc11=$?
set -e
assert_contains "$(cat "$TMPROOT/t11.out") $(cat "$TMPROOT/t11.err")" "detached" \
    "the refusal names the detached HEAD"
if g "$WT11" for-each-ref --format='%(refname)' 'refs/campaign-reentry/**' | grep -q .; then
    bad "a backup ref was written before the detached-HEAD refusal"
else
    ok "nothing was written before the refusal"
fi
if g "$WT11" stash list | grep -q .; then
    bad "the tree was stashed before the detached-HEAD refusal"
else
    ok "nothing was stashed before the refusal"
fi

# --- T12: an ignored file the default branch tracks --------------------------
# git status never lists an ignored file, so it takes no stash and the backup
# ref (commits only) does not cover it — the one path a reset destroys with no
# recovery at all.
echo "T12: an ignored file the default branch tracks blocks the reset"
new_fixture t12; WT12="$FIXTURE"
printf 'local/\n' > "$WT12/.gitignore"
g "$WT12" add -A && g "$WT12" commit --quiet -m "ignore local/"
g "$WT12" update-ref refs/remotes/origin/master HEAD
g "$WT12" checkout --quiet -b "$BRANCH"
printf 'slice\n' > "$WT12/engine/slice.txt"
g "$WT12" add -A && g "$WT12" commit --quiet -m "slice"
advance_origin "$WT12" engine/slice.txt slice "slice (#1)"
advance_origin "$WT12" local/state.txt "the default branch version" "track local/state.txt (#6)" 1
# Written AFTER the advance: checking the campaign branch back out removes a
# path only the default branch tracks, so a file staged before it would be gone
# before the tool ever ran.
mkdir -p "$WT12/local"
printf 'precious local state\n' > "$WT12/local/state.txt"
set +e
run_tool "$WT12" --apply >"$TMPROOT/t12.out" 2>"$TMPROOT/t12.err"; rc12=$?
set -e
assert_eq "$rc12" "2" "--apply refuses rather than clobbering an ignored file"
assert_contains "$(cat "$TMPROOT/t12.out") $(cat "$TMPROOT/t12.err")" "local/state.txt" \
    "the at-risk file is named"
assert_eq "$(cat "$WT12/local/state.txt")" "precious local state" "the ignored file is untouched"

# --- T13: foreign open PRs and the campaign's own merge ----------------------
echo "T13: foreign open PRs intersect the surface; the campaign's own merge does not"
new_fixture t13; WT13="$FIXTURE"
g "$WT13" checkout --quiet -b "$BRANCH"
printf 'campaign\n' > "$WT13/engine/owned.txt"
g "$WT13" add -A && g "$WT13" commit --quiet -m "campaign owns owned.txt"
advance_origin "$WT13" engine/owned.txt campaign "campaign slice (#1)"
campaign_merge=$(g "$WT13" rev-parse refs/remotes/origin/master)
advance_origin "$WT13" engine/owned.txt "another lane" "other lane (#99)"
# A SECOND campaign merge, landing after the window opens. Its row carries no
# mergeCommit (gh does not always return one), so it cannot be the anchor and
# falls inside the reported window — only the campaign-number filter keeps it
# out. Without this the assertion is vacuous: the anchor excludes itself.
advance_origin "$WT13" engine/owned.txt "campaign again" "campaign slice two (#2)"
PRJSON_MERGED="$TMPROOT/pr-merged.json"
cat > "$PRJSON_MERGED" <<JSON
{"open": [], "merged": [{"number": 1, "title": "campaign slice", "mergedAt": "2026-01-03",
                         "headRefName": "$BRANCH", "mergeCommit": {"oid": "$campaign_merge"}},
                        {"number": 2, "title": "campaign slice two", "mergedAt": "2026-01-04",
                         "headRefName": "$BRANCH", "mergeCommit": null}]}
JSON
out=$("$TOOL" "$SLUG" --worktree "$WT13" --no-fetch --pr-json "$PRJSON_MERGED" || true)
assert_contains "$out" "other lane (#99)" "the other lane's merge is reported"
assert_absent "$out" "campaign slice two (#2)" \
    "a campaign merge INSIDE the window is excluded by number, not by the anchor"
if echo "$out" | grep -q "merged campaign PRs"; then
    ok "the merged campaign PR is listed in its own section"
else
    bad "the merged campaign PR row was dropped"
fi

# --- T14: open PRs from other lanes ------------------------------------------
# The tool reads each PR's files from `refs/pull/<n>/head` over git. A bare repo
# standing in for origin lets the fetch succeed offline, so the intersecting arm
# actually executes rather than falling through the unreadable branch.
echo "T14: a foreign open PR is scoped to its own merge base; an unfetchable head is not dropped"
new_fixture t14; WT14="$FIXTURE"
ORIGIN="$TMPROOT/t14-origin.git"
git init -q --bare "$ORIGIN"
g "$WT14" remote add origin "$ORIGIN"
g "$WT14" checkout --quiet -b "$BRANCH"
printf 'campaign\n' > "$WT14/engine/owned.txt"
g "$WT14" add -A && g "$WT14" commit --quiet -m "campaign owns owned.txt"
advance_origin "$WT14" engine/owned.txt campaign "campaign slice (#1)"
t14_merge=$(g "$WT14" rev-parse refs/remotes/origin/master)
# Unrelated lanes land between the campaign's fork point and the PR's base. A
# window anchored on the fork point would charge the PR with these.
advance_origin "$WT14" engine/noise-one.txt one "noise one (#50)"
advance_origin "$WT14" engine/noise-two.txt two "noise two (#51)"
# PR #7 branches off current master and touches the campaign's file.
g "$WT14" checkout --quiet -B __pr refs/remotes/origin/master
printf 'another lane edits the campaign file\n' > "$WT14/engine/owned.txt"
g "$WT14" add -A && g "$WT14" commit --quiet -m "pr7"
g "$WT14" push -q "$ORIGIN" HEAD:refs/pull/7/head
g "$WT14" checkout --quiet "$BRANCH"
g "$WT14" branch --quiet -D __pr
PRJSON_FOREIGN="$TMPROOT/pr-foreign.json"
cat > "$PRJSON_FOREIGN" <<JSON
{"open": [{"number": 7, "headRefName": "codex/other-lane", "title": "other lane touches owned.txt",
           "labels": []},
          {"number": 8, "headRefName": "codex/no-such-head", "title": "head never pushed",
           "labels": []}],
 "merged": [{"number": 1, "title": "campaign slice", "mergedAt": "2026-01-03",
             "headRefName": "$BRANCH", "mergeCommit": {"oid": "$t14_merge"}}]}
JSON
out=$("$TOOL" "$SLUG" --worktree "$WT14" --no-fetch --pr-json "$PRJSON_FOREIGN" || true)
assert_contains "$out" "other lane touches owned.txt" "the intersecting open PR is reported"
assert_contains "$out" "engine/owned.txt" "the intersected file is named"
# Scoped to PR #7's own block: the noise commits legitimately appear in the
# merged section, so an assertion over the whole report would not discriminate.
pr7_block=$(echo "$out" | awk '/open   #7/{f=1;next} /open   #|merged #/{f=0} f')
assert_absent "$pr7_block" "engine/noise-one.txt" \
    "the PR is scoped to its own merge base, not charged with unrelated lanes"
assert_contains "$out" "head unreadable" "an unfetchable head is reported, not dropped"

# --- T15: the since-window anchor -------------------------------------------
# Rows are ordered by creation, which is not merge order. The anchor must be the
# campaign merge nearest the tip, or the window silently swallows foreign work.
echo "T15: the window anchors on the campaign merge nearest the tip, not the first row"
new_fixture t15; WT15="$FIXTURE"
g "$WT15" checkout --quiet -b "$BRANCH"
printf 'campaign\n' > "$WT15/engine/owned.txt"
g "$WT15" add -A && g "$WT15" commit --quiet -m "campaign owns owned.txt"
advance_origin "$WT15" engine/owned.txt one "campaign slice one (#1)"
first_merge=$(g "$WT15" rev-parse refs/remotes/origin/master)
# Between the two campaign merges. Anchoring on #1 (row order) pulls this into
# the window; anchoring on #2 (nearest the tip) leaves it out. The campaign's
# own merges cannot discriminate — the number filter removes them either way.
advance_origin "$WT15" engine/owned.txt "earlier lane" "earlier lane (#98)"
advance_origin "$WT15" engine/owned.txt two "campaign slice two (#2)"
second_merge=$(g "$WT15" rev-parse refs/remotes/origin/master)
advance_origin "$WT15" engine/owned.txt "another lane" "other lane (#99)"
PRJSON_ORDER="$TMPROOT/pr-order.json"
# Deliberately oldest-first: taking row order would anchor on #1 and pull the
# campaign's own #2 into the window.
cat > "$PRJSON_ORDER" <<JSON
{"open": [], "merged": [{"number": 1, "title": "campaign slice one", "mergedAt": "2026-01-03",
                         "headRefName": "$BRANCH", "mergeCommit": {"oid": "$first_merge"}},
                        {"number": 2, "title": "campaign slice two", "mergedAt": "2026-01-04",
                         "headRefName": "$BRANCH", "mergeCommit": {"oid": "$second_merge"}}]}
JSON
out=$("$TOOL" "$SLUG" --worktree "$WT15" --no-fetch --pr-json "$PRJSON_ORDER" || true)
assert_contains "$out" "other lane (#99)" "work after the newest campaign merge is reported"
assert_absent "$out" "earlier lane (#98)" \
    "the window starts at the campaign merge nearest the tip, not the first row"
assert_absent "$out" "campaign slice one (#1)" "the campaign's own merges stay out"

# --- T16: the surface includes merged-PR files -------------------------------
# A file the campaign landed and has not touched since is still the campaign's.
# Only the merged-PR expansion puts it on the surface; the branch diff cannot.
echo "T16: a file only a merged campaign PR touched is still on the surface"
new_fixture t16; WT16="$FIXTURE"
g "$WT16" checkout --quiet -b "$BRANCH"
advance_origin "$WT16" engine/landed-long-ago.txt campaign "campaign slice (#1)"
campaign_only=$(g "$WT16" rev-parse refs/remotes/origin/master)
advance_origin "$WT16" engine/landed-long-ago.txt "another lane" "other lane (#99)"
PRJSON_SURFACE="$TMPROOT/pr-surface.json"
cat > "$PRJSON_SURFACE" <<JSON
{"open": [], "merged": [{"number": 1, "title": "campaign slice", "mergedAt": "2026-01-03",
                         "headRefName": "$BRANCH", "mergeCommit": {"oid": "$campaign_only"}}]}
JSON
out=$("$TOOL" "$SLUG" --worktree "$WT16" --no-fetch --pr-json "$PRJSON_SURFACE" || true)
assert_contains "$out" "other lane (#99)" \
    "a foreign edit to a file only a merged campaign PR touched is reported"
assert_contains "$out" "engine/landed-long-ago.txt" "the merged-PR file is on the surface"

# --- T17: an unresolvable default ref ----------------------------------------
# ahead/behind read through a helper that returns "" on failure, so a bad ref
# would otherwise present as level with the default branch.
echo "T17: an unresolvable default ref is unknown, never clean"
new_fixture t17; WT17="$FIXTURE"
g "$WT17" checkout --quiet -b "$BRANCH"
printf 'unlanded\n' > "$WT17/engine/slice.txt"
g "$WT17" add -A && g "$WT17" commit --quiet -m "unlanded slice"
set +e
out=$("$TOOL" "$SLUG" --worktree "$WT17" --no-fetch --pr-json "$PRJSON_EMPTY" \
    --default-ref origin/no-such-branch 2>&1); rc17=$?
set -e
assert_contains "$out" "verdict    UNKNOWN" "a bad default ref is unknown"
assert_absent "$out" "start the next slice here" "it never invites a slice on an unknown base"
assert_eq "$rc17" "1" "it exits non-zero"
set +e
"$TOOL" "$SLUG" --worktree "$WT17" --no-fetch --pr-json "$PRJSON_EMPTY" \
    --default-ref origin/no-such-branch --apply >/dev/null 2>&1; rc17a=$?
set -e
assert_eq "$rc17a" "2" "--apply refuses an unknown verdict"

# --- T18: an unreadable open-PR list -----------------------------------------
# An empty list and an unreadable one are different facts: the second cannot
# rule out an open PR on this branch, so no classification may follow.
echo "T18: an unreadable open-PR list is unknown, never a classification"
new_fixture t18; WT18="$FIXTURE"
g "$WT18" checkout --quiet -b "$BRANCH"
printf 'slice\n' > "$WT18/engine/slice.txt"
g "$WT18" add -A && g "$WT18" commit --quiet -m "slice"
advance_origin "$WT18" engine/slice.txt slice "slice (#1)"
PRJSON_UNREADABLE="$TMPROOT/pr-unreadable.json"
printf '{"open": null, "merged": []}\n' > "$PRJSON_UNREADABLE"
set +e
out=$("$TOOL" "$SLUG" --worktree "$WT18" --no-fetch --pr-json "$PRJSON_UNREADABLE" 2>&1); rc18=$?
set -e
assert_contains "$out" "verdict    UNKNOWN" \
    "an unreadable PR list blocks classification even when the content IS upstream"
assert_eq "$rc18" "1" "it exits non-zero"

# --- T19: a repair that needs no stash is not a failed one -------------------
# `git status --porcelain` can be non-empty over a tree `git stash push` saves
# nothing from. Assuming the stash took makes the later pop fail with "No stash
# entries found", which then reads as a conflict — reporting a repair that
# fully succeeded as a failure, and leaving the pre-apply verdict printed over
# an already-reset branch.
echo "T19: --apply succeeds when the dirty tree yields no stash"
new_fixture t19; WT19="$FIXTURE"
SUBSRC="$TMPROOT/t19-sub"
mkdir -p "$SUBSRC"
git -C "$SUBSRC" init -q -b master .
git -C "$SUBSRC" config user.email fixture@example.invalid
git -C "$SUBSRC" config user.name Fixture
echo sub > "$SUBSRC/f.txt"
git -C "$SUBSRC" add -A && git -C "$SUBSRC" commit -q -m sub
# The submodule belongs to the shared base, so the branch can still diverge and
# be superseded — the point of the case is the stash, not the submodule.
if g "$WT19" -c protocol.file.allow=always submodule add -q "$SUBSRC" sub 2>/dev/null; then
    g "$WT19" commit --quiet -m "add sub"
    g "$WT19" update-ref refs/remotes/origin/master HEAD
    g "$WT19" checkout --quiet -b "$BRANCH"
    printf 'slice\n' > "$WT19/engine/slice.txt"
    g "$WT19" add -A && g "$WT19" commit --quiet -m "slice"
    advance_origin "$WT19" engine/slice.txt slice "slice (#1)"
    # Untracked content inside the submodule: the parent reports " M sub", and a
    # parent-level stash push saves nothing from it.
    echo untracked > "$WT19/sub/stray.txt"
    if [[ "$(g "$WT19" status --porcelain | wc -l | tr -d ' ')" -gt 0 ]]; then
        out=$(run_tool "$WT19" --apply 2>&1 || true)
        assert_absent "$out" "FAILED" "a repair needing no stash is not reported as failed"
        assert_absent "$out" "conflict" "no conflict is invented over an empty stash"
        assert_contains "$out" "nothing for the stash to take" \
            "the report says plainly that the stash took nothing"
        assert_contains "$out" "verdict    CLEAN" \
            "the post-apply verdict is re-measured, not the pre-apply one"
        assert_eq "$(cat "$WT19/sub/stray.txt")" "untracked" "the submodule content is untouched"
    else
        echo "  skip: this git reports a submodule with untracked content as clean"
    fi
else
    echo "  skip: submodules unavailable in this environment"
fi

summarize "fleet-campaign-status tests"
