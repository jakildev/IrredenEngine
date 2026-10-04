#!/usr/bin/env bash
# Tests for fleet-dispatch-wrap's pre-launch clean-pane arm: a dispatcher
# launch into a pool worktree starts from a clean pane or does not start, and
# nothing else is ever cleaned. Uses a real temporary linked git worktree (not
# a git stub), since the arm's own `git diff`/`checkout` must actually run;
# only claude/codex/tmux/fleet-claude-stream/fleet-claim are stubbed. Checked
# through the FLEET_DISPATCH_PRINT_LAUNCH hook, which exits after the launch
# decision and before any agent spawns.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"
[[ -x "$WRAP" ]] || { echo "test setup: $WRAP not found"; exit 1; }

unset FLEET_PLAN_ISSUE FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
  FLEET_DISPATCH_REPO FLEET_DISPATCH_NUMBER FLEET_DISPATCH_REASON

# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_LEFTOVERS_DIR="$TMPROOT/leftovers"
mkdir -p "$FLEET_SESSIONS_DIR" "$FLEET_STATE_DIR" "$FLEET_LEFTOVERS_DIR"

# --- stubs: everything except git, which must run for real -----------------
BIN="$TMPROOT/bin"; mkdir -p "$BIN"
for tool in claude codex fleet-claude-stream tmux; do
  printf '#!/usr/bin/env bash\nexit 0\n' > "$BIN/$tool"
done
ln -s "$SCRIPT_DIR/../../engine/tools/bin/ir-acquire" "$BIN/ir-acquire"
ln -s "$SCRIPT_DIR/fleet-quiet-wait" "$BIN/fleet-quiet-wait"
export IR_LOCK_ROOT="$TMPROOT/locks"
export IR_QUIET_SETTLE_SAMPLE=.05 IR_QUIET_SETTLE_CPU=.25
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_RESERVATIONS_DIR"
export FLEET_CLAIM_RESV_FILE="$TMPROOT/resv.txt"
: > "$FLEET_CLAIM_RESV_FILE"
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
[[ "$1" == "reservation-of" ]] && { cat "${FLEET_CLAIM_RESV_FILE:-/dev/null}" 2>/dev/null; exit 0; }
exit 0
STUB
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

# --- a real origin + worktree clone -----------------------------------------
ORIGIN="$TMPROOT/origin.git"
git init --quiet --bare "$ORIGIN"
SEED="$TMPROOT/seed"
git init --quiet "$SEED"
git -C "$SEED" config user.email "test@example.com"
git -C "$SEED" config user.name "test"
git -C "$SEED" checkout --quiet -b master
echo "original" > "$SEED/tracked.txt"
printf '\x00\x01\xff\xfe\x00binary\x00' > "$SEED/image.bin"
git -C "$SEED" add tracked.txt image.bin
git -C "$SEED" commit --quiet -m "seed"
git -C "$SEED" remote add origin "$ORIGIN"
git -C "$SEED" push --quiet origin master

# The pane is a linked worktree of a main clone, as fleet-up lays pool panes
# out; the main clone's HEAD is detached so the pane can hold master.
MAIN="$TMPROOT/main"
git clone --quiet "$ORIGIN" "$MAIN"
git -C "$MAIN" config user.email "test@example.com"
git -C "$MAIN" config user.name "test"
git -C "$MAIN" checkout --quiet --detach
WT="$TMPROOT/pool-4"
git -C "$MAIN" worktree add --quiet "$WT" master
WT_GITDIR=$(git -C "$WT" rev-parse --absolute-git-dir)
SIDECAR="$FLEET_SESSIONS_DIR/pool-4.session.json"
RECORD="$FLEET_STATE_DIR/dispatch/pane-4.json"
mkdir -p "$FLEET_STATE_DIR/dispatch"

