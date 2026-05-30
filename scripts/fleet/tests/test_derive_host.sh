#!/usr/bin/env bash
# Tests for fleet-claim's host taxonomy (derive_host / host_from_uname).
#
# #1383 unified the WSL2 host key. derive_host once mapped WSL2 (uname Linux +
# /proc/version "microsoft") to "windows", disagreeing with the smoke / build /
# authored-on detectors, which all read raw `uname -s` and call WSL2 "linux".
# These tests pin the unified mapping through the `fleet-claim host
# [uname-string]` seam so they pass on any runner OS (no /proc/version or live
# `uname` dependency).
#
# Covers:
#   - Linux (WSL2 and native) → linux   (the #1383 regression pin)
#   - Darwin → mac
#   - MINGW*/MSYS*/CYGWIN* (native Windows) → windows
#   - unrecognized uname → unknown
#   - FLEET_TEST_HOST override takes precedence (test-scaffolding contract)
#   - derive_host (no arg) delegates to host_from_uname with the real uname

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
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

# Don't let an ambient override leak in from the runner's environment.
unset FLEET_TEST_HOST

# --- T1: Linux (WSL2 + native) → linux --------------------------------------
echo "T1: uname Linux → linux (WSL2 and native unified)"
assert_eq "$("$FLEET_CLAIM" host Linux)" "linux" \
    "uname Linux maps to linux (#1383: previously windows on WSL2)"

# --- T2: Darwin → mac -------------------------------------------------------
echo "T2: uname Darwin → mac"
assert_eq "$("$FLEET_CLAIM" host Darwin)" "mac" "uname Darwin maps to mac"

# --- T3: native Windows uname variants → windows ----------------------------
echo "T3: native Windows uname variants → windows"
assert_eq "$("$FLEET_CLAIM" host MINGW64_NT-10.0-19045)" "windows" "MINGW* → windows"
assert_eq "$("$FLEET_CLAIM" host MSYS_NT-10.0)" "windows" "MSYS* → windows"
assert_eq "$("$FLEET_CLAIM" host CYGWIN_NT-10.0)" "windows" "CYGWIN* → windows"

# --- T4: unrecognized uname → unknown ---------------------------------------
echo "T4: unrecognized uname → unknown"
assert_eq "$("$FLEET_CLAIM" host FreeBSD)" "unknown" "unrecognized uname → unknown"

# --- T5: FLEET_TEST_HOST override precedence --------------------------------
echo "T5: FLEET_TEST_HOST overrides the real uname"
assert_eq "$(FLEET_TEST_HOST=mac "$FLEET_CLAIM" host)" "mac" \
    "FLEET_TEST_HOST=mac wins over real uname"

# --- T6: derive_host delegates to host_from_uname with the real uname -------
echo "T6: no-arg host == host(uname -s) (delegation wiring)"
assert_eq "$("$FLEET_CLAIM" host)" "$("$FLEET_CLAIM" host "$(uname -s)")" \
    "derive_host maps the live uname through host_from_uname"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
