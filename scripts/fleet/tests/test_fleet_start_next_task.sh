#!/usr/bin/env bash
# Tests for fleet-start-next-task — the executable form of the
# start-next-task flow (docs/agents/skills/start-next-task.md).
#
# Runs against a real git fixture: a bare origin, a "main" clone whose
# .claude/worktrees/pool-7 is a linked worktree (so the real
# fleet-assert-worktree sees a fleet worktree), and a second clone that pushes
# to origin to simulate other merges. `gh` and `fleet-claim` are stubs that
# model the exact argument shapes the tool sends and reject anything else.
#
# Covers:
#   - standard mode: pushed task branch → claude/<basename>-scratch at the
#     freshly fetched origin/master tip, open PR reported
#   - refusals: tracked modifications; HEAD on no origin/* ref; main clone;
#     molecule not advanced past the current branch's issue
#   - fleet-stack mode: claude/<id>-<slug> based on the old branch head
#   - --branch override (claude/ prepended), --dry-run, --help, detached HEAD
#     at a pushed commit, untracked scratch bodies not blocking, a closed-PR
#     old branch only reported, a failing fleet-claim surfacing as exit 2
#   - stub fidelity: the gh stub rejects a flag the real binary rejects

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
TOOL="$SCRIPT_DIR/fleet-start-next-task"
if [[ ! -x "$TOOL" ]]; then
    echo "SKIP: $TOOL not found" >&2
    exit 3
fi
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fsnt.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
unset FLEET_ALLOW_MAIN_CLONE

git_q() { git -C "$1" "${@:2}" >/dev/null 2>&1; }

# --- fixture: origin + main clone + linked worktree + pusher --------------
ORIGIN="$TMPROOT/origin.git"
MAIN="$TMPROOT/main"
WT="$MAIN/.claude/worktrees/pool-7"
PUSHER="$TMPROOT/pusher"
git init --bare -q "$ORIGIN"
git -c init.defaultBranch=master init -q "$MAIN"
git -C "$MAIN" config user.email t@t
git -C "$MAIN" config user.name test
git -C "$MAIN" remote add origin "$ORIGIN"
echo v1 > "$MAIN/file"
git_q "$MAIN" add file
git_q "$MAIN" commit -m v1
git_q "$MAIN" branch -M master
git_q "$MAIN" push -u origin master
mkdir -p "$MAIN/.claude/worktrees"
git_q "$MAIN" worktree add "$WT" -b claude/123-topic
git -C "$WT" config user.email t@t
git -C "$WT" config user.name test
git clone -q "$ORIGIN" "$PUSHER"
git -C "$PUSHER" config user.email t@t
git -C "$PUSHER" config user.name test

push_new_master_commit() {  # $1 = content tag; advances origin/master
    echo "$1" >> "$PUSHER/file"
    git_q "$PUSHER" add file
    git_q "$PUSHER" commit -m "$1"
    git_q "$PUSHER" push origin master
}

# task_branch <name> — put the worktree on a fresh pushed task branch off origin/master
task_branch() {
    git_q "$WT" fetch origin master
    git_q "$WT" checkout -B "$1" origin/master
    echo "$1" > "$WT/task.txt"
    git_q "$WT" add task.txt
    git_q "$WT" commit -m "$1"
    git_q "$WT" push -u origin "$1"
}

# --- stubs ---------------------------------------------------------------
BIN="$TMPROOT/bin"; mkdir -p "$BIN"
export STUB_LOG="$TMPROOT/stub.log"; : > "$STUB_LOG"
cat > "$BIN/gh" <<'STUB'
#!/usr/bin/env bash
# Models the two gh shapes fleet-start-next-task sends; anything else is a
# usage error, as the real binary would report an unknown flag.
printf 'gh %s\n' "$*" >> "$STUB_LOG"
if [[ "$1 $2" == "pr list" ]]; then
    shift 2
    head=""; state=""; json=""; jq=""
    while (( $# )); do
        case "$1" in
            --head) head="$2"; shift ;;
            --state) state="$2"; shift ;;
            --json) json="$2"; shift ;;
            --jq) jq="$2"; shift ;;
            *) echo "unknown flag: $1" >&2; exit 64 ;;
        esac
        shift
    done
    [[ -n "$head" && "$state" == "open" && "$json" == "number,url" && -n "$jq" ]] || { echo "bad pr list args" >&2; exit 64; }
    [[ -n "${STUB_PR_NUMBER:-}" ]] && echo "PR #$STUB_PR_NUMBER https://example.invalid/pull/$STUB_PR_NUMBER"
    exit 0