reset_pane() {
  git -C "$WT" checkout --quiet master
  git -C "$WT" reset --quiet --hard origin/master
  git -C "$WT" clean --quiet -fd
  git -C "$WT" for-each-ref --format='%(refname:short)' refs/heads/ \
    | grep -vx master | xargs -r git -C "$WT" branch --quiet -D
  : > "$FLEET_CLAIM_RESV_FILE"
  rm -f "$FLEET_RESERVATIONS_DIR"/pool-4.json
  rm -f "$SIDECAR" "$RECORD"
}

write_resv() {  # $1 = task id, $2 = recorded branch (optional)
  printf '{"task_id": "%s", "worktree": "pool-4", "branch": "%s", "created_epoch": 1}\n' \
    "$1" "${2:-}" > "$FLEET_RESERVATIONS_DIR/pool-4.json"
  echo "$1" > "$FLEET_CLAIM_RESV_FILE"
}

seed_dirty() {  # $1 = worktree (default: the pool pane)
  local wt="${1:-$WT}"
  echo "modified" > "$wt/tracked.txt"
  printf '\x00\x01\xff\xfe\x00CHANGED\x00' > "$wt/image.bin"
  echo "loop" > "$wt/.retry-verdict.sh"
  echo "draft" > "$wt/.pr-body.md"
}

# Everything a launch that may not clean must leave byte-identical.
pane_state() {  # $1 = worktree
  git -C "$1" rev-parse HEAD
  git -C "$1" symbolic-ref -q HEAD || echo "(detached)"
  git -C "$1" status --porcelain --untracked-files=all
  cat "$1/tracked.txt" "$1/image.bin" "$1/.retry-verdict.sh" "$1/.pr-body.md" 2>&1 | cksum
}

tracked_dirty_present() {
  [[ -n "$(git -C "$WT" status --porcelain | grep -v '^??' || true)" ]]
}

latest_patch() { ls "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch 2>/dev/null | head -1; }

# The dispatcher's pane record for pane-4, before the wrapper stamps its PID.
# $1 = role, $2 = runtime, $3 = argv class, $4 = agent, $5 = wrapper_pid.
write_record() {
  local extra=""
  [[ -n "$3" ]] && extra=",\"launch_class\":\"$3\""
  printf '{"role":"%s","pane":"%%4","class":"sonnet","dispatched_at":"x","dispatched_epoch":1,"wrapper_pid":%s,"claim_marker":1,"runtime":"%s","agent":"%s"%s}\n' \
    "$1" "${5:-0}" "$2" "$4" "$extra" > "$RECORD"
}

# A dispatcher launch: the record matches the argv (role, runtime, class) and
# the worktree. launch_bare runs the wrapper with whatever record is on disk.
# LAUNCH_WT overrides the worktree the wrapper runs in.
launch() {  # args: model effort role [fallback] [mode] [target] [runtime] [class]
  write_record "$3" "${7:-claude}" "${8:-}" "$(basename "${LAUNCH_WT:-$WT}")"
  launch_bare "$@"
}
launch_bare() {
  ( cd "${LAUNCH_WT:-$WT}" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-4 "$@" 2>>"$TMPROOT/stderr.log" )
}

# =============================================================================
echo "T1: positive-fire — tracked mods + retry script, no reservation, no sidecar"
reset_pane; seed_dirty
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T1: fresh launch decision" || bad "T1: launch decision: $out"
tracked_dirty_present && bad "T1: tracked modifications survived" || ok "T1: worktree clean"
[[ "$(git -C "$WT" rev-parse HEAD)" == "$(git -C "$WT" rev-parse origin/master)" ]] \
  && ok "T1: HEAD reset to origin/master" || bad "T1: HEAD did not reset"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "claude/pool-4-scratch" ]] \
  && ok "T1: branch reset to claude/pool-4-scratch" || bad "T1: branch is $(git -C "$WT" rev-parse --abbrev-ref HEAD)"
