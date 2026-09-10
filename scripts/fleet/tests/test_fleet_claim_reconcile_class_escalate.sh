#!/usr/bin/env bash
# Tests for R9 (#2939): reconcile auto-escalation of a fleet:sonnet backing
# issue whose PR is parked in the design lane.
#
# The defect R9 closes: the design-resume tier (FLEET-FEEDBACK-HANDLING.md
# tier 4) is opus+-ONLY, and fleet_task_class.feedback_pr_class pins a
# fleet:design-unblocked PR to opus from the PR's labels alone. When the
# BACKING task is fleet:sonnet, the dispatcher launches only opus panes and
# every one of them correctly declines on the class gate ("never claim outside
# your class") — the work is unreachable by EVERY class, permanently. R9 holds
# the invariant the tier already assumes by re-tagging the backing issue one
# class up (sonnet -> opus), the same move role-worker.md step 8a sanctions on
# a worker's own task.
#
# Covered here:
#   Phase 1  report-only detects R9 on all THREE design-lane labels
#            (design-blocked / -unblocked / -proposed), records applied=False,
#            and mutates nothing (edit + comment logs empty).
#   Phase 1  negatives: an opus-backed design-parked PR, a sonnet-backed PLAIN
#            fleet:wip PR (no design label), and a sonnet-backed design-parked
#            PR whose issue carries a live claim label — none produce R9.
#   Phase 1  coexistence: the fleet:blocked row still yields a flag-only R2 and
#            no R7 (the post-#2926 residual stays human-visible; #2939's
#            Residual paragraph rests on this).
#   Phase 2  --apply performs exactly ONE atomic issue edit carrying BOTH
#            label flags, plus exactly ONE explanatory comment, per firing
#            issue — and nothing at all for the negatives.
#   Phase 3  idempotency: once the issue reads fleet:opus, R9 stops firing.
#
# Like the C1/C2 and R7-heal suites, `gh` is stubbed so the label/PR surfaces
# are canned JSON. The stub models the `issue comment` shape EXPLICITLY (R9 is
# reconcile's first gh issue comment caller) and fails closed on un-modelled
# `issue view` shapes, per scripts/fleet/CLAUDE.md (#2781): a call we do not
# emulate must FAIL, never fall through to a plausible-looking empty answer.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

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

# --- canned label/PR surfaces ---------------------------------------------
# Issues are 8xx, their PRs 9xx (PR number = issue + 100), so a stray
# cross-target assertion can never accidentally match.
#
#   #800/#900 — sonnet backing issue, PR parked fleet:design-blocked.  R9 fires.
#   #810/#910 — same, parked fleet:design-unblocked.                   R9 fires.
#   #820/#920 — same, parked fleet:design-proposed.                    R9 fires.
#     All three lane labels are asserted because R9 keys on the shared
#     PARKED_PR_LABELS frozenset; a local re-listing that dropped one would
#     pass a design-unblocked-only test.
#   #830/#930 — OPUS backing issue, PR design-blocked. No contradiction to fix.
#   #840/#940 — sonnet backing issue, PR is plain fleet:wip (NO design label).
#     Not in the design lane, so its class is nobody's business. (This row also
#     satisfies every R7 predicate and will be healed by R7 on later --apply
#     ticks; that is expected and is why the R9 assertions below grep for the
#     class-swap flags specifically rather than counting edits globally.)
#   #850/#950 — sonnet backing issue, PR design-blocked, but the ISSUE carries
#     fleet:claim-mac-pool-1: a pane is mid-edit on the task itself, so R9 waits
#     for the next tick after release.
#   #1100/#1150 — the fleet:blocked coexistence row copied from the R7-heal
#     suite: claimless fleet:wip PR on a queued+blocked issue. R2 flags it,
#     R7 skips it (#2926), and R9 has no opinion (no class label, no design
#     label) — the residual #2939 records as already-human-visible.
export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"

