"""Tests for project_queue_manager() in fleet-state-scout.

Verifies the projection hash flips when a PR merges, so the scout's
trigger machinery (update_role_trigger) fires queue-manager via the
normal event-driven path. Without this signal, queue-manager's
projection was insensitive to PR-merge events that don't also close
a fleet:queued issue, and the dispatcher had to fall back on an
unconditional 5-minute re-arm to avoid stranding the queue.

The test imports the function via importlib because the script has
no .py extension, mirroring test_enrich_stackable_blocker_prs.py.
"""
import importlib.machinery
import importlib.util
import json
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_queue_manager = _mod.project_queue_manager
slice_queue_manager = _mod.slice_queue_manager
stable_hash = _mod.stable_hash


def _state(*, engine_merged=None, engine_done=None, engine_human_approved=None):
    """Build a minimal scout-state dict with the fields the projection reads."""
    return {
        "repos": {
            "engine": {
                "needs_plan": [],
                "human_approved": engine_human_approved or [],
                "tasks": {"open": [], "in_progress": [], "done": engine_done or []},
                "closed_fleet_queued": [],
                "recent_merged_prs": engine_merged or [],
            },
        },
    }


def _hash(state):
    return stable_hash(project_queue_manager(state))


class ProjectionFiresOnMerge(unittest.TestCase):
    def test_empty_baseline_is_stable(self):
        h1 = _hash(_state())
        h2 = _hash(_state())
        self.assertEqual(h1, h2)

    def test_new_merged_pr_changes_hash(self):
        before = _state(engine_merged=[
            {"number": 540, "title": "T-123", "baseRefName": "master",
             "mergedAt": "2026-05-08T20:00:00Z"},
        ])
        after = _state(engine_merged=[
            {"number": 540, "title": "T-123", "baseRefName": "master",
             "mergedAt": "2026-05-08T20:00:00Z"},
            # Newly-merged PR appears at the top of the list.
            {"number": 548, "title": "T-125", "baseRefName": "master",
             "mergedAt": "2026-05-08T22:01:00Z"},
        ])
        self.assertNotEqual(_hash(before), _hash(after),
                            "adding a fresh merged PR must flip the projection hash")

    def test_same_merged_set_does_not_flip(self):
        # Quiescent state: scout polls every 30s; if no new merges happen
        # the projection must produce the same hash so the trigger doesn't
        # fire spuriously every poll.
        merged = [{"number": 540, "title": "T-123", "baseRefName": "master",
                   "mergedAt": "2026-05-08T20:00:00Z"}]
        self.assertEqual(_hash(_state(engine_merged=merged)),
                         _hash(_state(engine_merged=merged)))

    def test_merged_to_non_master_is_distinguishable(self):
        # PR #543 stranded-merge regression: a PR shows mergedAt set but
        # baseRefName points at a stale feature branch, not master. The
        # projection captures baseRefName so a downstream renderer can
        # distinguish reachable-from-master vs orphan merges.
        master_merge = [{"number": 543, "title": "T-124", "baseRefName": "master",
                         "mergedAt": "2026-05-08T22:01:00Z"}]
        feature_merge = [{"number": 543, "title": "T-124",
                          "baseRefName": "claude/T-123-...",
                          "mergedAt": "2026-05-08T22:01:00Z"}]
        self.assertNotEqual(_hash(_state(engine_merged=master_merge)),
                            _hash(_state(engine_merged=feature_merge)))

    def test_human_approved_addition_also_flips(self):
        # Pre-existing signal still works — a new human:approved issue
        # flips the hash so queue-manager runs ingestion.
        before = _hash(_state())
        after = _hash(_state(engine_human_approved=[
            {"number": 9000, "title": "Feature X", "labels": ["human:approved"]},
        ]))
        self.assertNotEqual(before, after)


class SlicerExposesMergedPRs(unittest.TestCase):
    def test_slice_includes_recent_merged_prs(self):
        merged = [{"number": 540, "title": "T-123", "baseRefName": "master",
                   "mergedAt": "2026-05-08T20:00:00Z"}]
        out = slice_queue_manager(_state(engine_merged=merged))
        self.assertIn("recent_merged_prs", out)
        self.assertEqual(len(out["recent_merged_prs"]), 1)
        self.assertEqual(out["recent_merged_prs"][0]["number"], 540)
        self.assertEqual(out["recent_merged_prs"][0]["repo"], "engine")


if __name__ == "__main__":
    unittest.main()
