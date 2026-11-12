#!/usr/bin/env bash
# Cross-lane mutex coverage for the force-pushing worker lanes and reviewers.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

export CLAIM_RACE_LIB CLAIM_RACE_SUITE_PID=$$
CLAIM_RACE_LIB="$(cd "$(dirname "$0")" && pwd)/lib_claim_race.sh"
source "$CLAIM_RACE_LIB"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-cross-lane.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
trap 'rm -rf "$TMPROOT"' EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_AMEND_SNAPSHOTS_DIR="$TMPROOT/amend-snapshots"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_TEST_HOST=mac
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=3
export CLAIM_STATE="$TMPROOT/labels"
export CLAIM_POST_LOG="$TMPROOT/posts"
export CLAIM_RUN="$TMPROOT/run"
export AMEND_LABEL="fleet:amending-mac-poolA"
export REVIEW_LABEL="fleet:reviewing-mac-poolB"
export RESOLVE_LABEL="fleet:resolving-mac-poolB"
mkdir -p "$FLEET_HEARTBEATS_DIR" "$FLEET_CLAIMS_DIR" \
    "$FLEET_RESERVATIONS_DIR" "$TMPROOT/bin"

cat > "$TMPROOT/bin/gh" <<'GH_STUB'
#!/usr/bin/env bash
set -euo pipefail
source "$CLAIM_RACE_LIB"

lock_state() {
    race_lock "${CLAIM_STATE}.lock"
}

unlock_state() {
    rmdir "${CLAIM_STATE}.lock"
}

emit_labels() {
    local first=1 label
    printf '['
    while IFS= read -r label; do
        [[ -n "$label" ]] || continue
        [[ "$first" -eq 1 ]] || printf ','
        printf '{"name":"%s"}' "$label"
        first=0
    done < "$CLAIM_STATE"
    printf ']\n'
}

emit_issue_labels() {
    local stored
    stored=$(emit_labels)
    if [[ "$stored" == "[]" ]]; then
        printf '[{"name":"fleet:needs-fix"}]\n'
    else
        printf '[{"name":"fleet:needs-fix"},%s\n' "${stored#\[}"
    fi
}

case "${1:-} ${2:-}" in
    "issue view")
        lock_state
        printf '{"state":"OPEN","labels":'
        emit_issue_labels
        printf ',"body":""}\n'
        unlock_state
        ;;
    "issue edit")
        remove=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == "--remove-label" ]]; then
                shift
                remove="${1:-}"
            fi
            shift || true
        done
        lock_state
        : > "${CLAIM_STATE}.next"
        while IFS= read -r label; do
            [[ "$label" == "$remove" ]] || printf '%s\n' "$label" >> "${CLAIM_STATE}.next"
        done < "$CLAIM_STATE"
        mv "${CLAIM_STATE}.next" "$CLAIM_STATE"
        unlock_state
        ;;
    "api "*)
        posted=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                labels\[\]=*) posted="${1#labels[]=}" ;;
            esac
            shift || true
        done
        if [[ -z "$posted" ]]; then
            lock_state
            printf '['
            emit_labels
            printf ']\n'
            unlock_state
            exit 0
        fi

        if [[ -d "$CLAIM_RUN" ]]; then
            race_stage post
            : > "$CLAIM_RUN/arrived-$CLAIMANT"
            race_await gate "$CLAIMANT" || exit 1
            race_stage gated
        fi

        lock_state
        present=0
        while IFS= read -r label; do
            [[ "$label" == "$posted" ]] && present=1
        done < "$CLAIM_STATE"
        [[ "$present" -eq 1 ]] || printf '%s\n' "$posted" >> "$CLAIM_STATE"
        printf '%s\n' "$posted" >> "$CLAIM_POST_LOG"
        emit_labels
        unlock_state
        ;;
    "label "*|"pr "*) ;;
esac
GH_STUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

assert_exit() {
    local actual="$1" expected="$2" message="$3"
    if [[ "$actual" -eq "$expected" ]]; then
        ok "$message"
    else
        bad "$message (expected $expected, got $actual)"
    fi
}

set_labels() {
    : > "$CLAIM_STATE"
    : > "$CLAIM_POST_LOG"
    local label
    for label in "$@"; do printf '%s\n' "$label" >> "$CLAIM_STATE"; done
}

label_count() {
    local count=0 label
    while IFS= read -r label; do [[ -n "$label" ]] && count=$((count + 1)); done < "$CLAIM_STATE"
    printf '%s\n' "$count"
}

assert_foreign_claim_refused() {
    local held_label="$1" command="$2" pr="$3" agent="$4" lane="$5" rc=0
    set_labels "$held_label"
    "$FLEET_CLAIM" "$command" "$pr" "$agent" >/dev/null 2>&1 || rc=$?
    assert_exit "$rc" 1 "foreign $lane refuses $command"
    assert_eq "$(cat "$CLAIM_STATE")" "$held_label" \
        "$command refusal preserves the exact label set"
    assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" \
        "$command refusal sends no POST"
}

echo "T1 fix: review claim refuses a foreign amend without POSTing"
assert_foreign_claim_refused "$AMEND_LABEL" review-claim 4101 poolB amend