write_issues() {  # write_issues <label-set-for-800>
    cat > "$ISSUES_JSON" <<JSON
[
  {"number":800,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"$1"}]},
  {"number":810,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}]},
  {"number":820,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}]},
  {"number":830,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"}]},
  {"number":840,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"}]},
  {"number":850,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:sonnet"},{"name":"fleet:claim-mac-pool-1"}]},
  {"number":1100,"state":"OPEN","labels":[{"name":"fleet:queued"},{"name":"fleet:blocked"}]}
]
JSON
}
cat > "$PRS_JSON" <<'JSON'
[
  {"number":900,"headRefName":"claude/800-sonnet-design-blocked","body":"Closes #800",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-blocked"}]},
  {"number":910,"headRefName":"claude/810-sonnet-design-unblocked","body":"Closes #810",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-unblocked"}]},
  {"number":920,"headRefName":"claude/820-sonnet-design-proposed","body":"Closes #820",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-proposed"}]},
  {"number":930,"headRefName":"claude/830-opus-design-blocked","body":"Closes #830",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-blocked"}]},
  {"number":940,"headRefName":"claude/840-sonnet-plain-wip","body":"Closes #840",
   "labels":[{"name":"fleet:wip"}]},
  {"number":950,"headRefName":"claude/850-sonnet-claimed","body":"Closes #850",
   "labels":[{"name":"fleet:wip"},{"name":"fleet:design-blocked"}]},
  {"number":1150,"headRefName":"claude/1100-blocked-issue","body":"Closes #1100",
   "labels":[{"name":"fleet:wip"}]}
]
JSON
write_issues fleet:sonnet

# --- gh stub ---------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log";       : > "$EDIT_LOG"
export COMMENT_LOG="$TMPROOT/comment.log"; : > "$COMMENT_LOG"
export CREATE_LOG="$TMPROOT/create.log";   : > "$CREATE_LOG"
export CLOSE_LOG="$TMPROOT/close.log";     : > "$CLOSE_LOG"
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
                # R9's class swap, R7's heal add-label, and R2's tracker refresh
                # all land here.
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            comment)
                # R9's explanatory comment. Modelled EXPLICITLY rather than
                # swallowed by a catch-all: a missing comment must fail the
                # suite, and reconcile has no other gh issue comment caller, so
                # a catch-all here would certify nothing (#2781).
                _args=$(printf '%s ' "$@")
                case "$_args" in
                    *"--body "*) ;;
                    *) echo "gh stub: 'issue comment' with no --body: $_args" >&2; exit 64 ;;
                esac
                printf '%s\n' "$*" >> "$COMMENT_LOG"
                exit 0 ;;
            close)
                printf '%s\n' "$*" >> "$CLOSE_LOG"
                rm -f "$TRACKER_STATE"
                exit 0 ;;
            view)
                # R8's live blocker lookup is the only modelled `issue view`:
                #   gh issue view <N> --repo <r> --json state --jq '.state'
                # Anything else fails closed rather than answering plausibly.
                _args=$(printf '%s ' "$@")
                case "$_args" in
                    *"--json state "*) ;;
                    *) echo "gh stub: unmodelled 'issue view' fields: $_args" >&2; exit 64 ;;
                esac
                echo "OPEN"
                exit 0 ;;
            *) echo "gh stub: unmodelled 'issue $2'" >&2; exit 64 ;;
        esac ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) echo "gh stub: unmodelled 'pr $2'" >&2; exit 64 ;;
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

# Count of R9's ATOMIC class swap for one issue: both flags on one logged line.
# Matching the whole line rather than either flag alone is what makes a split
# into two edits — the half-executed-swap hazard — fail here.
class_swaps() {
    grep -cE "^issue edit $1 .*--remove-label fleet:sonnet .*--add-label fleet:opus" \
        "$EDIT_LOG" 2>/dev/null || true
}
class_comments() { grep -cE "^issue comment $1 " "$COMMENT_LOG" 2>/dev/null || true; }

r9_targets() {  # sorted, space-joined R9 target issue numbers in the report
    python3 - "$REPORT" <<'PY'
import sys, json
r = json.load(open(sys.argv[1]))
print(" ".join(str(t) for t in sorted(
    f["target"] for f in r["findings"] if f["rule"] == "R9")))
PY
}

