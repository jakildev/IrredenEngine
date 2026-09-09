#!/usr/bin/env bash
# Tests for fleet-rebase's LLM re-arm predicate.
#
# The tier-0 pass re-arms the LLM merger (trigger content "llm") after each
# run. It used to do so whenever ANY PR was left over — including every
# approved + MERGEABLE PR waiting on the human's merge click — so every scout
# tick that touched a PR label spawned tier-0, which re-armed the LLM, which
# walked both repos to report "0 candidates" (measured 2026-09-09: four LLM
# merger iterations in eight minutes, all no-ops, on a fleet with zero
# conflicts). The re-arm predicate is now role-merger.md step 3's own
# candidate rule: CONFLICTING (or UNKNOWN older than 5 min), no skip label,
# and any fleet:merger-cooldown older than the cooldown.
#
#   T1: approved + MERGEABLE + label outside the auto-merge allowlist →
#       human_remaining, no re-arm, no trigger written.
#   T2: approved + MERGEABLE plan candidate whose diff is not pure
#       .fleet/plans → human_remaining, no re-arm.
#   T3: unapproved CONFLICTING, no skip labels → llm_remaining=1 and the
#       trigger reads "llm".
#   T4: CONFLICTING + fleet:merger-cooldown updated just now → cooling, no
#       re-arm.
#   T5: CONFLICTING + fleet:merger-cooldown updated 20 min ago → re-arms.
#   T6: UNKNOWN updated 1 min ago → not a candidate; 10 min ago → re-arms.
#   T7: MERGEABLE + fleet:merger-cooldown (no other skip label) →
#       stale-cooldown cleanup, no re-arm.
#   T8: CONFLICTING + fleet:wip → skip-labels, no re-arm.
#   T9: the 2026-09-09 slice shape (synthetic numbers) — five human-owned approved PRs plus
#       one semantic-conflict PR → llm_remaining=0, no re-arm.
#
# The gh stub serves canned REST responses (attempt_merge's live verify) and
# records calls; everything runs --dry-run so no git worktree is needed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "SKIP: fleet-rebase not executable at $REBASE" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
export GH_STUB_DIR="$TMPROOT/gh-stub"
export GH_STUB_LOG="$TMPROOT/gh-stub/calls.log"
TRIGGER="$FLEET_STATE_DIR/triggers/merger"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin" "$GH_STUB_DIR"

cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
echo "$*" >> "$GH_STUB_LOG"
if [[ "${1:-}" == "api" ]]; then
    n=$(printf '%s' "$2" | grep -oE 'pulls/[0-9]+' | grep -oE '[0-9]+')
    if [[ "$2" == *"/files"* ]]; then
        cat "$GH_STUB_DIR/files_$n.txt" 2>/dev/null || exit 1
    else
        cat "$GH_STUB_DIR/pr_$n.tsv" 2>/dev/null || exit 1
    fi
    exit 0
fi
exit 0
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

write_slice() { printf '{"prs": %s}\n' "$1" > "$FLEET_STATE_DIR/projections/merger.json"; }
stub_pr() { printf '%s\t%s\t%s\t%s\t%s\n' "$2" "$3" "$4" "sha$1" "$5" > "$GH_STUB_DIR/pr_$1.tsv"; }
stub_files() { local n="$1"; shift; printf '%s\n' "$@" > "$GH_STUB_DIR/files_$n.txt"; }
reset_stub() { rm -f "$GH_STUB_DIR"/pr_*.tsv "$GH_STUB_DIR"/files_*.txt "$GH_STUB_LOG" "$TRIGGER"; touch "$GH_STUB_LOG"; }
# Every run passes --rearm-trigger so the trigger file is the observable.
run_rebase() { "$REBASE" --auto --dry-run --rearm-trigger 2>&1 || true; }
iso_ago() { python3 -c 'import datetime,sys;print((datetime.datetime.now(datetime.timezone.utc)-datetime.timedelta(seconds=int(sys.argv[1]))).strftime("%Y-%m-%dT%H:%M:%SZ"))' "$1"; }

assert_trigger_absent() {
    if [[ -e "$TRIGGER" ]]; then bad "$1 (trigger file exists: '$(cat "$TRIGGER")')"; else ok "$1"; fi
}
assert_trigger_llm() {
    if [[ -f "$TRIGGER" && "$(cat "$TRIGGER")" == "llm" ]]; then ok "$1"; else bad "$1 (trigger: '$(cat "$TRIGGER" 2>/dev/null || echo MISSING)')"; fi
}

# === T1 ======================================================================
echo "T1: approved + MERGEABLE + disqualifying label -> human, no re-arm"
reset_stub
write_slice "[{\"repo\":\"game\",\"number\":901,\"headRefName\":\"claude/911-op1\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 86400)\",\"labels\":[\"fleet:approved\",\"fleet:needs-human\"]}]"
T1=$(run_rebase)
assert_contains "$T1" "llm_remaining=0 human_remaining=1" "T1 counted as human_remaining"
assert_absent "$T1" "re-armed merger trigger" "T1 no re-arm logged"
assert_trigger_absent "T1 no trigger written"

