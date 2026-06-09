#!/usr/bin/env bash
# Regression test for #1578: fleet-claim FLEET_LIB_DIR mis-resolves when
# invoked via a ~/bin symlink, causing silent exit 1 on claim/stack/claim-base.
#
# Before the fix, BASH_SOURCE[0] was the symlink path (e.g. ~/bin/fleet-claim),
# so FLEET_LIB_DIR resolved to the symlink's parent dir (~/bin), which doesn't
# contain fleet_branch_match.py. The post-fix defensive check at the top of the
# script fires with a clear error when the lib dir is wrong — even for `list`
# (which doesn't use fleet_branch_match directly) — so `list` via symlink is a
# clean regression canary: exit 0 means the symlink was resolved correctly.
#
# The fix: resolve the symlink chain with a `while -L / readlink` loop before
# computing FLEET_LIB_DIR, matching the pattern already used by fleet-run/fleet-build.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 0
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then
        ok "$msg"
    else
        fail "$msg (expected exit $expected, got $actual)"
    fi
}

# --- T1: direct invocation of 'list' succeeds --------------------------------
# Baseline: ensures fleet-claim list exits 0 from the real path.
echo "T1: direct invocation -> 'list' exits 0"
FLEET_CLAIMS_DIR="$TMPROOT/claims1" "$FLEET_CLAIM" list >/dev/null 2>&1
assert_exit $? 0 "fleet-claim list via real path exits 0"

# --- T2: symlink invocation of 'list' exits 0 --------------------------------
# The defensive check at the top of fleet-claim verifies FLEET_LIB_DIR contains
# fleet_branch_match.py. A symlink in a dir without that file would fail the
# check and exit 1 — so exit 0 here proves FLEET_LIB_DIR was resolved to the
# real script dir (scripts/fleet/), not the symlink's parent dir.
echo "T2: symlink invocation -> 'list' still exits 0 (FLEET_LIB_DIR resolved correctly)"
SYMLINK_DIR="$TMPROOT/bin"
mkdir -p "$SYMLINK_DIR"
ln -sf "$FLEET_CLAIM" "$SYMLINK_DIR/fleet-claim"
FLEET_CLAIMS_DIR="$TMPROOT/claims2" "$SYMLINK_DIR/fleet-claim" list >/dev/null 2>&1
assert_exit $? 0 "fleet-claim list via symlink exits 0 (lib dir resolved through symlink)"

# --- T3: defensive check fires visibly when lib dir is wrong -----------------
# Simulates what happens if the symlink fix is absent: the lib dir would point
# to an empty directory that lacks fleet_branch_match.py.
echo "T3: missing fleet_branch_match.py -> visible error on stderr, not silent exit 1"
FAKE_LIB="$TMPROOT/fake-lib"
mkdir -p "$FAKE_LIB"
FAKE_CLAIM="$FAKE_LIB/fleet-claim"
# Write a minimal stub that mirrors only the defensive check block:
cat > "$FAKE_CLAIM" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
# Simulate wrong FLEET_LIB_DIR (no readlink resolution, points to the stub's dir)
FLEET_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export FLEET_LIB_DIR
if [[ ! -f "$FLEET_LIB_DIR/fleet_branch_match.py" ]]; then
    echo "fleet-claim: fleet_branch_match.py not found in FLEET_LIB_DIR=$FLEET_LIB_DIR" >&2
    echo "             Ensure fleet-claim's real script dir is scripts/fleet/ in the engine repo." >&2
    exit 1
fi
echo "ok"
STUB
chmod +x "$FAKE_CLAIM"
# Create a symlink from a dir that has no fleet_branch_match.py:
FAKE_BIN="$TMPROOT/fake-bin"
mkdir -p "$FAKE_BIN"
ln -sf "$FAKE_CLAIM" "$FAKE_BIN/fleet-claim"
STDERR_OUT=$("$FAKE_BIN/fleet-claim" 2>&1 || true)
if echo "$STDERR_OUT" | grep -q 'fleet_branch_match.py not found'; then
    ok "bad FLEET_LIB_DIR -> visible error on stderr"
else
    fail "bad FLEET_LIB_DIR did not produce visible error; got: $STDERR_OUT"
fi

# --- T4: 'reservation-of' subcommand (no fleet_branch_match) works via symlink
# Belt-and-suspenders: a subcommand that never uses fleet_branch_match should
# also reach dispatch cleanly when invoked via a symlink.
echo "T4: reservation-of via symlink -> exits 0 (dispatch reached)"
SYMLINK4_DIR="$TMPROOT/bin4"
mkdir -p "$SYMLINK4_DIR"
ln -sf "$FLEET_CLAIM" "$SYMLINK4_DIR/fleet-claim"
FLEET_CLAIMS_DIR="$TMPROOT/claims4" \
FLEET_RESERVATIONS_DIR="$TMPROOT/res4" \
    "$SYMLINK4_DIR/fleet-claim" reservation-of no-such-worktree >/dev/null 2>&1
assert_exit $? 0 "reservation-of via symlink exits 0"

# --- summary ------------------------------------------------------------------
echo ""
if [[ $FAIL -eq 0 ]]; then
    echo "PASS: $PASS  FAIL: $FAIL"
    exit 0
else
    echo "PASS: $PASS  FAIL: $FAIL"
    exit 1
fi
