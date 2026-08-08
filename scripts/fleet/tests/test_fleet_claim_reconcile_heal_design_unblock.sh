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
# #900/#950: same claimless-wip-on-queued-issue shape, but PR #950 carries
# fleet:design-proposed — an epic-steward proposal park (#1663). It must be
# INVISIBLE to both R7 (healing it would un-park the proposal) and R2 (its
# no-claim state is protocol-correct), across every phase below.
#
# #2462 adds two more standing rows, both of which must stay unhealed for the
# whole run (phases 7-10 assert the specifics):
#   #1000/#1050 — the fleet:awaiting-infra park. PR #1050 is claimless wip on a
#     queued issue (so it satisfies every legacy R7 predicate) but carries the
#     park label and a `Parked-until: #7000` body line. Invisible to R7 AND R2;
#     visible to R8, which un-parks it once #7000 closes.
#   #1100/#1150 — the fleet:blocked backing issue. PR #1150 is a bare claimless
#     wip PR with NEITHER design label — the exact legacy R7 target state — but
#     its issue is fleet:blocked, so there is nothing for a resumed worker to
#     do. R7 must skip it; R2 deliberately still flags it.
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
cat > "$ISSUES_JSON" <<'JSON'
[
  {"number":800,"state":"OPEN","labels":[{"name":"fleet:queued"}]},
  {"number":900,"state":"OPEN","labels":[{"name":"fleet:queued"}]},
  {"number":1000,"state":"OPEN","labels":[{"name":"fleet:queued"}]},
  {"number":1100,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:blocked"}]}
]
JSON

# The #2462 rows are appended to every PR fixture so they are exercised on
# every tick, exactly as #950 is. PARKED_PR_JSON is swapped by the later
# phases to model park/malformed-park/un-parked without disturbing #850.
PARKED_PR_JSON='{"number":1050,"headRefName":"claude/1000-parked-infra",
   "body":"Closes #1000\n\nParked-until: #7000",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:awaiting-infra"}]}'
standing_rows() {
    cat <<JSON
  {"number":950,"headRefName":"claude/900-steward-proposed","body":"Closes #900",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-proposed"}]},
  $PARKED_PR_JSON,
  {"number":1150,"headRefName":"claude/1100-blocked-issue","body":"Closes #1100",
   "labels":[{"name":"fleet:wip"}]}
JSON
}
wip_only_prs() {
    cat > "$PRS_JSON" <<JSON
[
  {"number":850,"headRefName":"claude/800-stranded-wip","body":"Closes #800",
   "labels":[{"name":"fleet:wip"}]},
$(standing_rows)
]
JSON
}
healed_prs() {
    cat > "$PRS_JSON" <<JSON
[
  {"number":850,"headRefName":"claude/800-stranded-wip","body":"Closes #800",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"}]},
$(standing_rows)
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
# R8 blocker state: $BLOCKERS_DIR/<n> holds that issue's canned state. A blocker
# with no file reads OPEN, so the park stands by default and a test must opt in
# to the closed case.
export BLOCKERS_DIR="$TMPROOT/blockers"; mkdir -p "$BLOCKERS_DIR"
set_blocker() { printf '%s' "$2" > "$BLOCKERS_DIR/$1"; }
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
            view)
                # R8's live blocker lookup:
                #   gh issue view <N> --repo <r> --json state --jq '.state'
                # Model the real argument shape rather than just the endpoint
                # (scripts/fleet/CLAUDE.md): a call we do not emulate must FAIL,
                # never fall through to a plausible-looking empty answer, or the
                # suite would certify an invocation gh itself rejects (#2781).
                _args=$(printf '%s ' "$@")
                case "$_args" in
                    *"--json state "*) ;;
                    *) echo "gh stub: unmodelled 'issue view' fields: $_args" >&2; exit 64 ;;
                esac
                case "$_args" in
                    *"--jq .state "*) ;;
                    *) echo "gh stub: unmodelled 'issue view' jq: $_args" >&2; exit 64 ;;
                esac
                if [[ -f "$BLOCKERS_DIR/$3" ]]; then cat "$BLOCKERS_DIR/$3"; else echo "OPEN"; fi
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
# grep for the add-label line uniquely counts R7 heals). du_add_count is the
# whole-run total; du_adds scopes it to one PR. Per-phase assertions use the
# SCOPED form: the fixture carries several PRs that must each stay quiet for
# their own reason, and a global count silently couples every phase to all of
# them — a heal leaking from one row would then fail another row's phase with a
# message naming the wrong PR.
du_add_count() { grep -c 'add-label fleet:design-unblocked' "$EDIT_LOG" 2>/dev/null || true; }
du_adds() { grep -c "^issue edit $1 .*add-label fleet:design-unblocked" "$EDIT_LOG" 2>/dev/null || true; }

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
python3 - "$REPORT" <<'PY' && ok "design-proposed PR #950 invisible to R7 AND R2 (steward park respected)" || bad "R7/R2 fired on the design-proposed PR #950"
import sys, json
r = json.load(open(sys.argv[1]))
hits = [f for f in r["findings"] if f["target"] == 950 and f["rule"] in ("R2", "R7")]
assert not hits, f"design-proposed PR must be exempt, got: {hits}"
PY

