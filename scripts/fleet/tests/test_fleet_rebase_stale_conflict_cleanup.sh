#!/usr/bin/env bash
# Tests for fleet-rebase's stale fleet:semantic-conflict cleanup (issue #1654).
#
# Reproduces the fail-then-succeed race: two concurrent LLM merger passes run
# against the same stacked PR after its base merges. The first attempt
# conflicts and labels the PR fleet:semantic-conflict + fleet:merger-cooldown.
# The second attempt (46 seconds later) succeeds — force-pushes the rebased
# branch and comments "diff against master is now clean" — but does NOT
# remove the stale labels the first pass set. The PR then appears conflicted
# to the opus-worker step-1c scanner, which wastes a full build-verify
# iteration on a branch that has no real conflict.
#
# The fix: fleet-rebase's triage detects fleet:semantic-conflict on a
# MERGEABLE PR (no other skip labels, approved) and emits a "stale-conflict"
# verdict that routes to cleanup_stale_conflict() instead of skip-labels.
#
#   T1: MERGEABLE + fleet:semantic-conflict + fleet:approved → cleanup fires
#       (dry-run: logs "would remove").
#   T2: CONFLICTING + fleet:semantic-conflict → genuine conflict, still goes
#       to skip-labels (the LLM merger owns real conflicts).
#   T3: fleet:semantic-conflict + another skip label (fleet:wip) → skip-labels,
#       not cleaned (WIP guard takes precedence).
#   T4: fleet:semantic-conflict absent, MERGEABLE → normal llm-other path,
#       no cleanup triggered.
#
# All run --auto --dry-run: the stale-conflict cleanup logs its intent but
# does not call gh (which is gated behind DRY_RUN=0).

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

# feat-clean: already on master (its single commit is the init commit),
# simulating a PR whose branch was successfully rebased by the second pass.
git -C "$AUTH" checkout -b feat-clean master -q
echo "clean work" > "$AUTH/clean.txt"
git -C "$AUTH" add clean.txt
git -C "$AUTH" commit -q -m "clean commit"
git -C "$AUTH" push origin feat-clean -q

git clone "$REMOTE" "$ENGINE" -q
git -C "$ENGINE" config commit.gpgsign false

# Minimal stub gh: silent success on everything (preflight calls in
# attempt_pr are DRY_RUN-gated; cleanup_stale_conflict logs and returns
# in dry-run mode before calling gh).
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

# === T1: MERGEABLE + fleet:semantic-conflict → cleanup logs "would remove" ===
echo "T1: MERGEABLE + fleet:semantic-conflict (stale) -> cleanup fires in dry-run"
write_slice '[{
  "repo":"engine","number":300,
  "headRefName":"feat-clean","baseRefName":"master",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:semantic-conflict","fleet:stacked-rebase"]
}]'
T1=$(run_rebase)
assert_contains "$T1" "would remove" \
    "T1 cleanup logs 'would remove' for MERGEABLE stale-conflict"
assert_absent   "$T1" "llm_remaining=1" \
    "T1 stale-conflict cleanup does not count toward LLM_REMAINING"
assert_absent   "$T1" "conflicts; leaving for LLM" \
    "T1 no conflict bail"

# === T2: CONFLICTING + fleet:semantic-conflict → skip-labels (genuine conflict) =
echo "T2: CONFLICTING + fleet:semantic-conflict -> skip-labels (real conflict, LLM owns it)"
write_slice '[{
  "repo":"engine","number":301,
  "headRefName":"feat-conflict","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:semantic-conflict"]
}]'
T2=$(run_rebase)
assert_absent "$T2" "would remove" \
    "T2 CONFLICTING stale-conflict not cleaned (still a real conflict)"
assert_absent "$T2" "llm_remaining=1" \
    "T2 genuine conflict goes to skip-labels (not LLM_REMAINING)"

# === T3: fleet:wip guard takes precedence over stale-conflict cleanup =========
echo "T3: fleet:wip + fleet:semantic-conflict -> skip-labels (WIP guard wins)"
write_slice '[{
  "repo":"engine","number":302,
  "headRefName":"feat-wip","baseRefName":"master",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:semantic-conflict","fleet:wip"]
}]'
T3=$(run_rebase)
assert_absent "$T3" "would remove" \
    "T3 fleet:wip + stale-conflict not cleaned (WIP guard wins)"

# === T4: no fleet:semantic-conflict → normal path, no cleanup =================
echo "T4: no fleet:semantic-conflict -> normal llm-other path, no cleanup"
write_slice '[{
  "repo":"engine","number":303,
  "headRefName":"feat-normal","baseRefName":"master",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved"]
}]'
T4=$(run_rebase)
assert_absent "$T4" "would remove" \
    "T4 no stale label -> no cleanup"

# --- Summary ------------------------------------------------------------------
summarize "fleet-rebase stale-conflict cleanup tests"