[[ -f "$WT/.retry-verdict.sh" ]] && bad "T1: retry script survived" || ok "T1: retry script removed"
[[ -f "$WT/.pr-body.md" ]] && bad "T1: scratch body survived" || ok "T1: scratch body removed"
patch=$(latest_patch)
[[ -n "$patch" ]] && ok "T1: leftover patch written" || bad "T1: no leftover patch found"
if [[ -n "$patch" ]]; then
  grep -q "tracked.txt" "$patch" && ok "T1: patch covers the text file" || bad "T1: patch missing tracked.txt"
  grep -q "GIT binary patch" "$patch" && ok "T1: patch covers the binary file" || bad "T1: patch missing binary hunk"
fi
grep -q "backed up to" "$TMPROOT/stderr.log" && ok "T1: stderr names the patch" || bad "T1: stderr silent on the backup"

echo "T2: reservation exemption — a recorded branch (feedback-amend reserve) leaves the pane untouched"
reset_pane; seed_dirty
write_resv 9001 claude/9001-amend
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T2: fresh launch decision" || bad "T2: launch decision: $out"
tracked_dirty_present && ok "T2: tracked modifications survive under a live reservation" || bad "T2: tracked modifications were discarded"
[[ -f "$WT/.retry-verdict.sh" ]] && ok "T2: retry script survives" || bad "T2: retry script was removed"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "T2: branch untouched" || bad "T2: branch changed under reservation"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T2: no patch written" || bad "T2: a patch was written despite the reservation"

echo "T2b: reservation exemption — a branchless reservation with its task branch checked out"
reset_pane
git -C "$WT" checkout --quiet -b claude/9001-some-task
seed_dirty
write_resv 9001
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T2b: fresh launch decision" || bad "T2b: launch decision: $out"
tracked_dirty_present && ok "T2b: in-flight task edits survive" || bad "T2b: in-flight task edits were discarded"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "claude/9001-some-task" ]] && ok "T2b: task branch untouched" || bad "T2b: task branch changed"
[[ -z "$(latest_patch)" ]] && ok "T2b: no patch written" || bad "T2b: a patch was written for in-flight work"

echo "T3: resume exemption — a role-matching sidecar leaves it untouched"
reset_pane; seed_dirty
printf '{"session_id":"SID-1","role":"worker","model":"sonnet","effort":"high","runtime":"claude","created_epoch":1}\n' > "$SIDECAR"
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=1* ]] && ok "T3: resume decision" || bad "T3: launch decision: $out"
tracked_dirty_present && ok "T3: tracked modifications survive a resume" || bad "T3: tracked modifications were discarded on resume"
[[ -f "$WT/.retry-verdict.sh" ]] && ok "T3: retry script survives" || bad "T3: retry script was removed on resume"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T3: no patch written on resume" || bad "T3: a patch was written on resume"
rm -f "$SIDECAR"

echo "T4: clean pane is a no-op"
reset_pane
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T4: fresh launch decision" || bad "T4: launch decision: $out"
patch=$(latest_patch)
[[ -z "$patch" ]] && ok "T4: no patch written on a clean pane" || bad "T4: a patch was written on a clean pane"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "T4: branch unchanged on a clean pane" || bad "T4: branch changed on a clean pane"
[[ -s "$TMPROOT/stderr.log" ]] && bad "T4: spurious log line on a clean pane: $(cat "$TMPROOT/stderr.log")" || ok "T4: no spurious log line"

