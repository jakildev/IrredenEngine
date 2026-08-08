#!/usr/bin/env bash
# Tests for fleet-claim's dispatch-outcome marker (#2698).
#
# fleet-dispatch-wrap exports FLEET_CLAIM_FLAG (pane-keyed) and clears it at
# dispatch start; fleet-claim touches it on every arm that takes or releases a
# fleet lock; fleet-dispatcher reads it back once the pane returns to a shell
# and folds claimed-vs-empty into the empty-exit backoff streak. It replaces a
# wall-clock heuristic that could not tell a real claim from a diligent
# no-pick, which left the backoff dead for the worker role for 15 days.
#
# The properties that matter, and why each is here rather than left to a live
# observation:
#
#   - RELEASE-EARLY SAFE. The lock discipline is acquire-late/release-early, so
#     a productive iteration has usually released by the time it exits. Any
#     signal derived from live claim state reads `empty` for it. The marker is
#     a separate artifact from $CLAIMS_DIR, so it survives the release — that
#     is the assertion the prior plan's refuted claim-liveness design failed.
#   - SUCCESS-ONLY. A pane that tried to claim and lost did no work.
#   - `cleanup` EXCLUDED. It sweeps other agents' stale locks and is the
#     sanctioned zero-pick move, so stamping there would make every starved
#     iteration look productive and inarguably invert the fix.
#   - NO-OP OFF-DISPATCH. Interactive/architect use has no FLEET_CLAIM_FLAG.
#
# Hermetic: only GitHub-free arms are driven (reserve / release-worktree /
# reservation-of / molecule / cleanup-on-empty), against scratch
# FLEET_CLAIMS_DIR + FLEET_STATE_DIR.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -e "$CLAIM" ]]; then
    echo "SKIP: missing $CLAIM" >&2
    exit 3
fi

TMPROOT=""
cleanup_tmp() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup_tmp EXIT
TMPROOT=$(mktemp -d)

# fleet-claim keeps state in THREE dirs, each with its own override. Setting
# only FLEET_CLAIMS_DIR looks hermetic and is not: `reserve` writes to
# RESERVATIONS_DIR and `molecule` reads MOLECULES_DIR, so a partial override
# silently mutates the live fleet — an early draft of this suite left a real
# `pool-7` reservation behind, which a real pool-7 pane would have resumed at
# step 0.5. Override all three, and assert it below.
HERMETIC_DIRS=(claims molecules reservations)
FLAG=""
claim_run() {
    # usage: claim_run <scratch> <args...>
    local scratch="$1"; shift
    FLAG="$scratch/dispatch-claimed/pane-7"
    FLEET_CLAIMS_DIR="$scratch/claims" \
    FLEET_MOLECULES_DIR="$scratch/molecules" \
    FLEET_RESERVATIONS_DIR="$scratch/reservations" \
    FLEET_STATE_DIR="$scratch/state" \
    FLEET_CLAIM_FLAG="$FLAG" \
        "$CLAIM" "$@"
}

# Same, with no FLEET_CLAIM_FLAG in the environment (interactive use).
claim_run_undispatched() {
    local scratch="$1"; shift
    FLEET_CLAIMS_DIR="$scratch/claims" \
    FLEET_MOLECULES_DIR="$scratch/molecules" \
    FLEET_RESERVATIONS_DIR="$scratch/reservations" \
    FLEET_STATE_DIR="$scratch/state" \
        env -u FLEET_CLAIM_FLAG "$CLAIM" "$@"
}

# Fingerprint the live dirs so the run can prove it never touched them.
live_fingerprint() {
    local d
    for d in "${HERMETIC_DIRS[@]}"; do
        printf '%s:' "$d"
        ls -1 "$HOME/.fleet/$d" 2>/dev/null | sort | tr '\n' ','
        printf '\n'
    done
}
LIVE_BEFORE=$(live_fingerprint)

assert_stamped() {
    if [[ -f "$FLAG" ]]; then ok "$1"; else bad "$1"; echo "        no marker at: $FLAG"; fi
}
assert_not_stamped() {
    if [[ -f "$FLAG" ]]; then bad "$1"; echo "        unexpected marker at: $FLAG"; else ok "$1"; fi
}

