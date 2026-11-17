#!/usr/bin/env bash
# Cross-lane mutex coverage for the force-pushing worker lanes and reviewers.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

export CLAIM_RACE_LIB CLAIM_RACE_LABEL_LIB CLAIM_RACE_SUITE_PID=$$
CLAIM_RACE_LIB="$(cd "$(dirname "$0")" && pwd)/lib_claim_race.sh"
CLAIM_RACE_LABEL_LIB="$(cd "$(dirname "$0")" && pwd)/lib_claim_race_labels.sh"
source "$CLAIM_RACE_LIB"
source "$CLAIM_RACE_LABEL_LIB"

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
    "$FLEET_RESERVATIONS_DIR" "$CLAIM_STATE" "$TMPROOT/bin"

cat > "$TMPROOT/bin/gh" <<'GH_STUB'
#!/usr/bin/env bash
set -euo pipefail
source "$CLAIM_RACE_LIB"
source "$CLAIM_RACE_LABEL_LIB"

emit_labels() {
    race_labels_emit_json "$CLAIM_STATE"
    printf '\n'
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

# CLAIM_VIEW_FAIL="<claimant>:<n|always>[:partial] ...": the claimant's first
# <n> issue views (every view for `always`) fail, alternating a non-zero exit
# with a truncated document, the two shapes of a transient GitHub read
# failure. With `:partial` they instead succeed with a well-formed document
# that omits a requested field, cycling through each of labels, body and
# state. A claimant outside a race schedule is named `solo`.
view_fails_now() {
    local who="${CLAIMANT:-solo}" spec budget mode count=0 counter
    for spec in ${CLAIM_VIEW_FAIL:-}; do
        [[ "${spec%%:*}" == "$who" ]] || continue
        budget="${spec#*:}"
        mode="${budget#*:}"
        [[ "$mode" == "$budget" ]] && mode=""
        budget="${budget%%:*}"
        counter="${CLAIM_STATE}.view-fails-$who"
        [[ -f "$counter" ]] && count=$(cat "$counter")
        [[ "$budget" == always || "$count" -lt "$budget" ]] || return 1
        printf '%s\n' "$((count + 1))" > "$counter"
        if [[ "$mode" == partial ]]; then
            VIEW_FAIL_SHAPE=$((2 + count % 3))
        else
            VIEW_FAIL_SHAPE=$((count % 2))
        fi
        return 0
    done
    return 1
}

case "${1:-} ${2:-}" in
    "issue view")
        if view_fails_now; then
            case "$VIEW_FAIL_SHAPE" in
                0)
                    echo "gh stub: HTTP 502 on issue view (${CLAIMANT:-solo})" >&2
                    exit 1
                    ;;
                1) printf '{"state":"OPEN","labels":[' ;;
                2) printf '{"state":"OPEN","body":""}\n' ;;
                3) printf '{"state":"OPEN","labels":[]}\n' ;;
                4) printf '{"labels":[],"body":""}\n' ;;
            esac
            exit 0
        fi
        printf '{"state":"OPEN","labels":'
        emit_issue_labels
        printf ',"body":""}\n'
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
        race_labels_remove "$CLAIM_STATE" "$remove"
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
            printf '['
            emit_labels
            printf ']\n'
            exit 0
        fi

        if [[ -d "$CLAIM_RUN" ]]; then
            race_stage post
            : > "$CLAIM_RUN/arrived-$CLAIMANT"
            race_await gate "$CLAIMANT" || exit 1
            race_stage gated
        fi

        race_labels_add "$CLAIM_STATE" "$posted"
        printf '%s\n' "$posted" >> "$CLAIM_POST_LOG"
        emit_labels
        ;;
    "pr view") echo OPEN ;;
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
    race_labels_reset "$CLAIM_STATE"
    : > "$CLAIM_POST_LOG"
    rm -f "$CLAIM_STATE".view-fails-*
    local label
    for label in "$@"; do race_labels_add "$CLAIM_STATE" "$label"; done
}

