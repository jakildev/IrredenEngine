"""Tests for project_epic_steward() / slice_epic_steward() /
resolve_epic_children() in fleet-state-scout (#1664).

The steward's hash-input projection must be strictly edge-triggered:
every item disappears as a direct consequence of the steward's own
write (tick a box, heal a checklist, append a child, flip a design
label, close the umbrella). The quiescence invariant is the same one
project_merger documents — a transient field in the items would
self-trigger the role forever.
"""
import importlib.machinery
import importlib.util
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_epic_steward = _mod.project_epic_steward
slice_epic_steward = _mod.slice_epic_steward
resolve_epic_children = _mod.resolve_epic_children
project_sonnet_reviewer = _mod.project_sonnet_reviewer
project_opus_reviewer = _mod.project_opus_reviewer
project_merger = _mod.project_merger
enrich_stackable_blocker_prs = _mod.enrich_stackable_blocker_prs
stable_hash = _mod.stable_hash


def _entry(num, *, checked=False, closed=False):
    return {"number": num, "checked": checked, "closed": closed}


def _epic(num, *, checklist=None, labels=None, plan_exists=False,
          updated_at="2026-06-10T00:00:00Z"):
    return {
        "number": num,
        "title": f"epic {num}",
        "labels": sorted(labels or ["fleet:epic"]),
        "updatedAt": updated_at,
        "checklist": checklist if checklist is not None else [],
        "plan_path": f".fleet/plans/issue-{num}.md",
        "plan_exists": plan_exists,
    }


def _pr(num, *, labels=None, head="claude/1-feat", mergeable="MERGEABLE",
        base="master", draft=False):
    return {
        "number": num,
        "title": f"pr {num}",
        "headRefName": head,
        "baseRefName": base,
        "labels": sorted(labels or []),
        "mergeable": mergeable,
        "isDraft": draft,
        "author": "bot",
    }


def _task(num, *, epic=None, summary="task", in_progress=False, blocked_by="(none)"):
    return {
        "status": "~" if in_progress else " ",
        "title": f"#{num}",
        "summary": summary,
        "id": f"#{num}",
        "model": "opus",
        "owner": "free",
        "area": None,
        "blocked_by": blocked_by,
        "blocked": False,
        "issue": f"#{num}",
        "epic": epic,
    }


def _state(*, epics=None, prs=None, tasks_open=None, tasks_in_progress=None,
           human_approved=None, needs_plan=None, closed_fleet_queued=None,
           repo="engine", path=""):
    return {"repos": {repo: {
        "path": path,
        "epics": epics or [],
        "prs": prs or [],
        "tasks": {"open": tasks_open or [],
                  "in_progress": tasks_in_progress or [],
                  "done": []},
        "human_approved": human_approved or [],
        "needs_plan": needs_plan or [],
        "closed_fleet_queued": closed_fleet_queued or [],
    }}}


def _hash(state):
    return stable_hash(project_epic_steward(state))


class Quiescence(unittest.TestCase):
    """Same durable state twice -> same hash; transient fields never leak
    into the items."""

    def _busy_state(self):
        return _state(
            epics=[_epic(10, checklist=[
                _entry(11, checked=True, closed=True),
                _entry(12, closed=True),
                _entry(13),
            ])],
            prs=[_pr(101, labels=["fleet:design-blocked"], head="claude/13-x")],
            tasks_open=[_task(14, epic="#10")],
        )

    def test_same_state_same_hash(self):
        self.assertEqual(_hash(self._busy_state()), _hash(self._busy_state()))

    def test_umbrella_updated_at_does_not_flip_hash(self):
        before = _state(epics=[_epic(10, checklist=[_entry(11)])])
        after = _state(epics=[_epic(10, checklist=[_entry(11)],
                                    updated_at="2026-06-11T12:34:56Z")])
        self.assertEqual(_hash(before), _hash(after))

    def test_unrelated_pr_label_churn_does_not_flip_hash(self):
        # Reviewer/merger label traffic on a non-design PR is invisible.
        before = _state(epics=[_epic(10, checklist=[_entry(11)])],
                        prs=[_pr(101, labels=["fleet:approved"])])
        after = _state(epics=[_epic(10, checklist=[_entry(11)])],
                       prs=[_pr(101, labels=["fleet:approved",
                                             "fleet:merger-cooldown"])])
        self.assertEqual(_hash(before), _hash(after))

    def test_no_epics_projects_empty(self):
        self.assertEqual(project_epic_steward(_state()), [])


