#!/usr/bin/env bash
# Boot cleanup removes this host's marker-vouched lane claims before dispatch.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$SCRIPT_DIR/tests/lib_assert.sh"

FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "SKIP: missing $FLEET_CLAIM" >&2; exit 3; }

TMPROOT=$(mktemp -d)
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_TEST_HOST="mac"
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
export REMOVED_FILE="$TMPROOT/removed.log"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" "$TMPROOT/bin"
: > "$REMOVED_FILE"

cat > "$TMPROOT/bin/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1:$2" in
    repo:view) exit 1 ;;
    pr:list) cat "$PRS_JSON" ;;
    issue:list) cat "$ISSUES_JSON" ;;
    pr:edit|issue:edit)
        surface="$1"; target="$3"; shift 3
        while [[ $# -gt 0 ]]; do
            case "$1" in
                --remove-label)
                    printf '%s\t%s\t%s\n' "$surface" "$target" "$2" >> "$REMOVED_FILE"
                    shift 2 ;;
                *) shift ;;
            esac
        done ;;
    *) echo "gh stub: unmodeled $*" >&2; exit 2 ;;
esac
GHSTUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

cat > "$PRS_JSON" <<'EOF'
[
  {"number": 11, "labels": [{"name": "fleet:reviewing-mac-pool-1"}]},
  {"number": 12, "labels": [{"name": "fleet:resolving-mac-pool-2"}]},
  {"number": 13, "labels": [{"name": "fleet:reviewing-linux-pool-1"}, {"name": "fleet:amending-mac-pool-5"}]}
]
EOF
cat > "$ISSUES_JSON" <<'EOF'
[
  {"number": 13, "labels": [{"name": "fleet:reviewing-mac-pool-3"}]},
  {"number": 14, "labels": [{"name": "fleet:planning-mac-pool-4"}]},
  {"number": 16, "labels": [{"name": "fleet:planning-mac-pool-6"}]}
]
EOF
printf '{"task_id":"16","branch":"claude/16-plan","created_at":"t","created_epoch":1}\n' \
    > "$FLEET_RESERVATIONS_DIR/pool-6.json"

echo "T1: clear-all followed by the boot sweep removes orphaned lane labels"
"$FLEET_CLAIM" clear-all >/dev/null
out=$("$FLEET_CLAIM" reset-sweep-host-claims 2>&1)
assert_eq "$(wc -l < "$REMOVED_FILE" | tr -d ' ')" "4" "exactly four orphaned labels removed"
for expected in \
    $'pr\t11\tfleet:reviewing-mac-pool-1' \
    $'pr\t12\tfleet:resolving-mac-pool-2' \
    $'issue\t13\tfleet:reviewing-mac-pool-3' \
    $'issue\t14\tfleet:planning-mac-pool-4'; do
    assert_contains "$(cat "$REMOVED_FILE")" "$expected" "removed $expected"
done

echo "T2: foreign, amending, and reservation-backed labels survive"
assert_absent "$(cat "$REMOVED_FILE")" "fleet:reviewing-linux-pool-1" "foreign reviewing claim kept"
assert_absent "$(cat "$REMOVED_FILE")" "fleet:amending-mac-pool-5" "amending claim kept"
assert_absent "$(cat "$REMOVED_FILE")" "fleet:planning-mac-pool-6" "reserved planning claim kept"
assert_contains "$out" "reserved by worktree 'pool-6'" "reservation guard reported"

summarize "reset-sweep lane-label tests"
