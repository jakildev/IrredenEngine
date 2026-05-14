#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-reconcile-amendments.
#
# Covers each branch of the per-PR decision logic:
#   T1: PR with worktree-on-branch + reservation → no change
#   T2: PR with worktree-on-branch + NO reservation → reservation
#       reconstructed via stubbed `fleet-claim reserve`
#   T3: PR with NO worktree on branch → label reverted via stubbed
#       `gh pr edit` + recovery comment
#   T4: branch that doesn't match claude/T-NNN-* + worktree on it +
#       no reservation → warned-and-left-alone (no malformed task id)
#   T5: idempotent re-run on the post-T2 state → no second reserve

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RECONCILER="$SCRIPT_DIR/fleet-reconcile-amendments"

if [[ ! -x "$RECONCILER" ]]; then
    echo "test setup: fleet-reconcile-amendments not executable at $RECONCILER" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_file_exists() {
    local path="$1" msg="$2"
    if [[ -f "$path" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg (missing: $path)"
    fi
}

assert_file_absent() {
    local path="$1" msg="$2"
    if [[ ! -f "$path" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg (present: $path)"
    fi
}

# --- Sandbox setup ---------------------------------------------------------
TMPROOT=$(mktemp -d)
mkdir -p "$TMPROOT/reservations" "$TMPROOT/wt" "$TMPROOT/bin" "$TMPROOT/log"

export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_AMEND_RECONCILE_ENGINE_REPO="test/engine"
export FLEET_AMEND_RECONCILE_GAME_REPO=""  # skip game in tests
export FLEET_AMEND_RECONCILE_ENGINE_WT="$TMPROOT/wt"
# Use the stubbed gh/fleet-claim on PATH below.
export FLEET_AMEND_RECONCILE_GH_BIN="$TMPROOT/bin/gh"
export FLEET_AMEND_RECONCILE_FLEETCLAIM_BIN="$TMPROOT/bin/fleet-claim"

# Stub gh: prints JSON for `gh pr list ... --json ...`, logs other invocations
# to a side file so the test can inspect what got called. The stubs are
# spawned as child processes by the reconciler, so the test env vars they
# read (GH_LOG, FC_LOG, PR_LIST_FILE, FLEET_RESERVATIONS_DIR) must be
# exported so they reach the child shells.
export GH_LOG="$TMPROOT/log/gh-calls.log"
export PR_LIST_FILE="$TMPROOT/pr-list.json"
cat >"$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
# Test stub: respond to known gh subcommands; log every invocation.
printf '%s\n' "$*" >>"$GH_LOG"
sub1="$1"; sub2="$2"
case "$sub1 $sub2" in
    "pr list")
        # `gh pr list ... --label fleet:human-amending ... --json ...`
        # Return the JSON staged at $PR_LIST_FILE.
        cat "$PR_LIST_FILE"
        ;;
    "pr edit"|"pr comment")
        # Just log, no output, success.
        ;;
    *)
        # Unrecognized: print a recognizable string and exit 0 to
        # avoid masking the real failure with set -e in the script.
        echo "stub-gh: unhandled $*" >&2
        ;;
esac
exit 0
GHEOF
chmod +x "$TMPROOT/bin/gh"

# Stub fleet-claim: only the `reserve` subcommand is exercised here.
export FC_LOG="$TMPROOT/log/fleet-claim-calls.log"
# Pre-create both log files so `grep -c` against an empty (but
# existing) file deterministically prints "0" with rc=1, rather than
# rc=2 + empty stdout when the file is absent.
: >"$GH_LOG"
: >"$FC_LOG"
cat >"$TMPROOT/bin/fleet-claim" <<'FCEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$FC_LOG"
if [[ "$1" == "reserve" ]]; then
    task_id="$2"
    worktree="$3"
    branch="${4:-}"
    # Mimic the real fleet-claim reservation file format.
    cat >"$FLEET_RESERVATIONS_DIR/$worktree.json" <<RESEOF
{
  "task_id": "$task_id",
  "worktree": "$worktree",
  "branch": "$branch",
  "created_at": "2026-01-01T00:00:00Z",
  "created_epoch": 1735689600
}
RESEOF
fi
exit 0
FCEOF
chmod +x "$TMPROOT/bin/fleet-claim"

# Both stubs need to be invoked via the FLEET_AMEND_RECONCILE_*_BIN env
# overrides above, which point at absolute paths inside $TMPROOT/bin —
# so we don't need to manipulate PATH itself.

# Create fake git worktrees: each is a git repo on a specific branch.
make_worktree() {
    local name="$1" branch="$2"
    local path="$TMPROOT/wt/$name"
    mkdir -p "$path"
    (
        cd "$path"
        git init --quiet -b master
        git config user.email "test@test"
        git config user.name "test"
        # Empty commit so the branch exists on disk
        git commit --allow-empty --quiet -m "init" --no-gpg-sign
        git checkout --quiet -b "$branch"
    )
}

# Helper: write the gh pr list response payload for a given test scenario.
write_pr_list() {
    # $1 = JSON array literal of {number, headRefName, title}
    printf '%s' "$1" >"$PR_LIST_FILE"
}

# === T1: worktree + reservation → no change ================================
echo "T1: worktree on branch + reservation → leave alone"
make_worktree opus-worker-1 claude/T-163-stateless-particles
# Pre-populate the reservation that the AMEND path would have written.
cat >"$FLEET_RESERVATIONS_DIR/opus-worker-1.json" <<JSON
{"task_id":"T-163","worktree":"opus-worker-1","branch":"claude/T-163-stateless-particles","created_at":"2026-01-01T00:00:00Z","created_epoch":1735689600}
JSON
write_pr_list '[{"number":659,"headRefName":"claude/T-163-stateless-particles","title":"T-163 particles"}]'

t1_res_mtime_before=$(stat -f '%m' "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" 2>/dev/null \
    || stat -c '%Y' "$FLEET_RESERVATIONS_DIR/opus-worker-1.json")
"$RECONCILER" >"$TMPROOT/log/t1.log" 2>&1 || true
t1_res_mtime_after=$(stat -f '%m' "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" 2>/dev/null \
    || stat -c '%Y' "$FLEET_RESERVATIONS_DIR/opus-worker-1.json")
assert_eq "$t1_res_mtime_after" "$t1_res_mtime_before" "T1 reservation untouched"
# `grep -c` exits 1 when there are no matches but still prints "0" —
# use `|| true` to suppress the failure without appending another "0".
assert_eq "$(grep -c 'reserve' "$FC_LOG" 2>/dev/null || true)" "0" \
    "T1 no fleet-claim reserve call"
assert_eq "$(grep -c 'pr edit' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T1 no gh pr edit call (label not reverted)"

# === T2: worktree but no reservation → reconstruct ========================
echo "T2: worktree on branch + no reservation → reservation reconstructed"
rm -f "$FLEET_RESERVATIONS_DIR/opus-worker-1.json"
: >"$GH_LOG"; : >"$FC_LOG"
"$RECONCILER" >"$TMPROOT/log/t2.log" 2>&1 || true
assert_file_exists "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" \
    "T2 reservation created"
# Verify content matches the expected task_id.
if [[ -f "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" ]]; then
    got_task=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('task_id',''))" \
        "$FLEET_RESERVATIONS_DIR/opus-worker-1.json")
    assert_eq "$got_task" "T-163" "T2 reconstructed reservation has correct task_id"