# The dispatcher pre-claims a target-bound worker pane before the wrapper
# runs, and `fleet-claim claim` auto-writes a branchless reservation for it.
for kind in task stack; do
  target="target=$kind:engine:9001"
  [[ "$kind" == stack ]] && target="$target:9000"
  echo "T6 ($kind): a fresh pre-claim's own reservation does not exempt inherited dirt"
  reset_pane
  # The prior lane left a task branch checked out: the pre-claim match alone
  # must decide, not the branch evidence.
  git -C "$WT" checkout --quiet -b claude/8000-prior-lane
  seed_dirty
  write_resv 9001
  rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
  : > "$TMPROOT/stderr.log"
  out=$(launch sonnet high worker "" live "$target")
  [[ "$out" == "resumed=0 target=${target#target=}"* ]] && ok "T6 ($kind): fresh target-bound launch" || bad "T6 ($kind): launch decision: $out"
  tracked_dirty_present && bad "T6 ($kind): inherited tracked dirt survived the pre-claim" || ok "T6 ($kind): worktree clean"
  [[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "claude/pool-4-scratch" ]] \
    && ok "T6 ($kind): reset to the scratch branch" || bad "T6 ($kind): branch is $(git -C "$WT" rev-parse --abbrev-ref HEAD)"
  [[ -n "$(latest_patch)" ]] && ok "T6 ($kind): backup patch written" || bad "T6 ($kind): no backup patch"
  [[ -f "$WT/.retry-verdict.sh" ]] && bad "T6 ($kind): retry script survived" || ok "T6 ($kind): retry script removed"
done

echo "T6b: a branchless reservation for a DIFFERENT number than the target still exempts in-flight work"
reset_pane
git -C "$WT" checkout --quiet -b claude/9002-other-task
seed_dirty
write_resv 9002
out=$(launch sonnet high worker "" live "target=task:engine:9001")
tracked_dirty_present && ok "T6b: the other task's edits survive" || bad "T6b: the other task's edits were discarded"

echo "T7: a reservation with no in-flight evidence (no branch, scratch HEAD) does not exempt"
reset_pane
git -C "$WT" checkout --quiet -b claude/pool-4-scratch
seed_dirty
write_resv 9001
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
out=$(launch sonnet high worker "" live)
[[ "$out" == resumed=0* ]] && ok "T7: fresh launch decision" || bad "T7: launch decision: $out"
tracked_dirty_present && bad "T7: tracked dirt survived" || ok "T7: worktree clean"
[[ -n "$(latest_patch)" ]] && ok "T7: backup patch written" || bad "T7: no backup patch"

# Fail closed: no launch decision is printed (the provider is never reached),
# the exit is non-zero, and the tracked diff is still on disk.
assert_no_launch() {  # $1 = label, $2 = captured stdout, $3 = rc
  [[ -z "$2" ]] && ok "$1: no provider launch" || bad "$1: launched anyway: $2"
  [[ "$3" != 0 ]] && ok "$1: non-zero exit" || bad "$1: exit 0"
  grep -q "not launching worker" "$TMPROOT/stderr.log" && ok "$1: stderr names the refusal" || bad "$1: stderr: $(cat "$TMPROOT/stderr.log")"
}

echo "T5: backup failure — an unwritable leftovers dir fails closed with the tracked diff in place"
reset_pane; seed_dirty
write_resv 9001
# A regular file where the leftovers dir's parent should be: mkdir and the
# patch redirect both fail, independent of uid (a chmod'd dir does not stop root).
: > "$TMPROOT/not-a-dir"
: > "$TMPROOT/stderr.log"
out=$(FLEET_LEFTOVERS_DIR="$TMPROOT/not-a-dir/leftovers" launch sonnet high worker "" live "target=task:engine:9001"); rc=$?
assert_no_launch "T5" "$out" "$rc"
assert_eq "$(cat "$WT/tracked.txt")" "modified" "T5: text modification survives a failed backup"
cmp -s <(printf '\x00\x01\xff\xfe\x00CHANGED\x00') "$WT/image.bin" \
  && ok "T5: binary modification survives a failed backup" || bad "T5: binary modification was discarded"
[[ "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" == "master" ]] && ok "T5: branch untouched" || bad "T5: branch reset despite the failed backup"
grep -q "could not back up" "$TMPROOT/stderr.log" && ok "T5: stderr names the failed backup" || bad "T5: stderr: $(cat "$TMPROOT/stderr.log")"
[[ -f "$FLEET_RESERVATIONS_DIR/pool-4.json" ]] && ok "T5: pre-claim reservation left for the completion fold" || bad "T5: reservation was dropped"