label_count() {
    race_labels_list "$CLAIM_STATE" | awk 'NF { count++ } END { print count + 0 }'
}

labels_flat() {
    race_labels_list "$CLAIM_STATE"
}

assert_foreign_claim_refused() {
    local held_label="$1" command="$2" pr="$3" agent="$4" lane="$5" rc=0
    set_labels "$held_label"
    "$FLEET_CLAIM" "$command" "$pr" "$agent" >/dev/null 2>&1 || rc=$?
    assert_exit "$rc" 1 "foreign $lane refuses $command"
    assert_eq "$(labels_flat)" "$held_label" \
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
        assert_eq "$(labels_flat)" "$AMEND_LABEL" "$order leaves the amend winner's label"
    elif [[ "$review_rc" == 0 && "$amend_rc" != 0 ]]; then
        assert_eq "$(labels_flat)" "$REVIEW_LABEL" "$order leaves the review winner's label"
    else
        bad "$order final label belongs to the sole successful claimant"
    fi
    race_report_if_failed "$fails_before" "$order"
    rm -rf "$CLAIM_RUN"
}

echo "T7 fix: independent confirmation resolves every barrier order"
echo "    (each claimant's first live-label snapshot fails, as on hosted CI)"
export CLAIM_VIEW_FAIL="amend:1 review:1"
run_race amend-first
run_race review-first
run_race together
unset CLAIM_VIEW_FAIL

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
if [[ -e "$CLAIM_RUN" ]]; then
    bad "the broken schedule leaves rendezvous state behind"
else
    ok "the broken schedule leaves no rendezvous state behind"
fi

view_attempts() {
    cat "$CLAIM_STATE.view-fails-$1" 2>/dev/null || echo 0
}

echo "T9 fix: every lane re-reads an incomplete live snapshot and then claims"
t9_pr=4301
for t9_lane in review-claim:fleet:reviewing-mac-poolA \
        resolving-claim:fleet:resolving-mac-poolA \
        amending-claim:$AMEND_LABEL pr-claim:fleet:claim-mac-poolA; do
    t9_cmd="${t9_lane%%:*}" t9_label="${t9_lane#*:}"
    set_labels
    rc=0
    CLAIM_VIEW_FAIL="solo:2" "$FLEET_CLAIM" "$t9_cmd" "$t9_pr" poolA \
        >"$TMPROOT/t9.out" 2>&1 || rc=$?
    assert_exit "$rc" 0 "$t9_cmd recovers from a failed and a truncated snapshot"
    assert_eq "$(view_attempts solo)" "2" "$t9_cmd consumed both injected failures"
    assert_eq "$(labels_flat)" "$t9_label" "$t9_cmd leaves exactly its own label"
    assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "1" "$t9_cmd POSTs once"
    "$FLEET_CLAIM" "${t9_cmd%-claim}-release" "$t9_pr" poolA >/dev/null 2>&1 || true
    t9_pr=$((t9_pr + 1))
done

echo "T10 fix: a persistent snapshot failure is bounded and fails closed"
rm -rf "$FLEET_AMEND_SNAPSHOTS_DIR" "$FLEET_CLAIMS_DIR"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_AMEND_SNAPSHOTS_DIR"
t10_pr=4401
for t10_cmd in review-claim resolving-claim amending-claim pr-claim; do
    set_labels
    rc=0
    CLAIM_VIEW_FAIL="solo:always" "$FLEET_CLAIM" "$t10_cmd" "$t10_pr" poolA \
        >"$TMPROOT/t10.out" 2>&1 || rc=$?
    assert_exit "$rc" 1 "$t10_cmd refuses an unreadable target"
    assert_eq "$(view_attempts solo)" "$FLEET_CLAIM_ACQUIRE_RETRIES" \
        "$t10_cmd stops after the bounded attempt budget"
    assert_contains "$(cat "$TMPROOT/t10.out")" \
        "refused on PR#${t10_pr}: live labels unavailable after ${FLEET_CLAIM_ACQUIRE_RETRIES} incomplete fetch attempt(s)" \
        "$t10_cmd names the target and the incomplete fetch"
    assert_contains "$(cat "$TMPROOT/t10.out")" "HTTP 502 on issue view (solo)" \
        "$t10_cmd surfaces the last fetch's own error"
    assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "$t10_cmd sends no POST"
    assert_eq "$(label_count)" "0" "$t10_cmd leaves no label"
    t10_pr=$((t10_pr + 1))
