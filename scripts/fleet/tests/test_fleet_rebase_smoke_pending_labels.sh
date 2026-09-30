#!/usr/bin/env bash
# Tests that a pending cross-host smoke label never parks a PR away from
# tier-0, for all three smoke labels alike.
#
# The human merges without waiting for smoke, and the smoke should run on
# the head that will merge, so fleet-rebase's SKIP_LABEL_RE matches no
# smoke label (the scout's drift guard pins the regex itself). Skipping them
# used to park every approved conflict until a host of that tier smoked a
# head that could not merge.
#
#   T1: base != master, fleet:needs-linux-smoke   -> attempt
#   T2: base != master, fleet:needs-macos-smoke   -> attempt
#   T3: base != master, fleet:needs-windows-smoke -> attempt
#   T4: no smoke label -> attempt (control)
#   T5: base == master, CONFLICTING, fleet:needs-windows-smoke -> attempt
#   T6: a persistent ownership claim still blocks the attempt
#
# All run --auto --dry-run: attempt_pr's git push happens against a local
# bare remote in the sandbox, so a "clean rebase onto" or "attempted=1" line
# is proof branch work was attempted, not just logged.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "test setup: fleet-rebase not executable at $REBASE" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""

cleanup() {
    if [[ -n "$TMPROOT" && -d "$TMPROOT" ]]; then
        rm -rf "$TMPROOT"
    fi
}
trap cleanup EXIT

# --- Sandbox ------------------------------------------------------------------
TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin" "$TMPROOT/log"

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

REMOTE="$TMPROOT/remote.git"
ENGINE="$TMPROOT/src/IrredenEngine"

git init --bare -b master "$REMOTE" >/dev/null 2>&1

AUTH="$TMPROOT/auth"
git clone "$REMOTE" "$AUTH" -q
git -C "$AUTH" config commit.gpgsign false
echo init > "$AUTH/readme.txt"
git -C "$AUTH" add readme.txt
git -C "$AUTH" commit -q -m "init"
git -C "$AUTH" push origin master -q

# feat-parent: the upstream base a stacked child would rebase onto.
git -C "$AUTH" checkout -b feat-parent master -q
echo "parent work" > "$AUTH/parent.txt"
git -C "$AUTH" add parent.txt
git -C "$AUTH" commit -q -m "parent commit"
git -C "$AUTH" push origin feat-parent -q

# feat-child: already up to date with feat-parent, so a real rebase attempt
# would push cleanly (proving branch work was attempted, not just skipped).
git -C "$AUTH" checkout -b feat-child feat-parent -q
echo "child work" > "$AUTH/child.txt"
git -C "$AUTH" add child.txt
git -C "$AUTH" commit -q -m "child commit"
git -C "$AUTH" push origin feat-child -q

git clone "$REMOTE" "$ENGINE" -q
git -C "$ENGINE" config commit.gpgsign false

cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
exit 0
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

write_slice() {
    printf '{"prs": %s}\n' "$1" > "$FLEET_STATE_DIR/projections/merger.json"
}

run_rebase() {
    "$REBASE" --auto --dry-run 2>&1 || true
}

echo "T1: base != master, fleet:needs-linux-smoke -> attempt"
write_slice '[{
  "repo":"engine","number":500,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-linux-smoke"]
}]'
T1=$(run_rebase)
assert_contains "$T1" "attempted=1" \
    "T1 a pending smoke does not park the PR away from tier-0"

echo "T2: base != master, fleet:needs-macos-smoke -> attempt"
write_slice '[{
  "repo":"engine","number":501,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-macos-smoke"]
}]'
T2=$(run_rebase)
assert_contains "$T2" "attempted=1" \
    "T2 a pending smoke does not park the PR away from tier-0"

echo "T3: base != master, fleet:needs-windows-smoke -> attempt"
write_slice '[{
  "repo":"engine","number":502,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-windows-smoke"]
}]'
T3=$(run_rebase)
assert_contains "$T3" "attempted=1" \
    "T3 a pending smoke does not park the PR away from tier-0"

echo "T4: base != master, no smoke label -> attempt (control)"
write_slice '[{
  "repo":"engine","number":503,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved"]
}]'
T4=$(run_rebase)
assert_contains "$T4" "attempted=1" \
    "T4 an ordinary stacked PR still reaches the attempt path"

echo "T5: base == master, CONFLICTING, fleet:needs-windows-smoke -> attempt"
write_slice '[{
  "repo":"engine","number":505,
  "headRefName":"feat-child","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:needs-windows-smoke"]
}]'
T5=$(run_rebase)
assert_contains "$T5" "attempted=1" \
    "T5 an approved conflict owing a smoke still reaches the attempt path"

echo "T6: persistent ownership claim -> no attempt"
write_slice '[{
  "repo":"engine","number":504,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:claim-mac-interactive"]
}]'
owned=$(run_rebase)
assert_absent "$owned" "attempted=1" "persistent PR ownership blocks mechanical rebase"

# --- Summary ------------------------------------------------------------------
summarize "fleet-rebase smoke-pending label tests"