echo "=== Phase 1: report-only fires R9 on all three lane labels, stays pure ==="
run_reconcile
assert_eq "$(r9_targets)" "800 810 820" \
    "R9 fires on design-blocked/-unblocked/-proposed sonnet rows ONLY (830 opus, 840 unparked, 850 claimed excluded)"

python3 - "$REPORT" <<'PY' && ok "R9 findings carry the escalate_design_class apply payload, applied=False" || bad "R9 apply payload wrong"
import sys, json
r = json.load(open(sys.argv[1]))
assert r["apply"] is False, "report-only must not be apply mode"
want_pr = {800: 900, 810: 910, 820: 920}
for f in (f for f in r["findings"] if f["rule"] == "R9"):
    a = f.get("apply") or {}
    assert a.get("type") == "escalate_design_class", f"apply type: {a}"
    assert a.get("issue") == f["target"], f"apply.issue != target: {a}"
    assert a.get("pr") == want_pr[f["target"]], f"apply.pr wrong: {a}"
    assert a.get("from") == "fleet:sonnet" and a.get("to") == "fleet:opus", a
    assert f["target_kind"] == "issue", f["target_kind"]
    assert f["applied"] is False, "report-only records not-applied (False, not None)"
PY

python3 - "$REPORT" <<'PY' && ok "one R9 finding per issue (stable report keys), never one per parked PR" || bad "duplicate R9 findings for one issue"
import sys, json, collections
r = json.load(open(sys.argv[1]))
c = collections.Counter(f["target"] for f in r["findings"] if f["rule"] == "R9")
assert all(v == 1 for v in c.values()), dict(c)
PY

python3 - "$REPORT" <<'PY' && ok "fleet:blocked row: R2 still flags PR #1150, R7 skips it (#2926 residual stays visible)" || bad "R2/R7 coexistence broken on the fleet:blocked row"
import sys, json
r = json.load(open(sys.argv[1]))
r2 = [f for f in r["findings"] if f["rule"] == "R2" and f["target"] == 1150]
assert r2, "R2 should still flag the claimless wip PR on the blocked issue"
assert all(f.get("apply") is None for f in r2), "R2 must remain flag-only"
assert not [f for f in r["findings"] if f["rule"] == "R7" and f["target"] == 1150], \
    "R7 must skip a fleet:blocked backing issue"
assert not [f for f in r["findings"] if f["rule"] == "R9" and f["target"] == 1100], \
    "R9 has no opinion on an unclassed, un-parked row"
PY

c=$(wc -l <"$EDIT_LOG" | tr -d ' ');    assert_eq "$c" "0" "report-only made no gh issue edit calls"
c=$(wc -l <"$COMMENT_LOG" | tr -d ' '); assert_eq "$c" "0" "report-only made no gh issue comment calls"

echo "=== Phase 2: --apply swaps the class atomically and comments, once per issue ==="
run_reconcile --apply
for n in 800 810 820; do
    assert_eq "$(class_swaps $n)" "1" \
        "#$n: exactly one ATOMIC edit carrying --remove-label fleet:sonnet AND --add-label fleet:opus"
    assert_eq "$(class_comments $n)" "1" "#$n: exactly one explanatory comment"
done
for n in 830 840 850 1100; do
    assert_eq "$(class_swaps $n)" "0" "#$n: no class swap (negative row)"
    assert_eq "$(class_comments $n)" "0" "#$n: no comment (negative row)"
done
assert_eq "$(grep -c 'remove-label fleet:sonnet' "$EDIT_LOG" 2>/dev/null || true)" "3" \
    "exactly three class swaps total — no negative row leaked one"

echo "=== Phase 3: idempotency — an already-opus backing issue stops firing ==="
write_issues fleet:opus            # #800 is now opus; #810/#820 stay sonnet
: > "$EDIT_LOG"; : > "$COMMENT_LOG"
run_reconcile
assert_eq "$(r9_targets)" "810 820" "#800 no longer produces an R9 finding once it reads fleet:opus"
run_reconcile --apply
assert_eq "$(class_swaps 800)" "0" "#800: --apply makes no further edit (idempotent by construction)"
assert_eq "$(class_comments 800)" "0" "#800: --apply posts no further comment"

summarize "reconcile R9 class-escalate tests"
