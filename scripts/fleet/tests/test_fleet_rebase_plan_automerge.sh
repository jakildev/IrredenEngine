#!/usr/bin/env bash
# Tests for fleet-rebase's tier-0 plan-file auto-merge lane.
#
# The lane squash-merges an approved, MERGEABLE, master-based PR whose diff
# touches ONLY .fleet/plans/** — steward rollups, close-outs, umbrella plan
# filings. Everything else stays on the human's merge click. The triage
# pre-filters on the slice's labels (allowlist: any unknown label
# disqualifies), then attempt_merge re-verifies state/labels live via REST
# and checks the diff surface via the pulls/<N>/files endpoint.
#
#   T1: pure .fleet/plans diff, benign labels → squash-merged.
#   T2: mixed diff (plan file + engine source) → not merged, llm-other count.
#   T3: slice label outside the allowlist (fleet:human-deferred) → never a
#       merge candidate; zero gh calls for it.
#   T4: slice labels clean but LIVE labels drifted (human:needs-fix appeared
#       since the scout tick) → live verify refuses, not merged.
#   T5: live mergeable flipped to false (sibling plan PR merged first) →
#       live verify refuses, not merged.
#   T6: --dry-run on a pure plan PR → logs "would squash-merge", gh pr merge
#       never invoked.
#   T7: four eligible plan PRs → cap merges 3, defers the 4th, re-arms via
#       llm_remaining.
#
# The gh stub serves canned per-PR REST responses from $GH_STUB_DIR and
# records every invocation to $GH_STUB_LOG — no live GitHub anywhere.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "test setup: fleet-rebase not executable at $REBASE" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    if [[ -n "$TMPROOT" && -d "$TMPROOT" ]]; then
        rm -rf "$TMPROOT"
    fi
}
trap cleanup EXIT

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected to find: $needle"
        echo "        in: "; printf '%s\n' "$haystack" | sed 's/^/          | /'
    fi
}

assert_absent() {
    local haystack="$1" needle="$2" msg="$3"
    if printf '%s' "$haystack" | grep -qF -- "$needle"; then
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        did NOT expect: $needle"
        echo "        in: "; printf '%s\n' "$haystack" | sed 's/^/          | /'
    else
        PASS=$((PASS + 1)); echo "  ok: $msg"
    fi
}

# --- Sandbox ------------------------------------------------------------------
TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
export GH_STUB_DIR="$TMPROOT/gh-stub"
export GH_STUB_LOG="$TMPROOT/gh-stub/calls.log"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin" "$GH_STUB_DIR"

# Recording gh stub: `gh api repos/<slug>/pulls/<N>` serves pr_<N>.tsv,
# `.../pulls/<N>/files...` serves files_<N>.txt (missing file → exit 1, the
# fail-closed mock-miss rule); everything else records and succeeds.
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

write_slice() {
    printf '{"prs": %s}\n' "$1" > "$FLEET_STATE_DIR/projections/merger.json"
}

# stub_pr <number> <state> <base> <mergeable> <labels-csv>
stub_pr() {
    printf '%s\t%s\t%s\t%s\n' "$2" "$3" "$4" "$5" > "$GH_STUB_DIR/pr_$1.tsv"
}

# stub_files <number> <path>...
stub_files() {
    local n="$1"; shift
    printf '%s\n' "$@" > "$GH_STUB_DIR/files_$n.txt"
}

reset_stub() {
    rm -f "$GH_STUB_DIR"/pr_*.tsv "$GH_STUB_DIR"/files_*.txt "$GH_STUB_LOG"
    touch "$GH_STUB_LOG"
}

run_rebase() {
    "$REBASE" --auto 2>&1 || true
}

PLAN_SLICE_PR='{
  "repo":"engine","number":400,
  "headRefName":"claude/steward-rollup","baseRefName":"master",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:authored-on-macos"]
}'

# === T1: pure plan diff → squash-merged ========================================
echo "T1: pure .fleet/plans diff -> squash-merged"
reset_stub
write_slice "[$PLAN_SLICE_PR]"
stub_pr 400 open master true "fleet:approved,fleet:authored-on-macos"
stub_files 400 ".fleet/plans/issue-1394.md" ".fleet/plans/issue-667.md"
T1=$(run_rebase)
assert_contains "$T1" "engine#400: auto-merged" "T1 logs the auto-merge"
assert_contains "$T1" "merged=1 llm_remaining=0" "T1 summary counts the merge, no re-arm"
assert_contains "$(cat "$GH_STUB_LOG")" "pr merge 400 --repo jakildev/IrredenEngine --squash" \
    "T1 gh pr merge --squash invoked"
