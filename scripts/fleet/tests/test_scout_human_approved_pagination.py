"""Pins that fetch_human_approved pages `human:approved` /
`fleet:agent-approved` out via `_rest_list`'s `max_pages` (per_page=100,
max_pages=3 — a 300 cap) rather than requesting a single page, so an issue
past the first page still reaches repos.<repo>.human_approved and derived
surfaces (the ingest pending set) do not silently drop it.

The `conditional_get` stub below emulates real REST /issues pagination
semantics: created&desc (newest first), a full page continues, a short page
stops.

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
slice_queue_manager_ingest = _mod.slice_queue_manager_ingest

_REPO = "jakildev/IrredenEngine"

# Population sized past a single 100-item page so the test exercises real
# multi-page pagination, not merely a bigger per_page. The oldest fixture
# issue (number _OLDEST) lands on page 2 under REST's created&desc ordering,
# and carries fleet:plan-review (an ingest-skip label that is NOT in
# _ALREADY_QUEUED_LABELS, so the fetch still returns it) so it also probes
# the ingest-pending derived surface in the same population.
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
        _rest_issue(n, extra_labels=("fleet:plan-review",) if n == _OLDEST else ())
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

    def test_ingest_pending_set_gains_zero_rows(self):
        # fleet:plan-review is an _INGEST_SKIP_LABELS entry — an issue newly
        # reachable through pagination must not flip into the ingest
        # pending set merely because pagination surfaced it.
        state = {"repos": {"engine": {"path": "/tmp",
                                       "human_approved": fetch_human_approved(_REPO)}}}
        sliced = slice_queue_manager_ingest(state)
        pending_numbers = {i["number"] for i in sliced["pending_issues"]}
        self.assertNotIn(_OLDEST, pending_numbers)


if __name__ == "__main__":
    unittest.main()
