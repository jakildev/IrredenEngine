#!/usr/bin/env bash
# Tests for fleet-dispatcher's per-role concurrency cap.
#
# Covers:
#   - count_active_for_role with no reservations + no dispatch records
#   - count with only in-flight (dispatch records)
#   - count with only reserved (reservations whose task maps to role)
#   - dedup when a worktree has both a dispatch record and a reservation
#   - reservation on an idle pane does NOT count (resume-after-crash)
#   - reservation on a busy pane DOES count (live iteration)
#   - cap defaults (unified worker lane = 4; deprecated per-lane caps sum)
#   - env var override
#   - conf file override
#   - conf override beat by env var
#
# count-active queries the unified `worker` lane; dispatch records,
# pane tags, and reservations carry the same `worker` role string
# (P2-2 renamed the panes; P2-3 removed the legacy-alias shims).

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

# Build a sandbox: temp dirs for state + reservations. Tasks are keyed
# on issue numbers; `reservation-role` resolves the model by calling
# `gh issue view <N>`, which we stub to canned-respond per issue.
TMPROOT=$(mktemp -d)
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
# Point at a guaranteed non-existent tmux session so `session_exists`
# returns false in tests that don't install a tmux stub — otherwise a
# real fleet running on the dev machine would bleed pane state into
# these tests and skew the busy-pane reservation count.
export FLEET_SESSION="fleet-test-$$"

mkdir -p "$FLEET_STATE_DIR/dispatch" "$FLEET_RESERVATIONS_DIR" "$FLEET_CLAIMS_DIR"

# `gh` stub: reservation-role calls `gh issue view <N> --repo R --json
# labels,body`. Issue 901 → fleet:opus, 902 → fleet:sonnet. Everything
# else is empty.
mkdir -p "$TMPROOT/bin"
cat >"$TMPROOT/bin/gh" <<'GHSTUB'
#!/usr/bin/env bash
if [[ "$1" == "issue" && "$2" == "view" ]]; then
    case "$3" in
        901) echo '{"labels":[{"name":"fleet:opus"}],"body":""}' ;;
        902) echo '{"labels":[{"name":"fleet:sonnet"}],"body":""}' ;;
        *)   echo '{"labels":[],"body":""}' ;;
    esac
    exit 0
fi
exit 0
GHSTUB
chmod +x "$TMPROOT/bin/gh"

# Make our fake fleet-claim discoverable on PATH so the dispatcher's
# subshell call resolves to it (with the test env vars).
ln -s "$FLEET_CLAIM" "$TMPROOT/bin/fleet-claim"
export PATH="$TMPROOT/bin:$PATH"

# The dispatch records and pane tags below use the unified `worker`
# role string with per-pane worker-N worktrees (post-P2-2 layout).

# --- Test 1: no records, no reservations -----------------------------------
echo "T1: empty state — count is 0"
n=$("$DISPATCHER" --count-active worker)
assert_eq "$n" "0" "worker count is 0 with empty state"

# --- Test 2: in-flight only ------------------------------------------------
echo "T2: one in-flight dispatch record"
cat >"$FLEET_STATE_DIR/dispatch/pane-3.json" <<'JSON'
{"role":"worker","pane":"%3","dispatched_at":"2026-05-08T00:00:00Z"}
JSON
n=$("$DISPATCHER" --count-active worker)
assert_eq "$n" "1" "worker count is 1 with one in-flight"
n=$("$DISPATCHER" --count-active merger)
assert_eq "$n" "0" "merger count is 0 (in-flight is a worker-lane record)"

# --- Test 3: reservation only ----------------------------------------------
echo "T3: a reservation for a task on a different worktree"
"$FLEET_CLAIM" reserve 901 worker-2 claude/901-something >/dev/null
n=$("$DISPATCHER" --count-active worker)
assert_eq "$n" "2" "worker count is 2 (1 in-flight + 1 reserved on different worktree)"

