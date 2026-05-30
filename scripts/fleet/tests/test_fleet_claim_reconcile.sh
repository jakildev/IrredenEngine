#!/usr/bin/env bash
# Tests for fleet-claim's `reconcile` cross-surface reconciliation pass (#1356).
#
# reconcile cross-checks the four claim surfaces — issue/PR labels, open-PR
# state, host-local FS claims, and reservations — and reports (report-only,
# default) or repairs (--apply) drift between them.
#
# These tests stub `gh` so the label/PR surfaces are canned JSON instead of
# live GitHub. The host is pinned to `mac` via FLEET_TEST_HOST so claim-label
# construction is deterministic.
#
# Synthetic drift fixture (engine repo):
#   - FS claim #500 (owner opus-worker-1, created 1h ago, no open PR) → R1
#     stale host-local claim. Also reserved by opus-worker-1.
#   - FS claim #501 (owner opus-worker-2, created now, no open PR) → fresh,
#     must NOT be reaped.
#   - FS claim #502 (owner opus-worker-3) + reservation opus-worker-9 → #502
#     → R3 reservation/claim-owner mismatch (drop the reservation, keep claim).
#   - issue #503 carries design-blocked + design-unblocked → R4a contradiction
#     (design-unblocked applied later → remove the older design-blocked).
#   - issue #504 carries queued + in-progress, no claim/PR → R4b stale in-progress.
#   - PR #600 (head claude/505-orphan, fleet:wip) with no claim → R2 flag only.
#
# Covers the acceptance criteria of #1356:
#   - report-only mutates nothing and writes a well-formed drift-report.json
#   - --apply clears the stale claim (FS lock + label) and the mismatched
#     reservation, but leaves the fresh claim alone
#   - contradictory design labels collapse to the most-recently-applied
#   - a `mac` reconcile only ever removes fleet:claim-mac-* labels

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

ok()   { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad()  { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

assert_dir_present() {
    if [[ -d "$1" ]]; then ok "$2"; else bad "$2 (dir missing: $1)"; fi
}
assert_dir_absent() {
    if [[ ! -d "$1" ]]; then ok "$2"; else bad "$2 (dir still present: $1)"; fi
}
assert_file_present() {
    if [[ -f "$1" ]]; then ok "$2"; else bad "$2 (file missing: $1)"; fi
}
assert_file_absent() {
    if [[ ! -f "$1" ]]; then ok "$2"; else bad "$2 (file still present: $1)"; fi
}
assert_removed_contains() {
    if grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (not in removed log)"; fi
}
assert_removed_absent() {
    if ! grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; then ok "$2"; else bad "$2 (unexpectedly removed)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_STALE_SECS=1800
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

REPORT="$FLEET_STATE_DIR/drift-report.json"
REMOVED_FILE="$TMPROOT/removed.log"
: > "$REMOVED_FILE"
export REMOVED_FILE

# --- canned label/PR surfaces ---------------------------------------------
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":500,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-1"},{"name":"fleet:in-progress"}]},
  {"number":501,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:claim-mac-opus-worker-2"},{"name":"fleet:in-progress"}]},
  {"number":503,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:design-blocked"},{"name":"fleet:design-unblocked"}]},
  {"number":504,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:in-progress"}]}
]
JSON
cat > "$PRS_JSON" <<'JSON'
[
  {"number":600,"headRefName":"claude/505-orphan","labels":[{"name":"fleet:wip"}],"body":"Work in progress."},
  {"number":601,"headRefName":"claude/topic-506","labels":[{"name":"fleet:wip"}],"body":"Implements the thing.\n\nCloses #506\n"}
]
JSON

# --- gh stub --------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# Stub gh for reconcile tests.
case "$1" in
    issue)
        case "$2" in
            list) cat "$ISSUES_JSON"; exit 0 ;;
            edit)
                shift 2
                issue="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in
                        --remove-label) printf '%s\t%s\n' "$issue" "$2" >> "$REMOVED_FILE"; shift 2 ;;
                        *) shift ;;
                    esac
                done
                exit 0
                ;;
            *) exit 0 ;;
        esac
        ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac
        ;;
    api)
        # events endpoint; emulate --jq selecting created_at by label name.
        # design-unblocked applied later than design-blocked.
        if printf '%s ' "$@" | grep -q 'design-unblocked'; then
            echo "2026-05-29T02:00:00Z"
        elif printf '%s ' "$@" | grep -q 'design-blocked'; then
            echo "2026-05-29T01:00:00Z"
        fi
        exit 0
        ;;
    repo) exit 1 ;;   # game repo "not reachable" (unused; explicit --repo)
    label) exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# --- build the synthetic drift fixture ------------------------------------
NOW=$(date +%s)
mk_claim() {
    local slug="$1" owner="$2" created="$3"
    mkdir -p "$FLEET_CLAIMS_DIR/$slug"
    echo "$owner"   > "$FLEET_CLAIMS_DIR/$slug/owner"
    echo "$slug"    > "$FLEET_CLAIMS_DIR/$slug/title"
    echo "$created" > "$FLEET_CLAIMS_DIR/$slug/created"
}
mk_claim 500 opus-worker-1 $((NOW - 3600))   # stale → R1
mk_claim 501 opus-worker-2 "$NOW"            # fresh → survives
mk_claim 502 opus-worker-3 "$NOW"            # fresh, but reservation mismatches
mk_claim 506 opus-worker-4 $((NOW - 3600))   # stale age, BUT PR #601 closes it via body

