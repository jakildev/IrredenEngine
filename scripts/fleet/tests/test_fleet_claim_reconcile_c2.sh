#!/usr/bin/env bash
# Tests for the C2 (#1357) additions to fleet-claim `reconcile`:
#   - R6: an issue carrying fleet:queued + human:owned is flagged (flag-only).
#   - Deduped, persistence-gated escalation: flag-only drift that survives
#     N --apply ticks files EXACTLY ONE fleet:state-drift tracking issue, then
#     refreshes it in place (never a second one), and resets when drift clears.
#   - report-only stays pure: it neither advances persistence nor files issues.
#
# Like the C1 test, `gh` is stubbed so the label/PR surfaces are canned JSON.
# The stub is stateful for the tracker: `issue create` flips a marker file so a
# later `issue list --label fleet:state-drift` reports the tracker as existing
# (emulating --json number --jq '.[0].number' directly).

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
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_STALE_SECS=1800
export FLEET_RECONCILE_DRIFT_TICKS=3
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

REPORT="$FLEET_STATE_DIR/drift-report.json"
PERSIST="$FLEET_STATE_DIR/drift-persistence.json"

# --- canned label/PR surfaces ---------------------------------------------
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
# #700: fleet:queued + human:owned → R6 (flag-only). No FS claim, no PR.
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":700,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"human:owned"}]}
]
JSON
echo '[]' > "$PRS_JSON"

# --- stateful gh stub -----------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export CREATE_LOG="$TMPROOT/create.log"; : > "$CREATE_LOG"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
export CLOSE_LOG="$TMPROOT/close.log"; : > "$CLOSE_LOG"
export TRACKER_STATE="$TMPROOT/tracker.exists"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            list)
                # The escalation's tracker lookup carries --label fleet:state-drift
                # with --json number --jq '.[0].number'; emulate the jq output
                # directly (bare number when the tracker exists, else nothing).
                if printf '%s ' "$@" | grep -q 'fleet:state-drift'; then
                    [[ -f "$TRACKER_STATE" ]] && echo "9001" || true
                else
                    cat "$ISSUES_JSON"
                fi
                exit 0 ;;
            create)
                printf '%s\n' "$*" >> "$CREATE_LOG"
                touch "$TRACKER_STATE"
                echo "https://github.com/jakildev/IrredenEngine/issues/9001"
                exit 0 ;;
            edit)
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            close)
                # Auto-close path: log the call and flip the tracker marker off
                # so a later `issue list --label fleet:state-drift` reports none.
                printf '%s\n' "$*" >> "$CLOSE_LOG"
                rm -f "$TRACKER_STATE"
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)   exit 0 ;;
    repo)  exit 1 ;;   # game repo "not reachable"
    label) exit 0 ;;
    *)     exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

persist_count() {
    # Echo the count for key matching rule:repo:issue:700, or 0 if absent.
    python3 - "$PERSIST" <<'PY'
import sys, json
try:
    s = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
for k, v in s.items():
    if k.endswith(":issue:700"):
        print(v.get("count", 0)); raise SystemExit
print(0)
PY
}

run_reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo jakildev/IrredenEngine >/dev/null 2>&1; }

echo "=== Phase 1: report-only detects R6 + stays pure ==="
run_reconcile
python3 - "$REPORT" <<'PY' && ok "report-only drift report carries flag-only R6 for #700" || bad "R6 missing/misclassified"
import sys, json
r = json.load(open(sys.argv[1]))
assert r["apply"] is False
r6 = [f for f in r["findings"] if f["rule"] == "R6"]
assert any(f["target"] == 700 for f in r6), f"no R6 for #700: {[f['rule'] for f in r['findings']]}"
assert all(f["apply"] is None for f in r6), "R6 must be flag-only"
PY
if [[ ! -f "$PERSIST" ]]; then ok "report-only wrote no persistence state"; else bad "report-only advanced persistence"; fi
if [[ ! -s "$CREATE_LOG" ]]; then ok "report-only filed no tracker"; else bad "report-only filed a tracker"; fi

