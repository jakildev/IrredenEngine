"""Tests for degraded-fetch handling in fleet-state-scout.

Covers: failed fetch → last-known-good preserved + degraded marker;
clean empty fetch → not degraded; no-previous-state first-run fallback.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

collect_state = _mod.collect_state
STATE_FILE = _mod.STATE_FILE

_SAMPLE_PR = {"number": 42, "title": "foo", "headRefName": "feat/foo",
              "baseRefName": "master", "author": "bot", "labels": [],
              "mergeable": "MERGEABLE", "isDraft": False, "reviews": [],
              "updatedAt": "2026-06-11T00:00:00Z"}

_SAMPLE_TASK_QUEUE = {
    "open": [{"id": "#99", "title": "t", "status": " ", "model": "sonnet",
              "owner": "free", "blocked_by": "(none)", "blocked": False,
              "area": None, "effort": None, "issue": "#99"}],
    "in_progress": [],
    "done": [],
}


class TestScoutDegradedFetch(unittest.TestCase):

    def setUp(self):
        # collect_state also fetches engine plan_review, which these tests don't
        # otherwise stub. fetch_plan_review now goes through conditional_get
        # (REST + ETag cache), so leaving it live would hit the real GitHub API
        # and write the shared ~/.fleet ETag cache on every run — the same
        # hermeticity hazard the #2227 review flagged for fetch_task_queue.
        # Stub it to a clean empty result so the degraded assertions below key
        # only on the fetcher each test deliberately fails.
        patcher = patch.object(_mod, "fetch_plan_review", return_value=[])
        patcher.start()
        self.addCleanup(patcher.stop)

    def _write_prev_state(self, tmp_dir, prs=None, tasks=None):
        """Write a minimal previous state.json for fallback tests."""
        state = {
            "generated_at": "2026-06-11T19:00:00Z",
            "repos": {
                "engine": {
                    "path": str(Path.home() / "src" / "IrredenEngine"),
                    "prs": prs if prs is not None else [_SAMPLE_PR],
                    "needs_plan": [],
                    "human_approved": [],
                    "closed_fleet_queued": [],
                    "recent_merged_prs": [],
                    "tasks": tasks if tasks is not None else _SAMPLE_TASK_QUEUE,
                    "epics": [],
                }
            },
        }
        state_file = Path(tmp_dir) / "state.json"
        state_file.write_text(json.dumps(state))
        return state_file

    def test_failed_pr_fetch_preserves_last_known_good(self):
        """fetch_prs returns None → last-known-good prs[] preserved + degraded marked."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=None), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.prs", state["degraded"])
        # Data preserved from previous snapshot
        self.assertEqual(state["repos"]["engine"]["prs"], [_SAMPLE_PR])

    def test_clean_empty_pr_fetch_not_degraded(self):
        """fetch_prs returns [] (genuine empty) → no degraded marker."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertNotIn("degraded", state)
        self.assertEqual(state["repos"]["engine"]["prs"], [])

    def test_failed_task_fetch_preserves_last_known_good(self):
        """fetch_task_queue returns None → last-known-good tasks preserved + degraded marked."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=None), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.tasks", state["degraded"])
        self.assertEqual(
            state["repos"]["engine"]["tasks"]["open"],
            _SAMPLE_TASK_QUEUE["open"],
        )

    def test_no_previous_state_failed_fetch_uses_empty_fallback(self):
        """First run + failed fetch → empty fallback used + degraded marked."""
        missing = Path("/tmp/__fleet_state_missing_9999.json")
        with patch.object(_mod, "STATE_FILE", missing), \
             patch.object(_mod, "fetch_prs", return_value=None), \
             patch.object(_mod, "fetch_needs_plan", return_value=[]), \
             patch.object(_mod, "fetch_human_approved", return_value=[]), \
             patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
             patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
             patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
             patch.object(_mod, "fetch_epics", return_value=[]), \
             patch.object(_mod, "GAME", missing.parent / "no-game"):
            state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.prs", state["degraded"])
        # No previous data → empty fallback
        self.assertEqual(state["repos"]["engine"]["prs"], [])

    def test_multiple_failed_sections_all_listed(self):
        """Multiple failed sections all appear in degraded list."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=None), \
                 patch.object(_mod, "fetch_needs_plan", return_value=None), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("engine.prs", state["degraded"])
        self.assertIn("engine.needs_plan", state["degraded"])

    def test_no_failures_no_degraded_key(self):
        """All fetches succeed → no 'degraded' key at all."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[_SAMPLE_PR]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertNotIn("degraded", state)


if __name__ == "__main__":
    unittest.main()
