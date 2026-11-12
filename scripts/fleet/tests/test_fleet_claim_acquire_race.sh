#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

export CLAIM_RACE_LIB CLAIM_RACE_SUITE_PID=$$
CLAIM_RACE_LIB="$(cd "$(dirname "$0")" && pwd)/lib_claim_race.sh"
source "$CLAIM_RACE_LIB"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-claim-race.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
trap 'rm -rf "$TMPROOT"' EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export CLAIM_STATE="$TMPROOT/labels"
export CLAIM_RUN="$TMPROOT/run"
export CLAIM_LOG="$TMPROOT/calls"
export CLAIM_MODE=race
export CLAIM_PEERS="alpha beta"
export CLAIM_RETURN_ORDER=together
export FLEET_CLAIM_ACQUIRE_RETRIES=3
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" \
    "$FLEET_RESERVATIONS_DIR" "$FLEET_ORPHANS_DIR" "$TMPROOT/bin"

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

emit_flat() {
    local first=1 label
    printf '['
    while IFS= read -r label; do
        [[ -n "$label" ]] || continue
        [[ "$first" -eq 1 ]] || printf ','
        printf '{"name":"%s"}' "$label"
        first=0
    done < "$CLAIM_STATE"
    printf ']'
}

case "${1:-} ${2:-}" in
    "issue view")
        if [[ "$CLAIM_MODE" == race ]]; then
            race_stage pre
            : > "$CLAIM_RUN/pre-${CLAIMANT}"
            race_await pre $CLAIM_PEERS || exit 1
            printf '{"state":"OPEN","labels":[],"body":""}\n'
        else
            printf '{"state":"OPEN","labels":[],"body":""}\n'
        fi
        ;;
    "issue edit")
        remove=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == --remove-label ]]; then
                shift
                remove="${1:-}"
            fi
            shift || true
        done
        printf 'REMOVE %s %s\n' "$CLAIMANT" "$remove" >> "$CLAIM_LOG"
        [[ "$CLAIM_MODE" == cleanup-fail ]] && exit 1
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
        for arg in "$@"; do
            case "$arg" in
                labels\[\]=*) posted="${arg#labels[]=}" ;;
            esac
        done
        if [[ -n "$posted" ]]; then
            [[ "$CLAIM_MODE" == race ]] && race_stage post
            printf 'POST %s %s\n' "$CLAIMANT" "$posted" >> "$CLAIM_LOG"
            lock_state
            if ! grep -Fxq "$posted" "$CLAIM_STATE"; then
                printf '%s\n' "$posted" >> "$CLAIM_STATE"
            fi
            unlock_state
            if [[ "$CLAIM_MODE" == race && ! -e "$CLAIM_RUN/posted-${CLAIMANT}" ]]; then
                : > "$CLAIM_RUN/posted-${CLAIMANT}"
                race_await posted $CLAIM_PEERS || exit 1
                case "$CLAIM_RETURN_ORDER:$CLAIMANT" in
                    alpha-first:beta) race_await returned alpha || exit 1 ;;
                    beta-first:alpha) race_await returned beta || exit 1 ;;
                esac
                : > "$CLAIM_RUN/returned-${CLAIMANT}"
            fi
            printf '[{"name":"%s"}]\n' "$posted"
            exit 0
        fi

        count_file="$CLAIM_RUN/get-${CLAIMANT}"
        count=0
        [[ -f "$count_file" ]] && count=$(cat "$count_file")
        count=$((count + 1))
        printf '%s\n' "$count" > "$count_file"
        printf 'GET %s %s\n' "$CLAIMANT" "$count" >> "$CLAIM_LOG"
        case "$CLAIM_MODE" in
            http-fail|later-page-fail) exit 1 ;;
            malformed) printf '{\n'; exit 0 ;;
            wrong-shape) printf '[{"name":"wrong"}]\n'; exit 0 ;;
            absent-own|cleanup-fail)
                printf '[[{"name":"fleet:reviewing-mac-other"}]]\n'
                exit 0
                ;;
            later-page)
                printf '[[{"name":"fleet:reviewing-mac-solo"}],[{"name":"fleet:reviewing-mac-other"}]]\n'
                exit 0
                ;;
            confirmation-race)
                if [[ "$count" -eq 1 ]]; then
                    printf '[[{"name":"fleet:reviewing-mac-solo"}]]\n'
                else
                    lock_state
                    if ! grep -Fxq 'fleet:reviewing-mac-other' "$CLAIM_STATE"; then
                        printf '%s\n' 'fleet:reviewing-mac-other' >> "$CLAIM_STATE"
                    fi
                    unlock_state
                    printf '[[{"name":"fleet:reviewing-mac-solo"},{"name":"fleet:reviewing-mac-other"}]]\n'
                fi
                exit 0
                ;;
            crlf)
                printf '[[{"name":"fleet:reviewing-mac-solo"}]]\r\n'
                exit 0
                ;;
        esac
        printf '['
        emit_flat
        printf ']\n'
        ;;
    "label "*|"pr "*) ;;
    *) echo "gh stub: unexpected call: $*" >&2; exit 90 ;;
