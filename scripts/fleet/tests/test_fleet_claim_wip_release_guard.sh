#!/usr/bin/env bash
# Releasing a completed task must not leave its reviewable PR hidden behind
# fleet:wip. The guard fails before changing claim state; --wip-ok is the
# explicit escape hatch for genuinely unfinished work.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=$(mktemp -d)
cleanup() {
    rm -rf "$TMPROOT"
}
trap cleanup EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_TEST_HOST="mac"
export PRS_JSON="$TMPROOT/prs.json"
export REMOVED_FILE="$TMPROOT/removed.log"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"
: > "$REMOVED_FILE"

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    pr)
        [[ "$2" == "list" ]] && cat "$PRS_JSON"
        ;;
    issue)
        case "$2" in
            view) ;;
            edit)
                shift 3
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\n' "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                ;;
        esac
        ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

make_claim() {
    mkdir -p "$FLEET_CLAIMS_DIR/$1"
    printf 'pool-4\n' > "$FLEET_CLAIMS_DIR/$1/owner"
}

echo "== WIP release guard =="
cat > "$PRS_JSON" <<'JSON'
[
  {"number":800,"headRefName":"claude/700-release-guard","labels":[{"name":"fleet:wip"}]}
]
JSON
make_claim 700
set +e
guard_output=$("$FLEET_CLAIM" release 700 2>&1)
guard_rc=$?
set -e
assert_eq "$guard_rc" "1" "WIP-backed release fails"
assert_contains "$guard_output" "#800" "warning names the WIP PR"
assert_contains "$guard_output" 'gh pr edit 800 --remove-label "fleet:wip"' \
    "warning gives the finalize command"
[[ -d "$FLEET_CLAIMS_DIR/700" ]] \
    && ok "failed release leaves the filesystem claim intact" \
    || bad "failed release removed the filesystem claim"
[[ ! -s "$REMOVED_FILE" ]] \
    && ok "failed release leaves issue labels untouched" \
    || bad "failed release changed issue labels"

echo "== explicit WIP opt-out =="
"$FLEET_CLAIM" release 700 --wip-ok >/dev/null
[[ ! -d "$FLEET_CLAIMS_DIR/700" ]] \
    && ok "--wip-ok releases the claim" \
    || bad "--wip-ok did not release the claim"

make_claim 700
set +e
"$FLEET_CLAIM" release --wip-ok 700 >/dev/null 2>&1
leading_wip_rc=$?
set -e
assert_eq "$leading_wip_rc" "0" "leading --wip-ok is accepted"
[[ ! -d "$FLEET_CLAIMS_DIR/700" ]] \
    && ok "leading --wip-ok releases the claim" \
    || bad "leading --wip-ok did not release the claim"

set +e
missing_issue_output=$("$FLEET_CLAIM" release --wip-ok 2>&1)
missing_issue_rc=$?
set -e
assert_eq "$missing_issue_rc" "2" "leading --wip-ok still requires an issue number"
assert_contains "$missing_issue_output" \
    "usage: fleet-claim release <issue-number> [--wip-ok]" \
    "missing issue number prints release usage"
assert_absent "$missing_issue_output" "unbound variable" \
    "missing issue number does not crash under nounset"
help_output=$("$FLEET_CLAIM" --help)
assert_contains "$help_output" "release <issue-number> [--wip-ok]" \
    "help documents the WIP release override"

echo "== no-PR carve-out =="
printf '[]\n' > "$PRS_JSON"
make_claim 701
"$FLEET_CLAIM" release 701 >/dev/null
[[ ! -d "$FLEET_CLAIMS_DIR/701" ]] \
    && ok "no matching PR still releases normally" \
    || bad "no-PR release did not release the claim"

summarize "fleet-claim WIP release guard (#2533)"
