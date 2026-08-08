#!/usr/bin/env bash
# Tests that fleet-rebase's SKIP_LABEL_RE treats all three cross-host smoke
# labels identically (issue #2888).
#
# #2804 widened the scout's _SMOKE_PENDING_LABELS from two smoke labels to
# three (adding fleet:needs-windows-smoke), but SKIP_LABEL_RE here was a
# hand-listed consumer left at two. A stacked PR (base != master) carrying
# only fleet:approved + fleet:needs-windows-smoke fell through to the
# "attempt" verdict — tier-0 would mechanically rebase and force-push it,
# invalidating the pending smoke verification the label exists to protect
# (fleet-rebase:124's own stated rationale for skipping smoke-pending PRs).
#
#   T1: base != master, fleet:needs-linux-smoke   -> skip-labels (control)
#   T2: base != master, fleet:needs-macos-smoke   -> skip-labels (control)
#   T3: base != master, fleet:needs-windows-smoke -> skip-labels (the fix;
#       pre-fix this PR fell through to "attempt" and force-pushed the
#       branch out from under the pending Windows smoke run)
#   T4: no smoke label -> the normal "attempt" path is unchanged
#
# All run --auto --dry-run: attempt_pr's git push happens against a local
# bare remote in the sandbox, so a "clean rebase onto" or "attempted=1" line
# is proof branch work was attempted, not just logged.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
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

# Minimal stub gh: silent success on everything.
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

# === T1: fleet:needs-linux-smoke -> skip-labels (control) ====================
echo "T1: base != master, fleet:needs-linux-smoke -> skip-labels"
write_slice '[{
  "repo":"engine","number":500,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-linux-smoke"]
}]'
T1=$(run_rebase)
assert_absent "$T1" "clean rebase onto" \
    "T1 needs-linux-smoke blocks branch work"
assert_absent "$T1" "attempted=1" \
    "T1 needs-linux-smoke does not reach the attempt path"

# === T2: fleet:needs-macos-smoke -> skip-labels (control) ====================
echo "T2: base != master, fleet:needs-macos-smoke -> skip-labels"
write_slice '[{
  "repo":"engine","number":501,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-macos-smoke"]
}]'
T2=$(run_rebase)
assert_absent "$T2" "clean rebase onto" \
    "T2 needs-macos-smoke blocks branch work"
assert_absent "$T2" "attempted=1" \
    "T2 needs-macos-smoke does not reach the attempt path"

# === T3: fleet:needs-windows-smoke -> skip-labels (the #2888 fix) ============
echo "T3: base != master, fleet:needs-windows-smoke -> skip-labels"
write_slice '[{
  "repo":"engine","number":502,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:needs-windows-smoke"]
}]'
T3=$(run_rebase)
assert_absent "$T3" "clean rebase onto" \
    "T3 needs-windows-smoke blocks branch work — the orphaned label from #2888"
assert_absent "$T3" "attempted=1" \
    "T3 needs-windows-smoke does not reach the attempt path"

# === T4: no smoke label -> the normal attempt path is unchanged ==============
echo "T4: base != master, no smoke label -> attempt (unchanged)"
write_slice '[{
  "repo":"engine","number":503,
  "headRefName":"feat-child","baseRefName":"feat-parent",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved"]
}]'
T4=$(run_rebase)
assert_contains "$T4" "attempted=1" \
    "T4 an ordinary stacked PR still reaches the attempt path"

# --- Summary ------------------------------------------------------------------
summarize "fleet-rebase smoke-label skip parity tests"