# --- Test 4: dedup when same worktree has both ------------------------------
echo "T4: add second in-flight whose worktree matches the reservation"
# pane-5's dispatch record — without tmux, fallback identity is pane-5
# (cannot match the reservation key worker-2). To exercise dedup
# AND the new busy-pane-counts-reservation semantics, stub tmux for
# has-session, list-panes (so worktree-busy population works), and
# display-message (so dispatch records resolve to worktree names).
mkdir -p "$TMPROOT/bin-tmux"
cat >"$TMPROOT/bin-tmux/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        # Tab-separated: pane_id, fleet-role, pane_current_command.
        # Both panes report `claude` (busy) so the reservation on
        # worker-2 is counted via the live-iteration path, then
        # dedups with pane %5's dispatch record.
        printf '%%3\tworker\tclaude\n%%5\tworker\tclaude\n'
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            case "$pane" in
                "%3") echo "/fake/worktrees/worker-1" ;;
                "%5") echo "/fake/worktrees/worker-2" ;;
                *)    echo "/fake/worktrees/unknown" ;;
            esac
        elif [[ "$fmt" == *pane_pid* ]]; then
            # Irrelevant for the busy case (cmd!=shell short-circuits
            # the wrapper-child probe), but emit something plausible.
            echo "99000"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$TMPROOT/bin-tmux/tmux"

# Add a second dispatch record on pane %5 (worker-2)
cat >"$FLEET_STATE_DIR/dispatch/pane-5.json" <<'JSON'
{"role":"worker","pane":"%5","dispatched_at":"2026-05-08T00:00:00Z"}
JSON

# With tmux stub: pane %3 → worker-1, pane %5 → worker-2,
# both busy. Reservation is on worker-2 → dedup with pane %5's
# record. Total unique = 2 (worker-1, worker-2).
PATH="$TMPROOT/bin-tmux:$PATH" n=$("$DISPATCHER" --count-active worker)
assert_eq "$n" "2" "worker count dedups same-worktree dispatch+reservation"

