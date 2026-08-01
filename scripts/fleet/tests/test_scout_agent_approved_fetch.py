"""Tests for fetch_human_approved's agent-approved union (TASK-FILING.md
§"Agent-approved follow-up lane").

fetch_human_approved merges the human:approved and fleet:agent-approved
label fetches into one ingest set: deduped by issue number (an issue can
carry both labels), sorted by number, and failing closed — either
underlying fetch returning None fails the whole read so a transient error
can't flap the ingest projection with a partial set.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

fetch_human_approved = _mod.fetch_human_approved

_REPO = "jakildev/IrredenEngine"


def _issue(n, labels):
    return {"number": n, "title": f"issue {n}", "labels": labels,
            "body": "**Blocked by:** (none)"}


def _by_label(responses):
    """Stub fetch_issues_by_label: route on the filter label, ignore the rest."""
    def _fetch(repo, filter_label, exclude_labels=None, with_body=False,
               per_page=100):
        return responses[filter_label]
    return _fetch


class TestAgentApprovedFetchUnion(unittest.TestCase):

    def test_merges_both_labels_sorted_by_number(self):
        stub = _by_label({
            "human:approved": [_issue(20, ["human:approved"])],
            "fleet:agent-approved": [_issue(7, ["fleet:agent-approved"])],
        })
        with patch.object(_mod, "fetch_issues_by_label", side_effect=stub):
            out = fetch_human_approved(_REPO)
        self.assertEqual([i["number"] for i in out], [7, 20])

    def test_dedupes_issue_carrying_both_labels(self):
        both = _issue(11, ["human:approved", "fleet:agent-approved"])
        stub = _by_label({
            "human:approved": [both],
            "fleet:agent-approved": [dict(both)],
        })
        with patch.object(_mod, "fetch_issues_by_label", side_effect=stub):
            out = fetch_human_approved(_REPO)
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0]["number"], 11)

    def test_either_fetch_failing_fails_the_whole_read(self):
        for failing in ("human:approved", "fleet:agent-approved"):
            responses = {
                "human:approved": [_issue(20, ["human:approved"])],
                "fleet:agent-approved": [_issue(7, ["fleet:agent-approved"])],
            }
            responses[failing] = None
            with patch.object(_mod, "fetch_issues_by_label",
                              side_effect=_by_label(responses)):
                self.assertIsNone(fetch_human_approved(_REPO),
                                  f"{failing}=None must fail the whole read")

    def test_empty_sets_return_empty_not_none(self):
        stub = _by_label({"human:approved": [], "fleet:agent-approved": []})
        with patch.object(_mod, "fetch_issues_by_label", side_effect=stub):
            self.assertEqual(fetch_human_approved(_REPO), [])

    def test_parked_labels_excluded_from_both_fetches(self):
        # Both fetches must carry the parked-label exclusions, or a park that
        # KEEPS human:approved (fleet:needs-human, fleet:gated) is re-fetched on
        # every tick and never leaves repo_state["human_approved"] (#2762).
        # fleet:needs-human is the positive control: it has been excluded since
        # #1312, so a run where only it is asserted would pass even if
        # fleet:gated were missing.
        seen = {}

        def _capture(repo, filter_label, exclude_labels=None, with_body=False,
                     per_page=100):
            seen[filter_label] = set(exclude_labels or ())
            return []

        with patch.object(_mod, "fetch_issues_by_label", side_effect=_capture):
            fetch_human_approved(_REPO)
        self.assertEqual(set(seen), {"human:approved", "fleet:agent-approved"})
        for filter_label, excluded in seen.items():
            for parked in ("fleet:needs-human", "fleet:gated"):
                self.assertIn(parked, excluded,
                              f"{filter_label} fetch must exclude {parked}")


if __name__ == "__main__":
    unittest.main()