assert_contains "$(cat "$GH_STUB_LOG")" "pr comment 400" "T1 provenance comment posted"

# === T2: mixed diff → refused ==================================================
echo "T2: plan file + engine source in one diff -> not merged"
reset_stub
write_slice "[$PLAN_SLICE_PR]"
stub_pr 400 open master true "fleet:approved,fleet:authored-on-macos"
stub_files 400 ".fleet/plans/issue-1394.md" "engine/render/ir_render_canvas.cpp"
T2=$(run_rebase)
assert_contains "$T2" "diff not pure .fleet/plans (engine/render/ir_render_canvas.cpp)" \
    "T2 names the offending path"
assert_contains "$T2" "merged=0 llm_remaining=1" "T2 not merged, counted for re-arm"
assert_absent "$(cat "$GH_STUB_LOG")" "pr merge" "T2 gh pr merge never invoked"

# === T3: slice label outside allowlist → never a candidate ====================
echo "T3: fleet:human-deferred in slice labels -> not a merge candidate"
reset_stub
write_slice '[{
  "repo":"engine","number":401,
  "headRefName":"claude/deferred-pr","baseRefName":"master",
  "mergeable":"MERGEABLE",
  "labels":["fleet:approved","fleet:human-deferred"]
}]'
T3=$(run_rebase)
assert_contains "$T3" "merged=0 llm_remaining=1" "T3 stays llm-other"
assert_absent "$(cat "$GH_STUB_LOG")" "pulls/401" "T3 zero gh calls for the PR"

# === T4: live labels drifted since the scout tick ==============================
echo "T4: live labels grew human:needs-fix -> live verify refuses"
reset_stub
write_slice "[$PLAN_SLICE_PR]"
stub_pr 400 open master true "fleet:approved,human:needs-fix"
stub_files 400 ".fleet/plans/issue-1394.md"
T4=$(run_rebase)
assert_contains "$T4" "label(s) outside the auto-merge allowlist" "T4 live label gate fires"
assert_absent "$(cat "$GH_STUB_LOG")" "pr merge" "T4 gh pr merge never invoked"

# === T5: live mergeable flipped false ==========================================
echo "T5: live mergeable=false (sibling merged first) -> refused"
reset_stub
write_slice "[$PLAN_SLICE_PR]"
stub_pr 400 open master false "fleet:approved,fleet:authored-on-macos"
stub_files 400 ".fleet/plans/issue-1394.md"
T5=$(run_rebase)
assert_contains "$T5" "not auto-mergeable" "T5 live state gate fires"
assert_absent "$(cat "$GH_STUB_LOG")" "pr merge" "T5 gh pr merge never invoked"

# === T6: dry-run never merges ==================================================
echo "T6: --dry-run -> logs intent, no merge call"
reset_stub
write_slice "[$PLAN_SLICE_PR]"
stub_pr 400 open master true "fleet:approved,fleet:authored-on-macos"
stub_files 400 ".fleet/plans/issue-1394.md"
T6=$("$REBASE" --auto --dry-run 2>&1 || true)
assert_contains "$T6" "would squash-merge (dry-run)" "T6 dry-run logs intent"
assert_absent "$(cat "$GH_STUB_LOG")" "pr merge" "T6 gh pr merge never invoked"

# === T7: cap at 3 per run ======================================================
echo "T7: four eligible plan PRs -> 3 merged, 4th deferred + re-arm"
reset_stub
slice="["
for n in 410 411 412 413; do
    stub_pr "$n" open master true "fleet:approved"
    stub_files "$n" ".fleet/plans/issue-$n.md"
    slice+="{\"repo\":\"engine\",\"number\":$n,\"headRefName\":\"claude/plan-$n\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"labels\":[\"fleet:approved\"]},"
done
write_slice "${slice%,}]"
T7=$(run_rebase)
assert_contains "$T7" "merged=3 llm_remaining=1" "T7 cap merges 3, defers 1"
assert_contains "$T7" "auto-merge cap (3) reached this run" "T7 cap log line"

# --- Summary ------------------------------------------------------------------
echo ""
echo "fleet-rebase plan-automerge tests: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
