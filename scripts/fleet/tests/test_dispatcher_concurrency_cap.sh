#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-role concurrency cap.
#
# Covers:
#   - count_active_for_role with no reservations + no dispatch records
#   - count with only in-flight (dispatch records)
#   - count with only reserved (reservations whose task maps to role)
#   - dedup when a worktree has both a dispatch record and a reservation
#   - cap defaults (preserve current behavior at 2 for multi-pane roles)
#   - env var override
#   - conf file override
#   - conf override beat by env var

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$DISPATCHER" ]]; then
    echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2
    exit 1
fi
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

# Build a sandbox: temp dirs for state + reservations, a fake TASKS.md
# in a fake git repo so reservation-role can resolve task → Model.
TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_CONF="$TMPROOT/fleet-up.conf"

mkdir -p "$FLEET_STATE_DIR/dispatch" "$FLEET_RESERVATIONS_DIR" "$FLEET_CLAIMS_DIR"

FAKE_REPO="$TMPROOT/repo"
mkdir -p "$FAKE_REPO"
(
    cd "$FAKE_REPO"
    git init --quiet -b master
    git config user.email "test@test"
    git config user.name "test"
    cat >TASKS.md <<'TASKSEOF'
# TASKS

## Open

- [ ] **Task A — opus** — first opus task
  - **ID:** T-901
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)

- [ ] **Task B — sonnet** — first sonnet task
  - **ID:** T-902
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
TASKSEOF
    git add TASKS.md
    git commit --quiet -m "init" --no-gpg-sign
    # Set origin/master (reservation-role reads via `git show origin/master:TASKS.md`)
    git update-ref refs/remotes/origin/master HEAD
)
export FLEET_ENGINE_ROOT="$FAKE_REPO"
export FLEET_GAME_ROOT="$FAKE_REPO/no-game-here"

# Make our fake fleet-claim discoverable on PATH so the dispatcher's
# subshell call resolves to it (with the test env vars).
mkdir -p "$TMPROOT/bin"
ln -s "$FLEET_CLAIM" "$TMPROOT/bin/fleet-claim"
export PATH="$TMPROOT/bin:$PATH"

# --- Test 1: no records, no reservations -----------------------------------
echo "T1: empty state — count is 0"
n=$("$DISPATCHER" --count-active opus-worker)
assert_eq "$n" "0" "opus-worker count is 0 with empty state"

# --- Test 2: in-flight only ------------------------------------------------
echo "T2: one in-flight dispatch record for opus-worker"
cat >"$FLEET_STATE_DIR/dispatch/pane-3.json" <<'JSON'
{"role":"opus-worker","pane":"%3","dispatched_at":"2026-05-08T00:00:00Z"}
JSON
n=$("$DISPATCHER" --count-active opus-worker)
assert_eq "$n" "1" "opus-worker count is 1 with one in-flight"
n=$("$DISPATCHER" --count-active sonnet-author)
assert_eq "$n" "0" "sonnet-author count is 0 (in-flight is opus-worker)"

# --- Test 3: reservation only ----------------------------------------------
echo "T3: a reservation for an opus task on a different worktree"
"$FLEET_CLAIM" reserve T-901 opus-worker-2 claude/T-901-something >/dev/null
n=$("$DISPATCHER" --count-active opus-worker)
assert_eq "$n" "2" "opus-worker count is 2 (1 in-flight + 1 reserved on different worktree)"

# --- Test 4: dedup when same worktree has both ------------------------------
echo "T4: add second in-flight whose worktree matches the reservation"
# pane-5's dispatch record — without tmux, fallback identity is pane-5
# (cannot match the reservation key opus-worker-2). To exercise dedup,
# stub tmux so display-message returns the worktree path.
mkdir -p "$TMPROOT/bin-tmux"
cat >"$TMPROOT/bin-tmux/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
# Test stub: respond to `tmux has-session` (success) and
# `tmux display-message -t <pane> -p <fmt>` (returns canned path per pane).
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    display-message)
        pane=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) shift 2 ;;
                *)  shift ;;
            esac
        done
        case "$pane" in
            "%3") echo "/fake/worktrees/opus-worker-1" ;;
            "%5") echo "/fake/worktrees/opus-worker-2" ;;
            *)    echo "/fake/worktrees/unknown" ;;
        esac
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$TMPROOT/bin-tmux/tmux"

# Add a second dispatch record on pane %5 (opus-worker-2)
cat >"$FLEET_STATE_DIR/dispatch/pane-5.json" <<'JSON'
{"role":"opus-worker","pane":"%5","dispatched_at":"2026-05-08T00:00:00Z"}
JSON

# With tmux stub: pane %3 → opus-worker-1, pane %5 → opus-worker-2.
# Reservation is on opus-worker-2 → dedup with pane %5's record.
# Total unique = 2 (opus-worker-1, opus-worker-2).
PATH="$TMPROOT/bin-tmux:$PATH" n=$("$DISPATCHER" --count-active opus-worker)
assert_eq "$n" "2" "opus-worker count dedups same-worktree dispatch+reservation"

# --- Test 5: cap defaults --------------------------------------------------
echo "T5: cap defaults (no conf, no env)"
unset FLEET_CONCURRENCY_OPUS_WORKER FLEET_CONCURRENCY_SONNET_AUTHOR \
      FLEET_CONCURRENCY_SONNET_REVIEWER FLEET_CONCURRENCY_OPUS_REVIEWER \
      FLEET_CONCURRENCY_QUEUE_MANAGER FLEET_CONCURRENCY_MERGER
rm -f "$FLEET_CONF"
cap=$("$DISPATCHER" --print-cap opus-worker)
assert_eq "$cap" "2" "default cap for opus-worker is 2"
cap=$("$DISPATCHER" --print-cap sonnet-author)
assert_eq "$cap" "2" "default cap for sonnet-author is 2"
cap=$("$DISPATCHER" --print-cap merger)
assert_eq "$cap" "1" "default cap for merger is 1"

# --- Test 6: env var override ---------------------------------------------
echo "T6: env var overrides default"
cap=$(FLEET_CONCURRENCY_OPUS_WORKER=5 "$DISPATCHER" --print-cap opus-worker)
assert_eq "$cap" "5" "env var overrides default cap"

# --- Test 7: conf file override --------------------------------------------
echo "T7: conf file overrides default"
cat >"$FLEET_CONF" <<'CONFEOF'
FLEET_CONCURRENCY_OPUS_WORKER=4
FLEET_CONCURRENCY_MERGER=3
CONFEOF
cap=$("$DISPATCHER" --print-cap opus-worker)
assert_eq "$cap" "4" "conf file sets opus-worker cap to 4"
cap=$("$DISPATCHER" --print-cap merger)
assert_eq "$cap" "3" "conf file sets merger cap to 3"
cap=$("$DISPATCHER" --print-cap sonnet-author)
assert_eq "$cap" "2" "untouched roles keep their default"

# --- Test 8: env var beats conf --------------------------------------------
echo "T8: env var has higher priority than conf"
cap=$(FLEET_CONCURRENCY_OPUS_WORKER=7 "$DISPATCHER" --print-cap opus-worker)
assert_eq "$cap" "7" "env var beats conf file"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
