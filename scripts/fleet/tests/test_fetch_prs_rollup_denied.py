"""Tests for _fetch_prs_graphql's statusCheckRollup permission fallback.

`gh pr list --json statusCheckRollup` reads each check suite's workflow run,
which a GitHub App installation can only do with the Actions permission. On
a repo where the token lacks it the whole list fails with "Resource not
accessible by integration", the slice degrades every tick, and (before the
per-repo gate) the degraded gate parked reconcile and ingest for every repo.
These cases pin the fallback: the list is re-fetched without the rollup,
each head's `checks` comes from the REST head-check poll, the failing form is
not retried inside the window and is retried after it, and an unrelated
failure still returns None.

Hermetic: run_capture and _poll_head_checks are patched on the module.
Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout_rollup", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout_rollup", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

_REPO = "jakildev/irreden"
_DENIED = ("GraphQL: Resource not accessible by integration "
           "(repository.pullRequests.nodes.0.statusCheckRollup.nodes.0.commit."
           "statusCheckRollup.contexts.nodes.0.checkSuite.workflowRun)")
_LIST = json.dumps([
    {"number": 470, "headRefOid": "a" * 40, "labels": [], "author": {"login": "x"},
     "reviews": [], "body": "", "mergeable": "MERGEABLE"},
    {"number": 471, "headRefOid": None, "labels": [], "author": {"login": "x"},
     "reviews": [], "body": "", "mergeable": "MERGEABLE"},
])


class RollupDenied(unittest.TestCase):
    def setUp(self):
        self.calls = []
        _mod._rollup_denied_until.clear()
        self.now = 1_000_000.0

    def _gh(self, denied_stderr=_DENIED):
        def fake(cmd, cwd=None):
            fields = cmd[cmd.index("--json") + 1]
            self.calls.append(fields)
            if "statusCheckRollup" in fields:
                _mod._last_capture_stderr = denied_stderr
                return None
            return _LIST
        return fake

    def _poll(self, reading):
        def fake(repo, sha, keep):
            keep.add(Path(f"/etag/{sha}"))
            return (False, reading)
        return fake

    def _fetch(self, keep=None):
        with patch.object(_mod, "run_capture", side_effect=self._gh()), \
                patch.object(_mod, "_poll_head_checks", side_effect=self._poll("green")), \
                patch.object(_mod.time, "time", lambda: self.now), \
                patch.object(_mod, "log", lambda *_a, **_k: None):
            return _mod._fetch_prs_graphql(_REPO, keep)

    def test_denied_rollup_lists_without_it_and_reads_checks_over_rest(self):
        keep = set()
        prs = self._fetch(keep)
        self.assertEqual([pr["number"] for pr in prs], [470, 471])
        self.assertEqual(prs[0]["checks"], "green", "the REST reading fills checks")
        self.assertEqual(prs[1]["checks"], "unread", "a head with no sha withholds merge-ready")
        self.assertNotIn("statusCheckRollup", prs[0])
        self.assertIn(Path("/etag/" + "a" * 40), keep, "the poll's ETag entries join keep")
        self.assertEqual(len(self.calls), 2)
        self.assertIn("statusCheckRollup", self.calls[0])
        self.assertNotIn("statusCheckRollup", self.calls[1])

    def test_denied_repo_skips_the_failing_form_inside_the_window(self):
        self._fetch()
        self.calls.clear()
        self.now += 10
        prs = self._fetch()
        self.assertEqual(len(prs), 2)
        self.assertEqual(len(self.calls), 1)
        self.assertNotIn("statusCheckRollup", self.calls[0])

    def test_full_form_is_retried_after_the_window(self):
        self._fetch()
        self.calls.clear()
        self.now += _mod.ROLLUP_DENIED_RETRY_SECONDS + 1
        self._fetch()
        self.assertIn("statusCheckRollup", self.calls[0], "a granted permission is picked up")

    def test_other_failures_still_return_none(self):
        with patch.object(_mod, "run_capture", side_effect=self._gh("HTTP 502: bad gateway")), \
                patch.object(_mod, "log", lambda *_a, **_k: None):
            self.assertIsNone(_mod._fetch_prs_graphql(_REPO, set()))
        self.assertEqual(len(self.calls), 1)
        self.assertFalse(_mod._rollup_denied(_REPO, self.now))

    def test_unreadable_rest_page_reads_unread(self):
        with patch.object(_mod, "run_capture", side_effect=self._gh()), \
                patch.object(_mod, "_poll_head_checks", side_effect=self._poll(None)), \
                patch.object(_mod.time, "time", lambda: self.now), \
                patch.object(_mod, "log", lambda *_a, **_k: None):
            prs = _mod._fetch_prs_graphql(_REPO, set())
        self.assertEqual(prs[0]["checks"], "unread")


if __name__ == "__main__":
    unittest.main()
