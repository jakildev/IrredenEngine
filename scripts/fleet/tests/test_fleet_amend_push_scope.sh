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
# Part 3 — print_rewrite_diagnostics: every non-fast-forward with a checkout-
# time base prints base..HEAD's stat before push execution and warns for each
# text file whose deletions exceed its insertions. Fast-forward, legacy-
# sentinel, binary and unreadable-base behavior stays explicit.
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
assert_absent "$(cat "$TMPROOT/out")" "non-fast-forward comparison" \
    "ordinary fast-forward output has no rewrite diagnostic"
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
assert_absent "$(cat "$TMPROOT/out")" "non-fast-forward comparison" \
    "--continue output has no rewrite diagnostic"

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
CONCURRENT_BASE=$(git -C "$PR_WT" rev-parse HEAD)
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
assert_contains "$(cat "$TMPROOT/out")" "non-fast-forward comparison $CONCURRENT_BASE..HEAD" \
    "comparison stays anchored to the sentinel base after fetch"
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

# ----------------------------------------------------------------------
# Part 3 — diagnostics before a history-rewriting push.
# ----------------------------------------------------------------------

DIAG_ORIGIN="$TMPROOT/diag-origin.git"
DIAG_WT="$TMPROOT/eng3/.claude/worktrees/worker-7"
git init -q --bare "$DIAG_ORIGIN"
mkdir -p "$(dirname "$DIAG_WT")"
git clone -q "$DIAG_ORIGIN" "$DIAG_WT"
git -C "$DIAG_WT" config user.email d@d
git -C "$DIAG_WT" config user.name d

echo base > "$DIAG_WT/implementation.txt"
git -C "$DIAG_WT" add implementation.txt
git -C "$DIAG_WT" commit -qm "diagnostic root"
DIAG_ROOT=$(git -C "$DIAG_WT" rev-parse HEAD)
DIAG_SENTINEL="$DIAG_WT/.git/fleet-amend-ref"

REAL_GIT=$(command -v git)
GIT_SHIM_DIR="$TMPROOT/git-shim"
mkdir -p "$GIT_SHIM_DIR"
cat > "$GIT_SHIM_DIR/git" <<'SHIM'
#!/usr/bin/env bash
if [[ "${1:-}" == "push" ]]; then
    echo "PUSH_EXECUTION_MARKER"
fi
exec "$REAL_GIT" "$@"
SHIM
chmod +x "$GIT_SHIM_DIR/git"

write_diag_sentinel() {
    printf '%s\n%s\n' "$1" "$2" > "$DIAG_SENTINEL"
}

diag_push() {
    ( cd "$DIAG_WT" && env PATH="$GIT_SHIM_DIR:$PATH" REAL_GIT="$REAL_GIT" \
        "$WRAPPER" >"$TMPROOT/diag-out" 2>&1; echo "$?" )
}

assert_before() {
    local text="$1" first="$2" second="$3" label="$4"
    case "$text" in
        *"$first"*"$second"*) ok "$label" ;;
        *) bad "$label" ;;
    esac
}

echo "non-FF A/B/C rewrite: stat and per-file removals print before push"
git -C "$DIAG_WT" checkout -q -B diag-abc "$DIAG_ROOT"
echo "fix line" >> "$DIAG_WT/implementation.txt"
printf 'regression one\nregression two\n' > "$DIAG_WT/regression.txt"
git -C "$DIAG_WT" add implementation.txt regression.txt
git -C "$DIAG_WT" commit -qm "fix and regression lock"
git -C "$DIAG_WT" push -q origin HEAD:diag-abc
ABC_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
printf 'one\ntwo\nthree\nfour\nfive\n' > "$DIAG_WT/notes.txt"
git -C "$DIAG_WT" add notes.txt
git -C "$DIAG_WT" commit -qm "unrelated rewrite"
ABC_HEAD=$(git -C "$DIAG_WT" rev-parse HEAD)
write_diag_sentinel diag-abc "$ABC_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "A/B/C rewrite push succeeds"
assert_contains "$diag_output" "non-fast-forward comparison $ABC_BASE..HEAD" \
    "rewrite heading names the checkout-time comparison"
assert_contains "$diag_output" "3 files changed, 5 insertions(+), 3 deletions(-)" \
    "rewrite prints the base-to-HEAD diff stat"
assert_contains "$diag_output" "net-removed content in implementation.txt (1 more deleted line than added)" \
    "warning reports the dropped fix line"
assert_contains "$diag_output" "net-removed content in regression.txt (2 more deleted lines than added)" \
    "warning reports the dropped regression lock"
assert_before "$diag_output" "non-fast-forward comparison" "PUSH_EXECUTION_MARKER" \
    "diagnostics are emitted before push execution"
assert_eq "$(git -C "$DIAG_WT" rev-parse origin/diag-abc)" "$ABC_HEAD" \
    "rewrite updates the remote to HEAD"
assert_contains "$(sed -n '3p' "$DIAG_SENTINEL")" "consumed" \
    "successful diagnostic rewrite consumes the sentinel"

echo "same-tree message amend: heading prints, removal warning does not"
git -C "$DIAG_WT" checkout -q -B diag-same "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "original message"
git -C "$DIAG_WT" push -q origin HEAD:diag-same
SAME_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$SAME_BASE"
git -C "$DIAG_WT" commit -q --amend --allow-empty -m "amended message"
write_diag_sentinel diag-same "$SAME_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "same-tree amend succeeds"
assert_contains "$diag_output" "non-fast-forward comparison $SAME_BASE..HEAD" \
    "same-tree amend still prints its comparison heading"
