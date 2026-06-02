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
project_queue_manager_ingest = _mod.project_queue_manager_ingest
slice_queue_manager_ingest = _mod.slice_queue_manager_ingest
resolve_human_approved_blockers = _mod.resolve_human_approved_blockers
stable_hash = _mod.stable_hash


def _state(*, engine_merged=None, engine_done=None, engine_human_approved=None,
           engine_closed=None):
    """Build a minimal scout-state dict with the fields the projection reads."""
    return {
        "repos": {
            "engine": {
                "needs_plan": [],
                "human_approved": engine_human_approved or [],
                "tasks": {"open": [], "in_progress": [], "done": engine_done or []},
                "closed_fleet_queued": engine_closed or [],
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


class IngestProjectionFiresOnNewApprovedIssue(unittest.TestCase):
    """The queue-manager-ingest projection drives auto-spawning of
    fleet-queue-ingest. Hash flips ONLY when the human-approved-not-
    yet-queued set changes."""

    def test_empty_state_is_stable(self):
        h1 = stable_hash(project_queue_manager_ingest(_state()))
        h2 = stable_hash(project_queue_manager_ingest(_state()))
        self.assertEqual(h1, h2)

    def test_new_human_approved_issue_flips_hash(self):
        before = stable_hash(project_queue_manager_ingest(_state()))
        after = stable_hash(project_queue_manager_ingest(_state(
            engine_human_approved=[
                {"number": 9001, "title": "Feature X",
                 "labels": ["human:approved"]},
            ],
        )))
        self.assertNotEqual(before, after,
                            "new human:approved issue must flip ingest hash")

    def test_same_set_does_not_flip(self):
        approved = [{"number": 9001, "title": "Feature X",
                     "labels": ["human:approved"]}]
        h1 = stable_hash(project_queue_manager_ingest(_state(
            engine_human_approved=approved)))
        h2 = stable_hash(project_queue_manager_ingest(_state(
            engine_human_approved=approved)))
        self.assertEqual(h1, h2,
                         "stable set must not flip hash (no spurious spawns)")

    def test_set_shrink_also_flips_hash(self):
        # Post-ingestion: issue gets fleet:queued and drops out of
        # human_approved (fetch_human_approved excludes fleet:queued).
        # Hash flips again, fleet-queue-ingest re-fires, finds empty set,
        # exits in <1s. One wasted spawn per ingestion cycle, acceptable.
        with_issue = stable_hash(project_queue_manager_ingest(_state(
            engine_human_approved=[
                {"number": 9001, "title": "Feature X",
                 "labels": ["human:approved"]},
            ],
        )))
        empty = stable_hash(project_queue_manager_ingest(_state()))
        self.assertNotEqual(with_issue, empty)

    def test_slice_emits_pending_issues_list(self):
        approved = [{"number": 9001, "title": "Feature X",
                     "labels": ["human:approved"]}]
        out = slice_queue_manager_ingest(_state(engine_human_approved=approved))
        self.assertIn("pending_issues", out)
        self.assertEqual(len(out["pending_issues"]), 1)
        self.assertEqual(out["pending_issues"][0]["number"], 9001)
        self.assertEqual(out["pending_issues"][0]["repo"], "engine")

    def test_scope_shipped_excluded_from_pending(self):
        # fleet:scope-shipped marks issues whose scope already landed under a
        # different T-NNN; the projector and slicer must exclude them so the
        # queue-manager never tries to re-ingest them (issue #1175).
        shipped = [{"number": 9002, "title": "Feature Y",
                    "labels": ["human:approved", "fleet:scope-shipped"]}]
        h = stable_hash(project_queue_manager_ingest(_state(engine_human_approved=shipped)))
        self.assertEqual(h, stable_hash(project_queue_manager_ingest(_state())),
                         "scope-shipped issue must not contribute to the ingest hash")
        out = slice_queue_manager_ingest(_state(engine_human_approved=shipped))
        self.assertEqual(out["pending_issues"], [],
                         "scope-shipped issue must be absent from pending_issues slice")

    def test_scope_shipped_mix_keeps_non_shipped(self):
        # When some issues are scope-shipped and some are genuinely pending,
        # only the pending ones should appear in the slice.
        issues = [
            {"number": 9003, "title": "Pending", "labels": ["human:approved"]},
            {"number": 9004, "title": "Shipped", "labels": ["human:approved", "fleet:scope-shipped"]},
        ]
        out = slice_queue_manager_ingest(_state(engine_human_approved=issues))
        nums = [i["number"] for i in out["pending_issues"]]
        self.assertIn(9003, nums)
        self.assertNotIn(9004, nums)

    def test_needs_human_excluded_from_pending(self):
        # fleet:needs-human parks an approved issue (fleet can't do it
        # autonomously) while KEEPING human:approved. The ingest must not
        # re-stamp fleet:queued — otherwise the worker would re-claim a task
        # it can never complete. So the issue must be absent from the ingest
        # set even though human:approved is still on it (#1312).
        parked = [{"number": 1312, "title": "self-config edit",
                   "labels": ["human:approved", "fleet:needs-human"]}]
        h = stable_hash(project_queue_manager_ingest(_state(engine_human_approved=parked)))
        self.assertEqual(h, stable_hash(project_queue_manager_ingest(_state())),
                         "needs-human issue must not contribute to the ingest hash")
        out = slice_queue_manager_ingest(_state(engine_human_approved=parked))
        self.assertEqual(out["pending_issues"], [],
                         "needs-human issue must be absent from pending_issues slice")


class IngestHonorsBlockedBy(unittest.TestCase):
    """resolve_human_approved_blockers() flags issues whose `**Blocked by:**`
    predecessors are still open; the ingest projector + slicer then exclude
    them, so a freshly-filed stacked epic only queues its head until each
    blocker closes (#1476)."""

    def _resolved(self, *, human_approved, closed=None, merged=None):
        st = _state(engine_human_approved=human_approved,
                    engine_closed=closed, engine_merged=merged)
        resolve_human_approved_blockers(st)
        return st

    def test_open_blocker_marks_issue_blocked(self):
        st = self._resolved(human_approved=[
            {"number": 801, "title": "child", "labels": ["human:approved"],
             "body": "**Model:** opus\n**Blocked by:** #800"},
        ])
        issue = st["repos"]["engine"]["human_approved"][0]
        self.assertTrue(issue["blocked"], "open predecessor must mark child blocked")
        self.assertNotIn("body", issue, "body must be stripped after resolution")

    def test_closed_blocker_clears_blocked(self):
        st = self._resolved(
            human_approved=[
                {"number": 801, "title": "child", "labels": ["human:approved"],
                 "body": "**Model:** opus\n**Blocked by:** #800"},
            ],
            closed=[{"number": 800, "title": "head"}],
        )
        self.assertFalse(st["repos"]["engine"]["human_approved"][0]["blocked"],
                         "predecessor in closed_fleet_queued must clear blocked")

    def test_merged_head_pr_clears_blocked(self):
        # A merged `claude/<N>-*` head satisfies the blocker the same way
        # resolve_blocked_by() treats it, even before the issue's CLOSED state
        # has propagated into closed_fleet_queued.
        st = self._resolved(
            human_approved=[
                {"number": 811, "title": "child", "labels": ["human:approved"],
                 "body": "**Blocked by:** #810"},
            ],
            merged=[{"number": 950, "title": "T", "headRefName": "claude/810-head",
                     "baseRefName": "master", "mergedAt": "2026-06-01T00:00:00Z"}],
        )
        self.assertFalse(st["repos"]["engine"]["human_approved"][0]["blocked"],
                         "a merged claude/810-* head must clear the child's blocker")

    def test_no_blocker_is_not_blocked(self):
        st = self._resolved(human_approved=[
            {"number": 801, "title": "head", "labels": ["human:approved"],
             "body": "**Model:** opus\n**Blocked by:** (none)"},
        ])
        self.assertFalse(st["repos"]["engine"]["human_approved"][0]["blocked"])

    def test_prose_only_blocker_holds_child(self):
        # A blocker named only in prose with no resolvable #N (e.g. a redesign)
        # must hold the child back — matches resolve_blocked_by.
        st = self._resolved(human_approved=[
            {"number": 801, "title": "child", "labels": ["human:approved"],
             "body": "## Blocked on the lighting redesign PR\n\n**Model:** opus"},
        ])
        self.assertTrue(st["repos"]["engine"]["human_approved"][0]["blocked"])

    def test_blocked_child_excluded_from_projection_and_slice(self):
        st = self._resolved(human_approved=[
            {"number": 810, "title": "head", "labels": ["human:approved"],
             "body": "**Blocked by:** (none)"},
            {"number": 811, "title": "child", "labels": ["human:approved"],
             "body": "**Blocked by:** #810"},
        ])
        proj_nums = [i["issue"] for i in project_queue_manager_ingest(st)]
        self.assertIn(810, proj_nums, "workable head must contribute to the hash")
        self.assertNotIn(811, proj_nums, "blocked child must not contribute to the hash")
        slice_nums = [i["number"] for i in slice_queue_manager_ingest(st)["pending_issues"]]
        self.assertIn(810, slice_nums)
        self.assertNotIn(811, slice_nums)

    def test_unblock_flips_hash(self):
        # The crux of #1476: when the head closes, the child becomes workable,
        # re-enters the ingest set, and the trigger hash flips so the scout
        # re-fires fleet-queue-ingest to queue it.
        child = {"number": 811, "title": "child", "labels": ["human:approved"],
                 "body": "**Blocked by:** #810"}
        blocked = self._resolved(human_approved=[dict(child)])
        unblocked = self._resolved(human_approved=[dict(child)],
                                   closed=[{"number": 810, "title": "head"}])
        self.assertNotEqual(
            stable_hash(project_queue_manager_ingest(blocked)),
            stable_hash(project_queue_manager_ingest(unblocked)),
            "closing the blocker must flip the ingest hash so ingest re-fires")


if __name__ == "__main__":
    unittest.main()
