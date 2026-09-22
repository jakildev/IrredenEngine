#!/usr/bin/env bash
# Tests for planning-claim's re-stamp of its own leftover label, and the
# cleanup --gh planning pass that ages a claim from the label's newest
# `labeled` event.
#
# A re-dispatch onto an issue whose previous planner never released arrives
# with that planner's label still on the issue. Re-adding a present label
# creates no `labeled` event, so without the re-stamp the sweep ages the live
# claim from the dead one and strips it at the next tick. The contract spans
# the claim and the sweep, so both run against ONE stateful gh stub: a label
# set and a `labeled`-event log per issue, plus a call log. The stub state
# lives in files because the claim path calls gh inside command substitutions.
#
# test_fleet_claim_planning.sh pins the rest of the planning wiring; its
# stubs are stateless about events, which is why this is a separate suite.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 3
fi

TMPROOT=$(mktemp -d)
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

# Hermetic: an unset FLEET_CLAIMS_DIR would read the host's live markers.
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=2
unset FLEET_CLAIM_STALE_SECS_PLANNING FLEET_CLAIM_PRLABEL_ORPHAN_GRACE_SECS

set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

REPO="jakildev/IrredenEngine"
MINE="fleet:planning-mac-worker"
MARKER="$FLEET_CLAIMS_DIR/_prlabel-planning-worker"
STATE="$TMPROOT/state"
CALLS="$TMPROOT/calls.log"

iso_ago() {
    python3 -c "import datetime,time;print(datetime.datetime.fromtimestamp(time.time()-$1,datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))"
}

# reset_state — empty every issue, the call log, markers and sentinels.
reset_state() {
    rm -rf "$STATE" "$FLEET_CLAIMS_DIR" "$FLEET_ORPHANS_DIR"
    mkdir -p "$STATE" "$FLEET_CLAIMS_DIR" "$FLEET_ORPHANS_DIR"
    : > "$CALLS"
    STUB_STATE=OPEN; STUB_PLAN_COMMENTS=0; STUB_PROBE_FAIL=0; STUB_REMOVE_FAIL=0
}
# seed_issue <N> <label>... — the issue's label set, with no events.
seed_issue() {
    local n="$1"; shift
    printf '%s\n' "$@" > "$STATE/labels.$n"
    : > "$STATE/events.$n"
}
# seed_event <N> <label> <age-secs> — a `labeled` event <age-secs> ago.
seed_event() {
    printf '%s\t%s\n' "$(iso_ago "$3")" "$2" >> "$STATE/events.$1"
}
event_count() {
    awk -F'\t' -v l="$2" '$2 == l' "$STATE/events.$1" | wc -l | tr -d ' '
}
has_label() { grep -qxF -- "$2" "$STATE/labels.$1"; }
sentinel_count() { find "$FLEET_ORPHANS_DIR" -type f | wc -l | tr -d ' '; }
# call_line <pattern> — first call-log line number matching, or 0.
call_line() { grep -nF -- "$1" "$CALLS" | head -n1 | cut -d: -f1 || true; }

_stub_issue_from_path() {
    local re='issues/([0-9]+)/'
    [[ "$1" =~ $re ]] && printf '%s\n' "${BASH_REMATCH[1]}"
}
_stub_json_labels() {
    local n="$1" out='' l
    while IFS= read -r l; do
        [[ -z "$l" ]] && continue
        out+="${out:+,}{\"name\":\"$l\"}"
    done < "$STATE/labels.$n"
    printf '[%s]' "$out"
}
_stub_unmodelled() { printf 'UNMODELLED %s\n' "$*" >> "$CALLS"; return 1; }

