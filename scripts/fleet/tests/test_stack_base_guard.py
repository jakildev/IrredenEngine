"""Unit tests for fleet_stack_base.unsafe_base_reason (#1751).

The shared reject-state predicate used by BOTH the scout's offer-time filter
(enrich_stackable_blocker_prs) and fleet-claim's accept-time --stackable-on
re-verify. If these two disagree the fallback can offer a base the claim then
refuses (or vice versa), so the predicate is pinned here independently.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_stack_base import (  # noqa: E402
    NOT_STACKABLE_BASE_LABELS,
    NOT_STACKABLE_BASE_PREFIXES,
    unsafe_base_reason,
)


class TestUnsafeBaseReason(unittest.TestCase):

    def test_safe_base_returns_none(self):
        """Clean OPEN base: no reject labels, a real (non-empty) diff → safe."""
        self.assertIsNone(unsafe_base_reason(["fleet:approved"], ["engine/x.cpp"]))
        self.assertIsNone(unsafe_base_reason([], ["engine/x.cpp"]))
        self.assertIsNone(unsafe_base_reason(["IRRender", "fleet:changes-made"],
                                             ["engine/render/y.cpp"]))

    def test_each_reject_label(self):
        """Every label in the set is reported by name."""
        for label in NOT_STACKABLE_BASE_LABELS:
            with self.subTest(label=label):
                self.assertEqual(unsafe_base_reason([label]), label)

    def test_wip_and_active_rework_states(self):
        # WIP and design-UNBLOCKED (architect answered, worker resuming the
        # rework) have a moving head diff -> rejected. Frozen design states are
        # covered separately in test_frozen_design_states_are_stackable.
        self.assertEqual(unsafe_base_reason(["fleet:wip"]), "fleet:wip")
        self.assertEqual(unsafe_base_reason(["human:wip"]), "human:wip")
        self.assertEqual(unsafe_base_reason(["fleet:design-unblocked"]),
                         "fleet:design-unblocked")

    def test_amending_prefix_matched(self):
        """The dynamic per-host amend claim is matched by prefix, not equality."""
        for host_agent in ("mac-worker-2", "linux-sonnet-fleet-1", "mac-opus-worker-1"):
            label = f"fleet:amending-{host_agent}"
            with self.subTest(label=label):
                self.assertEqual(unsafe_base_reason([label]), label)

    def test_empty_claim_commit_rejected(self):
        """A known-empty diff ([]) is an empty claim-commit-only skeleton."""
        self.assertEqual(unsafe_base_reason([], []), "empty claim-commit")
        self.assertEqual(unsafe_base_reason(["fleet:approved"], []),
                         "empty claim-commit")

    def test_unknown_files_not_treated_as_empty(self):
        """changed_files=None means 'unknown', NOT empty — a clean-labelled base
        with unknown files is safe (the caller decides; the claim gate fetches
        live). Distinguishing this from [] prevents suppressing a not-yet-cached
        base at scout time."""
        self.assertIsNone(unsafe_base_reason(["fleet:approved"], None))
        self.assertIsNone(unsafe_base_reason([], None))
        self.assertIsNone(unsafe_base_reason(["fleet:approved"]))  # default None

    def test_label_reject_precedes_empty_check(self):
        """A WIP base that also happens to have an empty diff reports the WIP
        label (label states are checked before the empty-claim fallback)."""
        self.assertEqual(unsafe_base_reason(["fleet:wip"], []), "fleet:wip")

    def test_prefixes_constant_shape(self):
        """Guard the amending prefix tuple so a refactor can't silently empty it."""
        self.assertIn("fleet:amending-", NOT_STACKABLE_BASE_PREFIXES)

    def test_frozen_design_states_are_stackable(self):
        """Frozen-design bases (worker escalated and walked away → diff parked
        and stable) ARE valid stack bases. A non-approved base is fine to stack
        on; only an actively-moving head (WIP / amending / design-unblocked)
        disqualifies. So these must NOT be in the reject set and must return
        None when paired with a real diff."""
        for label in ("fleet:design-blocked", "fleet:design-escalated",
                      "fleet:design-proposed"):
            with self.subTest(label=label):
                self.assertNotIn(label, NOT_STACKABLE_BASE_LABELS)
                self.assertIsNone(unsafe_base_reason([label], ["engine/x.cpp"]))

    def test_awaiting_base_is_a_stackable_base(self):
        """#2805: `fleet:awaiting-base` describes the base's OWN base, not its
        head diff, and the merger mints it on every stacked PR whose base is
        still open — so rejecting it made depth-2+ stacking impossible. Live
        shape when filed: PR #2792 (fleet:approved + fleet:awaiting-base) was
        refused as a base for #2803, whose target code exists only on that
        branch. Graded against a base that STILL carries the label — the fix
        does not remove it from any PR."""
        self.assertNotIn("fleet:awaiting-base", NOT_STACKABLE_BASE_LABELS)
        self.assertIsNone(unsafe_base_reason(
            ["fleet:approved", "fleet:awaiting-base", "fleet:authored-on-macos"],
            ["scripts/fleet/witness"]))

    def test_awaiting_base_does_not_exempt_a_co_carried_reject(self):
        """Dropping the label must not turn its carriers into a blanket
        exemption: a base that is awaiting-base AND genuinely in flux still
        rejects, and reports the in-flux label by name. (Pre-fix these returned
        "fleet:awaiting-base" — it sorts first — so this flips with the fix.)"""
        for label in ("fleet:wip", "fleet:merger-cooldown",
                      "fleet:semantic-conflict", "fleet:amending-mac-pool-1"):
            with self.subTest(label=label):
                self.assertEqual(
                    unsafe_base_reason(["fleet:awaiting-base", label],
                                       ["scripts/fleet/witness"]),
                    label)
        self.assertEqual(
            unsafe_base_reason(["fleet:approved", "fleet:awaiting-base"], []),
            "empty claim-commit")

    def test_retained_reject_states_survive_the_awaiting_base_drop(self):
        """The states #2805 keeps must still reject, each reported by name — a
        bare `assertIsNone` on the safe case passes vacuously if a future edit
        empties the set. These assertions hold in BOTH arms of the #2805
        positive control (pre- and post-fix), so they are not keyed on the fix;
        only the two tests above flip."""
        for label in ("fleet:wip", "human:wip", "fleet:human-amending",
                      "fleet:merger-cooldown", "fleet:design-unblocked",
                      "fleet:semantic-conflict", "fleet:awaiting-upstream-review",
                      "fleet:fork-of-other-pr"):
            with self.subTest(label=label):
                self.assertIn(label, NOT_STACKABLE_BASE_LABELS)
                self.assertEqual(
                    unsafe_base_reason([label], ["scripts/fleet/witness"]), label)
        self.assertEqual(unsafe_base_reason(["fleet:approved"], []),
                         "empty claim-commit")

    def test_fork_of_other_pr_stays_rejected_on_its_own_grounds(self):
        """#2805's sibling decision: `fleet:fork-of-other-pr` shares the false
        "retired legacy label" framing but keeps rejecting — the branch carries
        commits inherited from another open PR, so the base is not the author's
        own work and the upstream author may rewrite the prefix under the
        stack. Unlike awaiting-base, that is a property of the head diff."""
        self.assertIn("fleet:fork-of-other-pr", NOT_STACKABLE_BASE_LABELS)
        self.assertEqual(
            unsafe_base_reason(["fleet:approved", "fleet:fork-of-other-pr"],
                               ["engine/x.cpp"]),
            "fleet:fork-of-other-pr")

    def test_semantic_conflict_rejected(self):
        """A PR awaiting merger rebase is not a safe stack base — its diff
        against master is meaningless until the conflict is resolved, and
        stacking would create a two-rebase chain."""
        self.assertEqual(unsafe_base_reason(["fleet:semantic-conflict"]),
                         "fleet:semantic-conflict")
        self.assertIn("fleet:semantic-conflict", NOT_STACKABLE_BASE_LABELS)

    def test_accepts_set_or_list(self):
        """labels may arrive as a list (scout) or a set (claim block)."""
        self.assertEqual(unsafe_base_reason({"fleet:wip"}), "fleet:wip")
        self.assertEqual(unsafe_base_reason(("fleet:wip",)), "fleet:wip")


if __name__ == "__main__":
    unittest.main()
