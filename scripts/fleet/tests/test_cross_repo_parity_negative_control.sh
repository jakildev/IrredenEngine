#!/usr/bin/env bash
# Committed negative control for test_cross_repo_shared_file_parity.sh: drives
# that suite against two throwaway fixture repos through green → drift → green,
# so its red arm is a repeatable assertion rather than session prose (#2827).
#
# The fixtures set refs/remotes/origin/master with update-ref and never talk to
# a network: the suite under test compares origin/master blobs via `git show`,
# so a plain commit plus that ref is the whole dependency. Both repo roots come
# from FLEET_ENGINE_ROOT / FLEET_GAME_ROOT, which exist for exactly this.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

SUBJECT="$SCRIPT_DIR/tests/test_cross_repo_shared_file_parity.sh"
if [[ ! -f "$SUBJECT" ]]; then
    echo "SKIP: subject not present at $SUBJECT" >&2
    exit 3  # skip status — a missing subject is never a pass (#2786)
fi

TMPROOT=$(mktemp -d)
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT

# Mirrors the subject's CONTRACT_FILES. The second entry is what proves a
# drift verdict is file-specific rather than blanket.
DRIFT_PATH=".github/workflows/auto-rereview.yml"
STABLE_PATH="scripts/fleet/classify-auto-rereview.sh"

make_fixture_repo() {
    local root="$1"
    mkdir -p "$root"
    git -C "$root" init -q
    git -C "$root" config user.email fixture@example.invalid
    git -C "$root" config user.name "parity fixture"
    for p in "$DRIFT_PATH" "$STABLE_PATH"; do
        mkdir -p "$root/$(dirname "$p")"
        printf 'shared contract content for %s\n' "$p" >"$root/$p"
    done
    git -C "$root" add -A
    git -C "$root" commit -qm "fixture: shared contract files"
    git -C "$root" update-ref refs/remotes/origin/master HEAD
}

# Commit the fixture's current worktree and re-point origin/master at it —
# the suite reads only the ref, so an uncommitted edit is invisible to it.
land_fixture_commit() {
    local root="$1" msg="$2"
    git -C "$root" add -A
    git -C "$root" commit -qm "$msg"
    git -C "$root" update-ref refs/remotes/origin/master HEAD
}

run_subject() {
    FLEET_ENGINE_ROOT="$TMPROOT/engine" FLEET_GAME_ROOT="$TMPROOT/game" \
        bash "$SUBJECT" 2>&1
}

make_fixture_repo "$TMPROOT/engine"
make_fixture_repo "$TMPROOT/game"

# --- green arm: identical contract files on both sides ------------------
out=$(run_subject) && rc=0 || rc=$?
assert_eq "$rc" "0" "green arm: identical fixtures exit 0"
assert_contains "$out" "$DRIFT_PATH: byte-identical" "green arm: workflow reported byte-identical"
assert_contains "$out" "$STABLE_PATH: byte-identical" "green arm: classifier reported byte-identical"
assert_absent "$out" "DRIFTED" "green arm: nothing reported as drifted"

# --- red arm: one byte differs on one file ------------------------------
printf 'x\n' >>"$TMPROOT/game/$DRIFT_PATH"
land_fixture_commit "$TMPROOT/game" "fixture: one-sided edit"

out=$(run_subject) && rc=0 || rc=$?
if [[ "$rc" -ne 0 ]]; then
    ok "red arm: one-byte drift exits non-zero"
else
    bad "red arm: one-byte drift exits non-zero (got $rc)"
fi
assert_contains "$out" "$DRIFT_PATH: DRIFTED" "red arm: names the drifted file exactly"
# The un-mutated sibling must still pass — a blanket red would satisfy the
# exit-code assertion above while telling a reader nothing about which file
# broke the contract.
assert_contains "$out" "$STABLE_PATH: byte-identical" "red arm: un-mutated sibling still passes"
assert_absent "$out" "$STABLE_PATH: DRIFTED" "red arm: verdict is file-specific, not blanket"

# --- restore arm: the red is caused by the drift, not by the fixture ----
printf 'shared contract content for %s\n' "$DRIFT_PATH" >"$TMPROOT/game/$DRIFT_PATH"
land_fixture_commit "$TMPROOT/game" "fixture: restore the drifted byte"

out=$(run_subject) && rc=0 || rc=$?
assert_eq "$rc" "0" "restore arm: reverting the drift returns to exit 0"
assert_absent "$out" "DRIFTED" "restore arm: no file reported as drifted"

# --- skip arm: absent downstream repo is a skip, never a vacuous pass ---
out=$(FLEET_ENGINE_ROOT="$TMPROOT/engine" FLEET_GAME_ROOT="$TMPROOT/nonexistent" \
    bash "$SUBJECT" 2>&1) && rc=0 || rc=$?
assert_eq "$rc" "3" "skip arm: missing downstream repo exits 3, not 0 (#2786)"
assert_contains "$out" "SKIP: downstream repo not present" "skip arm: prints the SKIP reason"

summarize "cross-repo parity negative control"
