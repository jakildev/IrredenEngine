#!/usr/bin/env bash
# Tests for fleet-claim's atomic-claim primitive (_acquire_label_on) and its
# pure decision seam (_claim_decision).
#
# Regression coverage for #1384: the old primitive won "iff my label is the
# lex-min of the POST-response snapshot". That let a LATER, lex-smaller
# claimant co-win with an existing holder that had already passed its own
# (smaller) snapshot check and never re-validated. Live incident on PR #1377:
#
#   20:27:43  +fleet:reviewing-windows-sonnet-fleet-2   ← holder, passes check
#   20:31:48  +fleet:reviewing-windows-sonnet-fleet-1   ← later, lex-SMALLER,
#                                                          also computes itself
#                                                          as min → both hold.
#
# The fix (Option A): a claimant wins only as the SOLE holder of the prefix.
# If others are present and I'm the lex-min I drop + retry (so a simultaneous
# race converges to one winner); if an earlier/lex-smaller holder remains I
# yield. Two same-prefix claims can therefore never coexist regardless of
# arrival order or lex order.
#
# _claim_decision is table-tested directly (pure, no gh). _acquire_label_on is
# driven end-to-end through a stateful gh stub for the terminal behaviors
# (sole-holder win, persistent-holder yield).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: '$expected'"
        echo "        actual:   '$actual'"
    fi
}

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

# Source fleet-claim as a library (defines helpers, skips the dispatch). Clear
# positional args first so the script's top-level --repo parse is a no-op.
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

# Label names used throughout. opus-worker-1 is lex-SMALLER than
# sonnet-fleet-2 ('o' 0x6f < 's' 0x73), matching the #1377 incident geometry
# (the later, lex-smaller claimant is the one that wrongly co-won).
P="fleet:reviewing-windows-"
SMALL="${P}opus-worker-1"
LARGE="${P}sonnet-fleet-2"

echo "== _claim_decision (pure) =="

# T1: sole holder → win.
echo "T1: sole holder of the prefix → win"
assert_eq "$(_claim_decision "$SMALL" "$SMALL")" "win" \
    "only my label present → win"

# T2: THE #1384 regression — I am the later, lex-smaller claimant; an existing
# holder is present. Old code returned 'win' (the bug); fix returns 'retry'
# (drop + re-acquire alone), never an outright co-win.
echo "T2: later lex-smaller claimant + existing holder → retry (NOT win)"
assert_eq "$(_claim_decision "$SMALL" "$SMALL" "$LARGE")" "retry" \
    "lex-min among present holders → retry, not a co-win"

# T3: I am the lex-larger of two present holders → yield to the smaller one.
echo "T3: lex-larger claimant + lex-smaller holder present → lose"
assert_eq "$(_claim_decision "$LARGE" "$SMALL" "$LARGE")" "lose" \
    "an earlier/lex-smaller holder remains → lose"

# T4: order-independence — the matching set is the same regardless of the
# order labels are passed; decision must not depend on arg order.
echo "T4: decision is independent of argument order"
assert_eq "$(_claim_decision "$LARGE" "$LARGE" "$SMALL")" "lose" \
    "reversed arg order still yields lose for the lex-larger label"

# T5: three-way contention — lex-min wins-to-retry, the rest lose.
echo "T5: three present holders — only the lex-min retries"
THIRD="${P}sonnet-fleet-9"
assert_eq "$(_claim_decision "$SMALL" "$SMALL" "$LARGE" "$THIRD")" "retry" \
    "lex-min of three → retry"
assert_eq "$(_claim_decision "$LARGE" "$SMALL" "$LARGE" "$THIRD")" "lose" \
    "middle of three → lose"
assert_eq "$(_claim_decision "$THIRD" "$SMALL" "$LARGE" "$THIRD")" "lose" \
    "lex-max of three → lose"

echo "== _acquire_label_on (end-to-end via gh stub) =="

# Stateful gh stub. STUB_HOLDERS = labels that are "already present" on the
# target; the POST response always echoes those plus the just-posted label.
# remove-label is a no-op success (mirrors gh_release_label's happy path).
STUB_HOLDERS=""
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
        issue|pr|label)
            return 0  # edit/remove-label/etc. → no-op success
            ;;
        *)
            return 0
            ;;
    esac
}

# Keep retries/sleeps test-fast and deterministic.
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=2

# T6: no existing holder → sole-holder win (exit 0).
echo "T6: no existing holder → acquire succeeds"
STUB_HOLDERS=""
rc=0
_acquire_label_on "owner/repo" 1 "$SMALL" "$P" >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "sole holder → exit 0 (own the lock)"

# T7: persistent lex-LARGER holder, I am lex-smaller and arrive later — the
# exact #1377 geometry. I retry (drop + re-POST) but the holder never yields,
# so after the bounded retries I lose. Critically: I do NOT co-win.
echo "T7: later lex-smaller claimant vs persistent holder → yield (exit 1)"
STUB_HOLDERS="$LARGE"
rc=0
_acquire_label_on "owner/repo" 1 "$SMALL" "$P" >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "persistent holder present → exit 1 (never a co-win)"

# T8: persistent lex-SMALLER holder, I am lex-larger → immediate yield.
echo "T8: lex-larger claimant vs lex-smaller holder → yield (exit 1)"
STUB_HOLDERS="$SMALL"
rc=0
_acquire_label_on "owner/repo" 1 "$LARGE" "$P" >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "earlier/lex-smaller holder remains → exit 1"

echo
echo "fleet-claim acquire tests: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