class NormalizeOp(unittest.TestCase):
    def test_managed_checklist_less_epic_emits_normalize(self):
        items = project_epic_steward(
            _state(epics=[_epic(10, plan_exists=True)]))
        self.assertEqual(items, [{"kind": "normalize", "repo": "engine",
                                  "epic": 10}])

    def test_healing_the_checklist_consumes_normalize(self):
        before = _state(epics=[_epic(10, plan_exists=True)])
        after = _state(epics=[_epic(10, plan_exists=True,
                                    checklist=[_entry(11)])])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_epic_steward(after), [])

    def test_legacy_epic_without_plan_file_emits_nothing(self):
        # ~17 pre-protocol epics have neither checklist nor plan file; they
        # must not hold the projection non-empty forever.
        self.assertEqual(
            project_epic_steward(_state(epics=[_epic(10)])), [])


class RollupOp(unittest.TestCase):
    def test_closed_unchecked_child_emits_rollup(self):
        items = project_epic_steward(_state(epics=[
            _epic(10, checklist=[_entry(11, closed=True), _entry(12)]),
        ]))
        self.assertEqual(items, [{"kind": "rollup", "repo": "engine",
                                  "epic": 10, "child": 11}])

    def test_checking_the_box_consumes_rollup(self):
        before = _state(epics=[_epic(10, checklist=[
            _entry(11, closed=True), _entry(12)])])
        after = _state(epics=[_epic(10, checklist=[
            _entry(11, checked=True, closed=True), _entry(12)])])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_epic_steward(after), [])

    def test_open_unchecked_child_emits_nothing(self):
        self.assertEqual(project_epic_steward(
            _state(epics=[_epic(10, checklist=[_entry(11)])])), [])


class AdoptOp(unittest.TestCase):
    def test_declared_child_missing_from_checklist_emits_adopt(self):
        items = project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(11)])],
            tasks_open=[_task(14, epic="#10")],
        ))
        self.assertEqual(items, [{"kind": "adopt", "repo": "engine",
                                  "epic": 10, "issue": 14}])

    def test_appending_to_checklist_consumes_adopt(self):
        before = _state(epics=[_epic(10, checklist=[_entry(11)])],
                        tasks_open=[_task(14, epic="#10")])
        after = _state(epics=[_epic(10, checklist=[_entry(11), _entry(14)])],
                       tasks_open=[_task(14, epic="#10")])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_epic_steward(after), [])

    def test_human_approved_issue_is_adoptable(self):
        items = project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(11)])],
            human_approved=[{"number": 15, "title": "new child",
                             "labels": ["human:approved"], "epic": "#10"}],
        ))
        self.assertEqual(items, [{"kind": "adopt", "repo": "engine",
                                  "epic": 10, "issue": 15}])

    def test_ref_to_unknown_umbrella_emits_nothing(self):
        # Closed or non-epic umbrella: no checklist to adopt into.
        self.assertEqual(project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(11)])],
            tasks_open=[_task(14, epic="#99")],
        )), [])


class DesignOp(unittest.TestCase):
    def test_design_blocked_child_pr_emits_triage(self):
        items = project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-blocked", "fleet:wip"],
                     head="claude/13-canvas")],
        ))
        self.assertEqual(items, [{"kind": "design", "op": "triage",
                                  "repo": "engine", "epic": 10, "pr": 101}])

    def test_non_epic_design_block_is_architects_lane(self):
        self.assertEqual(project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-blocked"],
                     head="claude/77-unrelated")],
        )), [])

    def test_design_propose_with_pending_proposal_emits_nothing(self):
        # Steward parked the PR and stamped the umbrella: no trigger until
        # the responder removes fleet:steward-proposal.
        self.assertEqual(project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(13)],
                         labels=["fleet:epic", "fleet:steward-proposal"])],
            prs=[_pr(101, labels=["fleet:design-proposed"],
                     head="claude/13-canvas")],
        )), [])

    def test_proposal_answered_emits_distribute(self):
        # Removing fleet:steward-proposal from the umbrella is the re-fire
        # edge: the parked PR surfaces as a distribute item.
        items = project_epic_steward(_state(
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-proposed"],
                     head="claude/13-canvas")],
        ))
        self.assertEqual(items, [{"kind": "design", "op": "distribute",
                                  "repo": "engine", "epic": 10, "pr": 101}])

    def test_design_unblock_consumes_the_item(self):
        before = _state(
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-proposed"],
                     head="claude/13-canvas")])
        after = _state(
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-unblocked"],
                     head="claude/13-canvas")])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_epic_steward(after), [])

    def test_game_branch_form_matches(self):
        # Game checklists must match both claude/<N>- and claude/game-<N>-.
        items = project_epic_steward(_state(
            repo="game",
            epics=[_epic(10, checklist=[_entry(13)])],
            prs=[_pr(101, labels=["fleet:design-blocked"],
                     head="claude/game-13-canvas")],
        ))
        self.assertEqual(items, [{"kind": "design", "op": "triage",
                                  "repo": "game", "epic": 10, "pr": 101}])