echo "=== Phase 2: --apply ticks accrue; freshly-opened PR NOT healed below threshold ==="
run_reconcile --apply
c=$(heal_count); [[ "$c" == "1" ]] && ok "apply tick 1 → heal count 1" || bad "tick 1 count=$c (want 1)"
c=$(du_adds 850); [[ "$c" == "0" ]] && ok "tick 1 below threshold → no heal (fresh-PR no-op)" || bad "tick 1 healed early (add count=$c)"

# Interleave a report-only run — must not advance the heal counter or heal.
run_reconcile
c=$(heal_count); [[ "$c" == "1" ]] && ok "interleaved report-only leaves heal count at 1" || bad "report-only changed heal count to $c"
c=$(du_adds 850); [[ "$c" == "0" ]] && ok "interleaved report-only heals nothing" || bad "report-only healed (add count=$c)"

run_reconcile --apply
c=$(heal_count); [[ "$c" == "2" ]] && ok "apply tick 2 → heal count 2" || bad "tick 2 count=$c (want 2)"
c=$(du_adds 850); [[ "$c" == "0" ]] && ok "tick 2 still below threshold → no heal" || bad "tick 2 healed early (add count=$c)"

echo "=== Phase 3: threshold tick heals (re-adds fleet:design-unblocked) ==="
run_reconcile --apply
c=$(heal_count); [[ "$c" == "3" ]] && ok "apply tick 3 → heal count 3 (== threshold)" || bad "tick 3 count=$c (want 3)"
c=$(du_adds 850); [[ "$c" == "1" ]] && ok "tick 3 healed: added fleet:design-unblocked exactly once" || bad "tick 3 add count=$c (want 1)"
if grep -q '850' "$EDIT_LOG" && grep -q 'add-label fleet:design-unblocked' "$EDIT_LOG"; then
    ok "heal targeted PR #850 with --add-label fleet:design-unblocked"
else
    bad "heal did not edit PR #850 with the design-unblocked label"
fi

echo "=== Phase 4: idempotent — once healed (label present), R7 stops firing ==="
healed_prs   # PR #850 now carries fleet:design-unblocked
run_reconcile --apply
c=$(heal_count); [[ "$c" == "0" ]] && ok "healed PR → R7 no longer fires; counter resets to 0" || bad "counter not reset (count=$c)"
c=$(du_adds 850); [[ "$c" == "1" ]] && ok "no further heal once the label is back (still 1 add)" || bad "re-healed an already-healed PR (add count=$c)"

