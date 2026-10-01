#!/usr/bin/env bash
# Tests fleet-up's scout reap (`reap_stale_scout`, lifted by name from the
# script as the other fleet-up suites lift their functions, so no tmux, git,
# or clone is touched): the Windows arm stops a previous scout through
# PowerShell, and only when the pid's command line still names
# fleet-state-scout; the POSIX arm keeps bash's kill; every arm drops the
# pid file.
#
# scout.pid holds the scout's own os.getpid(), a Windows pid on native
# Windows that bash's kill (MSYS pids) cannot see. A reap that knows only
# `kill -0` leaves every previous scout polling, one more per launch.
#
# Hermetic: reads fleet-up's source only; `powershell` is a PATH stub.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -f "$FLEET_UP" ]] || { echo "SKIP: fleet-up not found at $FLEET_UP" >&2; exit 3; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
SLEEPER_PID=""
cleanup() {
    [[ -n "$SLEEPER_PID" ]] && kill "$SLEEPER_PID" 2>/dev/null
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/scout-reap.XXXXXX")

BLOCK="$TMPROOT/scout-reap-block.sh"
sed -n '/^reap_stale_scout() {/,/^}/p' "$FLEET_UP" > "$BLOCK"
if grep -q '^reap_stale_scout()' "$BLOCK" && grep -q '^}' "$BLOCK"; then
    ok "T0: reap_stale_scout is a top-level function the suite can lift"
else
    bad "T0: reap_stale_scout is a top-level function the suite can lift"
    summarize "fleet-up scout reap"
    exit 1
fi

# powershell stub: records every invocation in $PS_LOG; the Get-CimInstance
# query answers with $PS_CMDLINE (the command line of the pid under test),
# Stop-Process succeeds silently, anything else fails closed.
STUB_BIN="$TMPROOT/bin"
mkdir -p "$STUB_BIN"
PS_LOG="$TMPROOT/powershell.log"
cat > "$STUB_BIN/powershell" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$PS_LOG"
case "$*" in
    *Get-CimInstance*) printf '%s\n' "${PS_CMDLINE:-}" ;;
    *Stop-Process*) : ;;
    *) echo "stub: unmodelled powershell call: $*" >&2; exit 2 ;;
esac
STUB
chmod +x "$STUB_BIN/powershell"

PID_FILE="$TMPROOT/scout.pid"

# reap <ostype> — runs the lifted block's reap on $PID_FILE under the given
# platform, with the stub first on PATH; prints the reap's own output plus
# whether the pid file survived.
reap() {
    : > "$PS_LOG"
    env -i PATH="$STUB_BIN:$PATH" HOME="$TMPROOT" PS_LOG="$PS_LOG" \
        PS_CMDLINE="${PS_CMDLINE:-}" FLEET_SCOUT_REAP_OSTYPE="$1" "$BASH" -c '
        set -uo pipefail
        source "$1"
        reap_stale_scout "$2"
        if [[ -f "$2" ]]; then echo "pid-file=kept"; else echo "pid-file=removed"; fi
    ' _ "$BLOCK" "$PID_FILE"
}

echo "T1: the windows arm stops a pid whose command line names the scout"
printf '4242\n' > "$PID_FILE"
out=$(PS_CMDLINE='C:\msys64\mingw64\bin\python3.exe C:/x/scripts/fleet/fleet-state-scout' reap msys)
assert_contains "$out" "stopping previous fleet-state-scout (windows pid 4242)" "T1 announces the stop"
assert_contains "$out" "pid-file=removed" "T1 drops the pid file"
assert_contains "$(cat "$PS_LOG")" "ProcessId=4242" "T1 reads the pid's command line first"
assert_contains "$(cat "$PS_LOG")" "Stop-Process -Id 4242 -Force" "T1 stops it through PowerShell"

echo "T2: the windows arm leaves a recycled pid alone"
printf '4242\n' > "$PID_FILE"
out=$(PS_CMDLINE='C:\Program Files\Browser\browser.exe --profile default' reap cygwin)
assert_absent "$(cat "$PS_LOG")" "Stop-Process" "T2 never stops a stranger"
assert_contains "$out" "pid-file=removed" "T2 still drops the stale file"

echo "T3: the windows arm with no process behind the pid"
printf '4242\n' > "$PID_FILE"
out=$(PS_CMDLINE='' reap msys)
assert_absent "$(cat "$PS_LOG")" "Stop-Process" "T3 has nothing to stop"
assert_contains "$out" "pid-file=removed" "T3 drops the stale file"

echo "T4: the posix arm terminates a live pid with bash's kill"
sleep 300 &
SLEEPER_PID=$!
printf '%s\n' "$SLEEPER_PID" > "$PID_FILE"
out=$(reap linux-gnu)
wait "$SLEEPER_PID" 2>/dev/null
if kill -0 "$SLEEPER_PID" 2>/dev/null; then
    bad "T4 the previous scout is gone"
else
    ok "T4 the previous scout is gone"
fi
SLEEPER_PID=""
assert_absent "$(cat "$PS_LOG")" "Get-CimInstance" "T4 never consults PowerShell"
assert_contains "$out" "pid-file=removed" "T4 drops the pid file"

echo "T5: a pid file that is not a number is dropped without any kill"
printf 'garbage\n' > "$PID_FILE"
out=$(reap msys)
assert_absent "$(cat "$PS_LOG")" "Get-CimInstance" "T5 queries nothing"
assert_contains "$out" "pid-file=removed" "T5 drops the file"

echo "T6: no pid file is a no-op"
rm -f "$PID_FILE"
out=$(reap msys)
assert_eq "$(cat "$PS_LOG")" "" "T6 calls nothing"
assert_contains "$out" "pid-file=removed" "T6 leaves no file behind"

summarize "fleet-up scout reap"
