#!/usr/bin/env bash
# Tests for fleet-rebase's retired stack-park label cleanup (issue #2764).
#
# Reproduces the native-stack strand: the merger labels a stacked child
# fleet:awaiting-base while its parent is open (role-merger.md step 5a.5 i).
# The parent merges; GitHub retargets the child to master server-side. That
# retarget is precisely what drops the child out of merger step 2.5's
# candidate set (it collects `baseRefName != "master"`), and 2.5 is the only
# thing that removes the label. Meanwhile fleet-rebase's triage routed any PR
# carrying the label to llm-other, and merger step 3 skips it on the — now
# false — inference that the base must still be open. An approved PR is
# stranded from every automated lane, permanently.
#
# The fix: base == master proves the park is discharged (a PR cannot be
# waiting on a base branch that IS master), so triage emits a
# "stale-base-park" verdict routing to cleanup_stale_base_park() instead of
# llm-other. A straggler still anchored to a real base (base != master) keeps
# the old llm-other routing.
#
# The gate is base == master alone — no fleet:approved requirement and no
# skip-label exclusion (#2840). Those guards belong to lanes that do branch
# work; this verdict does none. They also defeat each other here: the merger
# strips fleet:approved at the same moment it adds fleet:semantic-conflict,
# and that label leads SKIP_LABEL_RE, so a conflicted retargeted child fails
# both at once and lands on skip-labels, which no lane revisits — while
# worker step 1c and the scout projection both exclude fleet:awaiting-base,
# so no human-facing lane sees it either. T7 pins that shape.
#
#   T1: fleet:awaiting-base + base=master + approved -> cleanup fires
#       (dry-run: logs "would remove").
#   T2: fleet:needs-base-update + base=master + approved -> cleanup fires
#       (the twin retired label takes the same path).
#   T3: fleet:awaiting-base + base != master -> llm-other, NOT cleaned
#       (genuine mid-sequence straggler; the LLM pass owns it). UNCHANGED by
#       #2840 — that park is still real.
#   T4: fleet:awaiting-base + base=master + fleet:wip -> cleaned (#2840).
#       The WIP park is not discharged by this; it is preserved on the PR and
#       re-routes to skip-labels on the next tick — see T8.
#   T5: fleet:awaiting-base + base=master but NOT approved -> cleaned (#2840).
#   T6: CONFLICTING + base=master + no park label -> the normal attempt
#       path is untouched by this change.
#   T7: the #2840 live shape — awaiting-base + semantic-conflict + base=master
#       + NOT approved (both dropped guards failing at once) -> cleaned.
#   T8: fleet:wip + base=master + no park label -> skip-labels. This is T4's
#       PR on the tick after cleanup: the real park still parks it, so
#       removing the residue never unparks anyone.
#
# All run --auto --dry-run: cleanup_stale_base_park logs its intent and
# returns before calling gh (which is gated behind DRY_RUN=0).

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

# feat-retargeted: the shape under test — a child whose parent merged, so
# GitHub moved its base to master while the awaiting-base label stayed on.
git -C "$AUTH" checkout -b feat-retargeted master -q
echo "child work" > "$AUTH/child.txt"
git -C "$AUTH" add child.txt
git -C "$AUTH" commit -q -m "child commit"
git -C "$AUTH" push origin feat-retargeted -q

git clone "$REMOTE" "$ENGINE" -q
git -C "$ENGINE" config commit.gpgsign false

# Minimal stub gh: silent success on everything (preflight calls in
# attempt_pr are DRY_RUN-gated; cleanup_stale_base_park logs and returns
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

# === T1: fleet:awaiting-base + base=master -> cleanup logs "would remove" =====
echo "T1: fleet:awaiting-base + base=master -> stack-park cleanup fires in dry-run"
write_slice '[{
  "repo":"engine","number":400,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:awaiting-base","fleet:authored-on-macos"]
}]'
T1=$(run_rebase)
assert_contains "$T1" "retired stack-park label on a base=master PR — would remove" \
    "T1 cleanup logs 'would remove' for a retargeted child"
assert_absent   "$T1" "llm_remaining=1" \
    "T1 stack-park cleanup does not count toward LLM_REMAINING"
assert_absent   "$T1" "clean rebase onto" \
    "T1 cleanup does no branch work"

# === T2: fleet:needs-base-update takes the same path =========================
echo "T2: fleet:needs-base-update + base=master -> stack-park cleanup fires"
write_slice '[{
  "repo":"engine","number":401,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:needs-base-update"]
}]'
T2=$(run_rebase)
assert_contains "$T2" "retired stack-park label on a base=master PR — would remove" \
    "T2 the twin retired label takes the same cleanup path"

