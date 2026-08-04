#!/usr/bin/env bash
# Tests for the smoke lane's dispatch-side host gate (#2839) — fleet-dispatcher's
# smoke_worker_should_fire, exercised through the --smoke-check hook and one
# real dispatch_role tick per arm.
#
# Smoke is the only fleet lane whose work is definitionally host-specific: only a
# native-Windows host can clear fleet:needs-windows-smoke. The scout's projection
# is deliberately host-agnostic (it is the cross-host record of outstanding smoke
# debt that platform-catchup reads), so before this gate a standing
# Windows-pending set kept every non-Windows host dispatching smoke panes for
# work they could never do — measured at 4 no-op dispatches in 43 minutes on a
# macOS host, with the empty-exit backoff hitting its cap twice and the scout
# re-arming after each.
#
# The positive arm (T9) is the load-bearing one: a fix that simply silences the
# lane would pass every negative assertion here.
#
# Host routing under test is the fleet_task_class.py HOST_SMOKE_LABELS map, whose
# `mac` host key maps to a `macos` label (#1383 reconciled the host detectors,
# not the spelling). The python-side per-host pins live in
# test_smoke_worker_projection.py; this suite covers the bash gate and the
# trigger lifecycle around it.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "test setup: fleet-dispatcher not found at $DISPATCHER" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0     # no stagger between ticks
export FLEET_CONCURRENCY_SMOKE_WORKER=1
export BOOT_FANOUT_WINDOW_SECONDS=0         # post-window steady state
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" \
         "$FLEET_STATE_DIR/triggers" "$FLEET_RESERVATIONS_DIR"
touch "$FLEET_CONF"

WINDOWS="fleet:needs-windows-smoke"
LINUX="fleet:needs-linux-smoke"
MACOS="fleet:needs-macos-smoke"

# One idle pool pane; pgrep exits 1 so it reads as not running a wrapper.
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) printf '%%1|pool|zsh\n'; exit 0 ;;
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
            echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    send-keys) exit 0 ;;
    *) exit 0 ;;
esac
TMUXEOF
chmod +x "$STUB_BIN/tmux"
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
chmod +x "$STUB_BIN/pgrep"
export PATH="$STUB_BIN:$PATH"

SLICE="$FLEET_STATE_DIR/projections/smoke-worker.json"
TRIGGER="$FLEET_STATE_DIR/triggers/smoke-worker"

write_slice() {  # write_slice <label...> — one approved PR per label, #101+
    local n=101 body="" first=1 label
    for label in "$@"; do
        [[ $first -eq 1 ]] || body+=","
        first=0
        body+="{\"number\":$n,\"labels\":[\"fleet:approved\",\"$label\"]}"
        n=$((n + 1))
    done
    printf '{"smoke_pending_prs":[%s]}\n' "$body" > "$SLICE"
}

# Bound every dispatcher invocation. fleet-dispatcher's argument `case` falls
# through an unrecognized flag to `main`, i.e. the daemon loop — so running this
# suite against a PRE-FIX tree (which has no --smoke-check arm) would hang
# forever instead of failing, and the positive control that proves the suite
# non-vacuous could never terminate. coreutils `timeout` is absent on a stock
# macOS host, where the guard is simply skipped rather than failing the run —
# the same stance run_all.sh takes for its own per-suite guard.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_BIN="gtimeout"
fi

run_dispatcher() {  # run_dispatcher <host> <args...>
    local host="$1"; shift
    if [[ -n "$TIMEOUT_BIN" ]]; then
        FLEET_TEST_HOST="$host" "$TIMEOUT_BIN" 20 "$DISPATCHER" "$@"
    else
        FLEET_TEST_HOST="$host" "$DISPATCHER" "$@"
    fi
}

check() { run_dispatcher "$1" --smoke-check; }

tick() {  # tick <host> — one dispatch_role smoke-worker tick; prints the log
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$TRIGGER"
    run_dispatcher "$1" --dispatch-role smoke-worker 2>&1 >/dev/null
}

echo "T1: Windows-pending PR on a Windows host -> fire"
write_slice "$WINDOWS"
assert_eq "$(check windows)" "fire prs=101" "windows host serves its own smoke label"

echo "T2: the same PR on a macOS host -> quiet (the #2839 defect)"
assert_eq "$(check mac)" "quiet" "mac host stands down on windows-only smoke work"

echo "T3: ... and on a Linux host -> quiet"
assert_eq "$(check linux)" "quiet" "linux host stands down on windows-only smoke work"

echo "T4: macOS-pending PR on a macOS host -> fire (mac host key, macos label)"
write_slice "$MACOS"
assert_eq "$(check mac)" "fire prs=101" "the mac/macos vocabulary split is reconciled"
assert_eq "$(check windows)" "quiet" "windows host stands down on macos-only work"

echo "T5: a mixed slice reports only this host's PRs"
write_slice "$WINDOWS" "$MACOS" "$LINUX"
assert_eq "$(check windows)" "fire prs=101" "windows sees only #101"
assert_eq "$(check mac)" "fire prs=102" "mac sees only #102"
assert_eq "$(check linux)" "fire prs=103" "linux sees only #103"

echo "T6: an unrecognized host key fails closed"
assert_eq "$(check unknown)" "quiet" "unknown host key matches no label"

echo "T7: a missing slice is quiet, not an error"
mv "$SLICE" "$SLICE.bak"
assert_eq "$(check windows)" "quiet" "absent projection -> stand down"
mv "$SLICE.bak" "$SLICE"

echo "T8: a tick on a non-serving host consumes the trigger and dispatches nothing"
write_slice "$WINDOWS"
out=$(tick mac)
assert_contains "$out" "no smoke work pending for this host" "stand-down is logged"
assert_absent "$out" "dispatching smoke-worker" "no pane is spent"
if [[ -f "$TRIGGER" ]]; then
    bad "trigger consumed (scout re-arms on the next projection change)"
else
    ok "trigger consumed (scout re-arms on the next projection change)"
fi

echo "T9: positive control — a tick on the serving host still dispatches"
out=$(tick windows)
assert_contains "$out" "dispatching smoke-worker" "windows host still gets its pane"
assert_absent "$out" "no smoke work pending for this host" "the gate does not fire on real work"

summarize "fleet-dispatcher smoke host gate"
