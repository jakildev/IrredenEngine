#!/usr/bin/env bash
# Classify a force-push to a fleet:approved PR as a mechanical rebase (net
# content unchanged) vs a real content change, so the auto-rereview workflow
# only strips fleet:approved when the diff actually changed.
#
# Reads three commit SHAs from the environment (the synchronize-event payload):
#   BEFORE   — the PR head before the push (github.event.before). Orphaned by a
#              force-push, so it may be unfetchable.
#   AFTER    — the PR head after the push (github.event.after).
#   BASE_SHA — the PR's *current* base tip (github.event.pull_request.base.sha),
#              i.e. the base AFTER any retarget performed by the same operation.
#
# Prints exactly one line to stdout:  rebase_only=true | rebase_only=false
# Diagnostics go to stderr (surfaced in the Actions log). The decision is the
# stdout, not the exit status — exit is 0 on every decided path; a non-zero
# exit means the environment was malformed.
#
# Why we cannot just diff both tips against BASE_SHA (the game #229 / engine
# #2290 bug):
#   A stacked child PR retargeted onto master after its parent *squash-merges*
#   loses the parent's original commits from history. merge-base(BASE_SHA,
#   BEFORE) then falls back past the parent's fork point, so diff(mb, BEFORE)
#   wrongly folds the parent's entire diff into the child's net diff and the
#   before/after patch-ids differ even though the child's own change is
#   byte-identical — reproducing exactly the "mechanical rebase strips
#   fleet:approved" bug the guard exists to prevent.
#
# We recover the child's pre-retarget base *structurally* instead of persisting
# cross-event state:
#   * AFTER is the child replayed directly onto the current base, so the child's
#     own commit count is N = commits in BASE_SHA..AFTER.
#   * BEFORE's own base is therefore BEFORE~N — the commit just under BEFORE's
#     top N commits, which is the old parent tip whatever it was.
#   Diffing each tip against its *own* base yields the child's net diff on both
#   sides, which patch-id-compares equal for a mechanical rebase and unequal for
#   a real edit — with no dependence on the parent's commits still existing, and
#   no edited-event marker racing the synchronize handler.

set -uo pipefail

: "${BEFORE:?BEFORE (github.event.before) required}"
: "${AFTER:?AFTER (github.event.after) required}"
: "${BASE_SHA:?BASE_SHA (github.event.pull_request.base.sha) required}"

emit() { echo "rebase_only=$1"; }  # stdout: the decision, nothing else

# Best-effort: make the commits we reason about present locally. The workflow
# checkout brings AFTER + the base tip; BEFORE is orphaned by the force-push and
# may or may not be fetchable. A missing origin (e.g. the hermetic test sandbox)
# fails closed to the local objects we already have.
git fetch --no-tags --quiet origin "$AFTER" "$BASE_SHA" 2>/dev/null || true
git cat-file -e "${BEFORE}^{commit}" 2>/dev/null \
  || git fetch --no-tags --quiet origin "$BEFORE" 2>/dev/null || true

# Net content of a tip against an explicit base, hashed by patch-id (stable
# across rebase/SHA churn). Empty stdout => "unknown", handled by the caller.
net_patch_id() {  # $1 = base  $2 = tip
  git diff "$1" "$2" | git patch-id --stable | awk '{print $1}'
}

if ! git cat-file -e "${BEFORE}^{commit}" 2>/dev/null; then
  echo "before commit $BEFORE unavailable — cannot prove a rebase; re-reviewing." >&2
  emit false
  exit 0
fi

# The child's own commit count, measured on the side that is cleanly based on
# the current base (AFTER was just replayed onto BASE_SHA).
N=$(git rev-list --count "$BASE_SHA..$AFTER" 2>/dev/null) || N=""
if [ -z "$N" ] || [ "$N" -eq 0 ]; then
  echo "AFTER has no commits above the current base (N='${N:-<empty>}') — re-reviewing." >&2
  emit false
  exit 0
fi

# BEFORE's own base = the commit just under its top N commits (the pre-retarget
# parent tip, recovered structurally). If BEFORE has fewer than N ancestors,
# BEFORE~N does not resolve and we conservatively re-review.
OLD_BASE=$(git rev-parse --verify --quiet "${BEFORE}~${N}^{commit}") || OLD_BASE=""
if [ -z "$OLD_BASE" ]; then
  echo "BEFORE has fewer than N=$N commits — cannot recover pre-retarget base; re-reviewing." >&2
  emit false
  exit 0
fi

OLD_ID=$(net_patch_id "$OLD_BASE" "$BEFORE")
NEW_ID=$(net_patch_id "$BASE_SHA" "$AFTER")
echo "old net patch-id: '${OLD_ID:-<empty>}' (base ${OLD_BASE})" >&2
echo "new net patch-id: '${NEW_ID:-<empty>}' (base ${BASE_SHA})" >&2

if [ -n "$OLD_ID" ] && [ "$OLD_ID" = "$NEW_ID" ]; then
  echo "Net content diff unchanged — mechanical rebase. Skipping re-review." >&2
  emit true
else
  echo "Net content diff changed — real update. Re-reviewing." >&2
  emit false
fi
