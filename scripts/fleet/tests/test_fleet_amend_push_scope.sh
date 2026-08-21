#!/usr/bin/env bash
# Tests for fleet-pr-amend-push.
#
# Part 1 — the #2402 worktree-scope assert: the push runs from the cwd repo, so
# a stale amend-ref sentinel in a shared main clone would route it from the
# wrong tree. The wrapper calls fleet-assert-worktree before touching the
# sentinel — refuse from a main clone, proceed from a worktree.
#
# Part 2 — the #2734 second-amend path: a successful push MARKS the sentinel
# consumed instead of deleting it, so a follow-up amend in the same detached
# checkout gets a diagnostic naming the real cause (and `--continue`) rather
# than "sentinel missing", whose named remedy (re-run fleet-pr-checkout-detached)
# silently discards the commit just made.
#
# Hermetic: part 1 `git init`s two sandbox repos (a "main clone" whose toplevel
# lacks the /.claude/worktrees/ segment, and a worktree-shaped one) and drives
# the real wrapper; part 2 adds a local bare repo as `origin` so the FF pushes
# are real git pushes over the filesystem. No network is reached.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

WRAPPER="$SCRIPT_DIR/fleet-pr-amend-push"
[[ -x "$WRAPPER" ]] || { echo "SKIP: fleet-pr-amend-push not executable at $WRAPPER" >&2; exit 3; }

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-amend-scope.XXXXXX")
trap '[[ -n "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"' EXIT

# The wrapper's worktree assert is path-shaped; an inherited assignment from the
# surrounding fleet session would scope it to a real worktree instead.
unset FLEET_ASSIGNED_WORKTREE FLEET_ALLOW_MAIN_CLONE 2>/dev/null || true

MAIN="$TMPROOT/mainclone"
WT="$TMPROOT/eng/.claude/worktrees/worker-3"
mkdir -p "$MAIN" "$WT"
git -C "$MAIN" init -q
git -C "$WT" init -q

# run <cwd> [env-assignments...] → prints "<exit>\n<stderr>"
run_in() {
    local dir="$1"; shift
    ( cd "$dir" && "$@" "$WRAPPER" >/dev/null 2>"$TMPROOT/err"; echo "$?" )
}

echo "main clone: assert refuses before the sentinel is read"
rc=$(run_in "$MAIN")
assert_eq "$rc" "1" "amend-push refuses from a main-clone cwd (exit 1)"
assert_contains "$(cat "$TMPROOT/err")" "NOT a fleet worktree" \
    "refusal names the worktree guard"

echo "worktree: assert passes, falls through to the sentinel-missing check"
rc=$(run_in "$WT")
assert_eq "$rc" "1" "amend-push proceeds past the assert in a worktree (then sentinel-missing)"
assert_contains "$(cat "$TMPROOT/err")" "missing" \
    "got past the assert to the sentinel check (proves the assert allowed it)"

echo "main clone + FLEET_ALLOW_MAIN_CLONE: override lets it reach the sentinel check"
rc=$(run_in "$MAIN" env FLEET_ALLOW_MAIN_CLONE=1)
assert_eq "$rc" "1" "override reaches the sentinel-missing check (not the worktree refusal)"
assert_absent "$(cat "$TMPROOT/err")" "NOT a fleet worktree" \
    "override suppresses the worktree refusal"

echo "usage: an unknown argument is a usage error, --continue is not"
rc=$( cd "$WT" && "$WRAPPER" --bogus >/dev/null 2>"$TMPROOT/err"; echo "$?" )
assert_eq "$rc" "2" "unknown argument exits 2"
assert_contains "$(cat "$TMPROOT/err")" "--continue" \
    "usage error names the accepted flags"
rc=$( cd "$WT" && "$WRAPPER" --continue >/dev/null 2>"$TMPROOT/err"; echo "$?" )
assert_eq "$rc" "1" "--continue with no sentinel is a runtime refusal, not a usage error"
assert_contains "$(cat "$TMPROOT/err")" "nothing to continue" \
    "--continue names its own missing-sentinel case"

# ----------------------------------------------------------------------
# Part 2 — #2734: the second amend in one detached checkout.
# ----------------------------------------------------------------------

ORIGIN="$TMPROOT/origin.git"
PR_WT="$TMPROOT/eng2/.claude/worktrees/worker-9"
git init -q --bare "$ORIGIN"
mkdir -p "$(dirname "$PR_WT")"
git clone -q "$ORIGIN" "$PR_WT"
git -C "$PR_WT" config user.email t@t
git -C "$PR_WT" config user.name t

echo base > "$PR_WT/f.txt"
git -C "$PR_WT" add f.txt
git -C "$PR_WT" commit -qm base
git -C "$PR_WT" push -q origin HEAD:refs/heads/feature
git -C "$PR_WT" fetch -q origin feature
git -C "$PR_WT" checkout -q --detach origin/feature

SENTINEL="$PR_WT/.git/fleet-amend-ref"
write_sentinel() { printf '%s\n%s\n' "feature" "$(git -C "$PR_WT" rev-parse HEAD)" > "$SENTINEL"; }
write_sentinel