# Reservations: opus-worker-1 → #500 (consistent, dropped by R1 release),
#               opus-worker-9 → #502 (owner mismatch vs claim → R3 drop)
"$FLEET_CLAIM" reserve 500 opus-worker-1 >/dev/null
"$FLEET_CLAIM" reserve 502 opus-worker-9 >/dev/null

echo "=== Phase 1: report-only (must mutate nothing) ==="
REPORT_OUT=$("$FLEET_CLAIM" reconcile --repo jakildev/IrredenEngine 2>&1)
echo "$REPORT_OUT" | sed 's/^/    /'

# Nothing mutated.
assert_dir_present "$FLEET_CLAIMS_DIR/500" "report-only keeps stale FS claim #500"
assert_dir_present "$FLEET_CLAIMS_DIR/501" "report-only keeps fresh FS claim #501"
assert_dir_present "$FLEET_CLAIMS_DIR/502" "report-only keeps FS claim #502"
assert_dir_present "$FLEET_CLAIMS_DIR/506" "report-only keeps FS claim #506 (PR closes it via body)"
assert_file_present "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" "report-only keeps reservation opus-worker-1"
assert_file_present "$FLEET_RESERVATIONS_DIR/opus-worker-9.json" "report-only keeps reservation opus-worker-9"
if [[ ! -s "$REMOVED_FILE" ]]; then ok "report-only issued no gh remove-label"; else bad "report-only mutated labels: $(cat "$REMOVED_FILE")"; fi
assert_file_present "$REPORT" "report-only wrote drift-report.json"

# Report is well-formed and classifies the expected rules.
python3 - "$REPORT" <<'PY' && ok "drift-report.json well-formed + expected rules" || bad "drift-report.json malformed or missing rules"
import sys, json
r = json.load(open(sys.argv[1]))
assert r["apply"] is False, "apply should be false in report-only"
assert r["host"] == "mac"
rules = {f["rule"] for f in r["findings"]}
for want in ("R1", "R3", "R4a", "R4b", "R2"):
    assert want in rules, f"missing rule {want}: {rules}"
# Fresh claim #501 must NOT be an R1 finding.
r1_targets = {f["target"] for f in r["findings"] if f["rule"] == "R1"}
assert 500 in r1_targets, "R1 should flag #500"
assert 501 not in r1_targets, "R1 must NOT flag fresh #501"
# #506 is stale-aged but its PR closes it via a Closes #506 body ref (non-
# standard branch claude/topic-506) — robust PR match must suppress R1.
assert 506 not in r1_targets, "R1 must NOT flag #506 (PR closes it via body)"
# Flag-only R2 carries no apply action.
r2 = [f for f in r["findings"] if f["rule"] == "R2"]
assert all(f["apply"] is None for f in r2), "R2 must be flag-only"
PY

echo "=== Phase 2: --apply (gated host-local repairs) ==="
APPLY_OUT=$("$FLEET_CLAIM" reconcile --apply --repo jakildev/IrredenEngine 2>&1)
echo "$APPLY_OUT" | sed 's/^/    /'

# R1: stale claim released (FS lock gone + its reservation dropped).
assert_dir_absent  "$FLEET_CLAIMS_DIR/500" "--apply released stale FS claim #500"
assert_file_absent "$FLEET_RESERVATIONS_DIR/opus-worker-1.json" "--apply dropped #500 reservation"
assert_removed_contains $'500\tfleet:claim-mac-opus-worker-1' "--apply removed #500 host claim label"
assert_removed_contains $'500\tfleet:in-progress' "--apply removed #500 fleet:in-progress"

# Fresh claim survives; #506 survives because its PR closes it via body.
assert_dir_present "$FLEET_CLAIMS_DIR/501" "--apply keeps fresh FS claim #501"
assert_dir_present "$FLEET_CLAIMS_DIR/506" "--apply keeps FS claim #506 (PR closes it via body)"

# R3: mismatched reservation dropped, claim kept.
assert_dir_present  "$FLEET_CLAIMS_DIR/502" "--apply keeps claim #502 (authoritative)"
assert_file_absent  "$FLEET_RESERVATIONS_DIR/opus-worker-9.json" "--apply dropped mismatched reservation opus-worker-9"

# R4a: older design-blocked removed, design-unblocked kept.
assert_removed_contains $'503\tfleet:design-blocked' "--apply removed older design-blocked on #503"
assert_removed_absent  $'503\tfleet:design-unblocked' "--apply kept newer design-unblocked on #503"

# R4b: stale in-progress removed.
assert_removed_contains $'504\tfleet:in-progress' "--apply removed stale fleet:in-progress on #504"

# A mac reconcile must only ever touch fleet:claim-mac-* labels.
if ! grep -q 'fleet:claim-linux' "$REMOVED_FILE" 2>/dev/null; then ok "mac reconcile removed no fleet:claim-linux-* labels"; else bad "mac reconcile touched a linux claim label"; fi

# Report reflects apply mode.
python3 - "$REPORT" <<'PY' && ok "apply drift-report.json marks fixes applied" || bad "apply drift-report.json wrong"
import sys, json
r = json.load(open(sys.argv[1]))
assert r["apply"] is True
applied = [f for f in r["findings"] if f.get("apply")]
assert applied and all(f["applied"] is True for f in applied), "fix findings should be applied=true"
PY

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