echo "T2 control: same suffix may hold both lane labels"
set_labels "$AMEND_LABEL"
rc=0
"$FLEET_CLAIM" review-claim 4102 poolA >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "same-agent excluded lane passes"
assert_eq "$(label_count)" "2" "same-agent lane labels may coexist"

echo "T3 fix: incumbent amend re-acquire is a no-POST success"
set_labels "$AMEND_LABEL" "$REVIEW_LABEL"
rc=0
"$FLEET_CLAIM" amending-claim 4103 poolA >"$TMPROOT/t3.out" 2>&1 || rc=$?
assert_exit "$rc" 0 "incumbent amend keeps the item"
assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "incumbent re-acquire sends no POST"
assert_contains "$(cat "$TMPROOT/t3.out")" "WARN" "damaged coexistence state is visible"
if [[ -f "$FLEET_HEARTBEATS_DIR/poolA" ]]; then
    ok "incumbent amend refreshes its liveness marker"
else
    bad "incumbent amend did not refresh its liveness marker"
fi

echo "T4 control: non-incumbent amend remains refused"
set_labels "$AMEND_LABEL" "$REVIEW_LABEL"
rc=0
"$FLEET_CLAIM" amending-claim 4104 poolC >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "foreign review refuses non-incumbent amend"
assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "non-incumbent refusal sends no POST"

echo "T4b fix: incumbent review re-acquire is also a no-POST success"
set_labels "$AMEND_LABEL" "$REVIEW_LABEL"
rc=0
"$FLEET_CLAIM" review-claim 4105 poolB >"$TMPROOT/t4b.out" 2>&1 || rc=$?
assert_exit "$rc" 0 "incumbent review keeps the item"
assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "review re-acquire sends no POST"
assert_contains "$(cat "$TMPROOT/t4b.out")" "WARN" "review coexistence state is visible"
assert_eq "$(cat "$FLEET_CLAIMS_DIR/_prlabel-reviewing-poolB")" "4105" "review liveness marker is refreshed"

echo "T4c fix: resolving claim refuses a foreign amend without POSTing"
assert_foreign_claim_refused "$AMEND_LABEL" resolving-claim 4106 poolB amend

echo "T4d fix: amending claim refuses a foreign resolver without POSTing"
assert_foreign_claim_refused "$RESOLVE_LABEL" amending-claim 4107 poolA resolver

echo "T5 fix: pure contender filter covers the symmetric lane union"
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"
if declare -F _claim_contenders >/dev/null; then
    contenders=$(_claim_contenders "$AMEND_LABEL" "fleet:amending-" \
        "$AMEND_LABEL" "fleet:amending-mac-poolC" "$REVIEW_LABEL" \
        "fleet:reviewing-mac-poolA" "fleet:resolving-mac-poolD")
    assert_contains "$contenders" "$AMEND_LABEL" "own-prefix label participates"
    assert_contains "$contenders" "fleet:amending-mac-poolC" "same-prefix peer participates"
    assert_contains "$contenders" "$REVIEW_LABEL" "foreign-suffix excluded lane participates"
    assert_absent "$contenders" "fleet:reviewing-mac-poolA" "same-suffix excluded lane is carved out"
    assert_contains "$contenders" "fleet:resolving-mac-poolD" "amender sees foreign resolver as a contender"

    resolving_contenders=$(_claim_contenders "fleet:resolving-mac-poolA" \
        "fleet:resolving-" "fleet:resolving-mac-poolA" \
        "$REVIEW_LABEL" "fleet:reviewing-mac-poolA" \
        "fleet:amending-mac-poolC")
    assert_contains "$resolving_contenders" "$REVIEW_LABEL" "resolver sees foreign reviewer as a contender"
    assert_absent "$resolving_contenders" "fleet:reviewing-mac-poolA" "resolver carves out its own reviewer suffix"
    assert_contains "$resolving_contenders" "fleet:amending-mac-poolC" "resolver sees foreign amender as a contender"
else
    bad "own-prefix label participates (contender filter missing)"
    bad "same-prefix peer participates (contender filter missing)"
    bad "foreign-suffix excluded lane participates (contender filter missing)"
    bad "same-suffix excluded lane is carved out (contender filter missing)"
    bad "amender sees foreign resolver as a contender (contender filter missing)"
    bad "resolver sees foreign reviewer as a contender (contender filter missing)"
    bad "resolver carves out its own reviewer suffix (contender filter missing)"
    bad "resolver sees foreign amender as a contender (contender filter missing)"
fi

echo "T6 fix: exclusion table is symmetric"
claim_exclusions_symmetric() {
    local prefix excluded reverse candidate found
    for prefix in "${!FLEET_CLAIM_EXCLUDES[@]}"; do
        for excluded in ${FLEET_CLAIM_EXCLUDES[$prefix]}; do
            reverse="${FLEET_CLAIM_EXCLUDES[$excluded]:-}"
            found=0
            for candidate in $reverse; do
                [[ "$candidate" == "$prefix" ]] && found=1
            done
            [[ "$found" -eq 1 ]] || return 1
        done
    done
    return 0
}
if ! declare -p FLEET_CLAIM_EXCLUDES >/dev/null 2>&1; then
    bad "shipped lane-exclusion table is symmetric (table missing)"
    bad "deliberately asymmetric table is rejected (table missing)"
