#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-claim-race.XXXXXX")
trap 'rm -rf "$TMPROOT"' EXIT

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export CLAIM_STATE="$TMPROOT/labels"
export CLAIM_RUN="$TMPROOT/run"
export CLAIM_LOG="$TMPROOT/calls"
export CLAIM_MODE=race
export CLAIM_EXPECTED=2
export CLAIM_RETURN_ORDER=together
export FLEET_CLAIM_ACQUIRE_RETRIES=3
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" \
    "$FLEET_RESERVATIONS_DIR" "$FLEET_ORPHANS_DIR" "$TMPROOT/bin"

cat > "$TMPROOT/bin/gh" <<'GH_STUB'
#!/usr/bin/env bash
set -euo pipefail

lock_state() {
    local tick
    for ((tick = 0; tick < 1000; tick++)); do
        mkdir "${CLAIM_STATE}.lock" 2>/dev/null && return 0
        sleep 0.005
    done
    echo "gh stub: state lock timeout" >&2
    return 1
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

wait_for_count() {
    local prefix="$1" expected="$2" tick count
    for ((tick = 0; tick < 1000; tick++)); do
        count=0
        for path in "$CLAIM_RUN"/"$prefix"-*; do
            [[ -e "$path" ]] && count=$((count + 1))
        done
        [[ "$count" -ge "$expected" ]] && return 0
        sleep 0.005
    done
    echo "gh stub: $prefix barrier timeout" >&2
    return 1
}

case "${1:-} ${2:-}" in
    "issue view")
        if [[ "$CLAIM_MODE" == race ]]; then
            : > "$CLAIM_RUN/pre-${CLAIMANT}"
            wait_for_count pre "$CLAIM_EXPECTED"
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
            printf 'POST %s %s\n' "$CLAIMANT" "$posted" >> "$CLAIM_LOG"
            lock_state
            if ! grep -Fxq "$posted" "$CLAIM_STATE"; then
                printf '%s\n' "$posted" >> "$CLAIM_STATE"
            fi
            unlock_state
            if [[ "$CLAIM_MODE" == race && ! -e "$CLAIM_RUN/posted-${CLAIMANT}" ]]; then
                : > "$CLAIM_RUN/posted-${CLAIMANT}"
                wait_for_count posted "$CLAIM_EXPECTED"
                case "$CLAIM_RETURN_ORDER:$CLAIMANT" in
                    alpha-first:beta) while [[ ! -e "$CLAIM_RUN/returned-alpha" ]]; do sleep 0.005; done ;;
                    beta-first:alpha) while [[ ! -e "$CLAIM_RUN/returned-beta" ]]; do sleep 0.005; done ;;
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
    rm -rf "$CLAIM_RUN" "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" "$FLEET_ORPHANS_DIR"
    mkdir -p "$CLAIM_RUN" "$FLEET_CLAIMS_DIR" "$FLEET_HEARTBEATS_DIR" "$FLEET_ORPHANS_DIR"
    : > "$CLAIM_STATE"
    : > "$CLAIM_LOG"
}

label_count() {
    awk 'NF { count++ } END { print count + 0 }' "$CLAIM_STATE"
}

run_race() {
    local order="$1" host_a="$2" host_b="$3"
    reset_fixture
    export CLAIM_MODE=race CLAIM_EXPECTED=2 CLAIM_RETURN_ORDER="$order"
    unset FLEET_CLAIM_NO_SLEEP
    (rc=0; CLAIMANT=alpha FLEET_TEST_HOST="$host_a" "$FLEET_CLAIM" review-claim 7001 alpha >"$CLAIM_RUN/alpha.out" 2>&1 || rc=$?; printf '%s\n' "$rc" > "$CLAIM_RUN/alpha.rc") &
    pid_a=$!
    (rc=0; CLAIMANT=beta FLEET_TEST_HOST="$host_b" "$FLEET_CLAIM" review-claim 7001 beta >"$CLAIM_RUN/beta.out" 2>&1 || rc=$?; printf '%s\n' "$rc" > "$CLAIM_RUN/beta.rc") &
    pid_b=$!
    wait "$pid_a"
    wait "$pid_b"
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
}

run_race alpha-first mac mac
run_race beta-first mac mac
run_race together mac mac
run_race together mac windows

echo "three contenders can exhaust, but never co-win"
reset_fixture
export CLAIM_MODE=race CLAIM_EXPECTED=3 CLAIM_RETURN_ORDER=together
unset FLEET_CLAIM_NO_SLEEP
pids=()
for claimant in alpha beta gamma; do
    (rc=0; CLAIMANT="$claimant" FLEET_TEST_HOST=mac "$FLEET_CLAIM" review-claim 7002 "$claimant" >/dev/null 2>&1 || rc=$?; printf '%s\n' "$rc" > "$CLAIM_RUN/$claimant.rc") &
    pids+=("$!")
done
for pid in "${pids[@]}"; do wait "$pid"; done
successes=0
for claimant in alpha beta gamma; do
    [[ "$(cat "$CLAIM_RUN/$claimant.rc")" -eq 0 ]] && successes=$((successes + 1))
done
if [[ "$successes" -le 1 ]]; then ok "three contenders admit at most one owner"; else bad "three contenders admitted $successes owners"; fi
if [[ "$(label_count)" -le 1 ]]; then ok "three contenders leave at most one label"; else bad "three contenders left $(label_count) labels"; fi

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
