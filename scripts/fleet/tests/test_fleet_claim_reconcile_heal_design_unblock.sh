#!/usr/bin/env bash
# Tests for R7 (#1516): reconcile auto-heal of the half-executed-design-unblock
# state. The state: a fleet:wip PR with NO claim and NEITHER design label, whose
# backing issue is still fleet:queued — what's left when an architect unblock
# removed fleet:design-blocked but never added fleet:design-unblocked. The PR is
# then stranded (no resume signal for the worker loop; the duplicate-claim guard
# refuses the queued issue).
#
# R7 is the symmetric counterpart to R4a (R4a = BOTH design labels; R7 = NEITHER
# on a stranded PR). It auto-re-adds fleet:design-unblocked, but is
# persistence-gated by reconcile_heal_design_unblock with the same tick
# threshold R2 escalation uses, so a freshly-opened WIP PR mid-claim-propagation
# is NEVER healed. Covers: the heal at threshold, the freshly-opened no-op below
# threshold, report-only purity, idempotency once healed, and recurrence.
#
# Like the C1/C2 tests, `gh` is stubbed so the label/PR surfaces are canned JSON.
# The stub is stateful for the R2 state-drift tracker (R2 still flags this same
# state — acceptance: "R2 behavior unchanged" — so it coexists with R7 here).

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
HEALPERSIST="$FLEET_STATE_DIR/design-unblock-heal-persistence.json"

# --- canned label/PR surfaces ---------------------------------------------
# #800: fleet:queued, no claim label. PR #850 (claude/800-*) is the stranded
# wip PR: fleet:wip, no claim, NEITHER design label → the R7 target state.
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":800,"state":"OPEN","labels":[{"name":"fleet:queued"}]}
]
JSON
wip_only_prs() {
    cat > "$PRS_JSON" <<'JSON'
[
  {"number":850,"headRefName":"claude/800-stranded-wip","body":"Closes #800",
   "labels":[{"name":"fleet:wip"}]}
]
JSON
}
healed_prs() {
    cat > "$PRS_JSON" <<'JSON'
[
  {"number":850,"headRefName":"claude/800-stranded-wip","body":"Closes #800",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"}]}
]
JSON
}
wip_only_prs

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
                # R2's tracker lookup carries --label fleet:state-drift with
                # --json number --jq '.[0].number'; emulate the bare-number jq
                # output. Otherwise it's the queued-issue surface fetch.
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
                # R7 add-label heal AND R2 tracker refresh both land here.
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            close)
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
    repo)  exit 1 ;;   # game repo "not reachable" → engine-only scan
    label) exit 0 ;;
    *)     exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

run_reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo jakildev/IrredenEngine >/dev/null 2>&1; }

heal_count() {
    # Count for the heal-persistence key matching repo:850 (ends ":850"), else 0.
    python3 - "$HEALPERSIST" <<'PY'
import sys, json
try:
    s = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
for k, v in (s.items() if isinstance(s, dict) else []):
    if k.endswith(":850"):
        print(v.get("count", 0)); raise SystemExit
print(0)
PY
}

# How many times R7 has re-added fleet:design-unblocked (R2 never adds it, so a
# grep for the add-label line uniquely counts R7 heals).
du_add_count() { grep -c 'add-label fleet:design-unblocked' "$EDIT_LOG" 2>/dev/null || true; }

