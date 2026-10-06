#!/usr/bin/env bash
# Tests for ir-run's window-mode default (engine/tools/bin/ir-run "Window
# mode" block). An unattended launch must never take the host's screen, and
# a human's launch must show its window with no flags: only an unattended
# launch — marked FLEET_UNATTENDED=1 (what fleet-run and every fleet pane
# set) or run from an agent harness (CLAUDECODE / AI_AGENT, CODEX_SANDBOX /
# CODEX_THREAD_ID) — gets IR_WINDOW_MODE, the engine's --window-mode
# fallback, by the run's shape, and never over a caller's explicit choice:
#
#   no marker (a human's shell)                                       → untouched
#   capture verb (--auto-screenshot / --auto-record / --auto-profile) → offscreen
#   watchdog run (--timeout / FLEET_RUN_DEFAULT_TIMEOUT)              → background
#   plain exec (no verb, no timeout)                                  → background
#   IR_WINDOW_MODE already set, or --window-mode in the program args → untouched
#   FLEET_WINDOW_MODE                                                 → replaces every default, marker or not
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
# This suite itself runs inside an agent harness; its markers must not leak
# into the human-shell cases.
unset CLAUDECODE AI_AGENT CODEX_SANDBOX CODEX_THREAD_ID
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
assert_contains "$OUT" "window-mode=offscreen" "fleet-run exports offscreen for a capture verb"
OUT="$("$FLEET_RUN" --build-dir "$FAKE_BUILD" echo-mode 2>&1)"; RC=$?
assert_contains "$OUT" "window-mode=background" "fleet-run marks a plain exec background"

# Every case below runs as the fleet does: marked unattended.
export FLEET_UNATTENDED=1

echo "capture verb defaults to offscreen:"
run_ir echo-mode --auto-screenshot 10
assert_eq "$RC" "0" "capture run exits 0"
assert_contains "$OUT" "window-mode=offscreen" "--auto-screenshot exports offscreen"
assert_contains "$OUT" "(window mode: offscreen)" "launch line names the mode"

echo "every capture verb counts:"
run_ir echo-mode --auto-record
assert_contains "$OUT" "window-mode=offscreen" "--auto-record exports offscreen"
run_ir echo-mode --auto-profile 300
assert_contains "$OUT" "window-mode=offscreen" "--auto-profile exports offscreen"

echo "the inline =N form of every capture verb counts:"
run_ir echo-mode --auto-screenshot=10
assert_contains "$OUT" "window-mode=offscreen" "--auto-screenshot=N exports offscreen"
assert_contains "$OUT" "(window mode: offscreen)" "inline form names the mode"
run_ir echo-mode --auto-record=60
assert_contains "$OUT" "window-mode=offscreen" "--auto-record=N exports offscreen"
run_ir echo-mode --auto-profile=75
assert_contains "$OUT" "window-mode=offscreen" "--auto-profile=N exports offscreen"

echo "watchdog run defaults to background:"
run_ir --timeout 10 echo-mode
assert_eq "$RC" "0" "watchdog run exits 0"
assert_contains "$OUT" "window-mode=background" "--timeout exports background"
assert_contains "$OUT" "(window mode: background)" "launch line names the mode"

echo "FLEET_RUN_DEFAULT_TIMEOUT counts as a watchdog run:"
FLEET_RUN_DEFAULT_TIMEOUT=10 run_ir echo-mode
assert_contains "$OUT" "window-mode=background" "default timeout exports background"

echo "capture verb under a watchdog stays offscreen:"
run_ir --timeout 10 echo-mode --auto-screenshot
assert_contains "$OUT" "window-mode=offscreen" "verb outranks the watchdog default"
run_ir --timeout 10 echo-mode --auto-screenshot=4
assert_contains "$OUT" "window-mode=offscreen" "inline verb outranks the watchdog default"
run_ir --timeout 10 echo-mode --auto-profile=75
assert_contains "$OUT" "window-mode=offscreen" "inline profile verb outranks the watchdog default"

echo "plain exec (a probe, a GUI session) is background:"
run_ir echo-mode
assert_contains "$OUT" "window-mode=background" "no verb, no timeout: background exported"
assert_contains "$OUT" "(window mode: background)" "launch line names the mode"

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
assert_contains "$OUT" "window-mode=hidden" "plain-exec default replaced"

echo "an agent harness marks the launch without FLEET_UNATTENDED:"
unset FLEET_UNATTENDED
CLAUDECODE=1 run_ir echo-mode
assert_contains "$OUT" "window-mode=background" "CLAUDECODE plain exec is background"
AI_AGENT=1 run_ir echo-mode --auto-screenshot 10
assert_contains "$OUT" "window-mode=offscreen" "AI_AGENT capture run is offscreen"
CODEX_SANDBOX=seatbelt run_ir --timeout 10 echo-mode
assert_contains "$OUT" "window-mode=background" "CODEX_SANDBOX watchdog run is background"
CODEX_THREAD_ID=abc run_ir echo-mode
assert_contains "$OUT" "window-mode=background" "CODEX_THREAD_ID plain exec is background"
CLAUDECODE=1 run_ir echo-mode --window-mode normal
assert_contains "$OUT" "window-mode=unset" "--window-mode normal is the human's escape inside a harness"
CLAUDECODE=1 IR_WINDOW_MODE=normal run_ir echo-mode
assert_contains "$OUT" "window-mode=normal" "IR_WINDOW_MODE=normal survives a harness"

summarize