echo "T1: an acquire arm stamps the marker"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
claim_run "$S" reserve 4242 pool-7 >/dev/null 2>&1
assert_stamped "reserve stamps"

echo "T2: the marker survives the release (acquire-late / release-early)"
# This is AC 1: an iteration that claims, works, and releases before exiting
# must still read `claimed`. A claim-liveness probe reads empty here, and
# wall-clock cannot distinguish it from a no-pick that ran just as long.
claim_run "$S" release-worktree pool-7 >/dev/null 2>&1
assert_stamped "marker outlives release-worktree"
assert_eq "$(claim_run "$S" reservation-of pool-7 2>/dev/null)" "" \
    "…and the reservation really is gone (the marker is not just a stale lock)"

echo "T3: a release arm stamps on its own"
# Load-bearing for the step-0.5 reservation resume and the molecule resume: a
# full task's work while acquiring nothing, because the lock was taken in a
# prior iteration. Without the release arms those iterations record `empty`.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
claim_run "$S" reserve 4243 pool-7 >/dev/null 2>&1
rm -f "$FLAG"   # simulate the next dispatch's start-of-run clear
claim_run "$S" release-worktree pool-7 >/dev/null 2>&1
assert_stamped "release-worktree alone stamps (resume-without-acquire case)"

echo "T3b: each release arm stamps, not just the one T2/T3 happened to use"
# Coverage gap this closes: T2/T3 drive `release-worktree`, and passing there
# says nothing about `release` / `release-stack` — different case arms with
# different cmd_* bodies, any of which could exit before reaching the stamp.
# Both are driven on state they do NOT hold, which is also the weaker case: a
# no-op release still stamps, so the arms are reached unconditionally.
for arm in "release 999999" "release-stack no-such-agent"; do
    S=$(mktemp -d "$TMPROOT/s.XXXXXX")
    rm -f "$FLAG"
    # shellcheck disable=SC2086
    claim_run "$S" $arm >/dev/null 2>&1
    assert_stamped "release arm stamps: $arm"
done

echo "T4: read-only arms do not stamp"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
for arm in "reservation-of pool-7" "list" "list-reservations" "worktree-for-task 4242"; do
    rm -f "$FLAG"
    # shellcheck disable=SC2086
    claim_run "$S" $arm >/dev/null 2>&1
    assert_not_stamped "read-only arm does not stamp: $arm"
done

echo "T5: cleanup is excluded — the sanctioned zero-pick move stays empty"
# AC 5. Run against an empty claims dir so the sweep completes locally with
# nothing to do (and no network), which is exactly a starved iteration's call.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
mkdir -p "$S/claims"
rm -f "$FLAG"
claim_run "$S" cleanup >/dev/null 2>&1
rc=$?
assert_eq "$rc" "0" "cleanup on an empty claims dir succeeds (so the check below is not vacuous)"
assert_not_stamped "cleanup does not stamp"

echo "T6: failure does not stamp (success-only)"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
rm -f "$FLAG"
claim_run "$S" reserve >/dev/null 2>&1
assert_not_stamped "reserve with missing args does not stamp"
# The one that matters: a pane that CONTENDED for a lock and lost did no work,
# and must still record empty. `set -euo pipefail` aborts before the stamp.
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
claim_run "$S" reserve 4245 pool-7 >/dev/null 2>&1   # first one wins
rm -f "$FLAG"
claim_run "$S" reserve 4246 pool-7 >/dev/null 2>&1   # second contends and loses
rc=$?
assert_eq "$rc" "1" "a contended reserve really does fail (so the check below is not vacuous)"
assert_not_stamped "a lost lock acquisition does not stamp"

echo "T7: no marker path in the environment is a no-op, not an error"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
claim_run_undispatched "$S" reserve 4244 pool-7 >/dev/null 2>&1
rc=$?
assert_eq "$rc" "0" "reserve still succeeds with FLEET_CLAIM_FLAG unset (interactive use)"

echo "T8: molecule — progress arms stamp, the resume query does not"
S=$(mktemp -d "$TMPROOT/s.XXXXXX")
rm -f "$FLAG"
claim_run "$S" molecule resume pool-7 >/dev/null 2>&1
assert_not_stamped "molecule resume (a query that legitimately returns nothing) does not stamp"

