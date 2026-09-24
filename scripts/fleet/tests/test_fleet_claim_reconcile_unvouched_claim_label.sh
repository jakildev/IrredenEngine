#!/usr/bin/env bash
# Tests for reconcile's discounted claim-label reading on a wip PR.
#
# The state: a pane claimed an issue, opened a fleet:wip PR, and died. Its FS
# claim and reservation are gone, but the issue keeps `fleet:claim-<host>-<agent>`
# + `fleet:in-progress` — `release` and `cleanup --gh` both retain the label
# while the PR is open. Read strictly, that label alone keeps R7 from
# re-arming fleet:design-unblocked and R2 from flagging the PR, and every lane
# reads it as a live owner.
#
# R7 and R2's WIP kind discount a claim label that names THIS host with a
# known agent when nothing host-local vouches for it (FS claim, reservation,
# live task/stack dispatch record). Covers: the positive fire (report + heal at
# the persistence threshold), one control per vouching surface, the foreign-host
# and `unknown`-agent keeps, a mixed own/foreign label pair, and the scope
# control that R2's feedback kind keeps the strict reading.
#
# `gh` is stubbed so the issue/PR surfaces are canned JSON; each arm resets the
# host-local surfaces and the reconcile state dir, so arms are independent.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_AMEND_SNAPSHOTS_DIR="$TMPROOT/amend-snapshots"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_STALE_SECS=1800
export FLEET_RECONCILE_DRIFT_TICKS=3

REPORT="$FLEET_STATE_DIR/drift-report.json"
NOW=$(date +%s)

export ISSUES_JSON="$TMPROOT/issues.json"
export PRS_JSON="$TMPROOT/prs.json"
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"
export TRACKER_STATE="$TMPROOT/tracker.exists"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            list)
                # R2's state-drift tracker lookup vs the queued-issue fetch.
                if printf '%s ' "$@" | grep -q 'fleet:state-drift'; then
                    [[ -f "$TRACKER_STATE" ]] && echo "9001" || true
                else
                    cat "$ISSUES_JSON"
                fi
                exit 0 ;;
            create)
                touch "$TRACKER_STATE"
                echo "https://github.com/jakildev/IrredenEngine/issues/9001"
                exit 0 ;;
            edit)
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            view)
                # No arm models an R8 park; an issue view here is a call this
                # suite does not emulate and must fail, not answer plausibly.
                echo "gh stub: unmodelled 'issue view': $*" >&2
                exit 64 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in
            list) cat "$PRS_JSON"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    repo)  exit 1 ;;   # game repo "not reachable" → engine-only scan
    *)     exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

run_reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo jakildev/IrredenEngine >/dev/null 2>&1; }

# Fresh host-local surfaces + reconcile state for one arm. Issue 800 carries
# the given claim labels on top of fleet:queued + fleet:in-progress; PR 850
# (claude/800-*) carries the given PR labels.
reset_arm() {
    local claim_labels="$1" pr_labels="$2"
    rm -rf "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR" \
        "$FLEET_ORPHANS_DIR" "$FLEET_AMEND_SNAPSHOTS_DIR" "$TRACKER_STATE"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR/dispatch"
    : > "$EDIT_LOG"
    python3 - "$ISSUES_JSON" "$PRS_JSON" "$claim_labels" "$pr_labels" <<'PY'
import sys, json
issues_path, prs_path, claim_labels, pr_labels = sys.argv[1:5]
labs = ["fleet:queued", "fleet:in-progress"] + claim_labels.split()
json.dump([{"number": 800, "state": "OPEN",
            "labels": [{"name": l} for l in labs]}], open(issues_path, "w"))
json.dump([{"number": 850, "headRefName": "claude/800-dead-pane-wip",
            "body": "Closes #800",
            "labels": [{"name": l} for l in pr_labels.split()]}], open(prs_path, "w"))
PY
}

mk_claim() {
    local slug="$1" owner="$2"
    mkdir -p "$FLEET_CLAIMS_DIR/$slug"
    echo "$owner" > "$FLEET_CLAIMS_DIR/$slug/owner"
    echo "$slug"  > "$FLEET_CLAIMS_DIR/$slug/title"
    echo "$NOW"   > "$FLEET_CLAIMS_DIR/$slug/created"
}
mk_reservation() {
    printf '{"task_id":"%s","branch":"claude/%s-dead-pane-wip","created_at":"x","created_epoch":%s}\n' \
        "$2" "$2" "$NOW" > "$FLEET_RESERVATIONS_DIR/$1.json"
}
mk_dispatch() {
    printf '{"role":"worker","pane":"%%9","class":"opus","target":"%s","agent":"%s"}\n' \
        "$2" "$1" > "$FLEET_STATE_DIR/dispatch/$1.json"
}

