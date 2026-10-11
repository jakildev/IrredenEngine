#!/usr/bin/env bash
# claim-base's local and live resolution states. __remove_claim deletes the claim dir
# and the $CLAIMS_DIR/<slug>.meta sidecar together, so the claim dir
# discriminates a swept --stackable-on claim from a normal one rather than
# both silently falling through to the same "master" report:
#
#   dir + sidecar   → stackable base   (exit 0, no warning)
#   dir, no sidecar → "master"         (exit 0, no warning — affirmative)
#   no dir          → "master" + stderr warning (exit 0), or exit 1 under
#                     --strict
#
# Legacy one-line sidecars and no-sidecar states stay network-free. A sidecar
# carrying stackable_pr resolves that PR live through the fail-closed stub.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-} ${2:-}" == "pr view" ]] || exit 97
case "${3:-}" in
    8101) printf '%s\n' '{"state":"OPEN","headRefName":"claude/live-open","headRefOid":"1111111"}' ;;
    8102) printf '%s\n' '{"state":"MERGED","headRefName":"claude/live-merged","headRefOid":"2222222"}' ;;
    8103) printf '%s\n' '{"state":"CLOSED","headRefName":"claude/live-closed","headRefOid":"3333333"}' ;;
    8104) exit 1 ;;
    *) exit 98 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

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

echo "--- state 3 regression: a --stackable-on claim swept mid-iteration ---"
# A stackable claim whose dir + sidecar were swept together by __remove_claim
# while the task was still in flight.
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

echo "--- live sidecar: OPEN keeps the recorded branch ---"
mkdir -p "$FLEET_CLAIMS_DIR/8101"
printf 'stackable_base_branch=%s\nstackable_pr=8101\n' "$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/8101.meta"
run_claim_base 8101
assert_eq "$OUT" "$BLOCKER_BRANCH" "OPEN blocker keeps the recorded base"
assert_eq "$RC" "0" "OPEN blocker exits 0"
assert_eq "$ERR" "" "OPEN blocker is silent"

echo "--- live sidecar: MERGED repairs to master with rebase recipe ---"
mkdir -p "$FLEET_CLAIMS_DIR/8102"
printf 'stackable_base_branch=%s\nstackable_pr=8102\n' "$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/8102.meta"
run_claim_base 8102
assert_eq "$OUT" "master" "MERGED blocker resolves to master"
assert_eq "$RC" "0" "MERGED blocker exits 0"
assert_contains "$ERR" "blocker PR #8102 merged after the claim" "MERGED advisory names the blocker"
assert_contains "$ERR" "git rebase --onto origin/master 2222222" "MERGED advisory carries the parent head sha"

echo "--- live sidecar: CLOSED-unmerged fails closed ---"
mkdir -p "$FLEET_CLAIMS_DIR/8103"
printf 'stackable_base_branch=%s\nstackable_pr=8103\n' "$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/8103.meta"
run_claim_base 8103
assert_eq "$OUT" "" "CLOSED blocker prints no base"
assert_eq "$RC" "1" "CLOSED blocker exits non-zero"
assert_contains "$ERR" "closed without merging" "CLOSED blocker states the reason"
run_claim_base 8103 --strict
assert_eq "$OUT" "" "strict CLOSED blocker prints no base"
assert_eq "$RC" "1" "strict CLOSED blocker exits non-zero"

echo "--- live sidecar: lookup failure soft-degrades unless strict ---"
mkdir -p "$FLEET_CLAIMS_DIR/8104"
printf 'stackable_base_branch=%s\nstackable_pr=8104\n' "$BLOCKER_BRANCH" > "$FLEET_CLAIMS_DIR/8104.meta"
run_claim_base 8104
assert_eq "$OUT" "$BLOCKER_BRANCH" "failed lookup keeps the recorded base"
assert_eq "$RC" "0" "failed lookup is non-fatal by default"
assert_contains "$ERR" "UNVERIFIED" "failed lookup warns"
run_claim_base 8104 --strict
assert_eq "$OUT" "" "strict failed lookup prints no base"
assert_eq "$RC" "1" "strict failed lookup exits non-zero"

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
