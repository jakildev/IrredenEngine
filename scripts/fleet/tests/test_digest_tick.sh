#!/usr/bin/env bash
# Tests for fleet-digest-tick (per-host digest refresh + change notification).
#
# Hermetic: fleet-decisions and fleet-notify are PATH stubs (fixture output /
# call recorder); FLEET_HOME is a temp dir. No network, no real notifications.
#
# Covers:
#   - writes digest/latest.md from fleet-decisions output
#   - first tick notifies (no prior hash)
#   - unchanged decision content -> no second notification
#   - changed decision content -> notifies again
#   - Status-footer-only churn -> NO notification (hash excludes ## Status)
#   - notification title carries the fleet-decisions headline
#   - fleet-decisions failure -> exit 0, previous latest.md kept, no notify

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_DIGEST_TICK="$SCRIPT_DIR/fleet-digest-tick"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$FLEET_DIGEST_TICK" ]]; then
    echo "test setup: fleet-digest-tick not found at $FLEET_DIGEST_TICK" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-digest-tick.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/fleet-home"

write_fixture() {
    cat > "$TMP/fixture.md"
}

cat > "$TMP/bin/fleet-decisions" << EOF
#!/usr/bin/env bash
[[ -f "$TMP/decisions-fail" ]] && exit 1
cat "$TMP/fixture.md"
EOF
chmod +x "$TMP/bin/fleet-decisions"

cat > "$TMP/bin/fleet-notify" << EOF
#!/usr/bin/env bash
printf '%s|%s\n' "\$1" "\$2" >> "$TMP/notify.log"
EOF
chmod +x "$TMP/bin/fleet-notify"

run_tick() {
    PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/fleet-home" \
        "$FLEET_DIGEST_TICK" > "$TMP/out.txt" 2> "$TMP/err.txt"
    echo $?
}
notify_count() { wc -l < "$TMP/notify.log" 2>/dev/null | tr -d ' ' || echo 0; }

write_fixture << 'EOF'
fleet-decisions: 3 decision(s) waiting   [engine+game]

## Merge queue (1)
  engine PR #101  a thing  [approved]

## Decisions (2)
  engine issue #201  hold  [plan approach sign-off]

## Status
  engine: 5 open PR(s) · 1 queued · 1 needs-plan
EOF

# --- first tick: writes digest, notifies ------------------------------------

: > "$TMP/notify.log"
status=$(run_tick)
assert_eq "$status" "0" "first tick exits 0"
assert_contains "$(cat "$TMP/fleet-home/digest/latest.md")" "engine PR #101" "latest.md written from fleet-decisions output"
assert_eq "$(notify_count)" "1" "first tick notifies (no prior hash)"
assert_contains "$(cat "$TMP/notify.log")" "fleet: 3 decision(s) waiting" "notification title carries the headline"
assert_contains "$(cat "$TMP/notify.log")" "digest/latest.md" "notification body points at the digest file"

# --- unchanged content: no re-notification ----------------------------------

status=$(run_tick)
assert_eq "$status" "0" "unchanged tick exits 0"
assert_eq "$(notify_count)" "1" "unchanged decision content does not re-notify"

# --- Status-footer-only churn: still no notification ------------------------

write_fixture << 'EOF'
fleet-decisions: 3 decision(s) waiting   [engine+game]

## Merge queue (1)
  engine PR #101  a thing  [approved]

## Decisions (2)
  engine issue #201  hold  [plan approach sign-off]

## Status
  engine: 9 open PR(s) · 4 queued · 2 needs-plan
EOF
status=$(run_tick)
assert_eq "$(notify_count)" "1" "Status-footer-only churn does not notify"
assert_contains "$(cat "$TMP/fleet-home/digest/latest.md")" "9 open PR(s)" "latest.md still refreshed on footer churn"

# --- changed decision content: notifies again -------------------------------

write_fixture << 'EOF'
fleet-decisions: 4 decision(s) waiting   [engine+game]

## Merge queue (2)
  engine PR #101  a thing  [approved]
  engine PR #102  another  [approved]

## Status
  engine: 9 open PR(s) · 4 queued · 2 needs-plan
EOF
status=$(run_tick)
assert_eq "$(notify_count)" "2" "changed decision content notifies again"
assert_contains "$(tail -1 "$TMP/notify.log")" "4 decision(s)" "second notification carries updated headline"

# --- fleet-decisions failure: keep previous digest, no notify, exit 0 -------

touch "$TMP/decisions-fail"
status=$(run_tick)
assert_eq "$status" "0" "fleet-decisions failure exits 0"
assert_contains "$(cat "$TMP/err.txt")" "keeping previous digest" "failure noted on stderr"
assert_contains "$(cat "$TMP/fleet-home/digest/latest.md")" "engine PR #102" "previous latest.md kept on failure"
assert_eq "$(notify_count)" "2" "failure does not notify"
rm -f "$TMP/decisions-fail"

summarize "fleet-digest-tick tests"