esac
GH_STUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

reset_fixture() {
    race_reset
    rm -rf "${CLAIM_STATE}.lock" "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" "$FLEET_ORPHANS_DIR"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" "$FLEET_ORPHANS_DIR"
    : > "$CLAIM_STATE"
    : > "$CLAIM_LOG"
}

label_count() {
    awk 'NF { count++ } END { print count + 0 }' "$CLAIM_STATE"
}

# review_claim_as <host> <pr>: the claimant's review-claim, run under race_launch.
review_claim_as() {
    FLEET_TEST_HOST="$1" "$FLEET_CLAIM" review-claim "$2" "$CLAIMANT"
}

run_race() {
    local order="$1" host_a="$2" host_b="$3" fails_before="$FAIL"
    reset_fixture
    export CLAIM_MODE=race CLAIM_PEERS="alpha beta" CLAIM_RETURN_ORDER="$order"
    unset FLEET_CLAIM_NO_SLEEP
    race_launch alpha review_claim_as "$host_a" 7001
    race_launch beta review_claim_as "$host_b" 7001
    race_finish
    assert_eq "$RACE_BROKEN" "" "$order $host_a/$host_b rendezvous ends on claimant events"
    successes=$(( ($(cat "$CLAIM_RUN/alpha.rc") == 0) + ($(cat "$CLAIM_RUN/beta.rc") == 0) ))
    assert_eq "$successes" 1 "$order $host_a/$host_b race admits exactly one owner"
    assert_eq "$(label_count)" 1 "$order $host_a/$host_b race leaves one label"
    assert_contains "$(cat "$CLAIM_LOG")" "GET alpha" "$order alpha performs an independent GET"
    assert_contains "$(cat "$CLAIM_LOG")" "GET beta" "$order beta performs an independent GET"
    if [[ "$successes" -eq 1 ]]; then
        winner=alpha
        [[ "$(cat "$CLAIM_RUN/beta.rc")" -eq 0 ]] && winner=beta
        winner_gets=$(cat "$CLAIM_RUN/get-${winner}")
        if [[ "$winner_gets" -ge 2 ]]; then
            ok "$order winner confirms independently before returning"
        else
            bad "$order winner returned after only $winner_gets GET(s)"
        fi
    else
        bad "$order had no sole winner to confirm"
    fi
    marker_count=$(find "$FLEET_CLAIMS_DIR" -name '_prlabel-reviewing-*' -type f | wc -l | tr -d ' ')
    assert_eq "$marker_count" 1 "$order stamps liveness only for the admitted owner"
    race_report_if_failed "$fails_before" "$order $host_a/$host_b"
}

run_race alpha-first mac mac
run_race beta-first mac mac
run_race together mac mac
run_race together mac windows

echo "three contenders can exhaust, but never co-win"
fails_before="$FAIL"
reset_fixture
export CLAIM_MODE=race CLAIM_PEERS="alpha beta gamma" CLAIM_RETURN_ORDER=together
unset FLEET_CLAIM_NO_SLEEP
for claimant in alpha beta gamma; do
    race_launch "$claimant" review_claim_as mac 7002
done
race_finish
assert_eq "$RACE_BROKEN" "" "three-contender rendezvous ends on claimant events"
successes=0
for claimant in alpha beta gamma; do
    [[ "$(cat "$CLAIM_RUN/$claimant.rc")" -eq 0 ]] && successes=$((successes + 1))
done
if [[ "$successes" -le 1 ]]; then ok "three contenders admit at most one owner"; else bad "three contenders admitted $successes owners"; fi
if [[ "$(label_count)" -le 1 ]]; then ok "three contenders leave at most one label"; else bad "three contenders left $(label_count) labels"; fi
race_report_if_failed "$fails_before" "three-contender"

echo "a claimant that exits before a barrier breaks it with a diagnostic"
reset_fixture
export CLAIM_MODE=race CLAIM_PEERS="alpha beta" CLAIM_RETURN_ORDER=together
unset FLEET_CLAIM_NO_SLEEP
saved_race_exit="${CLAIM_RACE_EXIT:-}"
export CLAIM_RACE_EXIT="beta:post"
race_launch alpha review_claim_as mac 7005
race_launch beta review_claim_as mac 7005
race_finish
export CLAIM_RACE_EXIT="$saved_race_exit"
assert_eq "$RACE_BROKEN" "" "an early exit ends the schedule without the deadlock guard"
assert_eq "$(race_rc beta)" 1 "the forced claimant exits non-zero"
assert_eq "$(race_rc alpha)" 1 "the peer at the broken barrier exits instead of waiting"
assert_contains "$(cat "$CLAIM_RUN/alpha.out")" "'posted' barrier broken: beta exited with rc 1 before arriving" \
    "the peer names the broken barrier and the exited claimant"
