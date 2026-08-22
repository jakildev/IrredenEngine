"""Tests for fetch_prs's bounded mergeable=UNKNOWN re-query (#3038).

GraphQL's `mergeable` reads UNKNOWN while GitHub computes it, and the
computation is (re)started by the query itself whenever the base advances.
fetch_prs gates the GraphQL list behind a conditional REST GET, so on a
repo whose master moves constantly every ETag-flip refetch landed
mid-recompute and the 304 fast path then served UNKNOWN indefinitely — the
merger, which (correctly) reads UNKNOWN as "not computed", never saw the
engine repo's CONFLICTING PRs. These cases pin the re-query: a 304 with a
reused list that still carries UNKNOWN re-asks GraphQL, stops once the list
settles, is capped, resets on a real change, and never lets a failed
re-query degrade a valid cache.

Hermetic: both network seams (conditional_get, _fetch_prs_graphql) are
patched on the module; a miss would raise, never reach gh/urllib.
Import the script via importlib because it has no .py extension.
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

_REPO = "jakildev/IrredenEngine"


def _pr(n, mergeable):
    # closes_issues present ⇒ the reuse gate's cache-desync marker is satisfied.
    return {"number": n, "mergeable": mergeable, "closes_issues": []}


def _not_changed(*_a, **_k):
    return (False, None)


def _changed(*_a, **_k):
    return (True, None)


class MergeableUnknownRequery(unittest.TestCase):
    def setUp(self):
        _mod._mergeable_requery_ticks.clear()
        self.calls = 0

    def _graphql(self, result):
        def fake(repo):
            self.calls += 1
            return result
        return fake

    def test_304_with_unknown_requeries_graphql(self):
        prev = [_pr(1, "UNKNOWN"), _pr(2, "MERGEABLE")]
        settled = [_pr(1, "CONFLICTING"), _pr(2, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(settled)):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, 1, "UNKNOWN on a 304 must re-ask GraphQL")
        self.assertEqual(out[0]["mergeable"], "CONFLICTING")
        self.assertEqual(_mod._mergeable_requery_ticks.get(_REPO), 0,
                         "a settled list clears the budget")

    def test_304_with_unknown_requeries_graphql_settles_to_mergeable(self):
        prev = [_pr(1, "UNKNOWN"), _pr(2, "MERGEABLE")]
        settled = [_pr(1, "MERGEABLE"), _pr(2, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(settled)):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, 1, "UNKNOWN on a 304 must re-ask GraphQL")
        self.assertEqual(out[0]["mergeable"], "MERGEABLE")
        self.assertEqual(_mod._mergeable_requery_ticks.get(_REPO), 0,
                         "a settled list clears the budget")

    def test_304_without_unknown_reuses_prev(self):
        prev = [_pr(1, "CONFLICTING"), _pr(2, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql([])):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, 0, "no UNKNOWN ⇒ the 304 fast path stands")
        self.assertIs(out, prev)

    def test_requery_is_capped_then_gate_owns_cadence(self):
        prev = [_pr(1, "UNKNOWN")]
        still = [_pr(1, "UNKNOWN")]
        cap = _mod.MERGEABLE_UNKNOWN_REQUERY_CAP
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(still)):
            for _ in range(cap + 3):
                _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, cap,
                         "a never-settling PR re-queries at most CAP ticks")

    def test_real_change_resets_budget(self):
        prev = [_pr(1, "UNKNOWN")]
        still = [_pr(1, "UNKNOWN")]
        cap = _mod.MERGEABLE_UNKNOWN_REQUERY_CAP
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(still)):
            for _ in range(cap):
                _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, cap)
        # An ETag flip (200) refetches unconditionally and re-arms the budget.
        with patch.object(_mod, "conditional_get", _changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(still)):
            _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(_mod._mergeable_requery_ticks.get(_REPO), 0)
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(still)):
            _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, cap + 2,
                         "after a real change the UNKNOWN re-query runs again")

    def test_failed_requery_keeps_prev(self):
        prev = [_pr(1, "UNKNOWN")]
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(None)):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertIs(out, prev, "a failed re-query must not degrade a valid cache")
        self.assertEqual(_mod._mergeable_requery_ticks.get(_REPO), 1,
                         "…but it spends budget, so it cannot spin")

    def test_budget_is_per_repo(self):
        prev = [_pr(1, "UNKNOWN")]
        cap = _mod.MERGEABLE_UNKNOWN_REQUERY_CAP
        with patch.object(_mod, "conditional_get", _not_changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(prev)):
            for _ in range(cap):
                _mod.fetch_prs(_REPO, prev=prev)
            _mod.fetch_prs("jakildev/irreden", prev=prev)
        self.assertEqual(self.calls, cap + 1,
                         "exhausting one repo's budget leaves the other's intact")


if __name__ == "__main__":
    unittest.main()