done
t10_leftovers=$(cd "$TMPROOT" && find claims amend-snapshots -mindepth 1 | tr '\n' ' ')
assert_eq "$t10_leftovers" "" "refused claims leave no liveness marker, lock, or amend record"

echo "T11 fix: a racing lane that cannot read the target never reaches its POST"
export CLAIM_VIEW_FAIL="amend:always"
race_schedule together
unset CLAIM_VIEW_FAIL
assert_contains "$RACE_BROKEN" "amend exited with rc 1 before: test -e $CLAIM_RUN/arrived-amend" \
    "the schedule names the unreadable claimant"
assert_eq "$(race_rc amend)" 1 "the unreadable claimant fails closed"
assert_contains "$(cat "$CLAIM_RUN/amend.out")" "live labels unavailable after" \
    "the unreadable claimant reports the incomplete fetch"
assert_eq "$(race_rc review)" 1 "the gated peer is released, not left waiting"
assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "neither lane POSTs"
assert_eq "$(label_count)" "0" "no lane label survives"
assert_eq "$(race_live_count)" 0 "no claimant outlives the schedule"
rm -rf "$CLAIM_RUN"
if [[ -e "$CLAIM_RUN" ]]; then
    bad "the unreadable schedule leaves rendezvous state behind"
else
    ok "the unreadable schedule leaves no rendezvous state behind"
fi

echo "T12 fix: a well-formed snapshot missing a requested field is incomplete"
t12_pr=4501
for t12_lane in review-claim:fleet:reviewing-mac-poolA \
        resolving-claim:fleet:resolving-mac-poolA \
        amending-claim:$AMEND_LABEL pr-claim:fleet:claim-mac-poolA; do
    t12_cmd="${t12_lane%%:*}" t12_label="${t12_lane#*:}"
    set_labels "fleet:reviewing-mac-poolZ"
    rc=0
    CLAIM_VIEW_FAIL="solo:always:partial" "$FLEET_CLAIM" "$t12_cmd" "$t12_pr" poolA \
        >"$TMPROOT/t12.out" 2>&1 || rc=$?
    assert_exit "$rc" 1 "$t12_cmd refuses a snapshot that omits labels, body or state"
    assert_eq "$(view_attempts solo)" "$FLEET_CLAIM_ACQUIRE_RETRIES" \
        "$t12_cmd re-reads a partial snapshot within the bounded budget"
    assert_contains "$(cat "$TMPROOT/t12.out")" \
        "refused on PR#${t12_pr}: live labels unavailable after ${FLEET_CLAIM_ACQUIRE_RETRIES} incomplete fetch attempt(s)" \
        "$t12_cmd names the partial snapshot as an incomplete fetch"
    assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "$t12_cmd sends no POST"
    assert_eq "$(labels_flat)" "fleet:reviewing-mac-poolZ" \
        "$t12_cmd leaves the hidden foreign label as the only label"
    t12_pr=$((t12_pr + 1))

    set_labels
    rc=0
    CLAIM_VIEW_FAIL="solo:2:partial" "$FLEET_CLAIM" "$t12_cmd" "$t12_pr" poolA \
        >"$TMPROOT/t12.out" 2>&1 || rc=$?
    assert_exit "$rc" 0 "$t12_cmd recovers once a complete snapshot arrives"
    assert_eq "$(labels_flat)" "$t12_label" "$t12_cmd then claims its own label"
    "$FLEET_CLAIM" "${t12_cmd%-claim}-release" "$t12_pr" poolA >/dev/null 2>&1 || true
    t12_pr=$((t12_pr + 1))
done

summarize "fleet-claim cross-lane mutex"
