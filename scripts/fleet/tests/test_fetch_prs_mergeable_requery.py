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

A check completing on a head moves neither ETag, so a PR's cached `checks`
would outlive the check: for every PR whose merger signal reads it, the
head's check runs and commit statuses are polled by ETag, and a move on
either refetches the list.

Hermetic: both network seams (conditional_get, _fetch_prs_graphql) are
patched on the module; a miss would raise, never reach gh/urllib. The
head-check ETag directory is a temporary one.
Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
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


def _approved(n, checks, mergeable="MERGEABLE", labels=("fleet:approved",)):
    return {"number": n, "mergeable": mergeable, "checks": checks,
            "labels": list(labels), "baseRefName": "master",
            "headRefOid": f"{n:040d}", "closes_issues": [],
            "schema": _mod.PR_RECORD_SCHEMA}


class FakeChecks:
    """The REST reads fetch_prs makes, with GitHub's ETag behavior for a
    head's check runs and commit statuses: a page is `changed` exactly when
    its body differs from the last one served for the same request. The open
    list is always a 304. Any other path raises."""

    def __init__(self, open_numbers):
        self.open_body = json.dumps([{"number": n} for n in open_numbers])
        self.runs = {}      # sha -> check-run list
        self.statuses = {}  # sha -> commit-status list
        self.served = {}    # (sha, surface, page) -> last body
        self.polled = []    # (sha, page) of the check-runs surface, in order
        self.surfaces = []  # (sha, surface), in order
        self.fail = set()   # shas whose poll errors

    def __call__(self, _repo, path, params=None, **_k):
        if path == "pulls":
            return (False, self.open_body)
        _commits, sha, surface = path.split("/")
        lists = {"check-runs": ("check_runs", self.runs),
                 "status": ("statuses", self.statuses)}
        key, source = lists[surface]
        page = int((params or {}).get("page", "1"))
        self.surfaces.append((sha, surface))
        if surface == "check-runs":
            self.polled.append((sha, page))
        if sha in self.fail:
            return (True, None)
        per = _mod.HEAD_CHECKS_PER_PAGE
        items = source.get(sha, [])
        body = json.dumps({"total_count": len(items),
                           key: items[(page - 1) * per:page * per]})
        changed = self.served.get((sha, surface, page)) != body
        self.served[(sha, surface, page)] = body
        return (changed, body)


