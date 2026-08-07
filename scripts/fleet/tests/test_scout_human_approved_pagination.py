"""Regression test for #2856: fetch_human_approved's per_page=30 (no
pagination) silently truncated to the newest 30 issues per label, so any
older issue past that window never reached repos.<repo>.human_approved —
and derived surfaces (review_plan, the ingest pending set) inherited the
drop with no warning.

The fix pages `human:approved` / `fleet:agent-approved` out via `_rest_list`'s
existing `max_pages` (per_page=100, max_pages=3 — a 300 cap) instead of
raising per_page alone, since the population already exceeds 100 in practice
and keeps growing.

The `conditional_get` stub below emulates real REST /issues pagination
semantics (created&desc — newest first; a full page continues, a short page
stops) rather than special-casing the fixed call site, so this suite fails
against the pre-fix code: the old per_page=30, no-pagination fetch requests
exactly one 30-item page and never sees the older issues this test plants
past that window.

Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.
"""
import importlib.machinery
import importlib.util
import json
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

fetch_human_approved = _mod.fetch_human_approved
_populate_review_plan = _mod._populate_review_plan
slice_queue_manager_ingest = _mod.slice_queue_manager_ingest

_REPO = "jakildev/IrredenEngine"

# Population sized past a single 100-item page (not just past the old 30-item
# one) so the test exercises real multi-page pagination, not merely a bigger
# per_page. Issue #1 is the oldest — REST's created&desc ordering puts it on
# page 2 — and carries human:review-plan so it also probes the review_plan
# and ingest-pending derived surfaces in one population.
_POPULATION = 130
_OLDEST = 1


def _rest_issue(number, extra_labels=()):
    labels = [{"name": "human:approved"}] + [
        {"name": name} for name in extra_labels
    ]
    return {
        "number": number,
        "title": f"issue {number}",
        "labels": labels,
        "updated_at": "2026-08-01T00:00:00Z",
        "body": "**Blocked by:** (none)",
    }


def _stub_conditional_get(repo_slug, path, params=None, **_kwargs):
    params = params or {}
    label = params.get("labels")
    per_page = int(params.get("per_page", 100))
    page = int(params.get("page", "1"))

    if label != "human:approved":
        # fleet:agent-approved: empty, keeps the union simple.
        return (True, json.dumps([]))

    # REST /issues defaults to created&desc: newest (highest number) first.
    all_numbers = list(range(_POPULATION, 0, -1))
    start = (page - 1) * per_page
    window = all_numbers[start:start + per_page]
    body = [
        _rest_issue(n, extra_labels=("human:review-plan",) if n == _OLDEST else ())
        for n in window
    ]
    return (True, json.dumps(body))


class TestHumanApprovedPagination(unittest.TestCase):

    def setUp(self):
        patcher = patch.object(_mod, "conditional_get",
                               side_effect=_stub_conditional_get)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_issue_past_old_30_window_reaches_human_approved(self):
        out = fetch_human_approved(_REPO)
        self.assertIsNotNone(out)
        numbers = {i["number"] for i in out}
        self.assertIn(_OLDEST, numbers,
                      f"issue #{_OLDEST} (oldest of {_POPULATION}) must survive "
                      "pagination, not just the newest-30/newest-100 window")
        self.assertEqual(len(numbers), _POPULATION)

    def test_issue_past_old_window_reaches_review_plan(self):
        state = {"repos": {"engine": {"path": "/tmp",
                                       "human_approved": fetch_human_approved(_REPO)}}}
        _populate_review_plan(state)
        review_numbers = {i["number"] for i in state["repos"]["engine"]["review_plan"]}
        self.assertIn(_OLDEST, review_numbers)

    def test_ingest_pending_set_gains_zero_rows(self):
        # human:review-plan is an _INGEST_SKIP_LABELS entry — the newly
        # reachable issue must NOT flip into the ingest pending set just
        # because pagination surfaced it (the fix must be
        # behaviour-preserving for the ingest lane, per #2856's own
        # measurement that the ingest set gains 0 rows).
        state = {"repos": {"engine": {"path": "/tmp",
                                       "human_approved": fetch_human_approved(_REPO)}}}
        sliced = slice_queue_manager_ingest(state)
        pending_numbers = {i["number"] for i in sliced["pending_issues"]}
        self.assertNotIn(_OLDEST, pending_numbers)


if __name__ == "__main__":
    unittest.main()
