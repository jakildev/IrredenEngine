"""Shared "is this PR safe to stack a new task on?" predicate for the fleet.

The stackable-blocked fallback tier lets a worker claim a task whose single
blocker still has an open PR, basing the new branch on that PR's head so the
diff stays isolated. Two surfaces decide which bases are eligible:

  * `fleet-state-scout` OFFERS a base by attaching `stackable_blocker_pr`
    to a queued task (`enrich_stackable_blocker_prs`).
  * `fleet-claim claim --stackable-on <pr>` ACCEPTS a base, re-verifying it
    live at claim time.

Both must reject a base that isn't safe to stack on, and they must agree.
Before #1751 each surface filtered a different (narrow) subset:

  * the scout rejected only design-blocked bases + area-overlap;
  * the claim gate re-checked only `state` / `headRefName` (MERGED / OPEN /
    CLOSED), not labels and not the diff.

So a base that was OPEN-but-WIP, OPEN-but-`design-unblocked`, OPEN-but-amending
(`fleet:amending-<host>-<agent>`), or an OPEN empty claim-commit skeleton sailed
through both and got stacked on — a half-built or in-flux base that the worker's
diff then folded in or conflicted against (the 2026-06-06 empty-claim skeleton
case; cf. the just-design-unblocked skeleton hazard, fleet memory
`stackable_blocker_empty_skeleton`).

Centralizing the reject-state predicate here means the offer and the accept
can't drift: both call `unsafe_base_reason()`. The label vocabulary mirrors the
scout's existing reviewer-skip / design-block sets (a base in any of those
states is mid-flux for the same reasons a reviewer would skip it) but is kept
purpose-named here so the two concerns can evolve independently.

Stdlib only — imported by `fleet-state-scout` (pure Python) and by
`fleet-claim` (bash, via an inline `python3` block through `FLEET_LIB_DIR`),
exactly like `fleet_branch_match.py`.
"""

# A base PR carrying any of these labels is in a non-stackable state. The
# discriminator is whether the base's HEAD DIFF is moving. A WIP, amending, or
# design-UNBLOCKED PR is being actively rewritten, so a stack on it would fold
# in or conflict against shifting code. The FROZEN-design states
# (design-blocked / -proposed / -escalated) are the opposite: the worker hit a
# question, escalated, and walked away, so the diff is parked and stable — a
# fine base to stack on (a non-approved base is OK; only WIP/active-rework must
# be gone). If the eventual design answer reworks the base, the merger
# cascade-rebases the stack — the normal stacked-PR maintenance path. So the
# frozen-design labels are deliberately NOT rejected here.
NOT_STACKABLE_BASE_LABELS = frozenset({
    # Work-in-progress / mid-amend — the head diff will move under the stack.
    "fleet:wip", "human:wip", "fleet:human-amending", "fleet:merger-cooldown",
    # Architect responded and the worker is resuming the rework — the head diff
    # is about to move, like WIP. (The frozen design states — design-blocked /
    # -proposed / -escalated — are intentionally absent: stable diff, stackable.)
    "fleet:design-unblocked",
    # Pending merger rebase — diff against master is meaningless until resolved;
    # stacking would create a two-rebase chain when the conflict is resolved.
    "fleet:semantic-conflict",
    # Not yet rebaseable against its own base / not its own work.
    "fleet:awaiting-base", "fleet:awaiting-upstream-review",
    "fleet:fork-of-other-pr",
})

# Dynamic per-host amend claim (`fleet:amending-<host>-<agent>`) — a worker is
# actively rewriting this PR for feedback, so the head is mid-flux. Matched by
# prefix, mirroring the scout's `REVIEW_SKIP_PREFIXES`.
NOT_STACKABLE_BASE_PREFIXES = ("fleet:amending-",)


def unsafe_base_reason(labels, changed_files=None):
    """Return a short human-readable reason a PR is NOT a safe stack base, or
    None when it is safe.

    `labels` — iterable of the base PR's label names.
    `changed_files` — the base PR's changed-file paths when known, or None when
        unknown (the caller couldn't determine them, e.g. a scout cache miss).
        An empty *known* list ([]) is an empty claim-commit-only skeleton and is
        rejected; None (unknown) is NOT treated as empty — the caller decides
        how to handle an unverifiable base (the scout safely omits the offer;
        the claim gate refuses before ever calling with None).
    """
    labels = set(labels or ())

    hit = labels & NOT_STACKABLE_BASE_LABELS
    if hit:
        return sorted(hit)[0]

    for label in sorted(labels):
        for prefix in NOT_STACKABLE_BASE_PREFIXES:
            if label.startswith(prefix):
                return label

    if changed_files is not None and len(changed_files) == 0:
        return "empty claim-commit"

    return None
