#!/usr/bin/env bash
# Tests for the C2 additions to fleet-claim `reconcile`:
#   - R6: an issue carrying fleet:queued + human:owned is flagged (flag-only).
#   - Deduped, persistence-gated escalation: flag-only drift that survives
#     N --apply ticks files EXACTLY ONE fleet:state-drift tracking issue, then
#     refreshes it in place (never a second one), and resets when drift clears.
#   - report-only stays pure: it neither advances persistence nor files issues.
#   - The tracker lookup itself (`reconcile_open_drift_trackers`) reads REST
#     (`gh api repos/.../issues?labels=fleet:state-drift&state=open`, filtering
#     out pull requests) rather than `gh issue list --json` (GraphQL), and a
#     FAILED lookup is distinguished from a CONFIRMED-empty one: a tick whose
#     lookup errors neither files nor closes a tracker, and a tick that sees
#     more than one open tracker (the exact state a failed lookup used to
#     cause) refreshes the newest and closes the rest, naming the survivor in
#     the close comment.
#
# Like the C1 test, `gh` is stubbed so the label/PR surfaces are canned JSON.
# The stub is stateful for the tracker: it keeps an ordered list of open
# tracker numbers (newest first) in $TRACKER_LIST, `issue create` prepends a
# freshly-minted number, `issue close <n>` removes `<n>`, and `gh api` against
# the state-drift query renders the list (plus a labeled PR decoy) as REST JSON
# and runs the caller's own --jq program over it through real `jq`. The stub
# resolves the HTTP method as gh does (any parameter flag without --method
# means POST) and fails a non-GET lookup. Touching $FAIL_LOOKUP makes that
# `gh api` call exit 1 with no output, modeling a rate-limited/errored lookup.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "test setup: jq not on PATH; skipping (the gh stub runs the tracker lookup's real --jq filter)" >&2
    exit 0
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
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
# issue 700: fleet:queued + human:owned → R6 (flag-only). No FS claim, no PR.
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
export API_LOG="$TMPROOT/api.log"; : > "$API_LOG"
export ISSUE_LIST_LOG="$TMPROOT/issue-list.log"; : > "$ISSUE_LIST_LOG"
# Ordered (newest-first) list of open tracker numbers, one per line.
export TRACKER_LIST="$TMPROOT/tracker.list"; : > "$TRACKER_LIST"
export TRACKER_NEXT="$TMPROOT/tracker.next"; echo 9001 > "$TRACKER_NEXT"
# Touch this to make the state-drift `gh api` lookup fail (exit 1, no output).
export FAIL_LOOKUP="$TMPROOT/fail-lookup.flag"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            list)
                printf '%s\n' "$*" >> "$ISSUE_LIST_LOG"
                # A GraphQL tracker lookup shares $FAIL_LOOKUP with the REST
                # one, so a regression back to `issue list` fails the same way.
                if printf '%s ' "$@" | grep -q 'fleet:state-drift'; then
                    [[ -f "$FAIL_LOOKUP" ]] && exit 1
                    head -n1 "$TRACKER_LIST"
                    exit 0
                fi
                cat "$ISSUES_JSON"
                exit 0 ;;
            create)
                printf '%s\n' "$*" >> "$CREATE_LOG"
                n=$(cat "$TRACKER_NEXT")
                echo "$((n + 1))" > "$TRACKER_NEXT"
                { echo "$n"; cat "$TRACKER_LIST"; } > "$TRACKER_LIST.tmp"
                mv "$TRACKER_LIST.tmp" "$TRACKER_LIST"
                echo "https://github.com/jakildev/IrredenEngine/issues/$n"
                exit 0 ;;
            edit)
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            close)
                printf '%s\n' "$*" >> "$CLOSE_LOG"
                grep -vFx "$3" "$TRACKER_LIST" > "$TRACKER_LIST.tmp" || true
                mv "$TRACKER_LIST.tmp" "$TRACKER_LIST"
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        printf '%s\n' "$*" >> "$API_LOG"
        # Real `gh api` sends POST once any parameter flag is present unless
        # --method/-X names another verb; POST to the issues list endpoint is
        # issue creation, which 422s without a title.
        shift
        method="" has_params=0 jqexpr="" prev=""
        for a in "$@"; do
            case "$prev" in
                -X|--method) method="$a" ;;
                --jq|-q)     jqexpr="$a" ;;
            esac
            case "$a" in
                -f|-F|--field|--raw-field|--input) has_params=1 ;;
                --method=*) method="${a#--method=}" ;;
            esac
            prev="$a"
        done
        [[ -z "$method" ]] && { (( has_params )) && method=POST || method=GET; }
        if printf '%s ' "$@" | grep -q 'labels=fleet:state-drift'; then
            if [[ "$method" != GET ]]; then
                echo "gh: Validation Failed (HTTP 422)" >&2
                exit 1
            fi
            [[ -f "$FAIL_LOOKUP" ]] && exit 1
            # The issues endpoint returns PRs carrying the label too; the
            # decoy proves the caller's --jq filters them out.
            {
                echo '[{"number":8999,"pull_request":{"url":"x"}}'
                while IFS= read -r n; do
                    [[ -n "$n" ]] && echo ",{\"number\":$n}"
                done < "$TRACKER_LIST"
                echo ']'
            } | jq -r "${jqexpr:-.}"
            exit 0
        fi
        exit 0 ;;
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
echo '[]' > "$ISSUES_JSON"   # issue 700 no longer queued+human:owned
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

