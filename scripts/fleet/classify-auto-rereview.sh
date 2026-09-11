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
# Prints one or two lines to stdout (each `key=value`, routed to
# $GITHUB_OUTPUT):
#   rebase_only=true                  — net content unchanged (mechanical)
#   rebase_only=false
#   docs_only=true|false              — only with rebase_only=false: true when
#                                       every file whose NET content changed is
#                                       non-canon documentation (*.md outside
#                                       docs/design/ and the game design tiers,
#                                       docs/pr-screenshots/**)
#                                       — the workflow keeps fleet:approved for
#                                       that class instead of burning a full
#                                       re-review round on a wording delta
#                                       (measured: ~2/3 of re-review rounds
#                                       changed nothing; see the review-cycle
#                                       economics section of REVIEWER-PROTOCOL).
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

emit() { echo "rebase_only=$1"; }       # stdout: the decision, nothing else
emit_docs() { echo "docs_only=$1"; }    # second decision line (false-paths only)

# Non-canon documentation predicate for the docs-only delta class. Canon design
# docs re-review like code — their accuracy IS the product: the engine's
# docs/design/**, and the game repo's design tiers (GDD.md and irreden/docs/*
# outside irreden/docs/dev/). This script is byte-identical across both repos,
# so both repos' canon paths are listed here; each pattern is inert in the repo
# that doesn't have it.
is_docs_path() {  # $1 = repo-relative path
  case "$1" in
    docs/design/*) return 1 ;;
    GDD.md|*/GDD.md) return 1 ;;
    irreden/docs/dev/*) : ;;      # game dev docs: not canon — fall through
    irreden/docs/*) return 1 ;;
  esac
  case "$1" in
    docs/pr-screenshots/*) return 0 ;;
    *.md) return 0 ;;
    *) return 1 ;;
  esac
}

# docs_only decision for a real (non-mechanical) update: compare the two net
# diffs PER FILE (patch-id, rebase-invariant) so rebase noise from master never
# counts, and require every net-changed file to satisfy is_docs_path. Empty
# changed set (rename edge cases) is conservative: false.
docs_only_delta() {
  local f o n all any=false
  all=$( { git diff --name-only "$OLD_BASE" "$BEFORE"
           git diff --name-only "$BASE_SHA" "$AFTER"; } | sort -u )
  if [ -z "$all" ]; then echo false; return; fi
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    o=$(git diff "$OLD_BASE" "$BEFORE" -- "$f" | git patch-id --stable | awk '{print $1}')
    n=$(git diff "$BASE_SHA" "$AFTER" -- "$f" | git patch-id --stable | awk '{print $1}')
    [ "$o" = "$n" ] && continue
    any=true
    if ! is_docs_path "$f"; then echo false; return; fi
    echo "net-changed docs path: $f" >&2
  done <<EOF_FILES
$all
EOF_FILES
  if [ "$any" = true ]; then echo true; else echo false; fi
}

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
  emit_docs false
  exit 0
fi

# Fast-forward push (BEFORE is an ancestor of AFTER): commits were added on
# top with the base untouched — the dominant nit-fix / follow-up-commit shape.
# Not a mechanical rebase by construction, and the delta is exactly
# BEFORE..AFTER, so the docs-only decision reads it directly instead of going
# through the BEFORE~N base recovery below (which assumes a count-preserving
# force-push and mis-anchors when commits were added).
if git merge-base --is-ancestor "$BEFORE" "$AFTER" 2>/dev/null; then
  echo "fast-forward push — delta is BEFORE..AFTER." >&2
  emit false
  DOCS_ONLY=true
  ANY_CHANGED=false
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    ANY_CHANGED=true
    if ! is_docs_path "$f"; then DOCS_ONLY=false; break; fi
    echo "changed docs path: $f" >&2
  done <<EOF_FF
$(git diff --name-only "$BEFORE" "$AFTER")
EOF_FF
  [ "$ANY_CHANGED" = true ] || DOCS_ONLY=false
  if [ "$DOCS_ONLY" = true ]; then
    echo "Every changed file is non-canon documentation — keeping approval." >&2
  else
    echo "Delta reaches code or canon docs — re-reviewing." >&2
  fi
  emit_docs "$DOCS_ONLY"
  exit 0
fi

# The child's own commit count, measured on the side that is cleanly based on
# the current base (AFTER was just replayed onto BASE_SHA).
N=$(git rev-list --count "$BASE_SHA..$AFTER" 2>/dev/null) || N=""
if [ -z "$N" ] || [ "$N" -eq 0 ]; then
  echo "AFTER has no commits above the current base (N='${N:-<empty>}') — re-reviewing." >&2
  emit false
  emit_docs false
  exit 0
fi

# BEFORE's own base = the commit just under its top N commits (the pre-retarget
# parent tip, recovered structurally). If BEFORE has fewer than N ancestors,
# BEFORE~N does not resolve and we conservatively re-review.
OLD_BASE=$(git rev-parse --verify --quiet "${BEFORE}~${N}^{commit}") || OLD_BASE=""
if [ -z "$OLD_BASE" ]; then
  echo "BEFORE has fewer than N=$N commits — cannot recover pre-retarget base; re-reviewing." >&2
  emit false
  emit_docs false
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
  echo "Net content diff changed — real update." >&2
  emit false
  DOCS_ONLY=$(docs_only_delta)
  if [ "$DOCS_ONLY" = true ]; then
    echo "Every net-changed file is non-canon documentation — keeping approval." >&2
  else
    echo "Delta reaches code or canon docs — re-reviewing." >&2
  fi
  emit_docs "$DOCS_ONLY"
fi
