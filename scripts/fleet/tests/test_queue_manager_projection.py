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
_ingest_unblock_candidates = _mod._ingest_unblock_candidates
stable_hash = _mod.stable_hash


def _state(*, engine_merged=None, engine_done=None, engine_human_approved=None,
           engine_closed=None, engine_tasks_open=None):
    """Build a minimal scout-state dict with the fields the projection reads."""
    return {
        "repos": {
            "engine": {
                "needs_plan": [],
                "human_approved": engine_human_approved or [],
                "tasks": {"open": engine_tasks_open or [],
                          "in_progress": [], "done": engine_done or []},
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

    def test_revise_plan_overrides_skip_into_pending(self):
        # human:revise-plan is the human-added "change the posted plan" gate. It
        # lands on an issue mid-review (fleet:plan-review / human:review-plan,
        # both skip labels), so it must OVERRIDE the skip — re-entering the
        # ingest set so adding it flips the hash (fires ingest) and surfaces it
        # in pending_issues for fleet-queue-ingest to reset to fleet:needs-plan.
        revise = [{"number": 2043, "title": "revise me",
                   "labels": ["human:approved", "fleet:plan-review",
                              "human:review-plan", "human:revise-plan"]}]
        h = stable_hash(project_queue_manager_ingest(_state(engine_human_approved=revise)))
        self.assertNotEqual(h, stable_hash(project_queue_manager_ingest(_state())),
                            "human:revise-plan must re-enter the ingest set so ingest fires")
        out = slice_queue_manager_ingest(_state(engine_human_approved=revise))
        nums = [i["number"] for i in out["pending_issues"]]
        self.assertIn(2043, nums,
                      "human:revise-plan issue must appear in pending_issues for reconcile")

    def test_plan_review_without_revise_still_excluded(self):
        # Guard the override is narrow: a plain mid-review plan (no revise-plan)
        # stays out of the ingest set exactly as before.
        review = [{"number": 2044, "title": "in review",
                   "labels": ["human:approved", "fleet:plan-review",
                              "human:review-plan"]}]
        out = slice_queue_manager_ingest(_state(engine_human_approved=review))
        self.assertEqual(out["pending_issues"], [],
                         "plan-review issue without revise-plan must stay excluded")


class IngestHonorsBlockedBy(unittest.TestCase):
    """resolve_human_approved_blockers() flags issues whose `**Blocked by:**`
    predecessors are still open. Since #1527 that flag is informational only:
    the ingest projector + slicer no longer exclude blocked issues — they are
    queued up front (with a fleet:blocked marker). These tests cover both the
    flag computation (still used for state.json visibility) and the new
    include-blocked projection behavior."""

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

    def test_cross_repo_blocker_does_not_block(self):
        # #1522: a blocker in a *different* repo is unresolvable from this
        # repo's window — the scout must defer (treat as non-blocking) rather
        # than block forever. Mirrors game#125 → IrredenEngine#1476, flipped.
        st = self._resolved(human_approved=[
            {"number": 820, "title": "child", "labels": ["human:approved"],
             "body": "**Model:** opus\n**Blocked by:** jakildev/irreden#125"},
        ])
        self.assertFalse(st["repos"]["engine"]["human_approved"][0]["blocked"],
                         "cross-repo blocker must defer to ingest, not block")

    def test_same_repo_qualified_blocker_still_blocks(self):
        # `IrredenEngine#N` on an engine child names the issue's *own* repo —
        # the qualifier must resolve via the in-memory same-repo path, so an
        # open predecessor still blocks (#1522 must not turn self-refs into
        # defers).
        st = self._resolved(human_approved=[
            {"number": 821, "title": "child", "labels": ["human:approved"],
             "body": "**Blocked by:** IrredenEngine#800"},
        ])
        self.assertTrue(st["repos"]["engine"]["human_approved"][0]["blocked"])

    def test_same_repo_qualified_blocker_clears_when_closed(self):
        st = self._resolved(
            human_approved=[
                {"number": 822, "title": "child", "labels": ["human:approved"],
                 "body": "**Blocked by:** IrredenEngine#800"},
            ],
            closed=[{"number": 800, "title": "head"}],
        )
        self.assertFalse(st["repos"]["engine"]["human_approved"][0]["blocked"])

    def test_blocked_child_included_in_projection_and_slice(self):
        # #1527: blocked children are now ingestion targets — they get queued
        # up front (fleet:queued + model + fleet:blocked), so both head AND
        # child contribute to the hash and appear in pending_issues.
        st = self._resolved(human_approved=[
            {"number": 810, "title": "head", "labels": ["human:approved"],
             "body": "**Blocked by:** (none)"},
            {"number": 811, "title": "child", "labels": ["human:approved"],
             "body": "**Blocked by:** #810"},
        ])
        proj_nums = [i["issue"] for i in project_queue_manager_ingest(st)]
        self.assertIn(810, proj_nums, "workable head must contribute to the hash")
        self.assertIn(811, proj_nums,
                      "blocked child is now an ingest target (#1527)")
        slice_nums = [i["number"] for i in slice_queue_manager_ingest(st)["pending_issues"]]
        self.assertIn(810, slice_nums)
        self.assertIn(811, slice_nums)

    def test_skip_label_still_excluded_when_blocked(self):
        # _INGEST_SKIP_LABELS still wins over the include-blocked change: a
        # blocked epic/needs-plan/needs-info issue stays out of the set.
        st = self._resolved(human_approved=[
            {"number": 812, "title": "epic", "labels": ["human:approved", "fleet:epic"],
             "body": "**Blocked by:** #810"},
        ])
        self.assertEqual(project_queue_manager_ingest(st), [],
                         "skip-labeled issue stays out even when blocked")
        self.assertEqual(slice_queue_manager_ingest(st)["pending_issues"], [])


class IngestUnblockRemovePath(unittest.TestCase):
    """#1527 remove-half: a queued task carrying fleet:blocked whose last
    blocker has closed becomes an unblock-candidate sourced from tasks.open (it
    has already left human_approved by carrying fleet:queued). The candidate
    flips the ingest hash so fleet-queue-ingest re-fires and strips the marker,
    then disappears the next tick once the label is gone."""

    def _task(self, *, num, blocked, blocked_by):
        return {"id": f"#{num}", "issue": f"#{num}", "title": f"#{num}",
                "summary": "t", "status": " ", "owner": "free", "model": "opus",
                "area": None, "blocked": blocked, "blocked_by": blocked_by}

    def _st(self, tasks_open):
        return _state(engine_tasks_open=tasks_open)

    def test_unblocked_marked_task_is_candidate(self):
        st = self._st([self._task(num=811, blocked=True, blocked_by="(none)")])
        self.assertEqual(_ingest_unblock_candidates(st["repos"]["engine"]), [811])

    def test_still_blocked_marked_task_is_not_candidate(self):
        st = self._st([self._task(num=811, blocked=True, blocked_by="#810")])
        self.assertEqual(_ingest_unblock_candidates(st["repos"]["engine"]), [])

    def test_unmarked_task_is_not_candidate(self):
        # No fleet:blocked label (blocked=False) → never an unblock candidate,
        # even when blocked_by resolves to (none).
        st = self._st([self._task(num=811, blocked=False, blocked_by="(none)")])
        self.assertEqual(_ingest_unblock_candidates(st["repos"]["engine"]), [])

    def test_candidate_in_projection_and_slice(self):
        st = self._st([self._task(num=811, blocked=True, blocked_by="(none)")])
        self.assertIn({"repo": "engine", "issue": 811, "op": "unblock"},
                      project_queue_manager_ingest(st))
        self.assertEqual(slice_queue_manager_ingest(st)["unblock_issues"],
                         [{"number": 811, "repo": "engine"}])

    def test_unblock_transition_flips_hash(self):
        still_blocked = self._st([self._task(num=811, blocked=True, blocked_by="#810")])
        now_unblocked = self._st([self._task(num=811, blocked=True, blocked_by="(none)")])
        self.assertNotEqual(
            stable_hash(project_queue_manager_ingest(still_blocked)),
            stable_hash(project_queue_manager_ingest(now_unblocked)),
            "blocker closing must flip the ingest hash so fleet:blocked is removed")

    def test_marker_cleared_settles_hash(self):
        unblocked = self._st([self._task(num=811, blocked=True, blocked_by="(none)")])
        cleared = self._st([self._task(num=811, blocked=False, blocked_by="(none)")])
        self.assertNotEqual(
            stable_hash(project_queue_manager_ingest(unblocked)),
            stable_hash(project_queue_manager_ingest(cleared)),
            "clearing the marker flips the hash once more, then the set quiesces")


if __name__ == "__main__":
    unittest.main()
