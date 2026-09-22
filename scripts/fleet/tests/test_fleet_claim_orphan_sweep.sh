#!/usr/bin/env bash
# Tests for the confirmed-orphan fast path in `fleet-claim cleanup --gh`'s
# open-issue claim sweep.
#
# A claim names its owning host (fleet:claim-<host>-<agent>). fleet-claim writes
# the local lock dir ($CLAIMS_DIR/<slug>) BEFORE adding the GitHub label, so a
# label naming THIS host with no local lock is a claim the owning host has no
# record of — an orphan from an iteration that died without releasing. Such a
# claim strands a claimable task as in_progress for the whole cross-host TTL.
# The fast path sweeps it immediately; cross-host claims and live
# (lock-present) claims keep the TTL.
#
# The TTL is set LARGE here and every claim is reported as freshly added, so the
# age gate alone would sweep nothing — anything swept is the orphan fast path.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }
assert_removed_contains() {
    if grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (not swept)"; fi
}
assert_removed_absent() {
    if ! grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (unexpectedly swept)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
# Large TTL: the age gate would sweep nothing on its own (every claim label
# reports as freshly added). So anything removed proves the confirmed-orphan
# fast path fired.
export FLEET_CLAIM_STALE_SECS_ISSUES=99999
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR"

export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
REMOVED_FILE="$TMPROOT/removed.log"; : > "$REMOVED_FILE"; export REMOVED_FILE

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# `issue list` applies the command's own label qualifiers to the fixture. A
# stub that returns the whole fixture regardless would pass whatever
# population the sweep selects, which is the property under test here.
stub_issue_list() {
    local -a want=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --label|-l) want+=("$2"); shift 2 ;;
            --search)
                local q="$2"; shift 2
                while [[ -n "${q// /}" ]]; do
                    if [[ "$q" =~ ^[[:space:]]*label:\"([^\"]+)\"(.*)$ ]]; then
                        want+=("${BASH_REMATCH[1]}"); q="${BASH_REMATCH[2]}"
                    else
                        echo "gh stub: unmodeled --search qualifier '$q'" >&2
                        exit 2
                    fi
                done ;;
            *) shift ;;
        esac
    done
    WANT_LABELS=$(printf '%s\n' ${want[@]+"${want[@]}"}) python3 -c '
import json, os, sys
want = [w for w in os.environ.get("WANT_LABELS", "").split("\n") if w]
issues = json.load(sys.stdin)
json.dump([i for i in issues
           if all(any((l or {}).get("name") == w for l in (i.get("labels") or []))
                  for w in want)], sys.stdout)
' < "$ISSUES_JSON"
}
case "$1" in
    issue)
        case "$2" in
            list) shift 2; stub_issue_list "$@"; exit 0 ;;
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
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        # events endpoint — report every claim label as added JUST NOW so the
        # age TTL never clears on its own. Only the orphan fast path can sweep.
        printf '%s ' "$@" | grep -q 'events' && date -u +%Y-%m-%dT%H:%M:%SZ
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

mk_claim() {  # create a live local lock for <slug>
    local slug="$1"
    mkdir -p "$FLEET_CLAIMS_DIR/$slug"
    echo "worker-1" > "$FLEET_CLAIMS_DIR/$slug/owner"
    echo "$slug"    > "$FLEET_CLAIMS_DIR/$slug/title"
    date +%s        > "$FLEET_CLAIMS_DIR/$slug/created"
}

# issue 900 same-host, NO local lock, no PR         -> orphan -> swept
# issue 901 same-host, NO local lock, ACTIVE PR      -> kept (PR carries the work)
# issue 902 same-host, local lock present, no PR     -> kept (live worker, within TTL)
# issue 903 cross-host (linux), NO local lock, no PR -> kept (TTL not elapsed)
# issue 904 same-host orphan on a PARKED issue (no fleet:queued) -> swept; a
#     population selected by label:"fleet:queued" cannot see it at all, and
#     `release` is a no-op on a lockless claim, so the label would be stuck.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":900,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":901,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":902,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":903,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-linux-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":904,"state":"OPEN","labels":[{"name":"fleet:needs-human"},{"name":"fleet:claim-mac-worker-1"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[
  {"number":951,"headRefName":"claude/901-live","labels":[{"name":"fleet:wip"}]}
]
JSON
mk_claim 902   # only issue 902 has a live local lock

echo "=== cleanup --gh orphan fast path ==="
OUT=$("$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 || true)
echo "$OUT" | sed 's/^/    /'

assert_removed_contains $'900\tfleet:claim-mac-worker-1' "same-host orphan (no lock, no PR) swept now"
assert_removed_contains $'900\tfleet:in-progress'        "orphan #900 fleet:in-progress cleared"
assert_removed_absent   $'901\tfleet:claim-mac-worker-1' "same-host orphan with ACTIVE PR kept"
assert_removed_absent   $'902\tfleet:claim-mac-worker-1' "same-host claim with live local lock kept"
assert_removed_absent   $'903\tfleet:claim-linux-worker-1' "cross-host claim within TTL kept"
assert_removed_contains $'904\tfleet:claim-mac-worker-1' "orphan on a PARKED issue (no fleet:queued) swept"

# Stub fidelity: the assertion above only proves something if the old
# label:"fleet:queued" population really did drop 904.
echo "=== stub fidelity: the fleet:queued population excludes the parked row ==="
queued_pop=$("$STUB_DIR/gh" issue list --repo jakildev/IrredenEngine --state open \
    --search 'label:"fleet:queued"' --json number,labels)
if echo "$queued_pop" | grep -q '904'; then
    bad "label:\"fleet:queued\" listing still returns the parked row (fixture is inert)"
else
    ok "label:\"fleet:queued\" listing drops the parked row"
fi
unqualified_pop=$("$STUB_DIR/gh" issue list --repo jakildev/IrredenEngine --state open \
    --json number,labels)
if echo "$unqualified_pop" | grep -q '904'; then
    ok "unqualified listing returns the parked row"
else
    bad "unqualified listing dropped the parked row (stub filter is wrong)"
fi

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
