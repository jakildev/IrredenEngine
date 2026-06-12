#!/usr/bin/env bash
# Tests for the epic-steward claim class (#1663): steward-claim /
# steward-release on an umbrella ISSUE via the fleet:stewarding-<host>-<agent>
# label, and the cleanup --gh third pass that sweeps stale stewarding labels
# off open fleet:epic issues after FLEET_CLAIM_STALE_SECS_STEWARD.
#
# The claim path reuses _acquire_label_on / _claim_decision, whose race
# semantics are exhaustively covered by test_fleet_claim_acquire.sh — here we
# pin the steward-specific wiring: the prefix, the issue-target wording, the
# sole-holder win/yield through the wrappers, and the TTL sweep (separate
# third pass over open fleet:epic issues; the PR pass never sees these
# labels).

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

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        ok "$msg"
    else
        bad "$msg (expected '$expected', got '$actual')"
    fi
}

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        ok "$msg"
    else
        bad "$msg (expected exit $expected_exit, got $actual_exit)"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=2

# Source fleet-claim as a library (defines helpers, skips the dispatch). Clear
# positional args first so the script's top-level --repo parse is a no-op.
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

P="fleet:stewarding-mac-"
MINE="${P}epic-steward"
OTHER="fleet:stewarding-linux-epic-steward"

echo "== steward-claim / steward-release wrappers (gh stub) =="

# Stateful gh stub for the claim path. STUB_HOLDERS = stewarding labels
# "already present" on the umbrella; the POST response echoes those plus the
# just-posted label. remove-label calls are logged so yield-self-removal and
# release are observable.
STUB_HOLDERS=""
REMOVE_LOG="$TMPROOT/remove.log"; : > "$REMOVE_LOG"
gh() {
    case "${1:-}" in
        api)
            local posted="" a
            for a in "$@"; do
                case "$a" in
                    labels\[\]=*) posted="${a#labels[]=}" ;;
                esac
            done
            local out='[' first=1 h
            for h in $STUB_HOLDERS $posted; do
                [[ $first -eq 1 ]] || out+=','
                out+="{\"name\":\"$h\"}"
                first=0
            done
            out+=']'
            printf '%s\n' "$out"
            return 0
            ;;
        issue)
            if [[ "${2:-}" == "edit" ]]; then
                printf '%s\n' "$*" >> "$REMOVE_LOG"
            fi
            return 0
            ;;
        *)
            return 0
            ;;
    esac
}

echo "T1: sole holder → steward-claim acquires (exit 0), issue-worded output"
STUB_HOLDERS=""
rc=0
out=$(cmd_steward_claim 1661 epic-steward 2>&1) || rc=$?
assert_exit "$rc" 0 "no existing stewarding holder → exit 0"
case "$out" in
    *"issue#1661"*) ok "acquire message names the umbrella as an issue" ;;
    *) bad "acquire message should say issue#1661, got: $out" ;;
esac

echo "T2: another host already stewarding → yield (exit 1) + self-remove"
STUB_HOLDERS="$OTHER"
: > "$REMOVE_LOG"
rc=0
cmd_steward_claim 1661 epic-steward >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "persistent stewarding holder → exit 1 (never a co-win)"
if grep -q -- "--remove-label $MINE" "$REMOVE_LOG"; then
    ok "losing claimant self-removed its stewarding label"
else
    bad "losing claimant did not self-remove (log: $(cat "$REMOVE_LOG"))"
fi

echo "T3: non-numeric umbrella rejected with issue wording (exit 2)"
rc=0
out=$(cmd_steward_claim not-a-number epic-steward 2>&1) || rc=$?
assert_exit "$rc" 2 "non-numeric target → exit 2"
case "$out" in
    *"issue must be a number"*) ok "rejection message uses issue wording" ;;
    *) bad "expected 'issue must be a number', got: $out" ;;
esac

echo "T4: steward-release removes the label"
: > "$REMOVE_LOG"
rc=0
cmd_steward_release 1661 epic-steward >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "release exits 0"
if grep -q -- "--remove-label $MINE" "$REMOVE_LOG"; then
    ok "release removed $MINE"
else
    bad "release did not remove $MINE (log: $(cat "$REMOVE_LOG"))"
fi

echo "T5: _claim_decision geometry holds for the stewarding prefix"
# Lex order: ...-linux-... < ...-mac-... ('l' < 'm'), so MINE (mac) is the
# lex-larger of the pair and must yield; OTHER (linux) retries as lex-min.
assert_eq "$(_claim_decision "$MINE" "$MINE")" "win" \
    "sole stewarding holder → win"