echo "=== Phase 5: re-stranded PR re-accrues + re-heals after threshold ==="
wip_only_prs   # label lost again (e.g. another half-executed unblock)
run_reconcile --apply   # count 1
run_reconcile --apply   # count 2
c=$(du_adds 850); [[ "$c" == "1" ]] && ok "re-accrual below threshold does not re-heal yet" || bad "re-healed early (add count=$c)"
run_reconcile --apply   # count 3 == threshold → re-heal
c=$(heal_count); [[ "$c" == "3" ]] && ok "re-stranded reaches threshold again (count 3)" || bad "re-accrual count=$c (want 3)"
c=$(du_adds 850); [[ "$c" == "2" ]] && ok "recurring stranded state heals again after threshold" || bad "no re-heal after recurrence (add count=$c)"

echo "=== Phase 6: design-proposed PR #950 never healed across any apply tick ==="
if grep -q '950' "$EDIT_LOG"; then
    bad "an apply pass edited the design-proposed PR #950: $(grep '950' "$EDIT_LOG")"
else
    ok "no apply pass ever edited PR #950"
fi
c=$(python3 - "$HEALPERSIST" <<'PY'
import sys, json
try:
    s = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
print(sum(1 for k in (s if isinstance(s, dict) else {}) if k.endswith(":950")))
PY
)
[[ "$c" == "0" ]] && ok "no heal-persistence key accrued for PR #950" || bad "heal persistence tracked the design-proposed PR (#950 keys=$c)"

# --- #2462: the fleet:awaiting-infra park (R8) + the fleet:blocked predicate --
# Quiet #850 first (give it back its design-unblocked label) so it stops
# re-heal-cycling every 3 ticks and its lines stop interleaving into the log.
# Every assertion below is PR-scoped regardless, so this is hygiene, not a
# dependency.
healed_prs

# remove-label edits of the park label, whole-run.
ai_remove_count() { grep -c 'remove-label fleet:awaiting-infra' "$EDIT_LOG" 2>/dev/null || true; }
# Any edit the apply passes made to a specific PR number.
edits_touching() { grep -c "^issue edit $1 " "$EDIT_LOG" 2>/dev/null || true; }

echo "=== Phase 7: fleet:awaiting-infra park is invisible to R7 + R2, visible to R8 ==="
run_reconcile
python3 - "$REPORT" <<'PY' && ok "parked PR #1050: R8 fires with an unpark_infra apply naming blocker #7000" || bad "R8 missing/misshaped for the parked PR"
import sys, json
r = json.load(open(sys.argv[1]))
r8 = [f for f in r["findings"] if f["rule"] == "R8" and f["target"] == 1050]
assert r8, f"no R8 for #1050: {[(f['rule'], f['target']) for f in r['findings']]}"
a = (r8[0].get("apply") or {})
assert a.get("type") == "unpark_infra", f"R8 apply wrong: {a}"
assert a.get("blocker") == 7000, f"R8 parsed the wrong blocker: {a}"
PY
python3 - "$REPORT" <<'PY' && ok "parked PR #1050 invisible to R7 AND R2 (park respected on both sites)" || bad "R7/R2 fired on the parked PR #1050"
import sys, json
r = json.load(open(sys.argv[1]))
hits = [f for f in r["findings"] if f["target"] == 1050 and f["rule"] in ("R2", "R7")]
assert not hits, f"parked PR must be exempt from R7+R2, got: {hits}"
PY
# The park has been standing since tick 1 (blocker #7000 defaults OPEN), so the
# whole run so far is the >=threshold-ticks evidence.
c=$(edits_touching 1050); [[ "$c" == "0" ]] && ok "no apply tick ever edited the parked PR #1050 (blocker still open)" || bad "an apply pass edited the parked PR (edits=$c)"
c=$(python3 - "$HEALPERSIST" <<'PY'
import sys, json
try:
    s = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
print(sum(1 for k in (s if isinstance(s, dict) else {}) if k.endswith(":1050")))
PY
)
[[ "$c" == "0" ]] && ok "no heal-persistence key accrued for the parked PR #1050" || bad "heal persistence tracked the parked PR (#1050 keys=$c)"
run_reconcile --apply
run_reconcile --apply
run_reconcile --apply
c=$(ai_remove_count); [[ "$c" == "0" ]] && ok "3 more apply ticks with the blocker OPEN → still no un-park" || bad "un-parked while the blocker was open (removes=$c)"
c=$(du_adds 1050); [[ "$c" == "0" ]] && ok "park suppresses the R7 heal across threshold ticks" || bad "healed a parked PR (#1050 adds=$c, want 0)"

