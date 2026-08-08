#!/usr/bin/env bash
# Tests for fleet-pr-checkout-detached's #2734 orphan guard: the wrapper must
# refuse, before moving HEAD, when the current HEAD carries commits reachable
# from no branch and no remote ref — and must NOT refuse on the routine
# cross-PR re-checkout, which is what the bounced `origin/<ref>..HEAD`
# predicate did.
#
# The positive-fire pair is the point of this suite: only running BOTH arms
# distinguishes the shipped predicate (`HEAD --not --branches --remotes`) from
# the one that was rejected. A third arm covers the benign-orphan case — a tip
# a merger force-push superseded — where the refusal must classify the commit
# as superseded rather than as work to preserve.
#
# Hermetic: a local bare repo stands in for `origin` (pushes and fetches are
# filesystem operations, no network) and `gh` is stubbed on PATH. The stub
# models `gh pr view <N> [--repo <slug>] --json headRefName -q <expr>` and
# rejects anything outside that surface the way the real binary does, so the
# suite cannot certify a call gh would refuse (#2781).

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

WRAPPER="$SCRIPT_DIR/fleet-pr-checkout-detached"
[[ -x "$WRAPPER" ]] || { echo "SKIP: fleet-pr-checkout-detached not executable at $WRAPPER" >&2; exit 3; }

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-detach-orphan.XXXXXX")
trap '[[ -n "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"' EXIT

unset FLEET_ASSIGNED_WORKTREE FLEET_ALLOW_MAIN_CLONE 2>/dev/null || true