echo "=== Phase 7: two open trackers → one tick refreshes the newest, closes the other, naming the survivor (AC2) ==="
# Simulate the exact state a failed lookup used to cause: a second tracker
# (9010) open alongside the one Phase 6 already filed (9002), newest first
# per the real REST call's sort=created&direction=desc.
{ echo "9010"; cat "$TRACKER_LIST"; } > "$TRACKER_LIST.setup"
mv "$TRACKER_LIST.setup" "$TRACKER_LIST"
edit_n_before=$(wc -l < "$EDIT_LOG" | tr -d ' ')
close_n_before=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
run_reconcile --apply
edit_n=$(wc -l < "$EDIT_LOG" | tr -d ' ')
[[ "$edit_n" -eq $((edit_n_before + 1)) ]] && ok "the newest tracker is refreshed via issue edit" || bad "edit count did not advance by 1 ($edit_n_before -> $edit_n)"
if tail -n1 "$EDIT_LOG" | grep -qE '\bissue edit 9010\b'; then ok "the refreshed tracker is #9010, the newest"; else bad "edit did not target #9010: $(tail -n1 "$EDIT_LOG")"; fi
close_n=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
[[ "$close_n" -eq $((close_n_before + 1)) ]] && ok "exactly one duplicate tracker closed this tick" || bad "close count did not advance by 1 ($close_n_before -> $close_n)"
last_close=$(tail -n1 "$CLOSE_LOG")
if printf '%s' "$last_close" | grep -qE '\bissue close 9002\b'; then ok "the duplicate closed is #9002 (the older tracker)"; else bad "closed the wrong tracker: $last_close"; fi
if printf '%s' "$last_close" | grep -q '#9010'; then ok "the close comment names the survivor (#9010)"; else bad "close comment does not name the survivor: $last_close"; fi
[[ "$(cat "$TRACKER_LIST")" == "9010" ]] && ok "exactly one tracker (#9010) remains open after dedup" || bad "tracker list after dedup: $(cat "$TRACKER_LIST" | tr '\n' ',')"

echo "=== Phase 8: a failing tracker lookup skips filing/refreshing entirely (AC1) ==="
create_n_before=$(wc -l < "$CREATE_LOG" | tr -d ' ')
edit_n_before=$(wc -l < "$EDIT_LOG" | tr -d ' ')
touch "$FAIL_LOOKUP"
run_reconcile --apply
create_n=$(wc -l < "$CREATE_LOG" | tr -d ' ')
[[ "$create_n" == "$create_n_before" ]] && ok "a failed lookup files no gh issue create (does not mistake the failure for 'no tracker')" || bad "a failed lookup filed a tracker anyway (create count $create_n_before -> $create_n)"
edit_n=$(wc -l < "$EDIT_LOG" | tr -d ' ')
[[ "$edit_n" == "$edit_n_before" ]] && ok "a failed lookup also skips the refresh (no gh issue edit)" || bad "a failed lookup still edited anyway (edit count $edit_n_before -> $edit_n)"

echo "=== Phase 9: drift clears but the lookup still fails → no gh issue close, tick still exits 0 (AC3) ==="
echo '[]' > "$ISSUES_JSON"   # issue 700 no longer queued+human:owned
close_n_before=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
rc=0
run_reconcile --apply || rc=$?
close_n=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
[[ "$close_n" == "$close_n_before" ]] && ok "a failed lookup on drift-clear makes no gh issue close call" || bad "a failed lookup on drift-clear closed anyway (close count $close_n_before -> $close_n)"
[[ "$rc" == "0" ]] && ok "reconcile --apply still exits 0 when the tracker lookup fails" || bad "reconcile --apply exited $rc when the tracker lookup fails (want 0)"

echo "=== Phase 10: once the lookup recovers, the next tick closes the survivor normally ==="
rm -f "$FAIL_LOOKUP"
close_n_before=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
run_reconcile --apply
close_n=$(wc -l < "$CLOSE_LOG" | tr -d ' ')
[[ "$close_n" -eq $((close_n_before + 1)) ]] && ok "a recovered lookup closes the surviving tracker (#9010) once drift stays cleared" || bad "recovered lookup did not close (count $close_n_before -> $close_n)"
[[ -z "$(cat "$TRACKER_LIST")" ]] && ok "no open trackers remain after recovery" || bad "tracker list not empty after recovery: $(cat "$TRACKER_LIST" | tr '\n' ',')"

echo "=== Phase 11: lookup shape + stub fidelity ==="
if grep -q 'fleet:state-drift' "$ISSUE_LIST_LOG"; then bad "the tracker lookup still goes through gh issue list (GraphQL)"; else ok "the tracker lookup never calls gh issue list (GraphQL)"; fi
lookup=$(grep 'labels=fleet:state-drift' "$API_LOG" | tail -n1 || true)
if printf '%s' "$lookup" | grep -q -- '--paginate'; then ok "the tracker lookup pages (--paginate)"; else bad "the tracker lookup does not page: $lookup"; fi
if printf '%s' "$lookup" | grep -q 'per_page=100'; then ok "the tracker lookup sets per_page=100"; else bad "the tracker lookup keeps the 30-item default: $lookup"; fi
if gh api "repos/jakildev/IrredenEngine/issues?labels=fleet:state-drift" -f state=open >/dev/null 2>&1; then
    bad "stub accepted a -f parameter without --method GET (real gh sends POST)"
else
    ok "stub rejects a -f parameter without --method GET, as real gh's POST would"
fi
if gh api --method GET "repos/jakildev/IrredenEngine/issues?labels=fleet:state-drift" -f state=open >/dev/null 2>&1; then
    ok "stub accepts -f parameters under an explicit --method GET"
else
    bad "stub rejected -f parameters under an explicit --method GET"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