echo "=== Phase 8: blocker closes → R8 un-parks → R7 re-surfaces the PR (positive fire) ==="
set_blocker 7000 CLOSED
run_reconcile --apply
c=$(ai_remove_count); [[ "$c" == "1" ]] && ok "closed blocker → exactly one remove-label fleet:awaiting-infra" || bad "un-park did not fire exactly once (removes=$c)"
if grep -q '^issue edit 1050 .*remove-label fleet:awaiting-infra' "$EDIT_LOG"; then
    ok "un-park targeted PR #1050 with --remove-label fleet:awaiting-infra"
else
    bad "un-park edit did not target PR #1050: $(grep 'awaiting-infra' "$EDIT_LOG" || echo none)"
fi
c=$(du_adds 1050); [[ "$c" == "0" ]] && ok "un-park does NOT itself add design-unblocked (R7 owns re-surfacing)" || bad "R8 short-circuited R7 (#1050 adds=$c, want 0)"
# Model the post-un-park PR: label gone, stale Parked-until line left behind
# (reconcile never edits bodies, so the inert line must not re-park it).
PARKED_PR_JSON='{"number":1050,"headRefName":"claude/1000-parked-infra",
   "body":"Closes #1000\n\nParked-until: #7000",
   "labels":[{"name":"fleet:wip"}]}'
healed_prs
run_reconcile --apply   # count 1
run_reconcile --apply   # count 2
c=$(du_adds 1050); [[ "$c" == "0" ]] && ok "un-parked PR accrues below threshold without healing" || bad "healed early after un-park (#1050 adds=$c)"
run_reconcile --apply   # count 3 == threshold
c=$(du_adds 1050); [[ "$c" == "1" ]] && ok "un-parked PR re-surfaces: R7 heals it at threshold" || bad "un-parked PR never re-surfaced (#1050 adds=$c, want 1)"
if grep -q '^issue edit 1050 .*add-label fleet:design-unblocked' "$EDIT_LOG"; then
    ok "the re-surfacing heal targeted PR #1050"
else
    bad "no design-unblocked add recorded for PR #1050"
fi
c=$(ai_remove_count); [[ "$c" == "1" ]] && ok "a stale Parked-until line with no label never re-parks or re-un-parks" || bad "stale marker drove another un-park (removes=$c)"

echo "=== Phase 9: malformed park (label, no Parked-until line) is flag-only ==="
PARKED_PR_JSON='{"number":1050,"headRefName":"claude/1000-parked-infra",
   "body":"Closes #1000",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:awaiting-infra"}]}'
healed_prs
run_reconcile
python3 - "$REPORT" <<'PY' && ok "malformed park → flag-only R8 (accrues to the state-drift tracker)" || bad "malformed park did not produce a flag-only R8"
import sys, json
r = json.load(open(sys.argv[1]))
r8 = [f for f in r["findings"] if f["rule"] == "R8" and f["target"] == 1050]
assert r8, "no R8 finding for the malformed park"
assert r8[0].get("apply") is None, f"malformed park must be flag-only, got: {r8[0].get('apply')}"
PY
before=$(ai_remove_count)
run_reconcile --apply
c=$(ai_remove_count); [[ "$c" == "$before" ]] && ok "malformed park is never auto-un-parked" || bad "un-parked a malformed park (removes=$c, was $before)"
c=$(du_adds 1050); [[ "$c" == "1" ]] && ok "malformed park still suppresses the R7 heal (no heal beyond the phase-8 one)" || bad "healed a malformed park (#1050 adds=$c, want 1)"

