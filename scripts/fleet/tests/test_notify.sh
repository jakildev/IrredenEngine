#!/usr/bin/env bash
# Tests for fleet-notify (cross-host best-effort notification).
#
# Hermetic: delivery binaries (osascript / powershell / powershell.exe /
# notify-send) are PATH stubs that record their invocation; FLEET_NOTIFY_HOST
# forces each host arm; FLEET_HOME is a temp dir. No real notifications fire.
#
# Covers:
#   - log-first contract: every call appends to notify.log on every host arm,
#     including when delivery is unavailable
#   - per-host routing: mac -> osascript, windows -> powershell,
#     wsl -> powershell.exe, linux -> notify-send
#   - title/body reach the delivery binary via env (mac arm)
#   - delivery failure / missing binary still exits 0 with a stderr note
#   - usage error (wrong argc) exits 1

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_NOTIFY="$SCRIPT_DIR/fleet-notify"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$FLEET_NOTIFY" ]]; then
    echo "test setup: fleet-notify not found at $FLEET_NOTIFY" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-notify.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/fleet-home"

make_stub() {
    local name="$1"
    cat > "$TMP/bin/$name" << EOF
#!/usr/bin/env bash
{
    echo "invoked: $name"
    echo "title-env: \${FLEET_NOTIFY_TITLE:-}"
    echo "args: \$*"
} >> "$TMP/calls.log"
exit 0
EOF
    chmod +x "$TMP/bin/$name"
}
make_stub osascript
make_stub powershell
make_stub powershell.exe
make_stub notify-send

run_notify() {
    local host="$1"; shift
    PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/fleet-home" FLEET_NOTIFY_HOST="$host" \
        "$FLEET_NOTIFY" "$@" > "$TMP/out.txt" 2> "$TMP/err.txt"
    echo $?
}

# --- per-host routing --------------------------------------------------------

for pair in "mac:osascript" "windows:powershell" "wsl:powershell.exe" "linux:notify-send"; do
    host="${pair%%:*}"
    binary="${pair#*:}"
    : > "$TMP/calls.log"
    status=$(run_notify "$host" "T-$host" "B-$host")
    calls=$(cat "$TMP/calls.log")
    assert_eq "$status" "0" "$host arm exits 0"
    assert_contains "$calls" "invoked: $binary" "$host arm routes to $binary"
done

# mac arm passes the message via env, not by splicing into the script
: > "$TMP/calls.log"
run_notify mac "quote'break\"title" "body" > /dev/null
assert_contains "$(cat "$TMP/calls.log")" "title-env: quote'break\"title" \
    "title reaches delivery binary via env untouched"

# --- log-first contract ------------------------------------------------------

log=$(cat "$TMP/fleet-home/notify.log")
assert_contains "$log" "T-mac" "mac call logged"
assert_contains "$log" "T-wsl" "wsl call logged"
assert_contains "$log" "B-windows" "windows body logged"

# unknown host: no delivery arm, still logs, still exits 0, notes on stderr
status=$(run_notify unknown "T-unknown" "B-unknown")
assert_eq "$status" "0" "unknown host exits 0 (best-effort contract)"
assert_contains "$(cat "$TMP/err.txt")" "delivery unavailable" "unavailable delivery noted on stderr"
assert_contains "$(cat "$TMP/fleet-home/notify.log")" "T-unknown" "unavailable delivery still logged"

# delivery binary absent from PATH: same contract
status=$(PATH="/usr/bin:/bin" FLEET_HOME="$TMP/fleet-home" FLEET_NOTIFY_HOST="wsl" \
    "$FLEET_NOTIFY" "T-nobin" "B-nobin" > /dev/null 2> "$TMP/err.txt"; echo $?)
assert_eq "$status" "0" "missing delivery binary exits 0"
assert_contains "$(cat "$TMP/fleet-home/notify.log")" "T-nobin" "missing binary still logged"

# delivery binary FAILS: same contract
cat > "$TMP/bin/osascript" << 'EOF'
#!/usr/bin/env bash
exit 7
EOF
chmod +x "$TMP/bin/osascript"
status=$(run_notify mac "T-fail" "B-fail")
assert_eq "$status" "0" "failing delivery binary exits 0"

# --- usage ------------------------------------------------------------------

status=$(run_notify mac "only-title"; exit 0)
status=$(PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/fleet-home" "$FLEET_NOTIFY" "only-title" > /dev/null 2>&1; echo $?)
assert_eq "$status" "1" "wrong argc exits 1 with usage"

summarize "fleet-notify tests"
