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
