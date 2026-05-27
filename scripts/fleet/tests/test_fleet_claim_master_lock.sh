#!/usr/bin/env bash
# Tests for fleet-claim's T-138 master-side TASKS.md lock.
#
# Covers:
#   - master_lock_task pushes [~] + Owner to origin/master
#   - concurrent claims: exactly one wins, the other rolls back FS lock
#   - master_lock_task is OFF by default (T-380); FLEET_CLAIM_MASTER_LOCK=1 opts in
#   - FLEET_CLAIM_NO_MASTER_LOCK=1 still skips the master push (legacy compat)
#   - cmd_release does NOT re-push master (release is queue-tick's job)
#   - cmd_stack atomicity: one bad task rolls back master pushes too
#
# Each test uses a fresh temp dir as a bare git "origin" + a working
# clone. The agent's cwd is the working clone. fleet-claim's
# `git rev-parse --show-toplevel` resolves to that clone, which is what
# the master_lock_task helper uses for the push.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
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

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        wanted substring: $needle"
        echo "        actual:           $haystack"
    fi
}

# Build a bare origin + working clone with a TASKS.md containing two
# free tasks. Reset between tests via `setup_fixture`.
TMPROOT=$(mktemp -d)
ORIGIN_BARE="$TMPROOT/origin.git"
WORK="$TMPROOT/work"

setup_fixture() {
    rm -rf "$ORIGIN_BARE" "$WORK"
    git init --bare --quiet "$ORIGIN_BARE"

    local seed="$TMPROOT/seed"
    rm -rf "$seed"
    git init --quiet -b master "$seed"
    (
        cd "$seed"
        git config user.email "test@test"
        git config user.name "test"
        git config commit.gpgsign false
        cat >TASKS.md <<'TASKSEOF'
# TASKS

## Open

- [ ] **Task A** — first task
  - **ID:** T-901
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)

- [ ] **Task B** — second task
  - **ID:** T-902
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
TASKSEOF
        git add TASKS.md
        git commit --quiet --no-gpg-sign -m "init"
        git remote add origin "$ORIGIN_BARE"
        git push --quiet origin master
    )

    git clone --quiet "$ORIGIN_BARE" "$WORK"
    git -C "$WORK" config user.email "test@test"
    git -C "$WORK" config user.name "test"
    git -C "$WORK" config commit.gpgsign false

    # Per-test claim/reservation state.
    rm -rf "$TMPROOT/claims" "$TMPROOT/reservations" "$TMPROOT/molecules"
    mkdir -p "$TMPROOT/claims" "$TMPROOT/reservations" "$TMPROOT/molecules"
}

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_MOLECULES_DIR="$TMPROOT/molecules"
# T-380: master_lock_task is OFF by default; opt in for tests that verify it.
export FLEET_CLAIM_MASTER_LOCK=1

# Read TASKS.md from the bare origin's tip — what observers would see
# after the master push. Avoids any working-tree-state confusion.
read_origin_tasks_md() {
    git -C "$ORIGIN_BARE" show "master:TASKS.md"
}