assert_eq "$(_claim_decision "$MINE" "$MINE" "$OTHER")" "lose" \
    "lex-larger stewarding claimant vs existing holder → lose"
assert_eq "$(_claim_decision "$OTHER" "$MINE" "$OTHER")" "retry" \
    "lex-min stewarding claimant amid contention → retry, never co-win"

echo "== cleanup --gh third pass: stale stewarding sweep over fleet:epic =="

# Fresh stub for the cleanup path. Surfaces:
#   pr list                          → []   (first pass: nothing)
#   issue list --search fleet:queued → []   (second pass: nothing)
#   issue list --label fleet:epic    → two umbrellas, one stale stewarding
#                                      label (#70, added 2h ago) and one
#                                      fresh (#71, added 10s ago)
#   api .../events                   → the canned labeled-event timestamp
#                                      for whichever label is being aged
# Removals are logged; only the stale one may be swept at the default TTL.
NOW_EPOCH=$(date +%s)
STALE_AT=$(python3 -c "
import datetime
print(datetime.datetime.fromtimestamp($NOW_EPOCH - 7200,
      datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
FRESH_AT=$(python3 -c "
import datetime
print(datetime.datetime.fromtimestamp($NOW_EPOCH - 10,
      datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
export STALE_AT FRESH_AT

EPICS_JSON="$TMPROOT/epics.json"
cat > "$EPICS_JSON" <<JSON
[
  {"number":70,"labels":[{"name":"fleet:epic"},{"name":"fleet:stewarding-mac-epic-steward"}]},
  {"number":71,"labels":[{"name":"fleet:epic"},{"name":"fleet:stewarding-linux-epic-steward"}]}
]
JSON
export EPICS_JSON

gh() {
    case "${1:-}" in
        pr)
            [[ "${2:-}" == "list" ]] && { echo "[]"; return 0; }
            return 0
            ;;
        issue)
            case "${2:-}" in
                list)
                    if printf '%s ' "$@" | grep -q -- "--label fleet:epic"; then
                        cat "$EPICS_JSON"
                    else
                        echo "[]"
                    fi
                    return 0
                    ;;
                edit)
                    printf '%s\n' "$*" >> "$REMOVE_LOG"
                    return 0
                    ;;
                *) return 0 ;;
            esac
            ;;
        api)
            # label_added_epoch events lookup: stale timestamp for #70's
            # label, fresh for #71's. The target number rides in the path arg.
            if printf '%s ' "$@" | grep -q "issues/70/"; then
                echo "$STALE_AT"
            else
                echo "$FRESH_AT"
            fi
            return 0
            ;;
        *)
            return 0
            ;;
    esac
}

echo "T6: default TTL (3600) sweeps the 2h-old label, keeps the fresh one"
: > "$REMOVE_LOG"
unset FLEET_CLAIM_STALE_SECS_STEWARD
out=$(cmd_cleanup_gh "jakildev/IrredenEngine" 2>&1)
if grep -q -- "--remove-label fleet:stewarding-mac-epic-steward" "$REMOVE_LOG"; then
    ok "stale stewarding label on epic#70 swept"
else
    bad "stale label not swept (log: $(cat "$REMOVE_LOG"); out: $out)"
fi
if grep -q -- "--remove-label fleet:stewarding-linux-epic-steward" "$REMOVE_LOG"; then
    bad "fresh stewarding label on epic#71 wrongly swept"
else
    ok "fresh stewarding label on epic#71 kept"
fi
case "$out" in
    *"removed stale 'fleet:stewarding-mac-epic-steward'"*) ok "sweep reported the removal" ;;
    *) bad "sweep output missing removal line: $out" ;;
esac

echo "T7: FLEET_CLAIM_STALE_SECS_STEWARD override keeps even the 2h-old label"
: > "$REMOVE_LOG"
export FLEET_CLAIM_STALE_SECS_STEWARD=999999
cmd_cleanup_gh "jakildev/IrredenEngine" >/dev/null 2>&1
if grep -q -- "--remove-label fleet:stewarding-" "$REMOVE_LOG"; then
    bad "label swept despite TTL override (log: $(cat "$REMOVE_LOG"))"
else
    ok "TTL override respected; nothing swept"
fi
unset FLEET_CLAIM_STALE_SECS_STEWARD

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
