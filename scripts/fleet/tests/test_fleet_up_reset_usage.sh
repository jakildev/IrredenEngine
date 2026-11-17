#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT
mkdir -p "$TMPROOT/bin" "$TMPROOT/home/.fleet/state/usage/.retest" "$TMPROOT/engine"

cat > "$TMPROOT/bin/claude" <<'STUB'
#!/usr/bin/env bash
exit 0
STUB
cat > "$TMPROOT/bin/tmux" <<'STUB'
#!/usr/bin/env bash
[[ "${1:-}" == "has-session" ]]
STUB
chmod +x "$TMPROOT/bin/claude" "$TMPROOT/bin/tmux"

export HOME="$TMPROOT/home"
export FLEET_ENGINE_ROOT="$TMPROOT/engine"
export FLEET_CONF=/dev/null
export PATH="$TMPROOT/bin:$PATH"
USAGE="$HOME/.fleet/state/usage"

assert_exists() {
    if [[ -e "$1" ]]; then ok "$2"; else bad "$2"; fi
}

assert_missing() {
    if [[ ! -e "$1" ]]; then ok "$2"; else bad "$2"; fi
}

printf '%s\n' '{"rateLimitType":"five_hour","observed_at":1}' > "$USAGE/five_hour.json"
printf '%s\n' '{"rateLimitType":"five_hour","observed_at":2,"status":"rejected"}' \
    > "$USAGE/five_hour.rejected.json"
printf '%s\n' '{"rateLimitType":"seven_day","observed_at":3}' > "$USAGE/seven_day.json"
printf '%s\n' '{"started_at":4,"rate_limit_type":"five_hour"}' \
    > "$USAGE/.retest/five_hour.rejected.json"
printf '%s\n' '{"started_at":4,"rate_limit_type":"five_hour"}' \
    > "$USAGE/.retest/orphan-five-hour.json"

out=$("$SCRIPT_DIR/fleet-up" --reset-usage five_hour 2>&1)
assert_contains "$out" "cleared 2 cached observation(s)" "targeted clear reports its count"
assert_missing "$USAGE/five_hour.json" "normal record cleared"
assert_missing "$USAGE/five_hour.rejected.json" "rejected record of the same type cleared"
assert_missing "$USAGE/.retest/five_hour.rejected.json" "matching retest state cleared"
assert_missing "$USAGE/.retest/orphan-five-hour.json" "orphaned state of the same type cleared"
assert_exists "$USAGE/seven_day.json" "unrelated type preserved"

out=$("$SCRIPT_DIR/fleet-up" --reset-usage missing_type 2>&1)
assert_contains "$out" "no cached observation (nothing to clear)" "missing type is a successful no-op"
assert_exists "$USAGE/seven_day.json" "no-op preserves other records"

out=$("$SCRIPT_DIR/fleet-up" --reset-usage --satellite 2>&1)
assert_contains "$out" "host profile: satellite" "satellite remains a host-profile flag"
assert_contains "$out" "wiped 1 cached observation(s)" "satellite is not consumed as a type"
assert_missing "$USAGE/seven_day.json" "reset composed with satellite still clears all records"

printf '%s\n' '{"rateLimitType":"seven_day","observed_at":3}' > "$USAGE/seven_day.json"
out=$("$SCRIPT_DIR/fleet-up" --reset-usage --no-attach 2>&1)
assert_contains "$out" "wiped 1 cached observation(s)" "no-attach is not consumed as a type"
assert_missing "$USAGE/seven_day.json" "reset composed with no-attach still clears all records"

printf '%s\n' '{"rateLimitType":"seven_day","observed_at":3}' > "$USAGE/seven_day.json"
out=$("$SCRIPT_DIR/fleet-up" --reset-usage 2>&1)
assert_contains "$out" "wiped 1 cached observation(s)" "unqualified reset still clears all"
assert_missing "$USAGE/seven_day.json" "full reset clears the remaining record"

summarize