echo "T8: reset failure — a held index lock fails closed with the backup kept"
reset_pane; seed_dirty
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
touch "$WT_GITDIR/index.lock"
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live); rc=$?
rm -f "$WT_GITDIR/index.lock"
assert_no_launch "T8" "$out" "$rc"
grep -q "could not reset" "$TMPROOT/stderr.log" && ok "T8: stderr names the failed reset" || bad "T8: stderr: $(cat "$TMPROOT/stderr.log")"
patch=$(latest_patch)
[[ -n "$patch" ]] && grep -q "tracked.txt" "$patch" && ok "T8: backup patch kept" || bad "T8: backup patch missing"
tracked_dirty_present && ok "T8: tracked diff still on disk" || bad "T8: tracked diff gone"

echo "T9: status failure — a corrupt index fails closed"
reset_pane; seed_dirty
cp "$WT_GITDIR/index" "$TMPROOT/index.good"
printf 'not an index' > "$WT_GITDIR/index"
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live); rc=$?
cp "$TMPROOT/index.good" "$WT_GITDIR/index"
assert_no_launch "T9" "$out" "$rc"
grep -q "git status failed" "$TMPROOT/stderr.log" && ok "T9: stderr names the failed status" || bad "T9: stderr: $(cat "$TMPROOT/stderr.log")"
assert_eq "$(cat "$WT/tracked.txt")" "modified" "T9: text modification survives"

echo "T10: fetch failure — resets to the last-fetched origin/master and launches"
reset_pane; seed_dirty
git -C "$WT" remote set-url origin "$TMPROOT/no-such-origin.git"
rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
: > "$TMPROOT/stderr.log"
out=$(launch sonnet high worker "" live); rc=$?
git -C "$WT" remote set-url origin "$ORIGIN"
[[ "$out" == resumed=0* && "$rc" == 0 ]] && ok "T10: launches after a failed fetch" || bad "T10: rc=$rc out=$out"
tracked_dirty_present && bad "T10: tracked dirt survived" || ok "T10: worktree clean"
grep -q "fetch origin master failed" "$TMPROOT/stderr.log" && ok "T10: stderr names the failed fetch" || bad "T10: stderr: $(cat "$TMPROOT/stderr.log")"

# --- Authorization: only a matching record for a pool worktree cleans ------
# Every fixture below is dirty and carries a scratch body and a retry script,
# so a refusal cannot pass vacuously; T1 and T6 are the authorized twins.

echo "T11: no dispatch record — a direct call launches and cleans nothing"
for target in "" "target=task:engine:9001"; do
  label="T11 (${target:-targetless})"
  reset_pane; seed_dirty
  [[ -n "$target" ]] && write_resv 9001
  rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
  before=$(pane_state "$WT")
  : > "$TMPROOT/stderr.log"
  out=$(launch_bare sonnet high worker "" live "$target"); rc=$?
  [[ "$out" == resumed=0* && "$rc" == 0 ]] && ok "$label: launch decision reached" || bad "$label: rc=$rc out=$out"
  assert_eq "$(pane_state "$WT")" "$before" "$label: HEAD, ref, status and scratch files byte-identical"
  [[ -z "$(latest_patch)" ]] && ok "$label: no patch written" || bad "$label: a patch was written"
done

