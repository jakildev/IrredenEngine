"""Tests for fetch_prs's bounded mergeable=UNKNOWN re-query.

GraphQL's `mergeable` reads UNKNOWN while GitHub computes it, and the
computation is (re)started by the query itself whenever the base advances.
fetch_prs gates the GraphQL list behind a conditional REST GET, so on a repo
whose master moves constantly an ETag-flip refetch can land mid-recompute,
and the 304 fast path then serves UNKNOWN indefinitely — the merger, which
(correctly) reads UNKNOWN as "not computed", never sees a PR that is
actually CONFLICTING. These cases pin the re-query: a 304 with a reused list
that still carries UNKNOWN re-asks GraphQL, stops once the list settles, is
capped, resets on a real change, and never lets a failed re-query degrade a
valid cache.

The reuse path also has two staleness holes with the same fix shape: a
merged-or-closed PR whose row the 304 fast path keeps serving (its cached
`mergeable`, UNKNOWN or otherwise, outliving the PR — the detector's REST
body is the post-change open set, so the row is dropped against it), and a
200 whose GraphQL fetch then failed (the ETag advanced, so the next 304
would reuse the pre-change list — the reuse is bypassed once instead).

Hermetic: both network seams (conditional_get, _fetch_prs_graphql) are
patched on the module; a miss would raise, never reach gh/urllib.
Import the script via importlib because it has no .py extension.
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

_REPO = "jakildev/IrredenEngine"


def _pr(n, mergeable):
    # schema at the current PR_RECORD_SCHEMA ⇒ the reuse gate's cache-desync
    # marker is satisfied; closes_issues rides along, as the shipped
    # projection emits both together.
    return {"number": n, "mergeable": mergeable, "closes_issues": [],
            "schema": _mod.PR_RECORD_SCHEMA}


def _not_changed(*_a, **_k):
    return (False, None)


def _changed(*_a, **_k):
    return (True, None)


def _not_changed_open(*numbers):
    body = json.dumps([{"number": n} for n in numbers])
    return lambda *_a, **_k: (False, body)


class MergeableUnknownRequery(unittest.TestCase):
    def setUp(self):
        _mod._mergeable_requery_ticks.clear()
        getattr(_mod, "_prs_refetch_pending", set()).clear()
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


class StaleRowsOnReuse(unittest.TestCase):
    def setUp(self):
        _mod._mergeable_requery_ticks.clear()
        getattr(_mod, "_prs_refetch_pending", set()).clear()
        self.calls = 0

    def _graphql(self, result):
        def fake(repo):
            self.calls += 1
            return result
        return fake

    def test_merged_while_unknown_row_is_dropped_on_the_304_path(self):
        prev = [_pr(3455, "UNKNOWN"), _pr(3457, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed_open(3457)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql([])):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual([p["number"] for p in out], [3457],
                         "a row the open set no longer carries must not outlive the tick")
        self.assertEqual(self.calls, 0, "no UNKNOWN left ⇒ no GraphQL re-query either")

    def test_dropped_before_the_unknown_requery_decides(self):
        # The merged row was the only UNKNOWN: dropping it settles the list.
        prev = [_pr(3455, "UNKNOWN"), _pr(3457, "CONFLICTING")]
        with patch.object(_mod, "conditional_get", _not_changed_open(3457)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(None)):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual([p["number"] for p in out], [3457])
        self.assertEqual(self.calls, 0)

    def test_unchanged_open_set_reuses_prev_unmodified(self):
        prev = [_pr(1, "CONFLICTING"), _pr(2, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed_open(1, 2)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql([])):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertIs(out, prev)

    def test_unparseable_body_keeps_prev(self):
        prev = [_pr(1, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", lambda *a, **k: (False, "not json")), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql([])):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertIs(out, prev)

    def test_failed_fetch_after_a_change_bypasses_the_next_reuse(self):
        prev = [_pr(1, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(None)):
            self.assertIsNone(_mod.fetch_prs(_REPO, prev=prev))
        fresh = [_pr(1, "MERGEABLE"), _pr(2, "CONFLICTING")]
        with patch.object(_mod, "conditional_get", _not_changed_open(1, 2)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(fresh)):
            out = _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, 2, "the 304 after a failed 200 fetch must re-ask GraphQL")
        self.assertEqual([p["number"] for p in out], [1, 2])
        # …and the bypass is one-shot.
        with patch.object(_mod, "conditional_get", _not_changed_open(1, 2)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(fresh)):
            self.assertIs(_mod.fetch_prs(_REPO, prev=fresh), fresh)
        self.assertEqual(self.calls, 2)

    def test_bypass_is_per_repo(self):
        with patch.object(_mod, "conditional_get", _changed), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(None)):
            _mod.fetch_prs(_REPO, prev=[])
        prev = [_pr(9, "MERGEABLE")]
        with patch.object(_mod, "conditional_get", _not_changed_open(9)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(prev)):
            self.assertIs(_mod.fetch_prs("jakildev/irreden", prev=prev), prev)
        self.assertEqual(self.calls, 1)


if __name__ == "__main__":
    unittest.main()
