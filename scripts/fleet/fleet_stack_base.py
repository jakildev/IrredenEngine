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

`missing_ancestor_reason()` is the second shared predicate (#2447): the
label/diff check above never verifies that the base branch actually contains
the work of a blocker that merged after the base forked, so both surfaces also
route the base's blocker ancestry through it (the scout with in-memory data +
the compare API, `fleet-claim` with live gh + `git merge-base`).

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


def missing_ancestor_reason(base_issue, get_blocker_refs, ref_merged,
                            merged_sha, contains, max_depth=8):
    """Return a short reason the stack base's head is missing a merged ancestor
    (so it is NOT safe to stack on), or None when every merged ancestor in the
    base's blocker chain is contained in the base head.

    `unsafe_base_reason` above validates a base by label state and diff only.
    It never checks ANCESTRY — whether the base branch actually contains work
    that has already merged to master and that a task stacked on it depends on.
    A base branch forks from some ancestor and can be missing a blocker that
    merged to master *after* the fork: a "collapsed" merged ref that the
    candidate task's own blocker list never names, because the hole lives one
    or more levels up the stack (#2447). This walks the base issue's blocker
    ancestry, treats every MERGED blocker as a frontier node, and asserts the
    base head contains that blocker's squash commit.

    Injected callables (the caller wires them to the scout's in-memory data or
    to fleet-claim's live gh/git — the walk itself stays pure and hermetically
    testable):

      get_blocker_refs(issue) -> list[str] | None
          Same-repo blocker issue numbers (strings, no '#') declared in
          `issue`'s **Blocked by:** field, or None when undeterminable (issue
          unresolvable, or a cross-repo ref is present that git containment
          can't evaluate). None fails the walk CLOSED.
      ref_merged(ref) -> bool | None
          True when `ref` is a merged blocker (a frontier node), False when it
          is still open (recurse through it), None when its state is unknown.
      merged_sha(ref) -> str | None
          The squash merge commit sha of a merged `ref`, or None when it can't
          be resolved.
      contains(sha) -> bool | None
          True when the base head contains `sha`, False when it provably does
          not, None when the verdict is unavailable. (The base head oid is
          captured by the caller's closure — this predicate only varies in sha.)

    Fail-closed rationale: any None (unknown blocker state, unresolvable sha,
    unavailable containment verdict, unresolvable ancestor blockers), a
    depth-cap hit, or a cycle returns an "ancestry undeterminable (...)" reason.
    A suppressed offer degrades to the safe pre-stackable status quo (the task
    waits for the blocker to merge), whereas failing OPEN reproduces the
    guaranteed-no-op churn loop this guard exists to kill.

    Merged nodes are checked, never recursed: master's squash history is
    linear, so if a frontier ancestor's squash is contained in the base head,
    everything merged before it is too — a deeper hole can't exist without a
    frontier miss.
    """
    undet = "ancestry undeterminable"
    visited = set()

    def walk(issue, depth):
        if depth > max_depth:
            return f"{undet} (blocker chain deeper than {max_depth} from base)"
        if issue in visited:
            return f"{undet} (blocker cycle at #{issue})"
        visited.add(issue)

        refs = get_blocker_refs(issue)
        if refs is None:
            return f"{undet} (blockers of #{issue} unresolvable)"

        for ref in refs:
            merged = ref_merged(ref)
            if merged is None:
                return f"{undet} (state of blocker #{ref} unknown)"
            if merged:
                sha = merged_sha(ref)
                if not sha:
                    return f"{undet} (merge sha of #{ref} unresolvable)"
                verdict = contains(sha)
                if verdict is None:
                    return f"{undet} (containment of #{ref} unverifiable)"
                if not verdict:
                    return f"missing merged ancestor #{ref}"
                # Contained → this frontier is satisfied; linear history means
                # everything merged before it is too, so don't recurse.
            else:
                # Open ancestor → its own merged blockers can be the hole.
                reason = walk(ref, depth + 1)
                if reason:
                    return reason
        return None

    return walk(str(base_issue), 0)