echo "=== Phase 2: --apply ticks accrue persistence; no tracker before threshold ==="
run_reconcile --apply
c=$(persist_count); [[ "$c" == "1" ]] && ok "apply tick 1 → count 1" || bad "tick 1 count=$c (want 1)"
if [[ ! -s "$CREATE_LOG" ]]; then ok "tick 1 files no tracker (below threshold)"; else bad "tick 1 filed a tracker early"; fi

# Interleave a report-only run — it must not advance the counter or file.
run_reconcile
c=$(persist_count); [[ "$c" == "1" ]] && ok "interleaved report-only leaves count at 1" || bad "report-only changed count to $c"
if [[ ! -s "$CREATE_LOG" ]]; then ok "interleaved report-only files nothing"; else bad "report-only filed a tracker"; fi

run_reconcile --apply
c=$(persist_count); [[ "$c" == "2" ]] && ok "apply tick 2 → count 2" || bad "tick 2 count=$c (want 2)"
if [[ ! -s "$CREATE_LOG" ]]; then ok "tick 2 still below threshold, no tracker"; else bad "tick 2 filed a tracker early"; fi

echo "=== Phase 3: threshold tick files exactly one tracker ==="
run_reconcile --apply
c=$(persist_count); [[ "$c" == "3" ]] && ok "apply tick 3 → count 3 (== threshold)" || bad "tick 3 count=$c (want 3)"
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "1" ]] && ok "tick 3 filed exactly one fleet:state-drift tracker" || bad "tick 3 create count=$create_n (want 1)"
if grep -q 'fleet:state-drift' "$CREATE_LOG"; then ok "tracker created with fleet:state-drift label"; else bad "create missing fleet:state-drift label"; fi

echo "=== Phase 4: subsequent tick refreshes in place (dedup, no 2nd issue) ==="
run_reconcile --apply
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "1" ]] && ok "tick 4 did NOT file a second tracker (still 1 create)" || bad "tick 4 create count=$create_n (want 1)"
edit_n=$(wc -l < "$EDIT_LOG" | tr -d ' ')
[[ "$edit_n" -ge "1" ]] && ok "tick 4 refreshed the existing tracker via issue edit" || bad "tick 4 did not edit the tracker"

echo "=== Phase 5: drift clears → counter resets + tracker auto-closed ==="
echo '[]' > "$ISSUES_JSON"   # #700 no longer queued+human:owned
run_reconcile --apply
c=$(persist_count); [[ "$c" == "0" ]] && ok "cleared drift resets #700 counter to 0" || bad "counter not reset (count=$c)"
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "1" ]] && ok "no new tracker filed after drift cleared" || bad "filed a tracker after clear (create count=$create_n)"
close_n=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
[[ "$close_n" == "1" ]] && ok "drift cleared → auto-closed the open tracker exactly once" || bad "tracker not auto-closed on clear (close count=$close_n)"
if grep -q '9001' "$CLOSE_LOG"; then ok "auto-close targeted the existing tracker #9001"; else bad "auto-close did not target the tracker number"; fi

# An idempotent re-run with no drift must NOT keep trying to close (tracker gone).
run_reconcile --apply
close_n=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
[[ "$close_n" == "1" ]] && ok "no-drift re-run does not re-close (tracker already gone)" || bad "re-run closed again (close count=$close_n)"

echo "=== Phase 6: drift re-appears → fresh tracker re-filed after threshold ==="
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":700,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"human:owned"}]}
]
JSON
run_reconcile --apply   # count 1
run_reconcile --apply   # count 2
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "1" ]] && ok "re-accrual below threshold files no tracker yet" || bad "re-accrual filed early (create count=$create_n)"
run_reconcile --apply   # count 3 == threshold → re-file
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "2" ]] && ok "recurring drift re-files a fresh tracker after auto-close" || bad "no fresh tracker after recurrence (create count=$create_n)"

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