else
    if claim_exclusions_symmetric; then
        ok "shipped lane-exclusion table is symmetric"
    else
        bad "shipped lane-exclusion table is asymmetric"
    fi
    saved_review_excludes="${FLEET_CLAIM_EXCLUDES[fleet:reviewing-]}"
    FLEET_CLAIM_EXCLUDES[fleet:reviewing-]=""
    if claim_exclusions_symmetric; then
        bad "deliberately asymmetric table was accepted"
    else
        ok "deliberately asymmetric table is rejected"
    fi
    FLEET_CLAIM_EXCLUDES[fleet:reviewing-]="$saved_review_excludes"
fi

# lane_claim_as <subcommand> <agent>: one lane's claim, run under race_launch.
lane_claim_as() {
    "$FLEET_CLAIM" "$1" 4201 "$2"
}

# race_schedule <amend-first|review-first|together>
#   Launch both lanes, hold each at its POST gate until both arrive, then open
#   the gates in <order>. A broken rendezvous is left in RACE_BROKEN.
race_schedule() {
    local order="$1" first="" second=""
    set_labels
    race_reset
    race_launch amend lane_claim_as amending-claim poolA
    race_launch review lane_claim_as review-claim poolB
    case "$order" in
        amend-first) first=amend second=review ;;
        review-first) first=review second=amend ;;
    esac
    if race_wait_for amend test -e "$CLAIM_RUN/arrived-amend" \
            && race_wait_for review test -e "$CLAIM_RUN/arrived-review"; then
        if [[ -z "$first" ]]; then
            : > "$CLAIM_RUN/gate-amend"
            : > "$CLAIM_RUN/gate-review"
        else
            : > "$CLAIM_RUN/gate-$first"
            race_wait_for "$first" test -s "$CLAIM_POST_LOG" \
                && : > "$CLAIM_RUN/gate-$second"
        fi
    fi
    [[ -z "$RACE_BROKEN" ]] || race_abort
    race_finish
}

run_race() {
    local order="$1" fails_before="$FAIL"
    race_schedule "$order"
    assert_eq "$RACE_BROKEN" "" "$order rendezvous ends on claimant events"
    amend_rc=$(race_rc amend)
    review_rc=$(race_rc review)
    successes=0
    [[ "$amend_rc" == 0 ]] && successes=$((successes + 1))
    [[ "$review_rc" == 0 ]] && successes=$((successes + 1))
    assert_eq "$successes" "1" "$order leaves exactly one successful claimant"
    assert_eq "$(label_count)" "1" "$order leaves exactly one lane label"
    if [[ "$amend_rc" == 0 && "$review_rc" != 0 ]]; then
        assert_eq "$(cat "$CLAIM_STATE")" "$AMEND_LABEL" "$order leaves the amend winner's label"
    elif [[ "$review_rc" == 0 && "$amend_rc" != 0 ]]; then
        assert_eq "$(cat "$CLAIM_STATE")" "$REVIEW_LABEL" "$order leaves the review winner's label"
    else
        bad "$order final label belongs to the sole successful claimant"
    fi
    race_report_if_failed "$fails_before" "$order"
    rm -rf "$CLAIM_RUN"
}

echo "T7 fix: independent confirmation resolves every barrier order"
run_race amend-first
run_race review-first
run_race together

echo "T8: a lane that exits before the POST barrier breaks it with a diagnostic"
saved_race_exit="${CLAIM_RACE_EXIT:-}"
export CLAIM_RACE_EXIT="review:post"
race_schedule together
export CLAIM_RACE_EXIT="$saved_race_exit"
assert_contains "$RACE_BROKEN" "review exited with rc 1 before: test -e $CLAIM_RUN/arrived-review" \
    "the broken barrier names the exited claimant and the event it never reached"
assert_eq "$(race_rc review)" 1 "the forced claimant exits non-zero"
assert_eq "$(race_rc amend)" 1 "the gated peer is released instead of waiting"
assert_contains "$(cat "$CLAIM_RUN/amend.out")" "'gate' barrier aborted: the suite abandoned the schedule" \
    "the gated peer reports why its gate never opened"
report=$(race_report)
assert_contains "$report" "claimant review: rc=1 markers=[]" "the report carries the exited claimant's rc and progress"
assert_contains "$report" "| gh stub: review forced to exit at post" "the report carries the exited claimant's output"
assert_contains "$report" "claimant amend: rc=1 markers=[arrived-amend]" "the report carries the peer's rc and progress"
assert_eq "$(race_live_count)" 0 "no claimant outlives the broken schedule"
rm -rf "$CLAIM_RUN"
if [[ -e "$CLAIM_RUN" || -e "${CLAIM_STATE}.lock" ]]; then
    bad "the broken schedule leaves rendezvous state behind"
else
    ok "the broken schedule leaves no rendezvous state behind"
fi

summarize "fleet-claim cross-lane mutex"
