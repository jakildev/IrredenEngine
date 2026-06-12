"""Tests for enrich_inflight_pr_tasks() in fleet-state-scout.

A queued task whose own issue already has an open implementation PR (branch
`claude/<N>-…`) is non-actionable off the queue — a parked design-blocked PR
releases its issue-side claim, so the task drops back into tasks.open looking
free while a fresh worker would refuse it. The enrichment tags it `inflight_pr`
so the dispatch resolver skips it (#1726, the #1640 / PR #1700 incident).

Import the function via importlib because the script has no .py extension.
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
enrich_inflight_pr_tasks = _mod.enrich_inflight_pr_tasks


def _state(engine_tasks=None, engine_prs=None, game_tasks=None, game_prs=None):
    return {
        "repos": {
            "engine": {
                "tasks": {"open": engine_tasks or []},
                "prs": engine_prs or [],
            },
            "game": {
                "tasks": {"open": game_tasks or []},
                "prs": game_prs or [],
            },
        }
    }


def _task(id_):
    # The enrichment keys on the task's OWN issue number; mirror the scout's
    # fetch_task_queue shape where `issue` == `id` == "#N".
    return {"id": id_, "issue": id_}


def _pr(number, head_ref, labels=None):
    return {"number": number, "headRefName": head_ref, "labels": labels or []}


class TestEnrichInflightPrTasks(unittest.TestCase):

    def test_parked_design_blocked_pr_tags_inflight(self):
        # The #1640 case: open task whose own PR is fleet:wip + design-blocked.
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/1640-metal-foreign-canvas-r32i-read",
                   labels=["fleet:wip", "fleet:design-blocked", "fleet:opus"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 1700)
        self.assertEqual(out["inflight_pr"]["headRefName"],
                         "claude/1640-metal-foreign-canvas-r32i-read")
        self.assertTrue(out["inflight_pr"]["parked"])

    def test_active_wip_pr_tags_inflight_not_parked(self):
        # A plain fleet:wip PR (orphaned / mid-flight) is still in flight: a
        # fresh queue claim is the wrong action, so it's tagged — parked False.
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/1640-metal-foreign-canvas-r32i-read",
                   labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertFalse(out["inflight_pr"]["parked"])

    def test_no_matching_pr_no_field(self):
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/9999-unrelated", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_prs_no_field(self):
        tasks = [_task("#1640")]
        state = _state(engine_tasks=tasks, engine_prs=[])
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination(self):
        # #164's task must not match a claude/1640- branch (trailing dash).
        tasks = [_task("#164")]
        prs = [_pr(1700, "claude/1640-metal", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_cross_repo_isolation(self):
        # An engine task must not match a same-numbered game PR.
        tasks = [_task("#101")]
        game_prs = [_pr(5, "claude/101-in-game-repo", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=[], game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_game_legacy_branch_prefix_matches(self):
        # Game accepts both claude/<N>- and the legacy claude/game-<N>- form.
        tasks = [_task("#101")]
        game_prs = [_pr(5, "claude/game-101-unit-movement", labels=["fleet:wip"])]
        state = _state(game_tasks=tasks, game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["game"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 5)

    def test_multiple_tasks_independent(self):
        tasks = [_task("#1640"), _task("#1726")]
        prs = [_pr(1700, "claude/1640-metal", labels=["fleet:design-blocked"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        self.assertIn("inflight_pr", open_tasks[0])
        # #1726 has no matching PR -> stays claimable.
        self.assertNotIn("inflight_pr", open_tasks[1])


if __name__ == "__main__":
    unittest.main()