class CheckStateOnReuse(unittest.TestCase):
    def setUp(self):
        _mod._mergeable_requery_ticks.clear()
        _mod._prs_refetch_pending.clear()
        self.calls = 0
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.etag_dir = Path(tmp.name)
        dir_patch = patch.object(_mod, "HEAD_CHECKS_ETAG_DIR", self.etag_dir)
        dir_patch.start()
        self.addCleanup(dir_patch.stop)

    def _graphql(self, result):
        def fake(repo):
            self.calls += 1
            return result
        return fake

    def _tick(self, fake, prev, fresh):
        with patch.object(_mod, "conditional_get", fake), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(fresh)):
            return _mod.fetch_prs(_REPO, prev=prev)

    @staticmethod
    def _signal(pr):
        return _mod._merger_action_signal(set(pr["labels"]), pr["mergeable"],
                                          pr["baseRefName"], pr["checks"])

    def test_a_check_turning_red_reaches_the_record_and_slice_in_the_same_tick(self):
        fake = FakeChecks([1])
        sha = f"{1:040d}"
        fake.runs[sha] = [{"id": 1, "status": "in_progress", "conclusion": None}]
        prev = [_approved(1, "unread")]
        self._tick(fake, prev, prev)   # first sight of the head primes its ETag
        self.assertIs(self._tick(fake, prev, None), prev, "a settled head is reused")
        calls_before = self.calls
        fake.runs[sha] = [{"id": 1, "status": "completed", "conclusion": "failure"}]
        out = self._tick(fake, prev, [_approved(1, "red")])
        self.assertEqual(self.calls, calls_before + 1,
                         "a check move on a 304 tick must re-ask GraphQL")
        self.assertEqual(out[0]["checks"], "red")
        self.assertEqual(self._signal(prev[0]), "checks-unread",
                         "a running check is never reported red")
        self.assertEqual(self._signal(out[0]), "checks-red")
        sliced = _mod.slice_merger({"repos": {"engine": {"prs": out}}})
        self.assertEqual([p["checks"] for p in sliced["prs"]], ["red"])

    def test_a_red_check_recovering_clears_checks_red(self):
        fake = FakeChecks([1])
        sha = f"{1:040d}"
        fake.runs[sha] = [{"id": 1, "status": "completed", "conclusion": "failure"}]
        prev = [_approved(1, "red")]
        self._tick(fake, prev, prev)
        fake.runs[sha] = [{"id": 2, "status": "completed", "conclusion": "success"}]
        out = self._tick(fake, prev, [_approved(1, "green")])
        self.assertEqual(self._signal(out[0]), "merge-ready")

    def test_unchanged_checks_reuse_prev_with_no_graphql(self):
        fake = FakeChecks([1])
        prev = [_approved(1, "green")]
        self._tick(fake, prev, prev)   # first sight of the head primes its ETag
        self.calls = 0
        out = self._tick(fake, prev, None)
        self.assertIs(out, prev)
        self.assertEqual(self.calls, 0, "an unchanged check list spends no GraphQL")

    def test_a_commit_status_turning_red_refetches_in_the_same_tick(self):
        # A StatusContext rides statusCheckRollup but never the check-runs
        # list, so only the commit-status poll can see it move.
        fake = FakeChecks([1])
        sha = f"{1:040d}"
        fake.statuses[sha] = [{"id": 1, "context": "ci/legacy", "state": "pending"}]
        prev = [_approved(1, "unread")]
        self._tick(fake, prev, prev)
        self.assertIs(self._tick(fake, prev, None), prev, "a settled head is reused")
        self.calls = 0
        fake.statuses[sha] = [{"id": 2, "context": "ci/legacy", "state": "failure"}]
        out = self._tick(fake, prev, [_approved(1, "red")])
        self.assertEqual(self.calls, 1, "a status move on a 304 tick must re-ask GraphQL")
        self.assertEqual(self._signal(out[0]), "checks-red")

    def test_a_commit_status_recovering_clears_checks_red(self):
        fake = FakeChecks([1])
        sha = f"{1:040d}"
        fake.statuses[sha] = [{"id": 1, "context": "ci/legacy", "state": "error"}]
        prev = [_approved(1, "red")]
        self._tick(fake, prev, prev)
        self.calls = 0
        fake.statuses[sha] = [{"id": 2, "context": "ci/legacy", "state": "success"}]
        out = self._tick(fake, prev, [_approved(1, "green")])
        self.assertEqual(self.calls, 1)
        self.assertEqual(self._signal(out[0]), "merge-ready")

    def test_every_check_reading_head_is_polled_on_both_surfaces(self):
        fake = FakeChecks([1, 2, 3, 4, 5])
        prev = [_approved(1, "green"),
                _approved(2, "red"),
                _approved(3, "unread"),
                _approved(4, "green", mergeable="CONFLICTING"),
                _approved(5, "green", labels=())]
        self._tick(fake, prev, prev)
        polled = [f"{n:040d}" for n in (1, 2, 3)]
        self.assertEqual(sorted(set(fake.surfaces)),
                         sorted((sha, surface) for sha in polled
                                for surface in ("check-runs", "status")))

    def test_a_poll_error_reads_as_moved(self):
        fake = FakeChecks([1])
        prev = [_approved(1, "green")]
        self._tick(fake, prev, prev)
        self.calls = 0
        fake.fail.add(f"{1:040d}")
        self._tick(fake, prev, prev)
        self.assertEqual(self.calls, 1, "an unreadable check list is never read as unchanged")

    def test_a_failed_refetch_after_a_move_bypasses_the_next_reuse(self):
        fake = FakeChecks([1])
        sha = f"{1:040d}"
        prev = [_approved(1, "green")]
        self._tick(fake, prev, prev)
        fake.runs[sha] = [{"id": 1, "status": "completed", "conclusion": "failure"}]
        self.assertIs(self._tick(fake, prev, None), prev)
        # The move's ETag already advanced, so the next poll is a 304: only
        # the bypass keeps the stale green from being served again.
        self.calls = 0
        out = self._tick(fake, prev, [_approved(1, "red")])
        self.assertEqual(self.calls, 1)
        self.assertEqual(out[0]["checks"], "red")

    def test_the_200_path_primes_so_the_next_304_does_not_refetch(self):
        fake = FakeChecks([1])
        prev = [_approved(1, "green")]
        def changed_open(repo, path, **k):
            return (True, None) if path == "pulls" else fake(repo, path, **k)
        with patch.object(_mod, "conditional_get", changed_open), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(prev)):
            _mod.fetch_prs(_REPO, prev=prev)
        self.assertEqual(self.calls, 1)
        self.assertIs(self._tick(fake, prev, None), prev)
        self.assertEqual(self.calls, 1, "a head primed on the 200 tick is a 304 on the next")

    def _changed_open(self, fake):
        def get(repo, path, **k):
            return (True, None) if path == "pulls" else fake(repo, path, **k)
        return get

    def test_a_fresh_scout_primes_the_fetched_heads(self):
        fake = FakeChecks([1, 2])
        fake.runs[f"{1:040d}"] = [{"id": 1, "status": "completed", "conclusion": "success"},
                                  {"id": 2, "status": "completed", "conclusion": "skipped"}]
        fake.statuses[f"{1:040d}"] = [{"id": 3, "context": "ci/legacy", "state": "success"}]
        fake.runs[f"{2:040d}"] = [{"id": 4, "status": "in_progress", "conclusion": None}]
        fresh = [_approved(1, "green"), _approved(2, "unread")]
        with patch.object(_mod, "conditional_get", self._changed_open(fake)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(fresh)):
            self.assertIs(_mod.fetch_prs(_REPO, prev=None), fresh)
        self.assertEqual(self.calls, 1)
        self.assertIs(self._tick(fake, fresh, None), fresh)
        self.assertEqual(self.calls, 1, "a fresh scout's 200 costs one GraphQL call, not two")

    def test_a_head_only_the_new_list_carries_is_primed(self):
        fake = FakeChecks([1, 2])
        prev = [_approved(1, "green")]
        fresh = [_approved(1, "green"), _approved(2, "green")]
        self._tick(fake, prev, prev)
        self.calls = 0
        with patch.object(_mod, "conditional_get", self._changed_open(fake)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(fresh)):
            _mod.fetch_prs(_REPO, prev=prev)
        self.assertIs(self._tick(fake, fresh, None), fresh)
        self.assertEqual(self.calls, 1, "a PR new to the list is primed by the 200 that fetched it")

    def test_a_requery_primes_a_head_it_makes_check_reading(self):
        # UNKNOWN is not merge-ready, so the pre-fetch poll skips the head;
        # the re-query settling it to MERGEABLE makes the signal read checks.
        fake = FakeChecks([1])
        prev = [_approved(1, "green", mergeable="UNKNOWN")]
        settled = [_approved(1, "green")]
        self.assertEqual(self._tick(fake, prev, settled), settled)
        self.assertEqual(self.calls, 1)
        self.assertIs(self._tick(fake, settled, None), settled)
        self.assertEqual(self.calls, 1, "the re-query's new check-reading head is primed")

    def test_a_check_moving_after_the_fetch_bypasses_the_next_reuse(self):
        # The record says running; by the post-fetch prime the check failed.
        # The prime's ETag now covers the failure, so only the disagreement
        # keeps the stale `unread` from being served.
        fake = FakeChecks([1])
        fake.runs[f"{1:040d}"] = [{"id": 1, "status": "completed", "conclusion": "failure"}]
        stale = [_approved(1, "unread")]
        with patch.object(_mod, "conditional_get", self._changed_open(fake)), \
             patch.object(_mod, "_fetch_prs_graphql", self._graphql(stale)):
            _mod.fetch_prs(_REPO, prev=None)
        out = self._tick(fake, stale, [_approved(1, "red")])
        self.assertEqual(self.calls, 2, "a prime that disagrees with the record re-asks GraphQL")
        self.assertEqual(out[0]["checks"], "red")

    def test_a_full_page_reads_the_next_and_a_list_full_at_the_cap_is_moved(self):
        per, cap = _mod.HEAD_CHECKS_PER_PAGE, _mod.HEAD_CHECKS_MAX_PAGES
        fake = FakeChecks([1, 2])
        fake.runs[f"{1:040d}"] = [{"id": i} for i in range(per + 1)]
        fake.runs[f"{2:040d}"] = [{"id": i} for i in range(per * cap)]
        prev = [_approved(1, "green"), _approved(2, "green")]
        self._tick(fake, prev, prev)
        self.assertIn((f"{1:040d}", 2), fake.polled, "a full first page reads page 2")
        self.assertNotIn((f"{1:040d}", 3), fake.polled, "a short page ends the read")
        prev = [_approved(2, "green")]
        self._tick(fake, prev, prev)
        self.calls = 0
        self._tick(fake, prev, prev)
        self.assertEqual(self.calls, 1, "a list still full at the cap is never read as complete")

    def test_entries_of_heads_no_longer_polled_are_pruned(self):
        path = "commits/{}/check-runs"
        params = {"per_page": str(_mod.HEAD_CHECKS_PER_PAGE), "page": "1"}
        def entry(n):
            return _mod.cache_entry_path(_REPO, path.format(f"{n:040d}"),
                                         params=params, cache_dir=self.etag_dir)
        for n in (1, 9):
            entry(n).parent.mkdir(parents=True, exist_ok=True)
            entry(n).write_text("{}")
        fake = FakeChecks([1])
        self._tick(fake, [_approved(1, "green")], None)
        self.assertTrue(entry(1).exists(), "a polled head keeps its entry")
        self.assertFalse(entry(9).exists(), "a head no longer polled loses its entry")


if __name__ == "__main__":
    unittest.main()