echo "=== Phase 9b: trailing prose parses, and only the FIRST #N is the blocker ==="
# Two contracts in one fixture. A worker WILL write the reason inline, so the
# match must not be $-anchored — but the aside here also mentions a second
# issue, and picking that up is the exact over-match that stranded a row on the
# sibling `Blocked by:` field (#2783). Widen the trailing match, not the capture.
PARKED_PR_JSON='{"number":1050,"headRefName":"claude/1000-parked-infra",
   "body":"Closes #1000\n\nParked-until: #7001 (the build wall; see also #7002)",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:awaiting-infra"}]}'
healed_prs
run_reconcile
python3 - "$REPORT" <<'PY' && ok "trailing prose parses, and the aside's #7002 is NOT taken as the blocker" || bad "Parked-until parse broke on trailing prose or over-matched the aside"
import sys, json
r = json.load(open(sys.argv[1]))
r8 = [f for f in r["findings"] if f["rule"] == "R8" and f["target"] == 1050]
assert r8, "no R8 finding at all"
a = (r8[0].get("apply") or {})
assert a.get("type") == "unpark_infra", f"parsed as malformed/flag-only: {r8[0]}"
assert a.get("blocker") == 7001, f"wrong blocker parsed (over-matched the aside?): {a}"
PY
# The capture is one-per-line, so the aside cannot un-park the PR either:
# #7002 is canned CLOSED, and the park must still stand on #7001 being open.
set_blocker 7002 CLOSED
before=$(ai_remove_count)
run_reconcile --apply
c=$(ai_remove_count); [[ "$c" == "$before" ]] && ok "a CLOSED issue mentioned only in the aside does not un-park" || bad "the aside's issue drove an un-park (removes=$c, was $before)"

echo "=== Phase 10: fleet:blocked backing issue suppresses R7 (not R2) ==="
# Non-vacuity: PR #1150 is a bare claimless wip PR with neither design label —
# byte-for-byte the legacy R7 target state — so the ONLY thing standing between
# it and a heal is its issue's fleet:blocked label. R2 flagging it is the proof
# the row reached the rules at all.
run_reconcile
python3 - "$REPORT" <<'PY' && ok "blocked-issue PR #1150 IS seen (R2 still flags it) but R7 skips it" || bad "R7/R2 wrong on the blocked-issue PR"
import sys, json
r = json.load(open(sys.argv[1]))
r2 = [f for f in r["findings"] if f["rule"] == "R2" and f["target"] == 1150]
r7 = [f for f in r["findings"] if f["rule"] == "R7" and f["target"] == 1150]
assert r2, "R2 must still flag the blocked-issue WIP PR (it is deliberately NOT narrowed)"
assert not r7, f"R7 must skip a fleet:blocked backing issue, got: {r7}"
PY
c=$(edits_touching 1150); [[ "$c" == "0" ]] && ok "no apply tick across the whole run ever edited PR #1150" || bad "an apply pass edited the blocked-issue PR (edits=$c)"
c=$(python3 - "$HEALPERSIST" <<'PY'
import sys, json
try:
    s = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
print(sum(1 for k in (s if isinstance(s, dict) else {}) if k.endswith(":1150")))
PY
)
[[ "$c" == "0" ]] && ok "no heal-persistence key accrued for the blocked-issue PR #1150" || bad "heal persistence tracked the blocked-issue PR (keys=$c)"
# Control: an otherwise-identical PR on an UNBLOCKED queued issue still heals,
# so the quiet above is the fleet:blocked predicate and not a dead fixture.
PARKED_PR_JSON='{"number":1050,"headRefName":"claude/1000-parked-infra",
   "body":"Closes #1000","labels":[{"name":"fleet:wip"}]}'
wip_only_prs   # #850 stranded again: the unblocked control
run_reconcile --apply
run_reconcile --apply
run_reconcile --apply
c=$(du_adds 850); [[ "$c" -gt 2 ]] && ok "control: the unblocked twin DOES heal on the same ticks" || bad "control did not heal — the blocked-issue quiet proves nothing (#850 adds=$c, want >2)"

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