echo "=== Phase 1: report-only detects R7 (fix-marked) + R2 (coexists) + stays pure ==="
run_reconcile
python3 - "$REPORT" <<'PY' && ok "report-only: R7 finding present for PR #850 with heal_design_unblock apply" || bad "R7 missing/misclassified"
import sys, json
r = json.load(open(sys.argv[1]))
assert r["apply"] is False, "report-only must not be apply mode"
r7 = [f for f in r["findings"] if f["rule"] == "R7"]
assert any(f["target"] == 850 for f in r7), f"no R7 for #850: {[f['rule'] for f in r['findings']]}"
f = next(f for f in r7 if f["target"] == 850)
assert (f.get("apply") or {}).get("type") == "heal_design_unblock", f"R7 apply wrong: {f.get('apply')}"
assert f["applied"] is False, "report-only must record R7 as not-applied (False, not the flag-only None)"
PY
python3 - "$REPORT" <<'PY' && ok "report-only: R2 still flags the same PR (R2 behavior unchanged)" || bad "R2 no longer flags the stranded PR"
import sys, json
r = json.load(open(sys.argv[1]))
r2 = [f for f in r["findings"] if f["rule"] == "R2" and f["target"] == 850]
assert r2, "R2 should still flag the claimless wip PR (acceptance: R2 unchanged)"
assert all(f.get("apply") is None for f in r2), "R2 must remain flag-only"
PY
if [[ ! -f "$HEALPERSIST" ]]; then ok "report-only wrote no heal-persistence state"; else bad "report-only advanced heal persistence"; fi
c=$(du_add_count); [[ "$c" == "0" ]] && ok "report-only added no design-unblocked label" || bad "report-only healed (add count=$c)"

echo "=== Phase 2: --apply ticks accrue; freshly-opened PR NOT healed below threshold ==="
run_reconcile --apply
c=$(heal_count); [[ "$c" == "1" ]] && ok "apply tick 1 → heal count 1" || bad "tick 1 count=$c (want 1)"
c=$(du_add_count); [[ "$c" == "0" ]] && ok "tick 1 below threshold → no heal (fresh-PR no-op)" || bad "tick 1 healed early (add count=$c)"

# Interleave a report-only run — must not advance the heal counter or heal.
run_reconcile
c=$(heal_count); [[ "$c" == "1" ]] && ok "interleaved report-only leaves heal count at 1" || bad "report-only changed heal count to $c"
c=$(du_add_count); [[ "$c" == "0" ]] && ok "interleaved report-only heals nothing" || bad "report-only healed (add count=$c)"

run_reconcile --apply
c=$(heal_count); [[ "$c" == "2" ]] && ok "apply tick 2 → heal count 2" || bad "tick 2 count=$c (want 2)"
c=$(du_add_count); [[ "$c" == "0" ]] && ok "tick 2 still below threshold → no heal" || bad "tick 2 healed early (add count=$c)"

echo "=== Phase 3: threshold tick heals (re-adds fleet:design-unblocked) ==="
run_reconcile --apply
c=$(heal_count); [[ "$c" == "3" ]] && ok "apply tick 3 → heal count 3 (== threshold)" || bad "tick 3 count=$c (want 3)"
c=$(du_add_count); [[ "$c" == "1" ]] && ok "tick 3 healed: added fleet:design-unblocked exactly once" || bad "tick 3 add count=$c (want 1)"
if grep -q '850' "$EDIT_LOG" && grep -q 'add-label fleet:design-unblocked' "$EDIT_LOG"; then
    ok "heal targeted PR #850 with --add-label fleet:design-unblocked"
else
    bad "heal did not edit PR #850 with the design-unblocked label"
fi

echo "=== Phase 4: idempotent — once healed (label present), R7 stops firing ==="
healed_prs   # PR #850 now carries fleet:design-unblocked
run_reconcile --apply
c=$(heal_count); [[ "$c" == "0" ]] && ok "healed PR → R7 no longer fires; counter resets to 0" || bad "counter not reset (count=$c)"
c=$(du_add_count); [[ "$c" == "1" ]] && ok "no further heal once the label is back (still 1 add)" || bad "re-healed an already-healed PR (add count=$c)"

echo "=== Phase 5: re-stranded PR re-accrues + re-heals after threshold ==="
wip_only_prs   # label lost again (e.g. another half-executed unblock)
run_reconcile --apply   # count 1
run_reconcile --apply   # count 2
c=$(du_add_count); [[ "$c" == "1" ]] && ok "re-accrual below threshold does not re-heal yet" || bad "re-healed early (add count=$c)"
run_reconcile --apply   # count 3 == threshold → re-heal
c=$(heal_count); [[ "$c" == "3" ]] && ok "re-stranded reaches threshold again (count 3)" || bad "re-accrual count=$c (want 3)"
c=$(du_add_count); [[ "$c" == "2" ]] && ok "recurring stranded state heals again after threshold" || bad "no re-heal after recurrence (add count=$c)"

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
