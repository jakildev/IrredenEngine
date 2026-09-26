#!/usr/bin/env bash
# Tests that fleet-rebase's scratch worktree setup is scoped to its own path,
# and that every suite driving fleet-rebase keeps it off a live clone.
#
#   T1: the clone carries a sibling worktree registration whose directory is
#       gone (how another host's registrations look to this host's git) and a
#       stale registration of the scratch path itself. A dry-run rebase still
#       creates the scratch worktree and rebases cleanly, the sibling
#       registration survives, and the scratch path is registered exactly once.
#   T2: ratchet — every suite that assigns a path to the real fleet-rebase
#       exports FLEET_ENGINE_ROOT under its own $TMPROOT. fleet-rebase resolves
#       its clone from that variable before $HOME, so a suite that sandboxes
#       only HOME inherits whatever clone the caller's environment names.
#
# Worktree paths are compared by suffix: `git worktree list --porcelain`
# prints the realpath, which is not the $TMPROOT spelling on macOS
# (/private/var/...) or under native-Windows git (C:/...).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"

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
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin"

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

REMOTE="$TMPROOT/remote.git"
ENGINE="$FLEET_ENGINE_ROOT"
AUTH="$TMPROOT/auth"

git init -q --bare -b master "$REMOTE"
git clone -q "$REMOTE" "$AUTH" 2>/dev/null
git -C "$AUTH" config commit.gpgsign false
echo init > "$AUTH/readme.txt"
git -C "$AUTH" add readme.txt
git -C "$AUTH" commit -q -m init
git -C "$AUTH" push -q origin master

# feat-behind forks before master advances on a disjoint file, so it rebases
# cleanly onto origin/master.
git -C "$AUTH" checkout -q -b feat-behind master
echo feature > "$AUTH/feature.txt"
git -C "$AUTH" add feature.txt
git -C "$AUTH" commit -q -m feature
git -C "$AUTH" push -q origin feat-behind
git -C "$AUTH" checkout -q master
echo later > "$AUTH/later.txt"
git -C "$AUTH" add later.txt
git -C "$AUTH" commit -q -m "master advances"
git -C "$AUTH" push -q origin master

git clone -q "$REMOTE" "$ENGINE" 2>/dev/null
git -C "$ENGINE" config commit.gpgsign false

# Both registrations point at directories that no longer exist.
SIBLING="$ENGINE/.claude/worktrees/pool-x"
SCRATCH="$FLEET_REBASE_SCRATCH/engine"
mkdir -p "$ENGINE/.claude/worktrees" "$FLEET_REBASE_SCRATCH"
git -C "$ENGINE" worktree add -q --detach "$SIBLING" 2>/dev/null
git -C "$ENGINE" worktree add -q --detach "$SCRATCH" 2>/dev/null
rm -rf "$SIBLING" "$SCRATCH"

# gh stub: a dry run never reaches the pre-push preflight, so any call is
# unmodelled and fails closed.
cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
echo "gh stub: unmodelled call: $*" >&2
exit 1
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

printf '{"prs": [{"repo":"engine","number":700,"headRefName":"feat-behind","baseRefName":"master","mergeable":"CONFLICTING","labels":["fleet:approved"]}]}\n' \
    > "$FLEET_STATE_DIR/projections/merger.json"

# registrations_ending <suffix> -> count of porcelain worktree lines ending so
registrations_ending() {
    git -C "$ENGINE" worktree list --porcelain \
        | tr -d '\r' \
        | grep -c -- "^worktree .*$1\$" || true
}

# === T1 ======================================================================
echo "T1: scratch setup leaves sibling registrations alone"
assert_eq "$(registrations_ending /.claude/worktrees/pool-x)" "1" \
    "T1 fixture: the sibling is registered before the run"
assert_eq "$(registrations_ending /.fleet/rebase-scratch/engine)" "1" \
    "T1 fixture: the scratch path carries a stale registration before the run"

T1=$("$REBASE" --auto --dry-run 2>&1 || true)
assert_contains "$T1" "engine#700: clean rebase onto origin/master (dry-run; not pushed)" \
    "T1 the scratch worktree was created and the rebase ran"
assert_eq "$(registrations_ending /.claude/worktrees/pool-x)" "1" \
    "T1 the missing sibling registration survives the scratch setup"
assert_eq "$(registrations_ending /.fleet/rebase-scratch/engine)" "1" \
    "T1 the scratch path is registered exactly once"
if [[ -d "$SCRATCH" ]]; then ok "T1 the scratch worktree exists on disk"; else bad "T1 the scratch worktree exists on disk"; fi

# === T2 ======================================================================
echo "T2: every suite that drives fleet-rebase pins FLEET_ENGINE_ROOT under \$TMPROOT"
population=0
offenders=()
for f in "$TESTS_DIR"/test_*.sh; do
    grep -qE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*="[^"]*/fleet-rebase"[[:space:]]*$' "$f" || continue
    population=$((population + 1))
    grep -qE '^[[:space:]]*export FLEET_ENGINE_ROOT="\$TMPROOT/' "$f" \
        || offenders+=("$(basename "$f")")
done
echo "  ratchet population: $population suite(s)"
if (( population >= 7 )); then
    ok "T2 the ratchet population is at least 7 ($population)"
else
    bad "T2 the ratchet population is at least 7 (got $population)"
fi
if (( ${#offenders[@]} == 0 )); then
    ok "T2 no suite drives fleet-rebase without the pin"
else
    bad "T2 suites drive fleet-rebase without the pin: ${offenders[*]}"
fi

# --- Summary ------------------------------------------------------------------
summarize "fleet-rebase scratch scope tests"