gh() {
    local args=" $* " jq_prog="" prev="" a
    for a in "$@"; do
        [[ "$prev" == "--jq" ]] && jq_prog="$a"
        prev="$a"
    done
    case "${1:-} ${2:-}" in
        "issue view")
            local n="$3"
            [[ -f "$STATE/labels.$n" ]] || { _stub_unmodelled "$@"; return 1; }
            case "$args" in
                *" --json state "*) printf '%s\n' "$STUB_STATE" ;;
                *" --json comments "*) printf '%s\n' "$STUB_PLAN_COMMENTS" ;;
                *" --json labels "*)
                    [[ "$STUB_PROBE_FAIL" -eq 1 ]] && return 1
                    local label_re='^any\(\.labels\[\]; \.name == "(.+)"\)$'
                    [[ "$jq_prog" =~ $label_re ]] || { _stub_unmodelled "$@"; return 1; }
                    printf 'probe %s %s\n' "$n" "${BASH_REMATCH[1]}" >> "$CALLS"
                    if has_label "$n" "${BASH_REMATCH[1]}"; then echo true; else echo false; fi ;;
                *) _stub_unmodelled "$@" ;;
            esac ;;
        "issue edit")
            local n="$3" label=""
            prev=""
            for a in "$@"; do
                [[ "$prev" == "--remove-label" ]] && label="$a"
                prev="$a"
            done
            [[ -n "$label" ]] || { _stub_unmodelled "$@"; return 1; }
            printf 'remove %s %s\n' "$n" "$label" >> "$CALLS"
            [[ "$STUB_REMOVE_FAIL" -eq 1 ]] && return 1
            grep -vxF -- "$label" "$STATE/labels.$n" > "$STATE/labels.tmp" || true
            mv "$STATE/labels.tmp" "$STATE/labels.$n" ;;
        "issue list")
            case "$args" in
                *" --label fleet:needs-plan "*|" issue list --repo $REPO --state open --json number,labels --limit 1000 ")
                    local out='' f n
                    for f in "$STATE"/labels.*; do
                        [[ -e "$f" ]] || continue
                        n="${f##*.}"
                        if [[ "$args" == *" --label fleet:needs-plan "* ]]; then
                            grep -qxF "fleet:needs-plan" "$f" || continue
                        fi
                        out+="${out:+,}{\"number\":$n,\"labels\":$(_stub_json_labels "$n")}"
                    done
                    printf '[%s]\n' "$out" ;;
                *" --label fleet:plan-review "*|*" --label fleet:epic "*|*'label:"fleet:queued"'*)
                    echo '[]' ;;
                *) _stub_unmodelled "$@" ;;
            esac ;;
        "pr list") echo '[]' ;;
        # The fifth pass's --jq selects flag-token-consumed names; none exist
        # here, so the filtered output is empty. A bare `[]` would be read as
        # a label NAME.
        "label list")
            [[ "$args" == *" --json name "* ]] || { _stub_unmodelled "$@"; return 1; }
            return 0 ;;
        "api "*)
            local path="$2" n
            n=$(_stub_issue_from_path "$path") || { _stub_unmodelled "$@"; return 1; }
            [[ -f "$STATE/labels.$n" ]] || { _stub_unmodelled "$@"; return 1; }
            case "$args" in
                *" --method POST "*)
                    local label=""
                    for a in "$@"; do case "$a" in labels\[\]=*) label="${a#labels[]=}" ;; esac; done
                    [[ -n "$label" ]] || { _stub_unmodelled "$@"; return 1; }
                    printf 'post %s %s\n' "$n" "$label" >> "$CALLS"
                    # GitHub: POSTing a label already present is a no-op that
                    # records no `labeled` event.
                    if ! has_label "$n" "$label"; then
                        printf '%s\n' "$label" >> "$STATE/labels.$n"
                        seed_event "$n" "$label" 0
                    fi
                    _stub_json_labels "$n"; echo ;;
                *" --slurp "*)
                    [[ "$path" == */labels\?per_page=100 ]] || { _stub_unmodelled "$@"; return 1; }
                    printf '[%s]\n' "$(_stub_json_labels "$n")" ;;
                *)
                    if [[ "$path" == */events && "$jq_prog" == *env.FLEET_LABEL_NAME* ]]; then
                        awk -F'\t' -v l="${FLEET_LABEL_NAME:-}" '$2 == l { print $1 }' "$STATE/events.$n"
                    else
                        _stub_unmodelled "$@"
                    fi ;;
            esac ;;
        *) _stub_unmodelled "$@" ;;
    esac
}

assert_modelled() {
    if grep -q '^UNMODELLED' "$CALLS"; then
        bad "$1: stub saw an unmodelled gh call: $(grep '^UNMODELLED' "$CALLS" | head -n1)"
    else
        ok "$1: every gh call was a modelled shape"
    fi
}