echo "T12: a record naming a different invocation fails closed and cleans nothing"
# what | record role | record runtime | record class | record agent | record pid | argv class
for row in "agent|worker|claude||pool-9|0|" \
           "wrapper pid|worker|claude||pool-4|1|" \
           "role|sonnet-reviewer|claude||pool-4|0|" \
           "runtime|worker|codex||pool-4|0|" \
           "record class|worker|claude|opus|pool-4|0|" \
           "argv class|worker|claude|opus|pool-4|0|sonnet"; do
  IFS='|' read -r what r_role r_runtime r_class r_agent r_pid argv_class <<< "$row"
  label="T12 ($what)"
  reset_pane; seed_dirty
  rm -f "$FLEET_LEFTOVERS_DIR"/pool-4-*.patch
  write_record "$r_role" "$r_runtime" "$r_class" "$r_agent" "$r_pid"
  before=$(pane_state "$WT")
  : > "$TMPROOT/stderr.log"
  out=$(launch_bare sonnet high worker "" live "" claude "$argv_class"); rc=$?
  assert_no_launch "$label" "$out" "$rc"
  assert_eq "$(pane_state "$WT")" "$before" "$label: HEAD, ref, status and scratch files byte-identical"
  [[ -z "$(latest_patch)" ]] && ok "$label: no patch written" || bad "$label: a patch was written"
done

echo "T12b: a record matching an argv class authorizes the reset"
reset_pane; seed_dirty
out=$(launch sonnet high worker "" live "" claude sonnet)
[[ "$out" == resumed=0* ]] && ok "T12b: launch decision reached" || bad "T12b: launch decision: $out"
tracked_dirty_present && bad "T12b: tracked dirt survived" || ok "T12b: worktree clean"

# A matching record outside a pool worktree grants nothing: the launch goes
# ahead untouched.
not_a_pool() {  # $1 = label, $2 = worktree
  local before out rc
  seed_dirty "$2"
  rm -f "$FLEET_LEFTOVERS_DIR"/*.patch
  before=$(pane_state "$2")
  : > "$TMPROOT/stderr.log"
  out=$(LAUNCH_WT="$2" launch sonnet high worker "" live); rc=$?
  [[ "$out" == resumed=0* && "$rc" == 0 ]] && ok "$1: launch decision reached" || bad "$1: rc=$rc out=$out"
  assert_eq "$(pane_state "$2")" "$before" "$1: HEAD, ref, status and scratch files byte-identical"
  [[ -z "$(ls "$FLEET_LEFTOVERS_DIR")" ]] && ok "$1: no patch written" || bad "$1: a patch was written"
}

echo "T13: an interactive linked worktree (opus-architect) is never cleaned"
ARCH="$TMPROOT/opus-architect"
git -C "$MAIN" worktree add --quiet -b claude/uncommitted-change "$ARCH" origin/master
not_a_pool "T13" "$ARCH"

echo "T14: a standalone clone named like a pool pane is never cleaned"
LONE="$TMPROOT/pool-5"
git clone --quiet "$ORIGIN" "$LONE"
not_a_pool "T14" "$LONE"

echo "T15: the wrap parks its lease before printing the launch decision"
reset_pane
record=$(python3 "$SCRIPT_DIR/../../engine/tools/lib/quiet_window.py" record-create \
  --pid "$$" --owner foreign --linger 0 --maximum 10 --drain 3 \
  --settle-cpu .25 --settle-sample .05)
write_record worker claude "" pool-4
launch_bare sonnet high worker "" live >"$TMPROOT/quiet-launch.out" &
quiet_wrap_pid=$!
for _attempt in $(seq 1 100); do
  status=$(ir-acquire --quiet-status --json 2>&1)
  [[ "$status" == *'"parked": 1'* ]] && break
  sleep .02
done
assert_eq "$(cat "$TMPROOT/quiet-launch.out")" "" \
  "T15: launch decision stays blocked during pre-launch park"
assert_contains "$status" '"parked": 1' "T15: wrap lease reads parked"
python3 "$SCRIPT_DIR/../../engine/tools/lib/quiet_window.py" refuse "$record"
wait "$quiet_wrap_pid"
assert_contains "$(cat "$TMPROOT/quiet-launch.out")" "resumed=0" \
  "T15: launch decision prints after the quiet window closes"

summarize "fleet-dispatch-wrap clean-pane pre-launch arm"
