#!/usr/bin/env bash
# Cross-lane mutex coverage for the force-pushing worker lanes and reviewers.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-cross-lane.XXXXXX")
trap 'rm -rf "$TMPROOT"' EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_TEST_HOST=mac
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=3
export CLAIM_STATE="$TMPROOT/labels"
export CLAIM_POST_LOG="$TMPROOT/posts"
export CLAIM_RUN="$TMPROOT/run"
export AMEND_LABEL="fleet:amending-mac-poolA"
export REVIEW_LABEL="fleet:reviewing-mac-poolB"
mkdir -p "$FLEET_HEARTBEATS_DIR" "$FLEET_CLAIMS_DIR" \
    "$FLEET_RESERVATIONS_DIR" "$TMPROOT/bin"

cat > "$TMPROOT/bin/gh" <<'GH_STUB'
#!/usr/bin/env bash
set -euo pipefail

lock_state() {
    local tick
    for ((tick = 0; tick < 500; tick++)); do
        mkdir "${CLAIM_STATE}.lock" 2>/dev/null && return 0
        sleep 0.01
    done
    echo "gh stub: timed out acquiring state lock" >&2
    return 1
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

case "${1:-} ${2:-}" in
    "issue view")
        lock_state
        printf '{"state":"OPEN","labels":'
        emit_labels
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
            tag=review
            [[ "$posted" == fleet:amending-* ]] && tag=amend
            : > "$CLAIM_RUN/arrived-$tag"
            for ((tick = 0; tick < 500; tick++)); do
                [[ -f "$CLAIM_RUN/gate-$tag" ]] && break
                sleep 0.01
            done
            if [[ ! -f "$CLAIM_RUN/gate-$tag" ]]; then
                echo "gh stub: timed out waiting for $tag gate" >&2
                exit 1
            fi
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

wait_for_file() {
    local mode="$1" path="$2" tick
    for ((tick = 0; tick < 500; tick++)); do
        if [[ "$mode" == exists && -e "$path" ]] \
                || [[ "$mode" == nonempty && -s "$path" ]]; then
            return 0
        fi
        sleep 0.01
    done
    return 1
}

echo "T1 fix: review claim refuses a foreign amend without POSTing"
set_labels "$AMEND_LABEL"
rc=0
"$FLEET_CLAIM" review-claim 4101 poolB >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "foreign amend refuses review claim"
assert_eq "$(cat "$CLAIM_STATE")" "$AMEND_LABEL" "refusal preserves the exact label set"
assert_eq "$(wc -l < "$CLAIM_POST_LOG" | tr -d ' ')" "0" "refusal sends no POST"

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
    assert_absent "$contenders" "fleet:resolving-mac-poolD" "unrelated lane is excluded"

    resolving_contenders=$(_claim_contenders "fleet:resolving-mac-poolA" \
        "fleet:resolving-" "fleet:resolving-mac-poolA" \
        "$REVIEW_LABEL" "fleet:reviewing-mac-poolA" "$AMEND_LABEL")
    assert_contains "$resolving_contenders" "$REVIEW_LABEL" "resolver sees foreign reviewer as a contender"
    assert_absent "$resolving_contenders" "fleet:reviewing-mac-poolA" "resolver carves out its own reviewer suffix"
    assert_absent "$resolving_contenders" "$AMEND_LABEL" "resolver does not absorb the independent amend lane"
else
    bad "own-prefix label participates (contender filter missing)"
    bad "same-prefix peer participates (contender filter missing)"
    bad "foreign-suffix excluded lane participates (contender filter missing)"
    bad "same-suffix excluded lane is carved out (contender filter missing)"
    bad "unrelated lane is excluded (contender filter missing)"
    bad "resolver sees foreign reviewer as a contender (contender filter missing)"
    bad "resolver carves out its own reviewer suffix (contender filter missing)"
    bad "resolver does not absorb the independent amend lane (contender filter missing)"
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

run_race() {
    local order="$1"
    set_labels
    rm -rf "$CLAIM_RUN"
    mkdir -p "$CLAIM_RUN"
    amend_rc_file="$CLAIM_RUN/amend.rc"
    review_rc_file="$CLAIM_RUN/review.rc"

    (rc=0; "$FLEET_CLAIM" amending-claim 4201 poolA >/dev/null 2>&1 || rc=$?; printf '%s\n' "$rc" > "$amend_rc_file") &
    amend_pid=$!
    (rc=0; "$FLEET_CLAIM" review-claim 4201 poolB >/dev/null 2>&1 || rc=$?; printf '%s\n' "$rc" > "$review_rc_file") &
    review_pid=$!

    if ! wait_for_file exists "$CLAIM_RUN/arrived-amend" \
            || ! wait_for_file exists "$CLAIM_RUN/arrived-review"; then
        kill "$amend_pid" "$review_pid" 2>/dev/null || true
        wait "$amend_pid" 2>/dev/null || true
        wait "$review_pid" 2>/dev/null || true
        bad "$order claimants reached the POST barrier"
        bad "$order leaves exactly one lane label"
        rm -rf "$CLAIM_RUN"
        return 0
    fi
    case "$order" in
        amend-first)
            : > "$CLAIM_RUN/gate-amend"
            if ! wait_for_file nonempty "$CLAIM_POST_LOG"; then
                kill "$amend_pid" "$review_pid" 2>/dev/null || true
                wait "$amend_pid" 2>/dev/null || true
                wait "$review_pid" 2>/dev/null || true
                bad "$order first POST completed"
                bad "$order leaves exactly one lane label"
                rm -rf "$CLAIM_RUN"
                return 0
            fi
            : > "$CLAIM_RUN/gate-review"
            ;;
        review-first)
            : > "$CLAIM_RUN/gate-review"
            if ! wait_for_file nonempty "$CLAIM_POST_LOG"; then
                kill "$amend_pid" "$review_pid" 2>/dev/null || true
                wait "$amend_pid" 2>/dev/null || true
                wait "$review_pid" 2>/dev/null || true
                bad "$order first POST completed"
                bad "$order leaves exactly one lane label"
                rm -rf "$CLAIM_RUN"
                return 0
            fi
            : > "$CLAIM_RUN/gate-amend"
            ;;
        together)
            : > "$CLAIM_RUN/gate-amend"
            : > "$CLAIM_RUN/gate-review"
            ;;
    esac
    wait "$amend_pid" || true
    wait "$review_pid" || true

    amend_rc=$(cat "$amend_rc_file")
    review_rc=$(cat "$review_rc_file")
    successes=0
    [[ "$amend_rc" -eq 0 ]] && successes=$((successes + 1))
    [[ "$review_rc" -eq 0 ]] && successes=$((successes + 1))
    assert_eq "$successes" "1" "$order leaves exactly one successful claimant"
    assert_eq "$(label_count)" "1" "$order leaves exactly one lane label"
    if [[ "$amend_rc" -eq 0 && "$review_rc" -ne 0 ]]; then
        assert_eq "$(cat "$CLAIM_STATE")" "$AMEND_LABEL" "$order leaves the amend winner's label"
    elif [[ "$review_rc" -eq 0 && "$amend_rc" -ne 0 ]]; then
        assert_eq "$(cat "$CLAIM_STATE")" "$REVIEW_LABEL" "$order leaves the review winner's label"
    else
        bad "$order final label belongs to the sole successful claimant"
    fi
    rm -rf "$CLAIM_RUN"
}

echo "T7 fix: independent confirmation resolves every barrier order"
run_race amend-first
run_race review-first
run_race together

summarize "fleet-claim cross-lane mutex"