BIN="$TMPROOT/bin"
mkdir -p "$BIN"
cat >"$BIN/gh" <<'GHEOF'
#!/usr/bin/env bash
# Minimal `gh` stub: models `gh pr view <N> [--repo <slug>] --json headRefName
# -q <expr>` and nothing else. Unmodelled commands, flags and field selections
# exit 1 with a message on stderr, mirroring how the real binary rejects them,
# so a wrapper edit that starts passing something gh would refuse fails here
# instead of being silently certified (#2781).
set -uo pipefail
[[ "${1:-}" == "pr" && "${2:-}" == "view" ]] || { echo "unknown command: ${*}" >&2; exit 1; }
shift 2
pr="${1:-}"; shift 2>/dev/null || true
[[ "$pr" =~ ^[0-9]+$ ]] || { echo "expected a PR number, got '$pr'" >&2; exit 1; }
json=""; jq_expr=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)   [[ $# -ge 2 ]] || { echo "--repo requires a value" >&2; exit 1; }; shift 2 ;;
        --json)   [[ $# -ge 2 ]] || { echo "--json requires a value" >&2; exit 1; }; json="$2"; shift 2 ;;
        -q|--jq)  [[ $# -ge 2 ]] || { echo "-q requires a value" >&2; exit 1; }; jq_expr="$2"; shift 2 ;;
        *)        echo "unknown flag: $1" >&2; exit 1 ;;
    esac
done
[[ "$json" == "headRefName" ]] || { echo "unmodelled --json '$json'" >&2; exit 1; }
[[ "$jq_expr" == ".headRefName" ]] || { echo "unmodelled -q '$jq_expr'" >&2; exit 1; }
printf '%s\n' "${FAKE_HEAD_REF:-feature-b}"
GHEOF
chmod +x "$BIN/gh"

echo "stub fidelity: the gh stub rejects what the real binary rejects"
rc=$( "$BIN/gh" pr view 7 --jsonx headRefName >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "1" "stub rejects an unknown flag (so it cannot certify one)"
rc=$( "$BIN/gh" pr list >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "1" "stub rejects an unmodelled subcommand"
rc=$( "$BIN/gh" pr view 7 --json headRefName -q .headRefName >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "0" "stub answers the exact call the wrapper makes"

# ----------------------------------------------------------------------
# Sandbox: bare origin + a worktree-shaped clone with branches A and B.
# ----------------------------------------------------------------------
ORIGIN="$TMPROOT/origin.git"
WT="$TMPROOT/eng/.claude/worktrees/worker-4"
git init -q --bare "$ORIGIN"
mkdir -p "$(dirname "$WT")"
git clone -q "$ORIGIN" "$WT" 2>/dev/null
git -C "$WT" config user.email t@t
git -C "$WT" config user.name t

echo base > "$WT/f.txt"
git -C "$WT" add f.txt
git -C "$WT" commit -qm M1
git -C "$WT" push -q origin HEAD:refs/heads/master

git -C "$WT" checkout -q -b feature-a
echo a1 > "$WT/a.txt"; git -C "$WT" add a.txt; git -C "$WT" commit -qm A1
echo a2 >> "$WT/a.txt"; git -C "$WT" commit -qam A2
git -C "$WT" push -q origin feature-a

git -C "$WT" checkout -q -B feature-b master
echo b1 > "$WT/b.txt"; git -C "$WT" add b.txt; git -C "$WT" commit -qm B1
git -C "$WT" push -q origin feature-b

git -C "$WT" fetch -q origin
# Park the way fleet-pr-checkout-detached parks: detached, with NO local branch
# holding the tip. Leaving the local branches in place makes --branches reach
# every tip and the predicate read 0 for the wrong reason.
git -C "$WT" checkout -q --detach origin/feature-a
git -C "$WT" branch -q -D feature-a feature-b master

# detach <ref...> → run the wrapper from the worktree; exit code on stdout,
# stderr in $TMPROOT/err.
detach() {
    ( cd "$WT" && PATH="$BIN:$PATH" "$WRAPPER" "$@" >/dev/null 2>"$TMPROOT/err"; echo "$?" )
}

echo "positive-fire 1: benign cross-PR re-checkout SUCCEEDS (nothing at risk)"
before=$(git -C "$WT" rev-parse HEAD)
rc=$(detach 42)
assert_eq "$rc" "0" "detached at PR A's fully pushed tip, checking out B succeeds"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$(git -C "$WT" rev-parse origin/feature-b)" \
    "HEAD moved to origin/feature-b"
assert_absent "$(cat "$TMPROOT/err")" "REFUSING" "no refusal on the benign path"
if [[ -f "$WT/.git/fleet-amend-ref" ]]; then ok "sentinel written on success"; else bad "sentinel written on success"; fi
assert_eq "$(sed -n '1p' "$WT/.git/fleet-amend-ref")" "feature-b" "sentinel names the head ref"
[[ "$before" != "$(git -C "$WT" rev-parse HEAD)" ]] && ok "the two arms are distinguishable (HEAD actually moved)" \
    || bad "the two arms are distinguishable (HEAD actually moved)"

echo "positive-fire 2: one unpushed commit on top REFUSES, HEAD unmoved"
echo unpushed >> "$WT/b.txt"
git -C "$WT" commit -qam "the fix commit CI demanded"
AT_RISK=$(git -C "$WT" rev-parse HEAD)
rc=$(detach 43)
assert_eq "$rc" "1" "refuses when HEAD carries an unpushed commit"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$AT_RISK" "HEAD is unmoved by the refusal"
assert_contains "$(cat "$TMPROOT/err")" "REFUSING" "refusal is loud"
assert_contains "$(cat "$TMPROOT/err")" "${AT_RISK:0:12}" "refusal names the at-risk SHA"
assert_contains "$(cat "$TMPROOT/err")" "AT RISK" "does not claim the fresh commit is superseded"
assert_contains "$(cat "$TMPROOT/err")" "git branch" "names the preserve-first remedy"
assert_contains "$(cat "$TMPROOT/err")" "fleet-pr-amend-push --continue" \
    "names the push-it-instead remedy"

echo "--discard: the documented opt-out proceeds, and git's own warning is no longer swallowed"
rc=$(detach 43 --discard)
assert_eq "$rc" "0" "--discard proceeds past the guard"
assert_eq "$(git -C "$WT" rev-parse HEAD)" "$(git -C "$WT" rev-parse origin/feature-b)" \
    "HEAD moved despite the orphan commit"
# The checkout's stderr redirect used to discard "Warning: you are leaving N
# commits behind" along with stdout, which is what made the loss silent.
assert_contains "$(cat "$TMPROOT/err")" "leaving" \
    "git's leaving-commits-behind warning reaches stderr"

echo "positive-fire 3: a tip superseded by a force-push is classified SUPERSEDED"
# Realistic shape: a second worktree parks on PR A the way the fleet parks it
# (through the wrapper, so the sentinel records feature-a), then another agent
# rebases and force-pushes that branch. The sentinel is the only record of which
# ref was rewritten — the ref being checked out is a different PR entirely.
WT3="$TMPROOT/eng/.claude/worktrees/worker-6"
git clone -q "$ORIGIN" "$WT3" 2>/dev/null
git -C "$WT3" config user.email t@t
git -C "$WT3" config user.name t
git -C "$WT3" fetch -q origin
rc=$( cd "$WT3" && PATH="$BIN:$PATH" FAKE_HEAD_REF=feature-a "$WRAPPER" 50 >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "0" "setup: worker-6 parks on PR A through the wrapper"
git -C "$WT3" branch -q -D master
PARKED=$(git -C "$WT3" rev-parse HEAD)

# Another agent advances master, rebases feature-a onto it, and force-pushes.
# (Rebasing onto a master the branch already sits on is a no-op that leaves the
# SHAs unchanged — this arm would then pass for the wrong reason.)
git -C "$WT" checkout -q -B advance-master origin/master
echo m2 >> "$WT/f.txt"
git -C "$WT" commit -qam M2
git -C "$WT" push -q origin advance-master:master
git -C "$WT" fetch -q origin
git -C "$WT" checkout -q -B rebase-tmp origin/feature-a
git -C "$WT" rebase -q origin/master >/dev/null 2>&1
git -C "$WT" push -q -f origin rebase-tmp:feature-a
git -C "$WT" checkout -q --detach origin/feature-b
git -C "$WT" branch -q -D rebase-tmp advance-master

git -C "$WT3" fetch -q origin --prune
assert_eq "$(git -C "$WT3" rev-list --count HEAD --not --branches --remotes)" "2" \
    "setup: the parked tip's 2 commits are now reachable from nothing"
rc=$( cd "$WT3" && PATH="$BIN:$PATH" "$WRAPPER" 51 >/dev/null 2>"$TMPROOT/err"; echo "$?" )
assert_eq "$rc" "1" "still refuses — the commits are genuinely reachable from nothing"
assert_eq "$(git -C "$WT3" rev-parse HEAD)" "$PARKED" "HEAD is unmoved by the refusal"
assert_contains "$(cat "$TMPROOT/err")" "SUPERSEDED" \
    "classifies the rewritten commits as superseded, not as work to preserve"
assert_contains "$(cat "$TMPROOT/err")" "--discard" \
    "points at --discard as the correct response for this case"
assert_absent "$(cat "$TMPROOT/err")" "AT RISK" \
    "does not tell the agent to preserve a commit the merger already rebased past"

echo "the classification is not vacuous: reachability-from-master could never say this"
for s in $(git -C "$WT3" rev-list HEAD --not --branches --remotes); do
    if git -C "$WT3" merge-base --is-ancestor "$s" origin/master 2>/dev/null; then
        bad "at-risk SHA ${s:0:8} is an ancestor of origin/master (unexpected)"
    else
        ok "at-risk SHA ${s:0:8} is NOT an ancestor of origin/master — the reachability test cannot classify it"
    fi
done

echo "fail-closed: a guard that cannot evaluate refuses rather than permitting"
# `git rev-list --count HEAD …` yields an empty string on an unborn HEAD, and an
# empty string tests as "not non-zero" — the guard must not read that as clean.
EMPTY_WT="$TMPROOT/eng/.claude/worktrees/worker-5"
mkdir -p "$EMPTY_WT"
git -C "$EMPTY_WT" init -q
git -C "$EMPTY_WT" remote add origin "$ORIGIN"
git -C "$EMPTY_WT" fetch -q origin
rc=$( cd "$EMPTY_WT" && PATH="$BIN:$PATH" "$WRAPPER" 45 >/dev/null 2>"$TMPROOT/err"; echo "$?" )
assert_eq "$rc" "1" "unborn HEAD refuses instead of silently permitting"
assert_contains "$(cat "$TMPROOT/err")" "could not evaluate" \
    "refusal names the un-evaluable guard rather than a bogus at-risk list"

echo "usage: --discard and --repo= parse, unknown flags do not"
rc=$( cd "$WT" && PATH="$BIN:$PATH" "$WRAPPER" 46 --bogus >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "2" "unknown flag is a usage error"
rc=$( cd "$WT" && PATH="$BIN:$PATH" "$WRAPPER" 46 --repo= >/dev/null 2>&1; echo "$?" )
assert_eq "$rc" "2" "an empty --repo= is rejected exactly as the space form is"

summarize "fleet-pr-checkout-detached orphan-guard tests (#2734)"