HOURS14=$((14 * 3600))

echo "T1: re-dispatch onto a 14h-old own label → re-stamped, and the sweep keeps it"
reset_state
seed_issue 90 "fleet:needs-plan" "$MINE"
seed_event 90 "$MINE" "$HOURS14"
rc=0; cmd_planning_claim 90 worker >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "0" "planning-claim acquires"
rm_at=$(call_line "remove 90 $MINE"); post_at=$(call_line "post 90 $MINE")
if [[ "${rm_at:-0}" -gt 0 && "${post_at:-0}" -gt "${rm_at:-0}" ]]; then
    ok "own label removed before the acquire's POST"
else
    bad "expected remove before POST (remove line ${rm_at:-0}, post line ${post_at:-0}; log: $(tr '\n' ';' < "$CALLS"))"
fi
assert_eq "$(event_count 90 "$MINE")" "2" "the re-acquire minted a fresh labeled event"
assert_eq "$(cat "$MARKER" 2>/dev/null)" "90" "marker holds the claimed issue"
: > "$CALLS"
cmd_cleanup_gh "$REPO" >/dev/null 2>&1
if [[ "$(call_line "remove 90 $MINE")" == "" ]] && has_label 90 "$MINE"; then
    ok "cleanup --gh keeps the re-stamped live claim"
else
    bad "cleanup --gh stripped the live claim (log: $(tr '\n' ';' < "$CALLS"))"
fi
assert_eq "$(sentinel_count)" "0" "no orphan sentinel written"
assert_modelled "T1"

echo "T2: control — the same 14h-old label vouched by a fresh marker alone is still swept"
# Pins the sweep's clock (newest labeled event, not the marker) and proves
# the fixture can fire.
reset_state
seed_issue 90 "fleet:needs-plan" "$MINE"
seed_event 90 "$MINE" "$HOURS14"
printf '90\n' > "$MARKER"
gh api "repos/$REPO/issues/90/labels" --method POST -f "labels[]=$MINE" >/dev/null
assert_eq "$(event_count 90 "$MINE")" "1" "stub fidelity: POST of a present label records no event"
: > "$CALLS"
out=$(cmd_cleanup_gh "$REPO" 2>&1)
if [[ -n "$(call_line "remove 90 $MINE")" ]]; then
    ok "cleanup --gh sweeps the old label despite the matching marker"
else
    bad "control did not fire (log: $(tr '\n' ';' < "$CALLS"); out: $out)"
fi
assert_contains "$out" "age: 840m" "the sweep aged it from the 14h-old event"
assert_modelled "T2"

echo "T3: both existing sweeps keep firing"
reset_state
seed_issue 91 "fleet:needs-plan" "$MINE"
seed_event 91 "$MINE" 7200
printf '91\n' > "$MARKER"
seed_issue 92 "fleet:needs-plan" "fleet:planning-mac-ghost"
seed_event 92 "fleet:planning-mac-ghost" 600
seed_issue 93 "fleet:needs-plan" "fleet:planning-mac-fresh"
seed_event 93 "fleet:planning-mac-fresh" 30
cmd_cleanup_gh "$REPO" >/dev/null 2>&1
if [[ -n "$(call_line "remove 91 $MINE")" ]]; then ok "crashed planner: 2h-old label with a leftover marker swept on the TTL"; else bad "crashed planner's label survived (log: $(tr '\n' ';' < "$CALLS"))"; fi
if [[ -n "$(call_line "remove 92 fleet:planning-mac-ghost")" ]]; then ok "same-host label with no marker past the grace swept"; else bad "no-marker orphan survived (log: $(tr '\n' ';' < "$CALLS"))"; fi
if [[ -z "$(call_line "remove 93 fleet:planning-mac-fresh")" ]]; then ok "same-host label with no marker inside the grace kept"; else bad "fresh no-marker label reaped inside the grace"; fi
assert_modelled "T3"

