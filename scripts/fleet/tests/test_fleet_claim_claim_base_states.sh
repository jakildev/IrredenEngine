#!/usr/bin/env bash
# Tests for fleet-claim claim-base's three resolution states (#2703).
#
# claim-base used to read only the $CLAIMS_DIR/<slug>.meta sidecar and fall
# through to a bare `echo master` when it was absent — so a --stackable-on task
# whose claim was TTL-swept mid-iteration silently reported "master", opening a
# PR that swallows the whole blocker branch.
#
# __remove_claim deletes the claim dir and the sidecar together, so the claim
# dir discriminates the ambiguous fallthrough:
#
#   dir + sidecar   → stackable base   (exit 0, no warning)
#   dir, no sidecar → "master"         (exit 0, no warning — affirmative)
#   no dir          → "master" + stderr warning (exit 0), or exit 1 under
#                     --strict
#
# Purely local: claim-base touches no network, so this suite needs no gh stub.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

BLOCKER_BRANCH="claude/2547-depth-aware-camera-center-focus"

# Capture into globals rather than packing into one string: the stderr advisory
# is multi-line, and `cut -d<sep>` is line-oriented, so a packed form would
# split wrongly.
OUT=""
ERR=""
RC=0
run_claim_base() {
    set +e
    OUT=$("$FLEET_CLAIM" claim-base "$@" 2>"$TMPROOT/stderr.txt")
    RC=$?
    set -e
    ERR=$(cat "$TMPROOT/stderr.txt")
}

echo "--- state 1: claim dir + --stackable-on sidecar → recorded base ---"
mkdir -p "$FLEET_CLAIMS_DIR/2548"
echo "stackable_base_branch=$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/2548.meta"
run_claim_base 2548
assert_eq "$OUT" "$BLOCKER_BRANCH" "stackable claim prints the recorded base"
assert_eq "$RC" "0" "stackable claim exits 0"
assert_eq "$ERR" "" "stackable claim warns nothing"

echo "--- state 2: claim dir, no sidecar → affirmative master, silent ---"
mkdir -p "$FLEET_CLAIMS_DIR/2703"
run_claim_base 2703
assert_eq "$OUT" "master" "normal active claim prints master"
assert_eq "$RC" "0" "normal active claim exits 0"
assert_eq "$ERR" "" "normal active claim warns nothing (no false alarm)"

echo "--- state 3: no claim dir → master on stdout, warning on stderr ---"
run_claim_base 9999
assert_eq "$OUT" "master" "unknown state still prints master on stdout (non-breaking)"
assert_eq "$RC" "0" "unknown state still exits 0 without --strict"
assert_contains "$ERR" "UNVERIFIED" "unknown state warns on stderr"
assert_contains "$ERR" "9999" "warning names the issue"

echo "--- state 3 regression: the swept --stackable-on claim (#2548 incident) ---"
# Exactly the observed failure: a stackable claim whose dir + sidecar were
# swept together by __remove_claim while the task was still in flight.
rm -rf "$FLEET_CLAIMS_DIR/2548" "$FLEET_CLAIMS_DIR/2548.meta"
run_claim_base 2548
assert_eq "$OUT" "master" "swept stackable claim falls back to master on stdout"
assert_contains "$ERR" "UNVERIFIED" "swept stackable claim is no longer SILENT"
assert_contains "$ERR" "swallow the blocker branch" "warning names the concrete harm"

echo "--- --strict fails closed only in the unknown state ---"
run_claim_base 2548 --strict
assert_eq "$RC" "1" "--strict exits 1 when the base is unverifiable"
assert_eq "$OUT" "" "--strict prints nothing to stdout when it fails closed"

mkdir -p "$FLEET_CLAIMS_DIR/2548"
echo "stackable_base_branch=$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/2548.meta"
run_claim_base 2548 --strict
assert_eq "$OUT" "$BLOCKER_BRANCH" "--strict is transparent when the sidecar is present"
assert_eq "$RC" "0" "--strict exits 0 when the sidecar is present"

mkdir -p "$FLEET_CLAIMS_DIR/2704"
run_claim_base 2704 --strict
assert_eq "$OUT" "master" "--strict is transparent for an active normal claim"
assert_eq "$RC" "0" "--strict exits 0 for an active normal claim"

echo "--- an unrecognized option is rejected, not ignored ---"
run_claim_base 2704 --stict
assert_eq "$RC" "2" "typo'd flag exits 2 rather than silently guessing"
assert_contains "$ERR" "unknown option" "typo'd flag names itself"

echo "--- game namespace keeps its own slug ---"
# The engine slug for 2548 exists (above); the game claim must not read it.
rm -rf "$FLEET_CLAIMS_DIR/game-2548" "$FLEET_CLAIMS_DIR/game-2548.meta"
set +e
GAME_OUT=$("$FLEET_CLAIM" --repo game claim-base 2548 2>"$TMPROOT/stderr.txt")
set -e
assert_eq "$GAME_OUT" "master" "game #2548 does not read the engine slug's sidecar"
assert_contains "$(cat "$TMPROOT/stderr.txt")" "UNVERIFIED" "game #2548 with no claim warns"

summarize