echo "T9: fidelity — every lock arm in the live case block is in the stamping set"
# The executable form of the plan-review finding: the stamping set was written
# from memory and omitted review-claim / steward-claim, which would have stood
# the reviewer and steward lanes down after EMPTY_STREAK_CAP *productive*
# dispatches. Enumerate from the source instead of trusting a hand list, so a
# newly added lock arm fails here rather than silently dropping out.
#
# The dispatch case arms are indented four spaces; the trailing stamp block is
# everything after the `_stamp_dispatch_outcome()` definition.
stamp_block=$(sed -n '/^_stamp_dispatch_outcome()/,$p' "$CLAIM")
lock_arms=$(grep -oE '^    (claim|release|stack|release-stack|reserve|release-worktree|[a-z-]+-(claim|release))\)' "$CLAIM" \
    | tr -d ' )' | sort -u)
# 8 acquire (claim, stack, reserve, {review,resolving,amending,steward,planning}-claim)
# + 8 release (release, release-stack, release-worktree, {review,resolving,
# amending,steward,planning}-release).
assert_eq "$(printf '%s\n' "$lock_arms" | grep -c .)" "16" \
    "enumerated the expected number of lock arms (update this suite when one is added)"
missing=""
while read -r arm; do
    [[ -n "$arm" ]] || continue
    printf '%s' "$stamp_block" | grep -qE "(^|[|\\\\ ])${arm}\)?([|\\\\]|\$| )" \
        || missing="${missing:+$missing }$arm"
done <<< "$lock_arms"
assert_eq "$missing" "" "every acquire/release arm appears in the stamping set"

# And the exclusions are deliberate, not omissions.
for excluded in cleanup reconcile clear-all reset-sweep-host-claims; do
    assert_absent "$(printf '%s' "$stamp_block" | grep -E "^ +${excluded}\)" || true)" \
        "$excluded)" "$excluded is NOT in the stamping set"
done

echo "T10: the marker path agrees across the three scripts that share it"
# scripts/fleet/CLAUDE.md: an inlined duplicate ships with a drift guard, or
# the duplication is pure cost. CLAIM_FLAG_DIR is spelled independently in
# fleet-dispatch-wrap (which exports FLEET_CLAIM_FLAG) and fleet-dispatcher
# (which reads the marker back). If the two ever disagree, the wrapper clears
# and the agent stamps one path while the dispatcher reads another — every
# dispatch would then record `empty` and every lane would stand down after
# EMPTY_STREAK_CAP productive dispatches, silently.
wrap_dir=$(grep -E '^CLAIM_FLAG_DIR=' "$SCRIPT_DIR/fleet-dispatch-wrap" | head -n 1 | sed 's/[[:space:]]*#.*//')
disp_dir=$(grep -E '^CLAIM_FLAG_DIR=' "$SCRIPT_DIR/fleet-dispatcher" | head -n 1 | sed 's/[[:space:]]*#.*//')
assert_eq "$wrap_dir" "$disp_dir" "CLAIM_FLAG_DIR agrees between fleet-dispatch-wrap and fleet-dispatcher"
assert_contains "$wrap_dir" 'CLAIM_FLAG_DIR="$STATE_DIR/dispatch-claimed"' \
    "…and is the expected STATE_DIR-relative path (not an absolute or \$HOME-relative spelling)"
# The wrapper must key the marker by PANE_KEY, which is what the dispatcher
# reconstructs via pane_id_to_key when folding the outcome.
assert_contains "$(grep -E 'export FLEET_CLAIM_FLAG=' "$SCRIPT_DIR/fleet-dispatch-wrap")" \
    '$CLAIM_FLAG_DIR/$PANE_KEY' "the wrapper keys the marker by PANE_KEY"

echo "T11: the suite itself stayed hermetic"
# Not a formality. fleet-claim has three state dirs behind three separate
# overrides; miss one and the suite mutates the live fleet while every
# assertion still passes. This is the check that fails instead.
assert_eq "$(live_fingerprint)" "$LIVE_BEFORE" \
    "live ~/.fleet claims/molecules/reservations unchanged by this run"

summarize "fleet-claim dispatch-marker tests"