echo "T4: --replan re-stamps the same way"
reset_state
STUB_PLAN_COMMENTS=1
seed_issue 94 "fleet:needs-plan" "$MINE"
seed_event 94 "$MINE" "$HOURS14"
rc=0; cmd_planning_claim 94 worker --replan >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "0" "--replan acquires"
rm_at=$(call_line "remove 94 $MINE"); post_at=$(call_line "post 94 $MINE")
if [[ "${rm_at:-0}" -gt 0 && "${post_at:-0}" -gt "${rm_at:-0}" ]]; then ok "--replan removed its own label before the POST"; else bad "--replan did not re-stamp (log: $(tr '\n' ';' < "$CALLS"))"; fi
assert_eq "$(event_count 94 "$MINE")" "2" "--replan minted a fresh labeled event"
assert_modelled "T4"

echo "T5: a first-time claim (own label absent) issues no removal"
reset_state
seed_issue 95 "fleet:needs-plan"
rc=0; cmd_planning_claim 95 worker >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "0" "first-time claim acquires"
assert_eq "$(grep -c '^remove' "$CALLS" || true)" "0" "no removal on a first-time claim"
assert_eq "$(event_count 95 "$MINE")" "1" "one labeled event from the POST"
assert_modelled "T5"

echo "T6: every refusal gate issues no removal"
# refuse_arm <name> <expected-rc> <claim args...> — the own label is on the
# issue in every arm, so a removal here would be the re-stamp firing early.
refuse_arm() {
    local name="$1" want="$2"; shift 2
    rc=0; cmd_planning_claim "$@" >/dev/null 2>&1 || rc=$?
    assert_eq "$rc" "$want" "$name: exit $want"
    if grep -q '^remove' "$CALLS"; then bad "$name: refused claim removed a label ($(grep '^remove' "$CALLS" | head -n1))"; else ok "$name: no removal"; fi
    if has_label 96 "$MINE"; then ok "$name: leftover label untouched"; else bad "$name: leftover label gone"; fi
}
reset_state; STUB_STATE=CLOSED
seed_issue 96 "fleet:needs-plan" "$MINE"; seed_event 96 "$MINE" "$HOURS14"
refuse_arm "closed issue" 1 96 worker
reset_state
seed_issue 96 "fleet:needs-plan" "fleet:needs-human" "$MINE"; seed_event 96 "$MINE" "$HOURS14"
refuse_arm "fleet:needs-human park" 1 96 worker
reset_state
seed_issue 96 "$MINE"; seed_event 96 "$MINE" "$HOURS14"
refuse_arm "stale candidate" 1 96 worker
reset_state; STUB_PLAN_COMMENTS=1
seed_issue 96 "fleet:needs-plan" "$MINE"; seed_event 96 "$MINE" "$HOURS14"
refuse_arm "## Plan dedup" 3 96 worker
reset_state; STUB_PLAN_COMMENTS=1
seed_issue 96 "$MINE"; seed_event 96 "$MINE" "$HOURS14"
refuse_arm "--replan without fleet:needs-plan" 2 96 worker --replan
assert_modelled "T6"

echo "T7: a failed label probe fails open to the plain acquire, with no removal"
reset_state; STUB_PROBE_FAIL=1
seed_issue 97 "fleet:needs-plan" "$MINE"; seed_event 97 "$MINE" "$HOURS14"
rc=0; cmd_planning_claim 97 worker >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "0" "probe unknown → claim still acquires"
assert_eq "$(grep -c '^remove' "$CALLS" || true)" "0" "probe unknown → no removal"
assert_modelled "T7"

echo "T8: a failing re-stamp removal still acquires and writes no orphan sentinel"
# A sentinel would be replayed by replay_orphans after the POST succeeded,
# stripping the live claim.
reset_state; STUB_REMOVE_FAIL=1
seed_issue 98 "fleet:needs-plan" "$MINE"; seed_event 98 "$MINE" "$HOURS14"
rc=0; cmd_planning_claim 98 worker >/dev/null 2>&1 || rc=$?
assert_eq "$rc" "0" "failed removal → claim still acquires"
assert_eq "$(grep -c "^remove 98 $MINE" "$CALLS" || true)" "1" "removal attempted exactly once (no retry)"
assert_eq "$(sentinel_count)" "0" "failed removal left no orphan sentinel"
assert_modelled "T8"

summarize "fleet-claim planning re-stamp"
