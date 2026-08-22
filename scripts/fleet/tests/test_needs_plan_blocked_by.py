"""Tests for resolve_needs_plan_blocked_by() in fleet-state-scout (#2287).

Covers the fix for: a fleet:needs-plan issue whose body declares a
standalone `**Blocked by:**` line to an open issue kept projecting as
plannable (the projection never parsed the field), firing a planner
dispatch every tick that found nothing to do on arrival.

Mirrors test_queue_manager_projection.py's IngestHonorsBlockedBy, which
covers the analogous resolve_human_approved_blockers() — in-memory only
(closed_fleet_queued + merged claude/<N>-* heads), no live gh.

Since #2986 the subject also takes a live `_resolve_ref_satisfied` fallback
for refs neither in-memory source covers, so these cases must stub that seam
or they reach the network — this suite's fixtures use real issue numbers
(#2280 as the "open" predecessor), and #2280 has since closed, which turned
`test_open_blocker_marks_issue_blocked` into a test of live GitHub state.
The stub models "no ref resolves live", reproducing exactly the pre-#2986
treatment of an unresolved ref, so every case below keeps its original
meaning. Coverage of the fallback itself — including that the in-memory hits
never reach it — lives in test_scout_needs_plan_live_fallback.py.
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

resolve_needs_plan_blocked_by = _mod.resolve_needs_plan_blocked_by
project_worker = _mod.project_worker
slice_worker = _mod.slice_worker


def _state(*, needs_plan=None, closed=None, merged=None):
    return {
        "repos": {
            "engine": {
                "needs_plan": needs_plan or [],
                "prs": [],
                "tasks": {"open": [], "in_progress": [], "done": []},
                "closed_fleet_queued": closed or [],
                "recent_merged_prs": merged or [],
            },
        },
    }


class ResolveNeedsPlanBlockedBy(unittest.TestCase):

    def setUp(self):
        # Close the network seam (#2986). Replacing the helper outright means
        # there is no path left to `gh`, so a fixture this suite does not model
        # cannot silently fall through to live GitHub state.
        self._orig_resolve = _mod._resolve_ref_satisfied
        _mod._resolve_ref_satisfied = lambda repo_slug, ref, cache: False

    def tearDown(self):
        _mod._resolve_ref_satisfied = self._orig_resolve

    def _resolved(self, *, needs_plan, closed=None, merged=None):
        st = _state(needs_plan=needs_plan, closed=closed, merged=merged)
        resolve_needs_plan_blocked_by(st)
        return st

    def test_open_blocker_marks_issue_blocked(self):
        st = self._resolved(needs_plan=[
            {"number": 2258, "labels": ["fleet:needs-plan"],
             "body": "**Blocked by:** #2280"},
        ])
        issue = st["repos"]["engine"]["needs_plan"][0]
        self.assertTrue(issue["blocked"], "open predecessor must mark issue blocked")
        self.assertNotIn("body", issue, "body must be stripped after resolution")

    def test_closed_blocker_clears_blocked(self):
        st = self._resolved(
            needs_plan=[
                {"number": 2258, "labels": ["fleet:needs-plan"],
                 "body": "**Blocked by:** #2280"},
            ],
            closed=[{"number": 2280, "title": "head"}],
        )
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"],
                         "predecessor in closed_fleet_queued must clear blocked")

    def test_merged_head_pr_clears_blocked(self):
        st = self._resolved(
            needs_plan=[
                {"number": 2258, "labels": ["fleet:needs-plan"],
                 "body": "**Blocked by:** #2280"},
            ],
            merged=[{"number": 999, "headRefName": "claude/2280-attribution",
                     "baseRefName": "master"}],
        )
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"],
                         "a merged claude/2280-* head must clear the blocker")

    def test_no_blocker_field_is_not_blocked(self):
        st = self._resolved(needs_plan=[
            {"number": 2092, "labels": ["fleet:needs-plan"],
             "body": "**Model:** opus\nno blocker line here"},
        ])
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"])

    def test_none_sentinel_is_not_blocked(self):
        st = self._resolved(needs_plan=[
            {"number": 2092, "labels": ["fleet:needs-plan"],
             "body": "**Blocked by:** (none)"},
        ])
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"])

    def test_missing_body_is_not_blocked(self):
        # No body key at all (defensive — fetch_needs_plan always sets one
        # with with_body=True, but the resolver must not crash without it).
        st = self._resolved(needs_plan=[
            {"number": 2092, "labels": ["fleet:needs-plan"]},
        ])
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"])

    def test_prose_only_blocker_holds_issue(self):
        # A `Blocked on …` header naming a PR but no resolvable #N (the
        # `_BLOCKED_ON_RE` fallback) parses to a non-empty raw value with no
        # extractable ref — matches resolve_human_approved_blockers' analogous
        # "prose blocker with no #N" case, which also holds the issue back.
        st = self._resolved(needs_plan=[
            {"number": 2258, "labels": ["fleet:needs-plan"],
             "body": "## Blocked on the lighting redesign PR\n\n**Model:** opus"},
        ])
        self.assertTrue(st["repos"]["engine"]["needs_plan"][0]["blocked"])

    def test_cross_repo_blocker_does_not_block(self):
        st = self._resolved(needs_plan=[
            {"number": 2258, "labels": ["fleet:needs-plan"],
             "body": "**Blocked by:** jakildev/irreden#125"},
        ])
        self.assertFalse(st["repos"]["engine"]["needs_plan"][0]["blocked"],
                         "cross-repo blocker is unresolvable from this window")


class ProjectAndSliceWorkerHonorNeedsPlanBlocked(unittest.TestCase):
    """The dispatch consequence: a needs-plan issue already annotated
    `blocked: True` by resolve_needs_plan_blocked_by must not surface in
    either the wake projection or the dispatch slice."""

    def test_blocked_dropped_from_project_worker(self):
        items = project_worker(_state(needs_plan=[
            {"number": 2258, "labels": ["fleet:needs-plan"], "blocked": True},
        ]))
        self.assertEqual(
            [i for i in items if i.get("kind") == "needs_plan"], [])

    def test_unblocked_still_surfaces_in_project_worker(self):
        items = project_worker(_state(needs_plan=[
            {"number": 2092, "labels": ["fleet:needs-plan"], "blocked": False},
        ]))
        self.assertEqual(
            [i["issue"] for i in items if i.get("kind") == "needs_plan"], [2092])

    def test_blocked_dropped_from_slice_worker(self):
        out = slice_worker(_state(needs_plan=[
            {"number": 2258, "labels": ["fleet:needs-plan"], "blocked": True},
        ]))
        self.assertEqual(out["needs_plan"], [])

    def test_unblocked_still_surfaces_in_slice_worker(self):
        out = slice_worker(_state(needs_plan=[
            {"number": 2092, "labels": ["fleet:needs-plan"], "blocked": False},
        ]))
        self.assertEqual([i["number"] for i in out["needs_plan"]], [2092])


if __name__ == "__main__":
    unittest.main()