class CloseoutOp(unittest.TestCase):
    def test_all_children_closed_emits_closeout(self):
        items = project_epic_steward(_state(epics=[
            _epic(10, checklist=[_entry(11, checked=True, closed=True),
                                 _entry(12, checked=True, closed=True)]),
        ]))
        self.assertEqual(items, [{"kind": "closeout", "repo": "engine",
                                  "epic": 10}])

    def test_closing_the_umbrella_consumes_closeout(self):
        # A closed umbrella leaves the open fleet:epic fetch entirely.
        before = _state(epics=[
            _epic(10, checklist=[_entry(11, checked=True, closed=True)])])
        after = _state(epics=[])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_epic_steward(after), [])

    def test_empty_checklist_never_emits_closeout(self):
        self.assertEqual(project_epic_steward(
            _state(epics=[_epic(10, plan_exists=True)])),
            [{"kind": "normalize", "repo": "engine", "epic": 10}])

    def test_closed_but_unchecked_emits_rollup_and_closeout(self):
        # Both pending: the steward ticks (rollup) then closes (closeout).
        kinds = sorted(i["kind"] for i in project_epic_steward(_state(epics=[
            _epic(10, checklist=[_entry(11, closed=True)])])))
        self.assertEqual(kinds, ["closeout", "rollup"])


class ResolveEpicChildren(unittest.TestCase):
    """Closed-state annotation uses the already-fetched open/closed sets;
    the live gh fallback is reserved for unchecked children invisible to
    every list fetch."""

    def setUp(self):
        self._orig = _mod._resolve_ref_satisfied
        self.fallback_calls = []

    def tearDown(self):
        _mod._resolve_ref_satisfied = self._orig

    def _stub_fallback(self, result):
        def stub(repo_slug, ref, cache):
            self.fallback_calls.append((repo_slug, ref))
            return result
        _mod._resolve_ref_satisfied = stub

    def test_visible_open_child_is_open_without_fallback(self):
        self._stub_fallback(True)
        state = _state(epics=[_epic(10, checklist=[_entry(11)])],
                       tasks_open=[_task(11)])
        resolve_epic_children(state)
        entry = state["repos"]["engine"]["epics"][0]["checklist"][0]
        self.assertFalse(entry["closed"])
        self.assertEqual(self.fallback_calls, [])

    def test_closed_fleet_queued_child_is_closed_without_fallback(self):
        self._stub_fallback(False)
        state = _state(epics=[_epic(10, checklist=[_entry(11)])],
                       closed_fleet_queued=[{"number": 11, "title": "t",
                                             "labels": []}])
        resolve_epic_children(state)
        entry = state["repos"]["engine"]["epics"][0]["checklist"][0]
        self.assertTrue(entry["closed"])
        self.assertEqual(self.fallback_calls, [])

    def test_checked_invisible_child_trusted_closed_without_fallback(self):
        # A ticked child aged out of the closed_fleet_queued window must not
        # cost a gh call every tick.
        self._stub_fallback(False)
        state = _state(epics=[_epic(10, checklist=[_entry(11, checked=True)])])
        resolve_epic_children(state)
        entry = state["repos"]["engine"]["epics"][0]["checklist"][0]
        self.assertTrue(entry["closed"])
        self.assertEqual(self.fallback_calls, [])

    def test_unchecked_invisible_child_uses_fallback(self):
        self._stub_fallback(True)
        state = _state(epics=[_epic(10, checklist=[_entry(11)])])
        resolve_epic_children(state)
        entry = state["repos"]["engine"]["epics"][0]["checklist"][0]
        self.assertTrue(entry["closed"])
        self.assertEqual(self.fallback_calls,
                         [("jakildev/IrredenEngine", "11")])

    def test_plan_existence_annotated_from_repo_checkout(self):
        self._stub_fallback(False)
        with tempfile.TemporaryDirectory() as repo:
            plans = Path(repo) / ".fleet" / "plans"
            plans.mkdir(parents=True)
            (plans / "issue-10.md").write_text("# plan\n")
            state = _state(epics=[_epic(10), _epic(20)], path=repo)
            resolve_epic_children(state)
            epics = state["repos"]["engine"]["epics"]
            self.assertTrue(epics[0]["plan_exists"])
            self.assertEqual(epics[0]["plan_path"], ".fleet/plans/issue-10.md")
            self.assertFalse(epics[1]["plan_exists"])