fi
if [[ "$1 $2" == "issue view" ]]; then
    [[ "$4 $5 $6 $7" == "--json title --jq .title" && "$3" =~ ^[0-9]+$ && $# -eq 7 ]] || { echo "bad issue view args" >&2; exit 64; }
    printf '%s\n' "${STUB_ISSUE_TITLE:-Untitled}"
    exit 0
fi
echo "unsupported gh call: $*" >&2; exit 64
STUB
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
printf 'fleet-claim %s\n' "$*" >> "$STUB_LOG"
[[ "$1 $2" == "molecule resume" && $# -eq 3 ]] || { echo "unsupported fleet-claim call: $*" >&2; exit 64; }
[[ -n "${STUB_MOLECULE_RC:-}" ]] && exit "$STUB_MOLECULE_RC"
[[ -n "${STUB_MOLECULE:-}" ]] && echo "$STUB_MOLECULE"
exit 0
STUB
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

run_tool() {  # runs the tool from the worktree; stdout+stderr captured, rc in $RC
    OUT=$( cd "$WT" && "$TOOL" "$@" 2>&1 ); RC=$?
}
branch_of() { git -C "$1" rev-parse --abbrev-ref HEAD; }

# =========================================================================
echo "T1: standard mode — pushed task branch resets to claude/pool-7-scratch at origin/master"
task_branch claude/123-topic
STUB_PR_NUMBER=5 run_tool
assert_eq "$RC" "0" "T1: exit 0"
assert_eq "$(branch_of "$WT")" "claude/pool-7-scratch" "T1: on the scratch branch"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$(git -C "$WT" rev-parse origin/master)" "T1: HEAD is origin/master"
assert_contains "$OUT" "mode:       standard" "T1: reports standard mode"
assert_contains "$OUT" "PR #5" "T1: reports the old branch's open PR"
assert_contains "$OUT" "old branch: claude/123-topic" "T1: names the old branch"
[[ -n "$(git -C "$WT" rev-parse --verify --quiet claude/123-topic)" ]] && ok "T1: old local branch left in place" || bad "T1: old branch deleted"
grep -q '^gh pr list --head claude/123-topic --state open --json number,url --jq ' "$STUB_LOG" && ok "T1: gh pr list called with the documented shape" || bad "T1: gh call shape: $(grep '^gh' "$STUB_LOG")"

echo "T2: refuses on tracked modifications"
task_branch claude/124-dirty
echo changed >> "$WT/file"
run_tool
assert_eq "$RC" "1" "T2: exit 1"
assert_eq "$(branch_of "$WT")" "claude/124-dirty" "T2: branch unchanged"
assert_contains "$OUT" "tracked modifications" "T2: names the reason"
git_q "$WT" checkout -- file

echo "T3: refuses when HEAD is on no origin/* ref (unpushed commit)"
echo more > "$WT/more.txt"; git_q "$WT" add more.txt; git_q "$WT" commit -m unpushed
run_tool
assert_eq "$RC" "1" "T3: exit 1"
assert_contains "$OUT" "on no origin/* ref" "T3: names the stranding hazard"
assert_eq "$(branch_of "$WT")" "claude/124-dirty" "T3: branch unchanged"
git_q "$WT" push origin claude/124-dirty

echo "T4: fleet-stack mode — molecule id bases the new branch on the old branch head"
task_branch claude/125-first
old_sha=$(git -C "$WT" rev-parse HEAD)
STUB_MOLECULE=456 STUB_ISSUE_TITLE="Add Foo: bar/baz  (phase 2)!" STUB_PR_NUMBER=9 run_tool
assert_eq "$RC" "0" "T4: exit 0"
assert_eq "$(branch_of "$WT")" "claude/456-add-foo-bar-baz-phase-2" "T4: claude/<id>-<slug> from the issue title"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$old_sha" "T4: based on the old branch head, not origin/master"
assert_contains "$OUT" "mode:       fleet-stack" "T4: reports fleet-stack mode"
assert_contains "$OUT" "base:       claude/125-first" "T4: names the upstream branch as base"

echo "T5: fleet-stack refuses when the molecule still names the current branch's issue"
task_branch claude/456-same
STUB_MOLECULE=456 run_tool
assert_eq "$RC" "1" "T5: exit 1"
assert_contains "$OUT" "advance it first" "T5: points at molecule advance"
assert_eq "$(branch_of "$WT")" "claude/456-same" "T5: branch unchanged"

echo "T6: --branch override, claude/ prepended when missing"
task_branch claude/126-x
run_tool --branch engine-next-thing
assert_eq "$RC" "0" "T6: exit 0"
assert_eq "$(branch_of "$WT")" "claude/engine-next-thing" "T6: prefixed branch name"
task_branch claude/127-y
run_tool --branch=claude/render-tuning
assert_eq "$(branch_of "$WT")" "claude/render-tuning" "T6: --branch= form, prefix kept"
run_tool --branch=
assert_eq "$RC" "2" "T6: empty --branch= is a usage error"

echo "T7: refuses in the shared main clone"
OUT=$( cd "$MAIN" && "$TOOL" 2>&1 ); RC=$?
assert_eq "$RC" "1" "T7: exit 1"
assert_contains "$OUT" "NOT a fleet worktree" "T7: fleet-assert-worktree's refusal"
assert_eq "$(branch_of "$MAIN")" "master" "T7: main clone untouched"

echo "T8: --dry-run prints the plan and changes nothing"
task_branch claude/128-dry
run_tool --dry-run
assert_eq "$RC" "0" "T8: exit 0"
assert_contains "$OUT" "new branch: claude/pool-7-scratch" "T8: plan names the new branch"
assert_contains "$OUT" "dry run: nothing changed" "T8: says nothing changed"
assert_eq "$(branch_of "$WT")" "claude/128-dry" "T8: branch unchanged"

echo "T9: the fetch is real — origin/master advanced elsewhere lands on the new tip"
task_branch claude/129-fetch
push_new_master_commit v2
run_tool
assert_eq "$RC" "0" "T9: exit 0"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$(git -C "$PUSHER" rev-parse origin/master)" "T9: HEAD is the advanced origin/master"

echo "T10: a pushed old branch with no open PR is reported, not refused"
task_branch claude/130-closed
STUB_PR_NUMBER="" run_tool
assert_eq "$RC" "0" "T10: exit 0"
assert_contains "$OUT" "no open PR for claude/130-closed" "T10: reports the missing PR"

echo "T11: untracked scratch bodies do not block"
task_branch claude/131-junk
echo stale > "$WT/.review-body.md"; mkdir -p "$WT/junk"; echo x > "$WT/junk/file"
run_tool
assert_eq "$RC" "0" "T11: exit 0"
assert_eq "$(branch_of "$WT")" "claude/pool-7-scratch" "T11: reset happened"
rm -rf "$WT/.review-body.md" "$WT/junk"

echo "T12: --help prints the header and no code"
OUT=$("$TOOL" --help 2>&1); RC=$?
assert_eq "$RC" "0" "T12: exit 0"
assert_contains "$OUT" "fleet-start-next-task [--branch <name>]" "T12: usage line"
assert_absent "$OUT" "set -euo" "T12: no code lines leak into --help"
OUT=$("$TOOL" --bogus 2>&1); RC=$?
assert_eq "$RC" "2" "T12: unknown argument is a usage error"

echo "T13: detached HEAD at a pushed commit resets in standard mode"
task_branch claude/132-detach
git_q "$WT" checkout --detach
run_tool
assert_eq "$RC" "0" "T13: exit 0"
assert_contains "$OUT" "old branch: HEAD" "T13: reports detached HEAD"
assert_eq "$(branch_of "$WT")" "claude/pool-7-scratch" "T13: on the scratch branch"

echo "T14: a failing fleet-claim molecule resume surfaces as exit 2"
task_branch claude/133-fault
STUB_MOLECULE_RC=3 run_tool
assert_eq "$RC" "2" "T14: exit 2"
assert_contains "$OUT" "molecule resume pool-7 failed" "T14: names the failing call"
assert_eq "$(branch_of "$WT")" "claude/133-fault" "T14: branch unchanged"

echo "T15: stub fidelity — the gh stub rejects a flag the real binary rejects"
"$BIN/gh" pr list --head x --state open --json number,url --jq . --limit 5 >/dev/null 2>&1; RC=$?
assert_eq "$RC" "64" "T15: unknown pr-list flag rejected"
"$BIN/gh" issue view 12 --json title --jq .title --repo x >/dev/null 2>&1; RC=$?
assert_eq "$RC" "64" "T15: extra issue-view flag rejected"

summarize "fleet-start-next-task tests"
