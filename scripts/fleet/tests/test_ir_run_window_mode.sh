#!/usr/bin/env bash
# Tests for ir-run's window-mode default (engine/tools/bin/ir-run "Window
# mode" block). An unattended launch must never take the host's screen, and
# a human's launch must show its window with no flags: only a launch marked
# FLEET_UNATTENDED=1 (what fleet-run and every fleet pane set) gets
# IR_WINDOW_MODE — the engine's --window-mode fallback — by the run's shape,
# and never over a caller's explicit choice:
#
#   no marker (a human's shell)                                       → untouched
#   capture verb (--auto-screenshot / --auto-record / --auto-profile) → hidden
#   watchdog run (--timeout / FLEET_RUN_DEFAULT_TIMEOUT)              → background
#   plain exec (no verb, no timeout)                                  → untouched
#   IR_WINDOW_MODE already set, or --window-mode in the program args → untouched
#   FLEET_WINDOW_MODE                                                 → replaces both defaults, marker or not
#
# Hermetic: a fake build dir with a script that prints the env it received
# stands in for a demo; an isolated IR_LOCK_ROOT keeps the capture-verb case
# (wrapped in ir-acquire gpu) off the host's real locks.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$IR_RUN" ]]; then
    echo "test setup: ir-run not found at $IR_RUN" >&2
    exit 1
fi

FAKE_BUILD="$(mktemp -d)"
trap 'rm -rf "$FAKE_BUILD"' EXIT
export IR_LOCK_ROOT="$FAKE_BUILD/locks"

# The host shell's own settings must not leak into the cases.
unset IR_WINDOW_MODE FLEET_WINDOW_MODE FLEET_RUN_DEFAULT_TIMEOUT FLEET_UNATTENDED
FLEET_RUN="$REPO_ROOT/scripts/fleet/fleet-run"

printf '#!/usr/bin/env bash\necho "window-mode=${IR_WINDOW_MODE:-unset}"\n' > "$FAKE_BUILD/echo-mode"
chmod +x "$FAKE_BUILD/echo-mode"

run_ir() {
    # Runs ir-run with the fake build dir; captures combined output and rc.
    OUT="$("$IR_RUN" --build-dir "$FAKE_BUILD" "$@" 2>&1)"
    RC=$?
}

echo "a human's shell (no marker) is never touched:"
run_ir echo-mode --auto-screenshot 10
assert_eq "$RC" "0" "unmarked capture run exits 0"
assert_contains "$OUT" "window-mode=unset" "capture verb without the marker exports nothing"
assert_absent "$OUT" "(window mode:" "no mode note without the marker"
run_ir --timeout 10 echo-mode
assert_contains "$OUT" "window-mode=unset" "watchdog without the marker exports nothing"
FLEET_WINDOW_MODE=background run_ir echo-mode --auto-screenshot
assert_contains "$OUT" "window-mode=background" "FLEET_WINDOW_MODE is an explicit ask even without the marker"

echo "the fleet-run shim marks the launch unattended:"
OUT="$("$FLEET_RUN" --build-dir "$FAKE_BUILD" echo-mode --auto-screenshot 10 2>&1)"; RC=$?
assert_eq "$RC" "0" "fleet-run capture run exits 0"
assert_contains "$OUT" "window-mode=hidden" "fleet-run exports hidden for a capture verb"
OUT="$("$FLEET_RUN" --build-dir "$FAKE_BUILD" echo-mode 2>&1)"; RC=$?
assert_contains "$OUT" "window-mode=unset" "fleet-run leaves a plain exec alone"

# Every case below runs as the fleet does: marked unattended.
export FLEET_UNATTENDED=1

echo "capture verb defaults to hidden:"
run_ir echo-mode --auto-screenshot 10
assert_eq "$RC" "0" "capture run exits 0"
assert_contains "$OUT" "window-mode=hidden" "--auto-screenshot exports hidden"
assert_contains "$OUT" "(window mode: hidden)" "launch line names the mode"

echo "every capture verb counts:"
run_ir echo-mode --auto-record
assert_contains "$OUT" "window-mode=hidden" "--auto-record exports hidden"
run_ir echo-mode --auto-profile 300
assert_contains "$OUT" "window-mode=hidden" "--auto-profile exports hidden"

echo "the inline =N form of every capture verb counts:"
run_ir echo-mode --auto-screenshot=10
assert_contains "$OUT" "window-mode=hidden" "--auto-screenshot=N exports hidden"
assert_contains "$OUT" "(window mode: hidden)" "inline form names the mode"
run_ir echo-mode --auto-record=60
assert_contains "$OUT" "window-mode=hidden" "--auto-record=N exports hidden"
run_ir echo-mode --auto-profile=75
assert_contains "$OUT" "window-mode=hidden" "--auto-profile=N exports hidden"

echo "watchdog run defaults to background:"
run_ir --timeout 10 echo-mode
assert_eq "$RC" "0" "watchdog run exits 0"
assert_contains "$OUT" "window-mode=background" "--timeout exports background"
assert_contains "$OUT" "(window mode: background)" "launch line names the mode"

echo "FLEET_RUN_DEFAULT_TIMEOUT counts as a watchdog run:"
FLEET_RUN_DEFAULT_TIMEOUT=10 run_ir echo-mode
assert_contains "$OUT" "window-mode=background" "default timeout exports background"

echo "capture verb under a watchdog stays hidden:"
run_ir --timeout 10 echo-mode --auto-screenshot
assert_contains "$OUT" "window-mode=hidden" "verb outranks the watchdog default"
run_ir --timeout 10 echo-mode --auto-screenshot=4
assert_contains "$OUT" "window-mode=hidden" "inline verb outranks the watchdog default"
run_ir --timeout 10 echo-mode --auto-profile=75
assert_contains "$OUT" "window-mode=hidden" "inline profile verb outranks the watchdog default"

echo "plain exec is left alone:"
run_ir echo-mode
assert_contains "$OUT" "window-mode=unset" "no verb, no timeout: nothing exported"
assert_absent "$OUT" "(window mode:" "launch line carries no mode note"

echo "an IR_WINDOW_MODE already in the environment wins:"
IR_WINDOW_MODE=normal run_ir --timeout 10 echo-mode
assert_contains "$OUT" "window-mode=normal" "explicit env survives a watchdog run"
IR_WINDOW_MODE=normal run_ir echo-mode --auto-screenshot
assert_contains "$OUT" "window-mode=normal" "explicit env survives a capture verb"

echo "--window-mode among the program args suppresses the default:"
run_ir --timeout 10 echo-mode --window-mode normal
assert_contains "$OUT" "window-mode=unset" "space-separated flag form"
run_ir echo-mode --auto-screenshot --window-mode=normal
assert_contains "$OUT" "window-mode=unset" "inline flag form"

echo "FLEET_WINDOW_MODE replaces both defaults:"
FLEET_WINDOW_MODE=background run_ir echo-mode --auto-screenshot
assert_contains "$OUT" "window-mode=background" "capture default replaced"
FLEET_WINDOW_MODE=hidden run_ir --timeout 10 echo-mode
assert_contains "$OUT" "window-mode=hidden" "watchdog default replaced"
FLEET_WINDOW_MODE=hidden run_ir echo-mode
assert_contains "$OUT" "window-mode=unset" "plain exec still untouched"

summarize
