#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-pr-claim-feedback.
#
# The wrapper composes two primitives in the one safe order:
#   amending-claim (lex-min mutex) → fleet-pr-checkout-detached
# with release-on-checkout-failure. These tests stub both primitives on
# PATH and assert the wrapper's control flow + exit codes:
#   T1: claim won + checkout ok (engine) → exit 0, both called once,
#       no release, no --repo flag on either call
#   T2: claim lost → exit 1, checkout NEVER called, nothing released
#       (touched nothing)
#   T3: claim won + checkout fails → exit 1, claim released exactly once
#   T4: game repo (--repo jakildev/irreden) → fleet-claim gets the
#       `--repo game` namespace, checkout gets the `--repo jakildev/irreden`
#       slug
#   T5: usage errors (non-int PR, missing agent, unknown slug) → exit 2,
#       no claim attempted

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WRAPPER="$SCRIPT_DIR/fleet-pr-claim-feedback"

if [[ ! -x "$WRAPPER" ]]; then
    echo "test setup: fleet-pr-claim-feedback not executable at $WRAPPER" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

# --- Sandbox + stubs -------------------------------------------------------
TMPROOT=$(mktemp -d)
BIN="$TMPROOT/bin"
mkdir -p "$BIN"

export FC_LOG="$TMPROOT/fleet-claim.log"
export CO_LOG="$TMPROOT/checkout.log"

# Stub fleet-claim: logs the full argv, honors $FAKE_CLAIM_RC for the
# amending-claim subcommand, always succeeds for amending-release. The
# subcommand is located by skipping the global `--repo <ns>` flag so the
# stub mirrors the real arg grammar (global flag before subcommand).
cat >"$BIN/fleet-claim" <<'FCEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$FC_LOG"
args=("$@"); sub=""; i=0
while (( i < ${#args[@]} )); do
    case "${args[$i]}" in
        --repo)   i=$((i + 2));;
        --repo=*) i=$((i + 1));;
        *) sub="${args[$i]}"; break;;
    esac
done
case "$sub" in
    amending-claim)   exit "${FAKE_CLAIM_RC:-0}";;
    amending-release) exit 0;;
    *) exit 0;;
esac
FCEOF
chmod +x "$BIN/fleet-claim"

# Stub fleet-pr-checkout-detached: logs argv, honors $FAKE_CHECKOUT_RC.
cat >"$BIN/fleet-pr-checkout-detached" <<'COEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$CO_LOG"
exit "${FAKE_CHECKOUT_RC:-0}"
COEOF
chmod +x "$BIN/fleet-pr-checkout-detached"

export PATH="$BIN:$PATH"

reset_logs() { : >"$FC_LOG"; : >"$CO_LOG"; }

# === T1: claim won + checkout ok (engine) ==================================
echo "T1: claim won + checkout ok (engine, no --repo) → exit 0"
reset_logs
FAKE_CLAIM_RC=0 FAKE_CHECKOUT_RC=0 "$WRAPPER" 123 opus-worker-2 >"$TMPROOT/t1.log" 2>&1
assert_eq "$?" "0" "T1 exit 0 on full success"
assert_eq "$(grep -c 'amending-claim 123 opus-worker-2' "$FC_LOG")" "1" \
    "T1 amending-claim called once with PR + agent"
assert_eq "$(grep -c '\-\-repo' "$FC_LOG" || true)" "0" \
    "T1 fleet-claim got no --repo namespace (engine default)"
assert_eq "$(grep -c '123' "$CO_LOG")" "1" \
    "T1 checkout called once for PR 123"
assert_eq "$(grep -c '\-\-repo' "$CO_LOG" || true)" "0" \
    "T1 checkout got no --repo slug (engine default)"
assert_eq "$(grep -c 'amending-release' "$FC_LOG" || true)" "0" \
    "T1 no release on success"

# === T2: claim lost → touch nothing =======================================
echo "T2: claim lost (amending-claim exit 1) → exit 1, no checkout"
reset_logs
set +e
FAKE_CLAIM_RC=1 FAKE_CHECKOUT_RC=0 "$WRAPPER" 123 opus-worker-2 >"$TMPROOT/t2.log" 2>&1
t2_rc=$?
set -e
assert_eq "$t2_rc" "1" "T2 exit 1 when claim lost"
assert_eq "$(grep -c 'amending-claim' "$FC_LOG")" "1" "T2 claim was attempted"
assert_eq "$(wc -l <"$CO_LOG" | tr -d ' ')" "0" "T2 checkout NEVER called (touched nothing)"
assert_eq "$(grep -c 'amending-release' "$FC_LOG" || true)" "0" \
    "T2 nothing to release (claim was never held)"

# === T3: claim won + checkout fails → release the claim ====================
echo "T3: claim won + checkout fails → exit 1, claim released"
reset_logs
set +e
FAKE_CLAIM_RC=0 FAKE_CHECKOUT_RC=1 "$WRAPPER" 123 opus-worker-2 >"$TMPROOT/t3.log" 2>&1
t3_rc=$?
set -e
assert_eq "$t3_rc" "1" "T3 exit 1 when checkout fails"
assert_eq "$(grep -c 'amending-claim 123 opus-worker-2' "$FC_LOG")" "1" "T3 claim acquired"
assert_eq "$(grep -c '123' "$CO_LOG")" "1" "T3 checkout was attempted"
assert_eq "$(grep -c 'amending-release 123 opus-worker-2' "$FC_LOG")" "1" \
    "T3 claim released exactly once (no dangling fleet:amending-*)"

# === T4: game repo → namespace + slug translation =========================
echo "T4: --repo jakildev/irreden → game namespace + slug"
reset_logs
FAKE_CLAIM_RC=0 FAKE_CHECKOUT_RC=0 "$WRAPPER" 45 opus-worker-2 --repo jakildev/irreden \
    >"$TMPROOT/t4.log" 2>&1
assert_eq "$?" "0" "T4 exit 0 on game-repo success"
assert_eq "$(grep -c '\-\-repo game amending-claim 45 opus-worker-2' "$FC_LOG")" "1" \
    "T4 fleet-claim got --repo game namespace before subcommand"
assert_eq "$(grep -c '45 --repo jakildev/irreden' "$CO_LOG")" "1" \
    "T4 checkout got --repo jakildev/irreden slug"

# === T5: usage errors → exit 2, no claim attempted ========================
echo "T5: usage errors → exit 2, nothing claimed"
for args in "abc opus-worker-2" "123" "123 opus-worker-2 --repo foo/bar"; do
    reset_logs
    set +e
    # shellcheck disable=SC2086
    "$WRAPPER" $args >"$TMPROOT/t5.log" 2>&1
    rc=$?
    set -e
    assert_eq "$rc" "2" "T5 '$args' → exit 2"
    assert_eq "$(wc -l <"$FC_LOG" | tr -d ' ')" "0" "T5 '$args' attempted no claim"
done

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