report=$(race_report)
assert_contains "$report" "claimant beta: rc=1 markers=[pre-beta]" "the report carries the exited claimant's rc and progress"
assert_contains "$report" "| gh stub: beta forced to exit at post" "the report carries the exited claimant's output"
assert_contains "$report" "claimant alpha: rc=1 markers=[posted-alpha pre-alpha]" "the report carries the peer's rc and progress"
assert_eq "$(race_live_count)" 0 "no claimant outlives the broken schedule"
reset_fixture
assert_eq "$(ls -A "$CLAIM_RUN")" "" "the next schedule starts without rendezvous state"
if [[ -e "${CLAIM_STATE}.lock" ]]; then bad "the next schedule starts with the state lock held"; else ok "the next schedule starts with the state lock free"; fi

echo "the deadlock guard abandons a wedged schedule and reports it"
reset_fixture
export CLAIM_MODE=race CLAIM_PEERS="alpha beta" CLAIM_RETURN_ORDER=together
unset FLEET_CLAIM_NO_SLEEP
saved_race_delay="${CLAIM_RACE_DELAY:-}"
export CLAIM_RACE_DELAY="beta:pre:3" CLAIM_RACE_GUARD_SECS=1
race_launch alpha review_claim_as mac 7006
race_launch beta review_claim_as mac 7006
race_finish
export CLAIM_RACE_DELAY="$saved_race_delay"
unset CLAIM_RACE_GUARD_SECS
assert_contains "$RACE_BROKEN" "deadlock guard expired before every claimant exited" "guard expiry is reported as a deadlock"
assert_contains "$(cat "$CLAIM_RUN/alpha.out")" "barrier aborted: the suite abandoned the schedule" \
    "the waiting claimant names the abandoned barrier"
assert_eq "$(race_rc alpha)" 1 "the waiting claimant exits once the schedule is abandoned"
assert_eq "$(race_rc beta)" 1 "the held claimant exits once the schedule is abandoned"
assert_eq "$(race_live_count)" 0 "no claimant outlives the abandoned schedule"

run_single() {
    local mode="$1" expected="$2" message="$3"
    reset_fixture
    export CLAIM_MODE="$mode" CLAIM_EXPECTED=1 CLAIM_RETURN_ORDER=together
    export FLEET_CLAIM_NO_SLEEP=1 FLEET_CLAIM_ACQUIRE_RETRIES=1
    rc=0
    CLAIMANT=solo FLEET_TEST_HOST=mac "$FLEET_CLAIM" review-claim 7003 solo >"$CLAIM_RUN/solo.out" 2>&1 || rc=$?
    assert_eq "$rc" "$expected" "$message"
}

run_single normal 0 "uncontested candidate succeeds"
assert_eq "$(cat "$CLAIM_RUN/get-solo")" 2 "uncontested success requires two GETs"

run_single confirmation-race 1 "competitor appearing at confirmation refuses success"
assert_eq "$(label_count)" 1 "confirmation refusal removes only the candidate"
assert_eq "$(cat "$CLAIM_STATE")" "fleet:reviewing-mac-other" "confirmation refusal preserves the competitor"

for mode in http-fail malformed wrong-shape absent-own later-page later-page-fail; do
    run_single "$mode" 1 "$mode response fails closed"
done

run_single crlf 0 "CRLF label pages are normalized"
assert_eq "$(cat "$CLAIM_RUN/get-solo")" 2 "CRLF success still confirms twice"

run_single cleanup-fail 1 "failed cleanup refuses success"
orphan_count=$(find "$FLEET_ORPHANS_DIR" -name '*.json' -type f | wc -l | tr -d ' ')
assert_eq "$orphan_count" 1 "failed cleanup records one orphan sentinel"
assert_eq "$(grep -c '^POST ' "$CLAIM_LOG")" 1 "failed cleanup does not re-POST"

echo "helper seams validate complete snapshots directly"
reset_fixture
export CLAIM_MODE=normal CLAIMANT=solo FLEET_TEST_HOST=mac
printf '%s\n' 'fleet:reviewing-mac-solo' > "$CLAIM_STATE"
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"
if declare -F _fetch_live_labels >/dev/null && declare -F _read_claim_decision >/dev/null; then
    labels=$(_fetch_live_labels owner/repo 7004 fleet:reviewing-mac-solo)
    assert_eq "$labels" "fleet:reviewing-mac-solo" "_fetch_live_labels returns the complete normalized set"
    decision=$(_read_claim_decision owner/repo 7004 fleet:reviewing-mac-solo fleet:reviewing-)
    assert_eq "$decision" win "_read_claim_decision admits an exact sole candidate"
else
    bad "_fetch_live_labels returns the complete normalized set (helper missing)"
    bad "_read_claim_decision admits an exact sole candidate (helper missing)"
fi

summarize "fleet-claim independent-read race"
