"""Tests for enrich_stackable_blocker_prs() in fleet-state-scout.

Covers prefix-discrimination, multi-match safety, multi-blocker guard,
single-ref-with-prose pass-through, None/empty/free-text blocked_by guards,
cross-repo isolation, and author pass-through. Import the function via
importlib because the script has no .py extension.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

# ---------------------------------------------------------------------------
# Import the function under test
# ---------------------------------------------------------------------------
_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
enrich_stackable_blocker_prs = _mod.enrich_stackable_blocker_prs


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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


def _task(id_, blocked_by):
    return {"id": id_, "blocked_by": blocked_by}


def _pr(number, head_ref, author="bot"):
    return {"number": number, "headRefName": head_ref, "author": author}


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestEnrichStackableBlockerPrs(unittest.TestCase):

    def test_happy_path_single_match(self):
        """Exactly one PR matches the blocked_by prefix → field is written."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(536, "claude/1111-scout-stackable-blocker-pr", author="jakildev")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(task["stackable_blocker_pr"]["headRefName"],
                         "claude/1111-scout-stackable-blocker-pr")
        self.assertEqual(task["stackable_blocker_pr"]["author"], "jakildev")

    def test_queued_blocked_task_is_enriched(self):
        """#1527: a queued-blocked task (carries the fleet:blocked-derived
        `blocked: True` flag) now lives in tasks.open, so enrichment attaches
        the blocker's open PR — the autonomous "feed stacking" half. The
        `blocked` flag must not interfere with enrichment."""
        task = {"id": "#1112", "blocked_by": "#1111", "blocked": True}
        prs = [_pr(536, "claude/1111-scout-stackable-blocker-pr", author="jakildev")]
        state = _state(engine_tasks=[task], engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", out)
        self.assertEqual(out["stackable_blocker_pr"]["number"], 536)

    def test_zero_matches_no_field(self):
        """No open PR matches prefix → field absent."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(999, "claude/2000-unrelated")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_prs_no_field(self):
        """No open PRs at all → field absent."""
        tasks = [_task("#1112", "#1111")]
        state = _state(engine_tasks=tasks, engine_prs=[])
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_match_no_field(self):
        """Two PRs share the same issue-number prefix (stale branch) → field absent."""
        tasks = [_task("#1112", "#1111")]
        prs = [
            _pr(536, "claude/1111-scout-stackable-blocker-pr"),
            _pr(537, "claude/1111-stale-bounced-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_blocker_no_field(self):
        """blocked_by naming two issues is a multi-blocker → field absent."""
        tasks = [_task("#1115", "#1112, #1113")]
        prs = [_pr(540, "claude/1112-some-branch"), _pr(541, "claude/1113-other")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_two_refs_with_prose_no_field(self):
        """Two #refs is a multi-blocker regardless of surrounding prose
        ("#A and #B", "#A, #B (note)") → field absent. Guards the single-ref
        relaxation from over-matching genuine multi-blockers."""
        for value in ("#101 and #102", "#101, #102 (both must land)"):
            with self.subTest(blocked_by=value):
                tasks = [_task("#999", value)]
                prs = [_pr(1, "claude/101-x"), _pr(2, "claude/102-y")]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_stackable_blocker_prs(state)
                self.assertNotIn(
                    "stackable_blocker_pr",
                    state["repos"]["engine"]["tasks"]["open"][0],
                )

    def test_none_blocked_by_no_field(self):
        """blocked_by is None → field absent."""
        tasks = [_task("#1112", None)]
        prs = [_pr(536, "claude/1111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_explicit_none_string_no_field(self):
        """blocked_by is '(none)' → field absent."""
        tasks = [_task("#1112", "(none)")]
        prs = [_pr(536, "claude/1111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_no_match(self):
        """#101 prefix must not match a 1011 branch (trailing dash prevents it)."""
        tasks = [_task("#102", "#101")]
        prs = [_pr(100, "claude/1011-longer-id")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_correct_match(self):
        """#101 with trailing dash matches 101-something but not 1011-something."""
        tasks = [_task("#102", "#101")]
        prs = [
            _pr(100, "claude/1011-longer-id"),
            _pr(101, "claude/101-real-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 101)

    def test_cross_repo_isolation(self):
        """Engine task cannot match a game PR."""
        engine_tasks = [_task("#1112", "#1111")]
        game_prs = [_pr(536, "claude/1111-in-game-repo")]
        state = _state(engine_tasks=engine_tasks, engine_prs=[], game_prs=game_prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multiple_tasks_independent(self):
        """Two tasks in the same repo with different blockers are enriched independently."""
        tasks = [
            _task("#1113", "#1111"),
            _task("#1114", "#1112"),
        ]
        prs = [
            _pr(536, "claude/1111-branch-a"),
            _pr(537, "claude/1112-branch-b", author="other"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        self.assertEqual(open_tasks[0]["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["number"], 537)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["author"], "other")

    def test_free_text_blocked_by_no_field(self):
        """blocked_by values that reference zero issues (free text, a bare
        URL with no #ref, legacy T-NNN) → field absent."""
        values = [
            "pending design",
            "https://github.com/x/y/issues/1",
            "T-101",
        ]
        for value in values:
            with self.subTest(blocked_by=value):
                tasks = [_task("#999", value)]
                prs = [_pr(1, "claude/101-x")]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_stackable_blocker_prs(state)
                self.assertNotIn(
                    "stackable_blocker_pr",
                    state["repos"]["engine"]["tasks"]["open"][0],
                )

    def test_single_ref_with_prose_enriched(self):
        """A single #ref followed by explanatory prose still stacks. The epic
        decomposer emits "#NNN (why)"; the old terse-only gate silently
        dropped the whole chain (#1309 blocked_by "#1308 (T1 must land
        first…)" never stacked on #1316, observed 2026-05-28)."""
        tasks = [_task("#1309", "#1308 (T1 must land first; stack on its PR)")]
        prs = [_pr(1316, "claude/1308-per-axis-trixel-canvas-infra",
                   author="jakildev")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 1316)
        self.assertEqual(task["stackable_blocker_pr"]["headRefName"],
                         "claude/1308-per-axis-trixel-canvas-infra")


if __name__ == "__main__":
    unittest.main()