class DesignProposedSkipSets(unittest.TestCase):
    """fleet:design-proposed parks a PR off the review/merger surfaces
    until the proposal resolves (#1664 acceptance criterion). Note: as of
    #2130 a frozen design-proposed diff IS a valid stack base — the
    stacking surface is no longer skipped (see
    test_design_proposed_blocker_is_stackable below)."""

    def test_absent_from_sonnet_reviewer_projection(self):
        state = _state(prs=[_pr(101, labels=["fleet:design-proposed"])])
        self.assertEqual(project_sonnet_reviewer(state), [])

    def test_absent_from_opus_reviewer_projection(self):
        state = _state(prs=[_pr(101, labels=["fleet:design-proposed",
                                             "fleet:has-nits"])])
        self.assertEqual(project_opus_reviewer(state), [])

    def test_absent_from_merger_projection(self):
        state = _state(prs=[_pr(101, labels=["fleet:design-proposed",
                                             "fleet:approved"])])
        self.assertEqual(project_merger(state), [])

    def test_design_proposed_blocker_is_stackable(self):
        # Post-#2130 (7fb58c6d): frozen-design labels were deliberately
        # dropped from NOT_STACKABLE_BASE_LABELS, so a design-proposed
        # blocker PR with a parked, stable diff IS offered as a stack
        # base. This case writes no PR cache, so it covers the cache-miss
        # (blocker_files=None) branch of unsafe_base_reason; the cache-hit
        # branch is covered by test_frozen_design_base_offered.
        state = _state(
            prs=[_pr(101, labels=["fleet:design-proposed"],
                     head="claude/11-base")],
            tasks_open=[_task(12, blocked_by="#11")],
        )
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 101)


class Slice(unittest.TestCase):
    def _state(self):
        return _state(
            epics=[_epic(10, checklist=[_entry(11, closed=True), _entry(13)])],
            prs=[_pr(101, labels=["fleet:design-blocked"],
                     head="claude/13-canvas")],
            tasks_open=[_task(14, epic="#10", summary="late child")],
        )

    def test_slice_shape_and_repo_tags(self):
        out = slice_epic_steward(self._state())
        self.assertEqual([e["number"] for e in out["epics"]], [10])
        self.assertEqual(out["epics"][0]["repo"], "engine")
        self.assertEqual(out["epics"][0]["checklist"][0]["closed"], True)

        self.assertEqual(len(out["design_prs"]), 1)
        pr = out["design_prs"][0]
        self.assertEqual((pr["repo"], pr["epic"], pr["child"], pr["op"]),
                         ("engine", 10, 13, "triage"))

        self.assertEqual(out["adoptable"],
                         [{"issue": 14, "epic": 10, "title": "late child",
                           "repo": "engine"}])

    def test_triggers_mirror_projector(self):
        state = self._state()
        self.assertEqual(slice_epic_steward(state)["triggers"],
                         project_epic_steward(state))

    def test_pending_proposal_pr_visible_with_null_op(self):
        # In the slice but not the hash: the steward can report it standing
        # by without being woken for it.
        state = _state(
            epics=[_epic(10, checklist=[_entry(13)],
                         labels=["fleet:epic", "fleet:steward-proposal"])],
            prs=[_pr(101, labels=["fleet:design-proposed"],
                     head="claude/13-canvas")],
        )
        out = slice_epic_steward(state)
        self.assertEqual(out["design_prs"][0]["op"], None)
        self.assertEqual(out["triggers"], [])


if __name__ == "__main__":
    unittest.main()