# Rules (R2/R7) the latest report carries for PR 850, space-separated, sorted.
rules_for_850() {
    python3 - "$REPORT" <<'PY'
import sys, json
r = json.load(open(sys.argv[1]))
print(" ".join(sorted(set(f["rule"] for f in r["findings"]
                          if f["target"] == 850 and f["rule"] in ("R2", "R7")))))
PY
}
du_adds() { grep -c '^issue edit 850 .*add-label fleet:design-unblocked' "$EDIT_LOG" 2>/dev/null || true; }
apply_ticks() { local i; for i in 1 2 3; do run_reconcile --apply; done; }

# A vouched / not-discountable arm: no R2/R7 for the PR in the report, and no
# heal across a full persistence window of apply ticks.
assert_quiet() {
    local what="$1" rules c
    run_reconcile
    rules=$(rules_for_850)
    [[ -z "$rules" ]] && ok "$what: no R2/R7 finding for PR #850" \
        || bad "$what: expected no R2/R7 for PR #850, got [$rules]"
    apply_ticks
    c=$(du_adds)
    [[ "$c" == "0" ]] && ok "$what: no design-unblocked heal across $FLEET_RECONCILE_DRIFT_TICKS apply ticks" \
        || bad "$what: healed PR #850 (adds=$c)"
}

echo "=== Positive fire: dead own-host label, nothing vouches ==="
reset_arm "fleet:claim-mac-pool-9" "fleet:wip"
run_reconcile
rules=$(rules_for_850)
[[ "$rules" == "R2 R7" ]] && ok "report carries R2 AND R7 for PR #850" \
    || bad "expected [R2 R7] for PR #850, got [$rules]"
run_reconcile --apply
run_reconcile --apply
c=$(du_adds); [[ "$c" == "0" ]] && ok "below the persistence threshold → no heal yet" \
    || bad "healed before the threshold (adds=$c)"
run_reconcile --apply
c=$(du_adds); [[ "$c" == "1" ]] && ok "threshold tick re-adds fleet:design-unblocked on PR #850" \
    || bad "no heal at threshold (adds=$c, want 1)"

echo "=== Control (a): an FS claim for the issue vouches ==="
reset_arm "fleet:claim-mac-pool-9" "fleet:wip"
mk_claim 800 pool-9
assert_quiet "FS claim"

echo "=== Control (b): a reservation for the issue vouches ==="
reset_arm "fleet:claim-mac-pool-9" "fleet:wip"
mk_reservation pool-9 800
assert_quiet "reservation"

echo "=== Control (c): a live dispatch record for the issue vouches ==="
reset_arm "fleet:claim-mac-pool-9" "fleet:wip"
mk_dispatch pool-9 "task:engine:800"
assert_quiet "dispatch record"

echo "=== Control (d): a foreign-host label is never discounted ==="
reset_arm "fleet:claim-linux-pool-9" "fleet:wip"
assert_quiet "foreign-host label"

echo "=== Control (e): an 'unknown' agent label is never discounted ==="
reset_arm "fleet:claim-mac-unknown" "fleet:wip"
assert_quiet "unknown-agent label"

echo "=== Control (f): one foreign label keeps an own-host dead label counted ==="
reset_arm "fleet:claim-mac-pool-9 fleet:claim-linux-pool-3" "fleet:wip"
assert_quiet "own-host + foreign-host labels"

echo "=== Scope: a non-wip feedback PR keeps the strict reading (no R2) ==="
# Non-vacuity: with no claim label at all, R2 does flag this feedback PR, so
# the quiet below is the strict reading and not a PR R2 never looks at.
reset_arm "" "fleet:needs-fix"
run_reconcile
rules=$(rules_for_850)
[[ "$rules" == "R2" ]] && ok "label-less feedback PR is flagged by R2 (fixture reaches the rule)" \
    || bad "expected [R2] for a label-less feedback PR, got [$rules]"
reset_arm "fleet:claim-mac-pool-9" "fleet:needs-fix"
assert_quiet "feedback-kind PR"

summarize "reconcile unvouched claim-label tests"
