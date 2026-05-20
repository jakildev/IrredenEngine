"""Tests for enrich_stackable_blocker_prs() in fleet-state-scout.

Covers prefix-discrimination, multi-match safety, multi-blocker guard,
None/empty/free-text blocked_by guards, cross-repo isolation, and author
pass-through. Import the function via importlib because the script has no
.py extension.
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
        tasks = [_task("T-112", "T-111")]
        prs = [_pr(536, "claude/T-111-scout-stackable-blocker-pr", author="jakildev")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(task["stackable_blocker_pr"]["headRefName"],
                         "claude/T-111-scout-stackable-blocker-pr")
        self.assertEqual(task["stackable_blocker_pr"]["author"], "jakildev")

    def test_zero_matches_no_field(self):
        """No open PR matches prefix → field absent."""
        tasks = [_task("T-112", "T-111")]
        prs = [_pr(999, "claude/T-200-unrelated")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_prs_no_field(self):
        """No open PRs at all → field absent."""
        tasks = [_task("T-112", "T-111")]
        state = _state(engine_tasks=tasks, engine_prs=[])
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_match_no_field(self):
        """Two PRs share the same T-NNN prefix (stale branch) → field absent."""
        tasks = [_task("T-112", "T-111")]
        prs = [
            _pr(536, "claude/T-111-scout-stackable-blocker-pr"),
            _pr(537, "claude/T-111-stale-bounced-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_blocker_no_field(self):
        """blocked_by with two IDs doesn't match ^T-\\d{3}$ → field absent."""
        tasks = [_task("T-115", "T-112, T-113")]
        prs = [_pr(540, "claude/T-112-some-branch"), _pr(541, "claude/T-113-other")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_none_blocked_by_no_field(self):
        """blocked_by is None → field absent."""
        tasks = [_task("T-112", None)]
        prs = [_pr(536, "claude/T-111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_explicit_none_string_no_field(self):
        """blocked_by is '(none)' → field absent."""
        tasks = [_task("T-112", "(none)")]
        prs = [_pr(536, "claude/T-111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_no_match(self):
        """T-101 prefix must not match a T-1011 branch (trailing dash prevents it)."""
        tasks = [_task("T-102", "T-101")]
        prs = [_pr(100, "claude/T-1011-longer-id")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_correct_match(self):
        """T-101 with trailing dash matches T-101-something but not T-1011-something."""
        tasks = [_task("T-102", "T-101")]
        prs = [
            _pr(100, "claude/T-1011-longer-id"),
            _pr(101, "claude/T-101-real-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 101)

    def test_cross_repo_isolation(self):
        """Engine task cannot match a game PR."""
        engine_tasks = [_task("T-112", "T-111")]
        game_prs = [_pr(536, "claude/T-111-in-game-repo")]
        state = _state(engine_tasks=engine_tasks, engine_prs=[], game_prs=game_prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multiple_tasks_independent(self):
        """Two tasks in the same repo with different blockers are enriched independently."""
        tasks = [
            _task("T-113", "T-111"),
            _task("T-114", "T-112"),
        ]
        prs = [
            _pr(536, "claude/T-111-branch-a"),
            _pr(537, "claude/T-112-branch-b", author="other"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        self.assertEqual(open_tasks[0]["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["number"], 537)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["author"], "other")

    def test_free_text_blocked_by_no_field(self):
        """Free-text or URL blocked_by values don't match regex → field absent."""
        values = [
            "pending design",
            "https://github.com/x/y/issues/1",
            "T-101 (waiting)",
        ]
        for value in values:
            with self.subTest(blocked_by=value):
                tasks = [_task("T-999", value)]
                prs = [_pr(1, "claude/T-101-x")]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_stackable_blocker_prs(state)
                self.assertNotIn(
                    "stackable_blocker_pr",
                    state["repos"]["engine"]["tasks"]["open"][0],
                )


if __name__ == "__main__":
    unittest.main()