# === T3: base != master -> genuine straggler, still llm-other ================
echo "T3: fleet:awaiting-base + base != master -> llm-other (straggler preserved)"
write_slice '[{
  "repo":"engine","number":402,
  "headRefName":"feat-retargeted","baseRefName":"feat-parent",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:awaiting-base"]
}]'
T3=$(run_rebase)
assert_absent   "$T3" "would remove" \
    "T3 a child still anchored to a real base is not cleaned"
assert_contains "$T3" "llm_remaining=1" \
    "T3 straggler still routes to the LLM pass"

# === T4: another skip label no longer blocks the cleanup (#2840) =============
echo "T4: fleet:wip + fleet:awaiting-base + base=master -> cleaned (#2840)"
write_slice '[{
  "repo":"engine","number":403,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:awaiting-base","fleet:wip"]
}]'
T4=$(run_rebase)
assert_contains "$T4" "retired stack-park label on a base=master PR — would remove" \
    "T4 a co-resident skip label no longer blocks the cleanup"
assert_absent "$T4" "clean rebase onto" \
    "T4 cleanup still does no branch work on someone else's WIP branch"
assert_absent "$T4" "llm_remaining=1" \
    "T4 cleanup does not count toward LLM_REMAINING"

# === T5: an unapproved PR is cleaned too (#2840) =============================
echo "T5: fleet:awaiting-base + base=master but not approved -> cleaned (#2840)"
write_slice '[{
  "repo":"engine","number":404,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:awaiting-base"]
}]'
T5=$(run_rebase)
assert_contains "$T5" "retired stack-park label on a base=master PR — would remove" \
    "T5 fleet:approved is no longer required to discharge the park"
assert_absent   "$T5" "llm_remaining=1" \
    "T5 the unapproved straggler no longer falls through to the LLM pass"
assert_absent   "$T5" "clean rebase onto" \
    "T5 cleanup does no branch work on an unapproved branch"

# === T6: no park label -> the normal attempt path is unchanged ===============
echo "T6: no park label + CONFLICTING + base=master -> normal attempt path"
write_slice '[{
  "repo":"engine","number":405,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved"]
}]'
T6=$(run_rebase)
assert_absent   "$T6" "would remove" \
    "T6 no park label -> no cleanup"
assert_contains "$T6" "attempted=1" \
    "T6 the ordinary rebase path is untouched by this change"

# === T7: the #2840 live shape — both dropped guards failing at once ==========
# PR #2746's state once its base merged and GitHub retargeted it to master.
# The merger strips fleet:approved as it adds fleet:semantic-conflict, and
# that label leads SKIP_LABEL_RE, so this single PR failed BOTH original
# guards. Pre-#2840 it landed on skip-labels, which no lane revisits, while
# worker step 1c and the scout projection excluded it on fleet:awaiting-base.
echo "T7: awaiting-base + semantic-conflict + base=master + not approved -> cleaned"
write_slice '[{
  "repo":"engine","number":406,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:stacked","fleet:awaiting-base","fleet:authored-on-macos","fleet:semantic-conflict"]
}]'
T7=$(run_rebase)
assert_contains "$T7" "retired stack-park label on a base=master PR — would remove" \
    "T7 the conflicted retargeted child is cleaned, unstranding worker step 1c"
assert_absent   "$T7" "clean rebase onto" \
    "T7 cleanup does no branch work on a conflicted branch"

# === T8: removing the residue never unparks a genuinely parked PR ============
# T4's PR on the tick after its cleanup: fleet:awaiting-base is gone, the real
# park label remains. It must route to skip-labels — the widened gate discharges
# only the retired residue, never the park a live label still expresses.
echo "T8: fleet:wip + base=master + no park label -> skip-labels (park survives)"
write_slice '[{
  "repo":"engine","number":407,
  "headRefName":"feat-retargeted","baseRefName":"master",
  "mergeable":"CONFLICTING",
  "labels":["fleet:approved","fleet:wip"]
}]'
T8=$(run_rebase)
assert_absent "$T8" "would remove" \
    "T8 nothing left to clean once the residue is gone"
assert_absent "$T8" "clean rebase onto" \
    "T8 the WIP park still keeps tier-0 off the branch"
assert_absent "$T8" "llm_remaining=1" \
    "T8 the PR rests on skip-labels, exactly as it did before #2840"

# --- Summary ------------------------------------------------------------------
summarize "fleet-rebase stale stack-park cleanup tests"