fi
assert_eq "$(grep -c 'reserve T-163 opus-worker-1' "$FC_LOG")" "1" \
    "T2 called fleet-claim reserve T-163 opus-worker-1"
assert_eq "$(grep -c 'pr edit' "$GH_LOG")" "0" \
    "T2 did not call gh pr edit (worktree owns branch)"

# === T3: no worktree on branch → label reverted ===========================
echo "T3: orphaned amendment (no worktree on branch) → revert label"
# A different PR + branch where no worktree is on it.
write_pr_list '[{"number":700,"headRefName":"claude/T-999-orphaned","title":"T-999 orphan"}]'
rm -f "$FLEET_RESERVATIONS_DIR"/*.json 2>/dev/null || true
: >"$GH_LOG"; : >"$FC_LOG"
"$RECONCILER" >"$TMPROOT/log/t3.log" 2>&1 || true
assert_eq "$(grep -c 'pr edit 700.*--remove-label fleet:human-amending' "$GH_LOG")" "1" \
    "T3 called gh pr edit to remove fleet:human-amending"
assert_eq "$(grep -c 'pr edit 700.*--add-label human:needs-fix' "$GH_LOG")" "1" \
    "T3 called gh pr edit to add human:needs-fix"
assert_eq "$(grep -c 'pr comment 700' "$GH_LOG")" "1" \
    "T3 left a recovery comment on the orphaned PR"
assert_eq "$(grep -c 'reserve' "$FC_LOG")" "0" \
    "T3 no fleet-claim reserve call (no worktree to reconstruct for)"
assert_file_absent "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" \
    "T3 no stray reservation written"

# === T4: branch on worktree but malformed name (no T-NNN prefix) ==========
echo "T4: worktree on non-T-NNN branch + no reservation → warn-and-leave"
make_worktree opus-worker-2 claude/feature-no-task-id
write_pr_list '[{"number":701,"headRefName":"claude/feature-no-task-id","title":"misnamed"}]'
rm -f "$FLEET_RESERVATIONS_DIR"/*.json 2>/dev/null || true
: >"$GH_LOG"; : >"$FC_LOG"
"$RECONCILER" >"$TMPROOT/log/t4.log" 2>&1 || true
assert_eq "$(grep -c 'reserve' "$FC_LOG")" "0" \
    "T4 no fleet-claim reserve attempt (branch isn't claude/T-NNN-*)"
assert_eq "$(grep -c 'pr edit 701' "$GH_LOG")" "0" \
    "T4 no gh pr edit (we have a worktree, just not the right kind)"
assert_file_absent "$FLEET_RESERVATIONS_DIR/opus-worker-2.json" \
    "T4 no reservation file written"

# === T5: idempotent re-run on already-reconstructed state =================
echo "T5: idempotent re-run after T2 — no second reserve call"
# Set up T2's state again, run reconciler twice, verify second run is no-op.
write_pr_list '[{"number":659,"headRefName":"claude/T-163-stateless-particles","title":"T-163 particles"}]'
rm -f "$FLEET_RESERVATIONS_DIR"/*.json 2>/dev/null || true
: >"$GH_LOG"; : >"$FC_LOG"
"$RECONCILER" >"$TMPROOT/log/t5a.log" 2>&1 || true
first_reserve_count=$(grep -c 'reserve' "$FC_LOG" || true)
: >"$GH_LOG"; : >"$FC_LOG"
"$RECONCILER" >"$TMPROOT/log/t5b.log" 2>&1 || true
second_reserve_count=$(grep -c 'reserve' "$FC_LOG" || true)
assert_eq "$first_reserve_count" "1" "T5 first run reconstructs reservation"
assert_eq "$second_reserve_count" "0" "T5 second run is a no-op (idempotent)"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
