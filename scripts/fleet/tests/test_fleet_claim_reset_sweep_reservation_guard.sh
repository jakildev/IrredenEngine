#!/usr/bin/env bash
# Tests for the reservation guard in `fleet-claim reset-sweep-host-claims`.
#
# The boot sweep removes this host's fleet:claim-<host>-* labels from queued
# issues with no open PR. But an interrupted-but-resumable task looks exactly
# like that in GitHub terms — claimed, no PR yet — while its local reservation
# (~/.fleet/reservations/<worktree>.json, which survives clear-all) pins the
# worktree that will resume it. Sweeping that label advertises the task as
# free to other hosts mid-resume: the duplicate-PR window. The guard keeps
# the label whenever a reservation names the issue.
#
# Covers:
#   - same-host claim, no PR, WITH a live reservation -> label kept
#   - same-host claim, no PR, no reservation          -> label swept
#   - reservation for a different issue does not shield the swept one

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_TEST_HOST="mac"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
REMOVED_FILE="$TMPROOT/removed.log"; : > "$REMOVED_FILE"; export REMOVED_FILE

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    repo)
        # game repo not reachable -> sweep covers the engine repo only
        exit 1 ;;
    issue)
        case "$2" in
            list) cat "$ISSUES_JSON"; exit 0 ;;
            edit)
                shift 2; issue="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\t%s\n' "$issue" "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        [[ "$2" == "list" ]] && { cat "$PRS_JSON"; exit 0; }
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# Two queued issues claimed by this host, neither with an open PR. #42 is
# reserved by pool-1 (interrupted mid-task, will resume); #43 is not.
cat > "$ISSUES_JSON" <<'EOF'
[
  {"number": 42, "labels": [{"name": "fleet:queued"}, {"name": "fleet:claim-mac-pool-1"}]},
  {"number": 43, "labels": [{"name": "fleet:queued"}, {"name": "fleet:claim-mac-pool-2"}]}
]
EOF
echo "[]" > "$PRS_JSON"
printf '{"task_id":"42","branch":"claude/42-topic","created_at":"t","created_epoch":1}\n' \
    > "$FLEET_RESERVATIONS_DIR/pool-1.json"

echo "T1: reserved issue keeps its claim label; unreserved is swept"
out=$("$FLEET_CLAIM" reset-sweep-host-claims 2>&1 || true)
assert_contains "$out" "keeping 'fleet:claim-mac-pool-1'" "reserved #42 reported as kept"
if grep -qF "fleet:claim-mac-pool-1" "$REMOVED_FILE"; then
    bad "reserved #42 label was swept (duplicate-PR window reopened)"
else
    ok "reserved #42 label untouched"
fi
if grep -qF "fleet:claim-mac-pool-2" "$REMOVED_FILE"; then
    ok "unreserved #43 label swept (guard is issue-scoped, not global)"
else
    bad "unreserved #43 label was not swept"
fi

summarize "reset-sweep reservation guard"