# === T2 ======================================================================
echo "T2: approved + MERGEABLE plan candidate, non-plan diff -> human, no re-arm"
reset_stub
stub_pr 902 open master true "fleet:approved"
stub_files 902 "docs/agents/SOME-DOC.md"
write_slice "[{\"repo\":\"game\",\"number\":902,\"headRefName\":\"claude/912-coding-improvement\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 86400)\",\"labels\":[\"fleet:approved\"]}]"
T2=$(run_rebase)
assert_contains "$T2" "diff not pure .fleet/plans" "T2 diff gate fires"
assert_contains "$T2" "llm_remaining=0 human_remaining=1" "T2 counted as human_remaining"
assert_trigger_absent "T2 no trigger written"

# === T3 ======================================================================
echo "T3: unapproved CONFLICTING, no skip labels -> llm-other, re-arms"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":500,\"headRefName\":\"claude/500-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 3600)\",\"labels\":[]}]"
T3=$(run_rebase)
assert_contains "$T3" "llm_remaining=1 human_remaining=0" "T3 counted as llm_remaining"
assert_contains "$T3" "re-armed merger trigger for the LLM pass" "T3 re-arm logged"
assert_trigger_llm "T3 trigger reads llm"

# === T4 / T5 =================================================================
echo "T4: CONFLICTING + fresh fleet:merger-cooldown -> cooling, no re-arm"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 30)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T4=$(run_rebase)
assert_contains "$T4" "not re-arming the LLM pass yet" "T4 cooling log line"
assert_contains "$T4" "llm_remaining=0 human_remaining=0 cooling=1" "T4 counted as cooling"
assert_trigger_absent "T4 no trigger written"

echo "T5: CONFLICTING + fleet:merger-cooldown older than the cooldown -> re-arms"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 1200)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T5=$(run_rebase)
assert_contains "$T5" "llm_remaining=1" "T5 cooldown elapsed, counted as llm_remaining"
assert_trigger_llm "T5 trigger reads llm"

# A shorter configured cooldown flips T4's verdict — the knob is live.
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 30)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T5b=$(FLEET_MERGER_COOLDOWN_SECONDS=10 run_rebase)
assert_contains "$T5b" "llm_remaining=1" "T5b FLEET_MERGER_COOLDOWN_SECONDS honored"

# === T6 ======================================================================
echo "T6: UNKNOWN respects the 5-minute grace"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":502,\"headRefName\":\"claude/502-x\",\"baseRefName\":\"master\",\"mergeable\":\"UNKNOWN\",\"updatedAt\":\"$(iso_ago 60)\",\"labels\":[]}]"
T6a=$(run_rebase)
assert_contains "$T6a" "llm_remaining=0" "T6a young UNKNOWN is not a candidate"
assert_trigger_absent "T6a no trigger written"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":502,\"headRefName\":\"claude/502-x\",\"baseRefName\":\"master\",\"mergeable\":\"UNKNOWN\",\"updatedAt\":\"$(iso_ago 600)\",\"labels\":[]}]"
T6b=$(run_rebase)
assert_contains "$T6b" "llm_remaining=1" "T6b stale UNKNOWN is a candidate"
assert_trigger_llm "T6b trigger reads llm"

# === T7 ======================================================================
echo "T7: MERGEABLE + fleet:merger-cooldown -> stale-cooldown cleanup, no re-arm"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":503,\"headRefName\":\"claude/503-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 60)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T7=$(run_rebase)
assert_contains "$T7" "stale fleet:merger-cooldown on MERGEABLE PR — would remove (dry-run)" "T7 cleanup fires"
assert_contains "$T7" "llm_remaining=0 human_remaining=0" "T7 cleanup counts nowhere"
assert_trigger_absent "T7 no trigger written"

# === T8 ======================================================================
echo "T8: CONFLICTING + fleet:wip -> skip-labels, no re-arm"
reset_stub
write_slice "[{\"repo\":\"game\",\"number\":903,\"headRefName\":\"claude/903-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 3600)\",\"labels\":[\"fleet:changes-made\",\"fleet:wip\"]}]"
T8=$(run_rebase)
assert_contains "$T8" "llm_remaining=0 human_remaining=0" "T8 skip label keeps it out of every tally"
assert_trigger_absent "T8 no trigger written"

# === T9 ======================================================================
echo "T9: the 2026-09-09 slice shape (synthetic numbers) -> nothing re-arms"
reset_stub
for n in 902 904 905; do
    stub_pr "$n" open master true "fleet:approved"
    stub_files "$n" "docs/CLAUDE.md"
done
old=$(iso_ago 86400)
write_slice "[
  {\"repo\":\"engine\",\"number\":3081,\"headRefName\":\"claude/2917-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:resolving-mac-pool-3\",\"fleet:semantic-conflict\"]},
  {\"repo\":\"game\",\"number\":901,\"headRefName\":\"claude/911-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\",\"fleet:needs-human\"]},
  {\"repo\":\"game\",\"number\":902,\"headRefName\":\"claude/912-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":903,\"headRefName\":\"claude/903-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:changes-made\",\"fleet:wip\"]},
  {\"repo\":\"game\",\"number\":904,\"headRefName\":\"claude/913-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":905,\"headRefName\":\"claude/914-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":907,\"headRefName\":\"claude/915-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:design-proposed\",\"fleet:wip\"]}
]"
T9=$(run_rebase)
assert_contains "$T9" "llm_remaining=0 human_remaining=4 cooling=0 deferred=0" "T9 four human-owned PRs, zero LLM work"
assert_absent "$T9" "re-armed merger trigger" "T9 no re-arm logged"
assert_trigger_absent "T9 no trigger written"

summarize "fleet-rebase LLM re-arm predicate tests"
