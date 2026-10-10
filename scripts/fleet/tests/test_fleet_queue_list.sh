#!/usr/bin/env bash
# Hermetic close-out partition and raw-JSON compatibility for fleet-queue-list.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_QUEUE_LIST="$SCRIPT_DIR/fleet-queue-list"
source "$(dirname "$0")/lib_assert.sh"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-fleet-queue-list.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMP"
trap 'rm -rf "$TMP"' EXIT

RAW='[{"number":10,"title":"delivered","labels":[{"name":"fleet:queued"}],"body":"**Model:** opus\n**Blocked by:** (none)"},{"number":11,"title":"partial","labels":[{"name":"fleet:queued"}],"body":"**Model:** opus\n**Blocked by:** (none)"},{"number":12,"title":"ordinary","labels":[{"name":"fleet:queued"}],"body":"**Model:** sonnet\n**Blocked by:** (none)"}]'
mkdir -p "$TMP/bin" "$TMP/fleet/state/declined"

apply_fixture() {
    printf '%s\n' '{"generated_at":"2099-01-01T00:00:00Z","repos":{"engine":{"tasks":{"open":[{"id":"#10","issue":"#10","updatedAt":"2026-10-07T12:00:00Z","shadow_merged_pr":{"number":900}},{"id":"#11","issue":"#11","updatedAt":"2026-10-07T12:00:00Z","shadow_merged_pr":{"number":901}},{"id":"#12","issue":"#12","updatedAt":"2026-10-07T12:00:00Z"}]}}}}' > "$TMP/fleet/state/state.json"
    printf '%s\n' '2026-10-07T12:00:00Z' 'implementation already merged' 'worker' 'shadow_merged_pr=900' > "$TMP/fleet/state/declined/task-engine-10"
}

apply_fixture
cat > "$TMP/bin/gh" <<'EOF'
#!/usr/bin/env bash
if [[ "$1 $2" == "issue list" ]]; then
    printf '%s\n' "$QUEUE_RAW"
    exit 0
fi
echo "unexpected gh call: $*" >&2
exit 99
EOF
chmod +x "$TMP/bin/gh"

export PATH="$TMP/bin:$PATH"
export FLEET_HOME="$TMP/fleet"
export FLEET_STATE_DIR="$TMP/fleet/state"
export QUEUE_RAW="$RAW"

closeout_json=$(python3 "$SCRIPT_DIR/fleet_task_class.py" --shadowed-closeouts \
    "$TMP/fleet/state/state.json" engine)
assert_contains "$closeout_json" '"number":"10"' "shared classifier returns compound task"

out=$($FLEET_QUEUE_LIST --repo engine 2>"$TMP/stderr")
assert_contains "$out" "## Shadowed close-out (1)" "compound task gets close-out section"
assert_contains "$out" "#10  shadowed by PR #900" "close-out names merged PR"
available=${out%%$'\n## Shadowed close-out'*}
assert_contains "$available" "#   11" "shadow-only task remains Available"
assert_contains "$available" "#   12" "ordinary task remains Available"
assert_absent "$available" "#   10" "compound task is absent from Available"

json_out=$($FLEET_QUEUE_LIST --repo engine --json)
assert_eq "$json_out" "$RAW" "--json remains the raw GitHub payload"

mv "$TMP/fleet/state/state.json" "$TMP/fleet/state/state.missing"
out=$($FLEET_QUEUE_LIST --repo engine 2>"$TMP/missing.err")
assert_contains "$out" "#   10" "missing state preserves live Available rows"
assert_contains "$(<"$TMP/missing.err")" "showing live queue only" "missing state warns"

summarize
