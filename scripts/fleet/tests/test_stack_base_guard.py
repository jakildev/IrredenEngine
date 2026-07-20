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
    missing_ancestor_reason,
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


class TestMissingAncestorReason(unittest.TestCase):
    """The transitive-ancestry containment walk (#2447). The base's head must
    contain the squash of every MERGED blocker in its ancestry; a hole one or
    more levels above the candidate task's own blocker list is the churn signature
    this guard exists to catch. All inputs are injected callables so the walk is
    pinned hermetically (no gh / no git)."""

    def _graph(self, blockers, merged, shas, contained):
        """Build (get_blocker_refs, ref_merged, merged_sha, contains) from plain
        dicts. `blockers`: issue -> [refs]; `merged`: set of merged refs;
        `shas`: ref -> squash sha; `contained`: set of shas in the base head."""
        def gbr(issue):
            return blockers.get(str(issue))

        def rm(ref):
            if ref in merged:
                return True
            if str(ref) in blockers:
                return False
            return None

        def ms(ref):
            return shas.get(ref)

        def contains(sha):
            return sha in contained

        return gbr, rm, ms, contains

    def test_direct_merged_hole_rejected(self):
        """The #2447 signature: base #3's own blocker #1 merged after the fork
        (its squash absent from the base head). Named in the reason."""
        gbr, rm, ms, contains = self._graph(
            blockers={"3": ["1", "2"], "2": []},
            merged={"1"}, shas={"1": "sha1"}, contained=set())
        self.assertEqual(
            missing_ancestor_reason("3", gbr, rm, ms, contains),
            "missing merged ancestor #1")

    def test_direct_merged_contained_offered(self):
        """Same base, but the merged blocker's squash IS in the base head → safe."""
        gbr, rm, ms, contains = self._graph(
            blockers={"3": ["1", "2"], "2": []},
            merged={"1"}, shas={"1": "sha1"}, contained={"sha1"})
        self.assertIsNone(missing_ancestor_reason("3", gbr, rm, ms, contains))

    def test_transitive_hole_two_levels_up(self):
        """The hole lives two levels above the base: base #4 → open #3 → merged
        #1 (not contained). The candidate's own blocker list (#4→#3) never names
        #1, yet the walk finds it."""
        gbr, rm, ms, contains = self._graph(
            blockers={"4": ["3"], "3": ["1"]},
            merged={"1"}, shas={"1": "sha1"}, contained=set())
        self.assertEqual(
            missing_ancestor_reason("4", gbr, rm, ms, contains),
            "missing merged ancestor #1")

    def test_no_blockers_offered(self):
        """A base with an empty blocker list has no ancestry to miss."""
        gbr, rm, ms, contains = self._graph(
            blockers={"5": []}, merged=set(), shas={}, contained=set())
        self.assertIsNone(missing_ancestor_reason("5", gbr, rm, ms, contains))

    def test_all_frontier_contained_offered(self):
        """Every merged ancestor contained (including a transitive one) → safe."""
        gbr, rm, ms, contains = self._graph(
            blockers={"4": ["3", "1"], "3": ["2"]},
            merged={"1", "2"}, shas={"1": "sha1", "2": "sha2"},
            contained={"sha1", "sha2"})
        self.assertIsNone(missing_ancestor_reason("4", gbr, rm, ms, contains))

    def test_unresolvable_blockers_fail_closed(self):
        """get_blocker_refs None (issue unfetchable / cross-repo ref) → undet."""
        reason = missing_ancestor_reason(
            "3", lambda i: None, lambda r: False, lambda r: None,
            lambda s: True)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("#3", reason)

    def test_unknown_ref_state_fail_closed(self):
        """ref_merged None (ref neither known-merged nor a tracked open task)."""
        reason = missing_ancestor_reason(
            "3", lambda i: ["99"], lambda r: None, lambda r: None,
            lambda s: True)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("#99", reason)

    def test_unresolvable_sha_fail_closed(self):
        """A merged frontier whose squash sha can't be resolved (out of the
        recent-merged window) fails closed rather than assuming containment."""
        gbr, rm, _ms, contains = self._graph(
            blockers={"3": ["1"]}, merged={"1"}, shas={}, contained=set())
        reason = missing_ancestor_reason("3", gbr, rm, lambda r: None, contains)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("#1", reason)

    def test_unavailable_containment_fail_closed(self):
        """contains None (compare API / git verdict unavailable) → undet."""
        gbr, rm, ms, _contains = self._graph(
            blockers={"3": ["1"]}, merged={"1"}, shas={"1": "sha1"},
            contained=set())
        reason = missing_ancestor_reason("3", gbr, rm, ms, lambda s: None)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("#1", reason)

    def test_cycle_terminates_fail_closed(self):
        """A blocker cycle terminates via the visited set instead of recursing
        forever."""
        cyc = {"1": ["2"], "2": ["1"]}
        reason = missing_ancestor_reason(
            "1", lambda i: cyc.get(str(i)), lambda r: False,
            lambda r: None, lambda s: True)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("cycle", reason)

    def test_depth_cap_terminates_fail_closed(self):
        """A blocker chain longer than max_depth fails closed at the cap."""
        chain = {str(i): [str(i + 1)] for i in range(1, 20)}
        reason = missing_ancestor_reason(
            "1", lambda i: chain.get(str(i)), lambda r: False,
            lambda r: None, lambda s: True, max_depth=3)
        self.assertTrue(reason.startswith("ancestry undeterminable"))
        self.assertIn("deeper than 3", reason)

    def test_frontier_not_recursed(self):
        """A merged, contained frontier is NOT recursed into — linear squash
        history means everything before it is contained too. merged_sha/contains
        are the only things consulted for it; get_blocker_refs is never called
        on the merged ref."""
        seen = []

        def gbr(issue):
            seen.append(str(issue))
            return {"3": ["1"]}.get(str(issue), [])

        def rm(ref):
            return ref == "1"

        self.assertIsNone(missing_ancestor_reason(
            "3", gbr, rm, lambda r: "sha1", lambda s: True))
        # #1 is merged+contained → walk consulted only #3's blockers, never #1's.
        self.assertEqual(seen, ["3"])


if __name__ == "__main__":
    unittest.main()