# --- Test 1: claim pushes [~]+Owner to origin/master ----------------------
echo "T1: claim updates origin/master TASKS.md with [~] + Owner"
setup_fixture
(
    cd "$WORK"
    "$FLEET_CLAIM" claim "T-901" agent-A >/dev/null
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [~] **Task A**" "T-901 status flipped to [~] on master"
assert_contains "$master_md" "**Owner:** agent-A" "Owner field shows agent-A on master"
assert_contains "$master_md" "- [ ] **Task B**" "Task B unaffected (still [ ])"
# T-902's Owner stays "free"
assert_contains "$master_md" "  - **Owner:** free" "Task B's Owner remains free"

# --- Test 2: concurrent claims — exactly one wins -------------------------
echo "T2: concurrent claims race — exactly one succeeds"
setup_fixture
# Two clones racing the same task. Each clone has its own claims dir
# (mimics two different hosts / fleet roots), so the FS-lock alone
# wouldn't catch them — only the master-push race resolution does.
WORK_A="$TMPROOT/work-a"
WORK_B="$TMPROOT/work-b"
git clone --quiet "$ORIGIN_BARE" "$WORK_A"
git clone --quiet "$ORIGIN_BARE" "$WORK_B"
git -C "$WORK_A" config user.email "a@test"; git -C "$WORK_A" config user.name "a"
git -C "$WORK_A" config commit.gpgsign false
git -C "$WORK_B" config user.email "b@test"; git -C "$WORK_B" config user.name "b"
git -C "$WORK_B" config commit.gpgsign false
mkdir -p "$TMPROOT/claims-a" "$TMPROOT/claims-b" \
         "$TMPROOT/resv-a" "$TMPROOT/resv-b"

# Spawn both in background.
(
    cd "$WORK_A"
    FLEET_CLAIMS_DIR="$TMPROOT/claims-a" \
    FLEET_RESERVATIONS_DIR="$TMPROOT/resv-a" \
    "$FLEET_CLAIM" claim "T-901" agent-A
) >"$TMPROOT/out-a" 2>"$TMPROOT/err-a" &
PID_A=$!
(
    cd "$WORK_B"
    FLEET_CLAIMS_DIR="$TMPROOT/claims-b" \
    FLEET_RESERVATIONS_DIR="$TMPROOT/resv-b" \
    "$FLEET_CLAIM" claim "T-901" agent-B
) >"$TMPROOT/out-b" 2>"$TMPROOT/err-b" &
PID_B=$!

set +e
wait $PID_A; rc_a=$?
wait $PID_B; rc_b=$?
set -e

# Exactly one should succeed (rc=0), the other should fail (rc=1).
winners=$(( (rc_a == 0 ? 1 : 0) + (rc_b == 0 ? 1 : 0) ))
assert_eq "$winners" "1" "exactly one of two concurrent claims wins"
losers=$(( (rc_a == 1 ? 1 : 0) + (rc_b == 1 ? 1 : 0) ))
assert_eq "$losers" "1" "exactly one of two concurrent claims loses with rc=1"

# Master-side TASKS.md shows [~] with the winner's agent name.
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [~] **Task A**" "winner's [~] visible on master"
if [[ $rc_a -eq 0 ]]; then
    assert_contains "$master_md" "**Owner:** agent-A" "winner is agent-A; Owner reflects it"
    # Loser (B) must NOT have an FS claim — the helper rolled it back.
    if [[ -d "$TMPROOT/claims-b/t-901" ]]; then
        FAIL=$((FAIL + 1))
        echo "  FAIL: loser (agent-B) FS claim was not rolled back"
    else
        PASS=$((PASS + 1))
        echo "  ok: loser (agent-B) FS claim rolled back"
    fi
else
    assert_contains "$master_md" "**Owner:** agent-B" "winner is agent-B; Owner reflects it"
    if [[ -d "$TMPROOT/claims-a/t-901" ]]; then
        FAIL=$((FAIL + 1))
        echo "  FAIL: loser (agent-A) FS claim was not rolled back"
    else
        PASS=$((PASS + 1))
        echo "  ok: loser (agent-A) FS claim rolled back"
    fi
fi

# --- Test 3: default behavior skips master push (T-380) --------------------
echo "T3: default (no env var) skips master push"
setup_fixture
(
    cd "$WORK"
    FLEET_CLAIM_MASTER_LOCK=0 \
        "$FLEET_CLAIM" claim "T-902" agent-X >/dev/null
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [ ] **Task B**" "T-902 master row unchanged by default (T-380)"
if [[ -d "$TMPROOT/claims/t-902" ]]; then
    PASS=$((PASS + 1))
    echo "  ok: FS claim was taken (default skips master push)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: FS claim missing"
fi

# --- Test 3b: FLEET_CLAIM_NO_MASTER_LOCK=1 still skips (legacy compat) ----
echo "T3b: legacy env var still skips master push"
setup_fixture
(
    cd "$WORK"
    FLEET_CLAIM_MASTER_LOCK=1 FLEET_CLAIM_NO_MASTER_LOCK=1 \
        "$FLEET_CLAIM" claim "T-902" agent-X >/dev/null
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [ ] **Task B**" "T-902 master row unchanged with NO_MASTER_LOCK=1 override"
if [[ -d "$TMPROOT/claims/t-902" ]]; then
    PASS=$((PASS + 1))
    echo "  ok: FS claim was taken (legacy NO_MASTER_LOCK overrides MASTER_LOCK)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: FS claim missing"
fi

# --- Test 4: re-claim by a different agent (with separate reservation dir)
# returns race-lost via the master-lock path (not the reservation gate).
echo "T4: re-claim of an already-[~] task fails cleanly via master-lock"
setup_fixture
(
    cd "$WORK"
    "$FLEET_CLAIM" claim "T-901" agent-A >/dev/null
)
# Another agent on its own FS-claims-dir AND its own reservations dir
# (mimics a different host) tries to claim the same task. The
# reservation gate doesn't see agent-A's binding, so the FS lock
# succeeds — only master_lock_task can catch the race.
mkdir -p "$TMPROOT/claims-2" "$TMPROOT/resv-2"
set +e
(
    cd "$WORK"
    FLEET_CLAIMS_DIR="$TMPROOT/claims-2" \
    FLEET_RESERVATIONS_DIR="$TMPROOT/resv-2" \
        "$FLEET_CLAIM" claim "T-901" agent-2 >"$TMPROOT/out-rc" 2>"$TMPROOT/err-rc"
)
rc=$?
set -e
assert_eq "$rc" "1" "second-agent re-claim returns 1 (race lost)"
err_text=$(cat "$TMPROOT/err-rc")
assert_contains "$err_text" "race lost" "stderr mentions race lost"
# Master-side TASKS.md still owned by agent-A.
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "**Owner:** agent-A" "master Owner is still agent-A after losing race"
# Loser's FS claim was rolled back.
if [[ -d "$TMPROOT/claims-2/t-901" ]]; then
    FAIL=$((FAIL + 1))
    echo "  FAIL: agent-2's FS claim was not rolled back"
else
    PASS=$((PASS + 1))
    echo "  ok: agent-2's FS claim rolled back"
fi

# --- Test 5: stack claim — atomic master push + rollback on race-lost -----
echo "T5: stack claim pushes [~] for each task atomically"
setup_fixture
(
    cd "$WORK"
    "$FLEET_CLAIM" stack "T-901 T-902" stack-agent >/dev/null
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [~] **Task A**" "stack: T-901 flipped to [~]"
assert_contains "$master_md" "- [~] **Task B**" "stack: T-902 flipped to [~]"

# Now race-lose on a stack: pre-claim T-902 from a separate FS, then try
# to stack-claim [T-901, T-902]. The T-902 race should fail and roll
# back T-901's master push.
setup_fixture
mkdir -p "$TMPROOT/claims-pre"
(
    cd "$WORK"
    FLEET_CLAIMS_DIR="$TMPROOT/claims-pre" \
        "$FLEET_CLAIM" claim "T-902" pre-agent >/dev/null
)
set +e
(
    cd "$WORK"
    "$FLEET_CLAIM" stack "T-901 T-902" stack-agent >"$TMPROOT/out-stk" 2>"$TMPROOT/err-stk"
)
rc=$?
set -e
assert_eq "$rc" "1" "stack claim fails when T-902 is already locked elsewhere"
master_md=$(read_origin_tasks_md)
# T-902 should be [~] (pre-agent's lock); T-901 should be back to [ ]
# because the stack rolled back its T-901 push.
assert_contains "$master_md" "- [~] **Task B**" "T-902 still [~] from pre-agent"
assert_contains "$master_md" "- [ ] **Task A**" "T-901 rolled back to [ ] on master"

# --- Test 6: reclaim resets a stranded [~] row to [ ]+free ----------------
echo "T6: reclaim a stranded [~] row whose Owner branch is gone"
setup_fixture
(
    cd "$WORK"
    "$FLEET_CLAIM" claim "T-901" agent-A >/dev/null
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [~] **Task A**" "T-901 starts as [~] (set by claim)"

# Owner is "agent-A" — looks like a worktree name, not a branch (no
# claude/* prefix). reclaim should accept (no branch to check).
(
    cd "$WORK"
    "$FLEET_CLAIM" reclaim "T-901" >/dev/null 2>"$TMPROOT/err-rc6"
)
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [ ] **Task A**" "T-901 reclaimed to [ ]"
# Match the indented Owner field (avoid catching done-section "Owner:" form)
if [[ "$master_md" == *"  - **Owner:** free"* ]]; then
    PASS=$((PASS + 1))
    echo "  ok: Owner reset to free after reclaim"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: Owner not reset to free"
fi

# --- Test 7: reclaim refuses to clobber when Owner branch is live ---------
echo "T7: reclaim refuses when Owner branch exists on origin"
setup_fixture
# Inject a [~]+claude/T-901-foo state on master, then push the
# claude/T-901-foo branch as if a worker held it.
(
    cd "$WORK"
    git checkout -b claude/T-901-foo --quiet
    git push --quiet origin claude/T-901-foo
    git checkout master --quiet
    # Edit Task A's row only — first occurrence of "Task A" header and
    # the matching Owner line. Use python (sed -i differs on macOS).
    python3 - <<'PYE'
import re, pathlib
p = pathlib.Path("TASKS.md")
text = p.read_text()
# Flip header
text = text.replace("- [ ] **Task A**", "- [~] **Task A**", 1)
# Flip Owner — first "  - **Owner:** free" only.
text = text.replace("  - **Owner:** free", "  - **Owner:** claude/T-901-foo", 1)
p.write_text(text)
PYE
    git add TASKS.md && git commit --quiet --no-gpg-sign -m "manual"
    git push --quiet origin master
)
set +e
(
    cd "$WORK"
    "$FLEET_CLAIM" reclaim "T-901" >/dev/null 2>"$TMPROOT/err-rc7"
)
rc=$?
set -e
assert_eq "$rc" "1" "reclaim refuses with rc=1"
err_text=$(cat "$TMPROOT/err-rc7")
assert_contains "$err_text" "still exists on origin" "stderr explains the refusal"
master_md=$(read_origin_tasks_md)
assert_contains "$master_md" "- [~] **Task A**" "T-901 still [~] after refused reclaim"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