# --- Test 4b: reservation on IDLE pane (resume case) does NOT count --------
echo "T4b: reservation on idle pane does NOT count (resume case)"
# Clean slate: drop dispatch records and the prior reservation; reserve
# #901 for worker-2 again.
rm -f "$FLEET_STATE_DIR"/dispatch/*.json
"$FLEET_CLAIM" release-worktree worker-2 >/dev/null 2>&1 || true
"$FLEET_CLAIM" reserve 901 worker-2 claude/901-something >/dev/null

mkdir -p "$TMPROOT/bin-tmux-idle"
cat >"$TMPROOT/bin-tmux-idle/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        # Pane reports `zsh` (idle shell). The pgrep stub alongside
        # this tmux stub forces the wrapper-child probe to "not
        # running" → the new count_active_for_role semantics skip
        # the reservation as a queued resume signal. "|"-delimited to
        # match list_pane_state's -F format (the stubs originally
        # printed tabs and silently parsed as an empty role).
        printf '%%5|worker|zsh\n'
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            echo "/fake/worktrees/worker-2"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$TMPROOT/bin-tmux-idle/tmux"

# Stub `pgrep` alongside the tmux stub so the wrapper-child probe is
# decoupled from the host's process table. Without this the test would
# rely on no real process matching `pgrep -P <fake-pid> -f
# fleet-dispatch-wrap`, which is true in practice on the fleet box but
# brittle in principle on a Linux host with a high kernel.pid_max.
cat >"$TMPROOT/bin-tmux-idle/pgrep" <<'PGREPEOF'
#!/usr/bin/env bash
# No match for the wrapper-child probe → pane reads as idle.
exit 1
PGREPEOF
chmod +x "$TMPROOT/bin-tmux-idle/pgrep"

n=$(PATH="$TMPROOT/bin-tmux-idle:$PATH" "$DISPATCHER" --count-active worker)
assert_eq "$n" "0" "reservation on idle pane is the resume signal — does not pin the cap"

# --- Test 4c: reservation on BUSY pane DOES count --------------------------
echo "T4c: reservation on busy pane DOES count (live iteration)"
mkdir -p "$TMPROOT/bin-tmux-busy"
cat >"$TMPROOT/bin-tmux-busy/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        # pane_current_command=claude → outside the shell allowlist
        # → considered busy without needing the wrapper-child probe.
        # "|"-delimited to match list_pane_state's -F format — with the
        # old tab delimiter this line parsed as an empty role and the
        # busy pane was invisible, so this assertion failed on master.
        printf '%%5|worker|claude\n'
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            echo "/fake/worktrees/worker-2"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "999998"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$TMPROOT/bin-tmux-busy/tmux"

n=$(PATH="$TMPROOT/bin-tmux-busy:$PATH" "$DISPATCHER" --count-active worker)
assert_eq "$n" "1" "reservation on busy pane counts (live iteration)"

# --- Test 5: cap defaults --------------------------------------------------
echo "T5: cap defaults (no conf, no env)"
unset FLEET_CONCURRENCY_WORKER FLEET_CONCURRENCY_OPUS_WORKER \
      FLEET_CONCURRENCY_SONNET_AUTHOR \
      FLEET_CONCURRENCY_SONNET_REVIEWER FLEET_CONCURRENCY_OPUS_REVIEWER \
      FLEET_CONCURRENCY_QUEUE_MANAGER FLEET_CONCURRENCY_MERGER
rm -f "$FLEET_CONF"
cap=$("$DISPATCHER" --print-cap worker)
assert_eq "$cap" "4" "default cap for the worker lane is 4"
cap=$("$DISPATCHER" --print-cap merger)
assert_eq "$cap" "1" "default cap for merger is 1"

# --- Test 5b: deprecated per-lane caps sum into the worker cap -------------
echo "T5b: deprecated OPUS_WORKER + SONNET_AUTHOR sum when WORKER unset"
cap=$(FLEET_CONCURRENCY_OPUS_WORKER=3 FLEET_CONCURRENCY_SONNET_AUTHOR=2 \
    "$DISPATCHER" --print-cap worker)
assert_eq "$cap" "5" "worker cap = deprecated OPUS_WORKER(3) + SONNET_AUTHOR(2)"
cap=$(FLEET_CONCURRENCY_WORKER=6 FLEET_CONCURRENCY_OPUS_WORKER=3 \
    FLEET_CONCURRENCY_SONNET_AUTHOR=2 "$DISPATCHER" --print-cap worker)
assert_eq "$cap" "6" "FLEET_CONCURRENCY_WORKER wins over the deprecated sum"

# --- Test 6: env var override ---------------------------------------------
echo "T6: env var overrides default"
cap=$(FLEET_CONCURRENCY_WORKER=5 "$DISPATCHER" --print-cap worker)
assert_eq "$cap" "5" "env var overrides default cap"

# --- Test 7: conf file override --------------------------------------------
echo "T7: conf file overrides default"
cat >"$FLEET_CONF" <<'CONFEOF'
FLEET_CONCURRENCY_WORKER=4
FLEET_CONCURRENCY_MERGER=3
CONFEOF
cap=$("$DISPATCHER" --print-cap worker)
assert_eq "$cap" "4" "conf file sets worker cap to 4"
cap=$("$DISPATCHER" --print-cap merger)
assert_eq "$cap" "3" "conf file sets merger cap to 3"
cap=$("$DISPATCHER" --print-cap sonnet-reviewer)
assert_eq "$cap" "4" "untouched roles keep their default"

# --- Test 8: env var beats conf --------------------------------------------
echo "T8: env var has higher priority than conf"
cap=$(FLEET_CONCURRENCY_WORKER=7 "$DISPATCHER" --print-cap worker)
assert_eq "$cap" "7" "env var beats conf file"

# --- Test 9: boot fan-out trigger retention --------------------------------
#
# When a successful dispatch leaves the role under cap and the dispatcher
# is still inside its boot fan-out window, the trigger must be retained
# (so the next tick can dispatch into a pane whose @fleet-role tag landed
# late at fleet-up). Outside the window, the legacy consume-on-success
# behavior applies regardless of cap headroom.
# Clear the conf override so the worker lane is back at default cap=4 for T9+.
rm -f "$FLEET_CONF"

echo "T9: boot fan-out — active < cap inside window → retain"
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=9999 \
    "$DISPATCHER" --retain-trigger-check worker 1)
assert_eq "$out" "retain" "active=1 < cap=4 inside window → retain"

echo "T10: boot fan-out — active == cap inside window → consume"
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=9999 \
    "$DISPATCHER" --retain-trigger-check worker 4)
assert_eq "$out" "consume" "active=4 == cap=4 inside window → consume"

echo "T11: boot fan-out — window expired → consume even if under cap"
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=0 \
    "$DISPATCHER" --retain-trigger-check worker 1)
assert_eq "$out" "consume" "active=1 < cap=4 but window expired → consume"

echo "T12: boot fan-out — single-pane role saturated → consume"
# Merger has cap=1; in the real dispatch flow, a successful dispatch
# means the merger pane just got a dispatch record so active==cap, which
# is the only realistic post-dispatch state for cap=1 roles.
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=9999 \
    "$DISPATCHER" --retain-trigger-check merger 1)
assert_eq "$out" "consume" "merger cap=1 with active=1 → consume (cap saturated)"

echo "T13: boot fan-out — cap=0 (unconfigured role) → consume"
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=9999 \
    FLEET_CONCURRENCY_WORKER=0 \
    "$DISPATCHER" --retain-trigger-check worker 0)
assert_eq "$out" "consume" "cap=0 short-circuits the retention check"

# --- Test 14: numeric guard on --retain-trigger-check active arg -----------
echo "T14: non-numeric active arg → usage error, no daemon crash"
if FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS=9999 \
    "$DISPATCHER" --retain-trigger-check worker notanumber >/dev/null 2>&1; then
    rc=0
else
    rc=$?
fi
assert_eq "$rc" "2" "non-numeric active arg exits with rc=2 (usage error)"

# --- Test 15: non-numeric BOOT_FANOUT_WINDOW_SECONDS clamps to default -----
echo "T15: non-numeric BOOT_FANOUT_WINDOW_SECONDS clamps to 60s default"
# Default-window (60s) + fresh CLI invocation (SECONDS≈0) + active<cap → retain.
# If the validate-and-clamp at startup fails, the bash arithmetic would either
# emit a stderr error (silent flip to consume) or trip set -u (rc=1).
out=$(FLEET_DISPATCHER_BOOT_FANOUT_WINDOW_SECONDS="9999s" \
    "$DISPATCHER" --retain-trigger-check worker 1 2>/dev/null)
assert_eq "$out" "retain" "non-numeric env override clamps to 60s → in-window retain"

# --- Tests 16-18: generic pane pool (--find-idle) ---------------------------
#
# Three idle panes: %1 and %2 are pool-tagged (worktrees pool-1, pool-2),
# %3 is pinned to smoke-worker (a dedicated secondary-host pane shape).
# The pgrep stub reports no running wrapper, so all three read as idle.
rm -f "$FLEET_RESERVATIONS_DIR"/*.json
mkdir -p "$TMPROOT/bin-tmux-pool"
cat >"$TMPROOT/bin-tmux-pool/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes)
        printf '%%1|pool|zsh\n'
        printf '%%2|pool|zsh\n'
        printf '%%3|smoke-worker|zsh\n'
        exit 0
        ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            case "$pane" in
                %1) echo "/fake/worktrees/pool-1" ;;
                %2) echo "/fake/worktrees/pool-2" ;;
                %3) echo "/fake/worktrees/smoke-worker" ;;
            esac
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$TMPROOT/bin-tmux-pool/tmux"
cat >"$TMPROOT/bin-tmux-pool/pgrep" <<'PGREPEOF'
#!/usr/bin/env bash
exit 1
PGREPEOF
chmod +x "$TMPROOT/bin-tmux-pool/pgrep"

echo "T16: pool-tagged panes serve any transient role"
out=$(PATH="$TMPROOT/bin-tmux-pool:$PATH" "$DISPATCHER" --find-idle sonnet-reviewer)
assert_eq "$out" "$(printf '%%1\n%%2')" "reviewer gets both pool panes, not the pinned smoke pane"

echo "T17: exact-tag pane stays pinned to its role"
out=$(PATH="$TMPROOT/bin-tmux-pool:$PATH" "$DISPATCHER" --find-idle smoke-worker)
assert_eq "$out" "$(printf '%%1\n%%2\n%%3')" "smoke-worker gets pool panes AND its pinned pane"

echo "T18: reserved worktree — worker-only, and served first"
"$FLEET_CLAIM" reserve 901 pool-2 claude/901-something >/dev/null
out=$(PATH="$TMPROOT/bin-tmux-pool:$PATH" "$DISPATCHER" --find-idle sonnet-reviewer)
assert_eq "$out" "%1" "non-worker role skips the reserved pool pane"
out=$(PATH="$TMPROOT/bin-tmux-pool:$PATH" "$DISPATCHER" --find-idle worker)
assert_eq "$out" "$(printf '%%2\n%%1')" "worker lists the reserved pane first (crash-resume priority)"
"$FLEET_CLAIM" release-worktree pool-2 >/dev/null 2>&1 || true

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