assert_absent "$diag_output" "net-removed content" \
    "same-tree amend makes no removal claim"

echo "additive and balanced rewrites show stats without removal warnings"
git -C "$DIAG_WT" checkout -q -B diag-add "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "remote message-only tip"
git -C "$DIAG_WT" push -q origin HEAD:diag-add
ADD_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
printf 'added one\nadded two\n' > "$DIAG_WT/additive.txt"
git -C "$DIAG_WT" add additive.txt
git -C "$DIAG_WT" commit -qm "additive rewrite"
write_diag_sentinel diag-add "$ADD_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "additive rewrite succeeds"
assert_contains "$diag_output" "additive.txt" "additive rewrite prints its stat"
assert_absent "$diag_output" "net-removed content" \
    "additive rewrite does not warn"

git -C "$DIAG_WT" checkout -q -B diag-balanced "$DIAG_ROOT"
printf 'remote\n' > "$DIAG_WT/implementation.txt"
git -C "$DIAG_WT" commit -qam "remote replacement"
git -C "$DIAG_WT" push -q origin HEAD:diag-balanced
BALANCED_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
printf 'local\n' > "$DIAG_WT/implementation.txt"
git -C "$DIAG_WT" commit -qam "local replacement"
write_diag_sentinel diag-balanced "$BALANCED_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "balanced rewrite succeeds"
assert_contains "$diag_output" "1 insertion(+), 1 deletion(-)" \
    "balanced rewrite prints its stat"
assert_absent "$diag_output" "net-removed content" \
    "balanced rewrite does not warn"

echo "pure dropped commit: path spaces are preserved in the warning"
git -C "$DIAG_WT" checkout -q -B diag-drop "$DIAG_ROOT"
printf 'first\nsecond\n' > "$DIAG_WT/regression test.txt"
git -C "$DIAG_WT" add "regression test.txt"
git -C "$DIAG_WT" commit -qm "test-only commit"
git -C "$DIAG_WT" push -q origin HEAD:diag-drop
DROP_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "drop test commit"
write_diag_sentinel diag-drop "$DROP_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "pure dropped-commit rewrite succeeds"
assert_contains "$diag_output" "net-removed content in regression test.txt (2 more deleted lines than added)" \
    "warning preserves a path containing spaces"

echo "binary rewrite: stat stays visible and binary counts do not warn"
git -C "$DIAG_WT" checkout -q -B diag-binary "$DIAG_ROOT"
printf '\000remote\001' > "$DIAG_WT/blob.bin"
git -C "$DIAG_WT" add blob.bin
git -C "$DIAG_WT" commit -qm "remote binary"
git -C "$DIAG_WT" push -q origin HEAD:diag-binary
BINARY_BASE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
printf '\000local\002' > "$DIAG_WT/blob.bin"
git -C "$DIAG_WT" add blob.bin
git -C "$DIAG_WT" commit -qm "local binary"
write_diag_sentinel diag-binary "$BINARY_BASE"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "binary rewrite succeeds"
assert_contains "$diag_output" "blob.bin" "binary rewrite remains visible in the stat"
assert_absent "$diag_output" "net-removed content" \
    "uncountable binary numstat row does not warn"

echo "legacy and invalid bases preserve fail-safe behavior"
git -C "$DIAG_WT" checkout -q -B diag-legacy "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "legacy remote"
git -C "$DIAG_WT" push -q origin HEAD:diag-legacy
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "legacy rewrite"
printf 'diag-legacy\n' > "$DIAG_SENTINEL"
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "0" "legacy non-FF push retains its existing behavior"
assert_contains "$diag_output" "sentinel has no base SHA" \
    "legacy non-FF push retains the bare-lease warning"
assert_absent "$diag_output" "non-fast-forward comparison" \
    "legacy non-FF push does not invent a comparison base"

git -C "$DIAG_WT" checkout -q -B diag-invalid "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "invalid-base remote"
git -C "$DIAG_WT" push -q origin HEAD:diag-invalid
INVALID_REMOTE=$(git -C "$DIAG_WT" rev-parse HEAD)
git -C "$DIAG_WT" checkout -q --detach "$DIAG_ROOT"
git -C "$DIAG_WT" commit -q --allow-empty -m "invalid-base rewrite"
write_diag_sentinel diag-invalid 0000000000000000000000000000000000000000
rc=$(diag_push)
diag_output=$(cat "$TMPROOT/diag-out")
assert_eq "$rc" "1" "unreadable comparison base refuses the push"
assert_contains "$diag_output" "could not read rewrite comparison" \
    "invalid-base refusal names the comparison failure"
assert_before "$diag_output" "non-fast-forward comparison" "nothing was pushed" \
    "invalid-base diagnostic fails before any push"
assert_absent "$diag_output" "PUSH_EXECUTION_MARKER" \
    "invalid-base path never invokes git push"
assert_eq "$(git -C "$DIAG_WT" rev-parse origin/diag-invalid)" "$INVALID_REMOTE" \
    "invalid-base refusal leaves the remote unchanged"
assert_absent "$(sed -n '3p' "$DIAG_SENTINEL")" "consumed" \
    "invalid-base refusal leaves the sentinel unconsumed"

summarize "fleet-pr-amend-push tests"
