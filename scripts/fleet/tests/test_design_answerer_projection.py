"""Tests for project_design_answerer() / slice_design_answerer() in
fleet-state-scout.

The lane is strictly edge-triggered by the answerer's own writes: an item
disappears when the answer removes `fleet:steward-proposal` or
`fleet:design-blocked`, or when the answerer escalates with
`fleet:needs-human`. Transient fields (timestamps, label order) never reach
the hash, the project_merger quiescence invariant.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_design_answerer = _mod.project_design_answerer
slice_design_answerer = _mod.slice_design_answerer
stable_hash = _mod.stable_hash


def _epic(num, *, checklist=(), labels=None, updated_at="2026-06-10T00:00:00Z"):
    return {
        "number": num,
        "title": f"epic {num}",
        "labels": sorted(labels or ["fleet:epic"]),
        "updatedAt": updated_at,
        "checklist": [{"number": n, "checked": False, "closed": False} for n in checklist],
        "managed": True,
    }


def _pr(num, *, labels=None, head="claude/1-feat", updated_at="2026-06-10T00:00:00Z"):
    return {
        "number": num,
        "title": f"pr {num}",
        "headRefName": head,
        "baseRefName": "master",
        "labels": sorted(labels or []),
        "mergeable": "MERGEABLE",
        "isDraft": False,
        "author": "bot",
        "updatedAt": updated_at,
    }


def _state(*, epics=(), prs=(), repo="engine"):
    return {"repos": {repo: {"epics": list(epics), "prs": list(prs)}}}


class ProposalItems(unittest.TestCase):
    def test_pending_proposal_is_an_item(self):
        state = _state(epics=[_epic(10, labels=["fleet:epic", "fleet:steward-proposal"])])
        self.assertEqual(project_design_answerer(state),
                         [{"kind": "proposal", "repo": "engine", "epic": 10}])

    def test_answered_proposal_disappears(self):
        state = _state(epics=[_epic(10, labels=["fleet:epic"])])
        self.assertEqual(project_design_answerer(state), [])

    def test_human_parked_proposal_is_not_triggered_but_is_listed(self):
        epic = _epic(10, labels=["fleet:epic", "fleet:steward-proposal", "fleet:needs-human"])
        state = _state(epics=[epic])
        self.assertEqual(project_design_answerer(state), [])
        sliced = slice_design_answerer(state)
        self.assertEqual([e["number"] for e in sliced["proposals"]], [10])
        self.assertEqual(sliced["triggers"], [])

    def test_items_are_repo_tagged(self):
        pending = ["fleet:epic", "fleet:steward-proposal"]
        state = {"repos": {
            "engine": {"epics": [_epic(10, labels=pending)], "prs": []},
            "game": {"epics": [_epic(10, labels=pending)], "prs": []},
        }}
        items = project_design_answerer(state)
        self.assertEqual(sorted(i["repo"] for i in items), ["engine", "game"])


class DesignItems(unittest.TestCase):
    def test_non_epic_design_block_is_an_item(self):
        pr = _pr(500, labels=["fleet:wip", "fleet:design-blocked"], head="claude/1-feat")
        self.assertEqual(project_design_answerer(_state(prs=[pr])),
                         [{"kind": "design", "repo": "engine", "pr": 500}])

    def test_epic_child_block_is_the_stewards(self):
        epic = _epic(10, checklist=[1])
        pr = _pr(500, labels=["fleet:wip", "fleet:design-blocked"], head="claude/1-feat")
        state = _state(epics=[epic], prs=[pr])
        self.assertEqual(project_design_answerer(state), [])
        self.assertEqual(slice_design_answerer(state)["design_prs"], [])

    def test_unblocked_pr_disappears(self):
        state = _state(prs=[_pr(500, labels=["fleet:wip", "fleet:design-unblocked"])])
        self.assertEqual(project_design_answerer(state), [])

    def test_escalated_pr_is_suppressed(self):
        pr = _pr(500, labels=["fleet:wip", "fleet:design-blocked", "fleet:needs-human"])
        state = _state(prs=[pr])
        self.assertEqual(project_design_answerer(state), [])


class Quiescence(unittest.TestCase):
    def test_hash_ignores_transient_fields(self):
        a = _state(
            epics=[_epic(10, labels=["fleet:epic", "fleet:steward-proposal"],
                         updated_at="2026-06-10T00:00:00Z")],
            prs=[_pr(500, labels=["fleet:design-blocked", "fleet:wip"],
                     updated_at="2026-06-10T00:00:00Z")])
        b = _state(
            epics=[_epic(10, labels=["fleet:steward-proposal", "fleet:epic"],
                         updated_at="2026-06-11T00:00:00Z")],
            prs=[_pr(500, labels=["fleet:wip", "fleet:design-blocked"],
                     updated_at="2026-06-12T00:00:00Z")])
        self.assertEqual(stable_hash(project_design_answerer(a)),
                         stable_hash(project_design_answerer(b)))

    def test_empty_state_projects_nothing(self):
        self.assertEqual(project_design_answerer({"repos": {}}), [])
        self.assertEqual(slice_design_answerer({"repos": {}}),
                         {"proposals": [], "design_prs": [], "triggers": []})

    def test_registered_as_projector_and_slicer(self):
        self.assertIs(_mod.PROJECTORS["design-answerer"], project_design_answerer)
        self.assertIs(_mod.SLICERS["design-answerer"], slice_design_answerer)


if __name__ == "__main__":
    unittest.main()