# amend_push <args...> → prints exit code; stderr in $TMPROOT/err, stdout in $TMPROOT/out
amend_push() {
    ( cd "$PR_WT" && "$WRAPPER" "$@" >"$TMPROOT/out" 2>"$TMPROOT/err"; echo "$?" )
}

echo "first amend: fast-forwards and marks the sentinel consumed (does not delete it)"
echo "fix one" >> "$PR_WT/f.txt"
git -C "$PR_WT" commit -qam "first fix"
rc=$(amend_push)
assert_eq "$rc" "0" "first amend-push succeeds"
assert_contains "$(cat "$TMPROOT/out")" "fast-forwarded" "first push took the FF path"
if [[ -f "$SENTINEL" ]]; then ok "sentinel survives the push (marked, not removed)"; else bad "sentinel survives the push (marked, not removed)"; fi
assert_contains "$(sed -n '3p' "$SENTINEL")" "consumed" "line 3 records the consumption"
assert_eq "$(sed -n '1p' "$SENTINEL")" "feature" "line 1 still carries the head ref"

echo "second amend: the diagnostic names the real cause, not a missing setup step"
echo "fix two" >> "$PR_WT/f.txt"
git -C "$PR_WT" commit -qam "second fix"
LOST=$(git -C "$PR_WT" rev-parse HEAD)
rc=$(amend_push)
assert_eq "$rc" "1" "second bare amend-push refuses"
assert_contains "$(cat "$TMPROOT/err")" "already consumed" \
    "refusal names the consumed sentinel"
assert_contains "$(cat "$TMPROOT/err")" "fleet-pr-amend-push --continue" \
    "refusal names the non-destructive recovery"
assert_absent "$(cat "$TMPROOT/err")" "was fleet-pr-checkout-detached run in this worktree" \
    "refusal does NOT steer at the destructive remedy"

echo "--continue: lands the second amend on the fast-forward path"
rc=$(amend_push --continue)
assert_eq "$rc" "0" "--continue succeeds"
assert_eq "$(git -C "$PR_WT" rev-parse origin/feature)" "$LOST" \
    "the commit CI demanded is now the remote tip"
assert_contains "$(sed -n '3p' "$SENTINEL")" "consumed" \
    "--continue re-marks the sentinel (one push per --continue)"

echo "--continue refuses when HEAD does not descend the remote tip"
git -C "$PR_WT" checkout -q --detach "$(git -C "$PR_WT" rev-parse origin/feature~2)"
echo divergent > "$PR_WT/f.txt"
git -C "$PR_WT" commit -qam "divergent rewrite"
rc=$(amend_push --continue)
assert_eq "$rc" "1" "--continue refuses a non-fast-forward"
assert_contains "$(cat "$TMPROOT/err")" "does not descend" \
    "refusal explains why"
assert_contains "$(cat "$TMPROOT/err")" "git branch" \
    "refusal names the preserve-first remedy"
assert_eq "$(git -C "$PR_WT" rev-parse origin/feature)" "$LOST" \
    "--continue never force-pushed: the remote tip is unchanged"

echo "anti-clobber (#1338/#1340) survives the mark-consumed rewrite"
# Non-FF push, leased against the sentinel's checkout-time base SHA, while
# another agent has moved origin/feature since that checkout: the lease must
# still REFUSE rather than clobber. Only the sentinel's disposal changed here,
# but that disposal sits on the same path.
git -C "$PR_WT" checkout -q --detach origin/feature
write_sentinel                                    # base_sha == the current tip
OTHER="$TMPROOT/other"
git clone -q "$ORIGIN" "$OTHER" 2>/dev/null
git -C "$OTHER" config user.email o@o
git -C "$OTHER" config user.name o
git -C "$OTHER" checkout -q -B feature origin/feature
echo "another agent's commit" >> "$OTHER/f.txt"
git -C "$OTHER" commit -qam "concurrent push"
git -C "$OTHER" push -q origin feature
THEIRS=$(git -C "$OTHER" rev-parse HEAD)
git -C "$PR_WT" commit -q --amend -m "amended, does not descend their push"
rc=$(amend_push)
assert_eq "$rc" "1" "the lease refuses when origin/feature moved since checkout"
assert_contains "$(cat "$TMPROOT/err")" "push REFUSED" "refusal names the moved ref"
git -C "$PR_WT" fetch -q origin feature
assert_eq "$(git -C "$PR_WT" rev-parse origin/feature)" "$THEIRS" \
    "the other agent's commit was not clobbered"
assert_absent "$(sed -n '3p' "$SENTINEL")" "consumed" \
    "a REFUSED push does not spend the sentinel"

echo "legacy one-line sentinel still routes normally (no migration needed)"
git -C "$PR_WT" fetch -q origin feature
git -C "$PR_WT" checkout -q --detach origin/feature
printf 'feature\n' > "$SENTINEL"
echo "fix three" >> "$PR_WT/f.txt"
git -C "$PR_WT" commit -qam "third fix"
rc=$(amend_push)
assert_eq "$rc" "0" "a two-field-less legacy sentinel is treated as live, not consumed"
assert_absent "$(cat "$TMPROOT/err")" "already consumed" \
    "legacy sentinel is not misread as consumed"

summarize "fleet-pr-amend-push tests (scope guard + second amend)"
