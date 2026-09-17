"""fetch_recent_merged_prs returns the `limit` newest merges by merged_at
regardless of PR number: it walks the newest-UPDATED closed page(s)
(updated_at >= merged_at, so that ordering bounds merge recency), stops once
the page tail is provably older than the limit-th newest merge already seen,
dedupes rows served on two pages, and caps at 3 pages so a large label sweep
(which bumps updated_at on many old merges at once) degrades to a best-effort
list plus an escalate-then-quiet warn rather than an unbounded walk.

The `conditional_get` stub emulates real REST /pulls pagination
(state=closed, sort in {created, updated}, direction=desc, page/per_page)
rather than special-casing a call site, so a sort=created fetch fails these
arms for the real reason: it never reaches an old-numbered PR once enough
newer-numbered PRs have merged.

Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet (the
alerts dir is a temp dir).
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

fetch_recent_merged_prs = _mod.fetch_recent_merged_prs

_REPO = "jakildev/IrredenEngine"
_BASE = datetime(2020, 1, 1)


def _ts(offset_minutes):
    return (_BASE + timedelta(minutes=offset_minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")


def _pr(number, created, merged=None, updated=None):
    if updated is None:
        updated = merged if merged is not None else created
    return {
        "number": number,
        "title": f"pr {number}",
        "head": {"ref": f"feat/{number}"},
        "base": {"ref": "master"},
        "created_at": _ts(created),
        "merged_at": _ts(merged) if merged is not None else None,
        "updated_at": _ts(updated),
    }


class _PagingStub:
    """Emulates REST /pulls?state=closed&sort=<created|updated>&direction=desc
    pagination over a fixed row list, sorted fresh per call (as GitHub would
    re-sort a live population), and counts pages served.
    """

    def __init__(self, rows):
        self._rows = rows
        self.pages_served = 0

    def __call__(self, repo_slug, path, params=None, **_kwargs):
        params = params or {}
        assert params.get("state") == "closed"
        sort_key = {"created": "created_at", "updated": "updated_at"}[params["sort"]]
        assert params.get("direction") == "desc"
        per_page = int(params.get("per_page", 100))
        page = int(params.get("page", "1"))
        ordered = sorted(self._rows, key=lambda r: r[sort_key], reverse=True)
        start = (page - 1) * per_page
        window = ordered[start:start + per_page]
        self.pages_served = max(self.pages_served, page)
        return (True, json.dumps(window))


class _HermeticCase(unittest.TestCase):
    """Alerts go to a temp dir and the per-repo streak starts at zero."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.alerts_dir = Path(tmp.name)
        patcher = patch.object(_mod, "_alerts_dir", return_value=self.alerts_dir)
        patcher.start()
        self.addCleanup(patcher.stop)
        # getattr so a pre-fix scout (no streak dict) fails the arms on
        # their own assertions rather than erroring here.
        streak = getattr(_mod, "_recent_merged_incomplete_streak", None)
        if streak is not None:
            streak.clear()
            self.addCleanup(streak.clear)


class TestOldNumberedLateMerge(_HermeticCase):
    """An old-numbered PR that merges after 30 newer-numbered PRs is present,
    and this population costs exactly one page."""

    def setUp(self):
        super().setUp()
        rows = [_pr(1, created=0, merged=1_000_000)]
        for n in range(2, 132):
            rows.append(_pr(n, created=n, merged=1000 + n))
        for n in (9001, 9002, 9003):
            rows.append(_pr(n, created=5, merged=None, updated=1))
        self.stub = _PagingStub(rows)
        patcher = patch.object(_mod, "conditional_get", side_effect=self.stub)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_oldest_created_newest_merged_pr_present(self):
        out = fetch_recent_merged_prs(_REPO)
        self.assertIsNotNone(out)
        numbers = [pr["number"] for pr in out]
        self.assertIn(1, numbers,
                      "PR #1 (oldest-created, newest-merged) must survive — "
                      "sort=created would floor the fetch below it")
        self.assertEqual(len(numbers), 30)
        self.assertEqual(self.stub.pages_served, 1)

    def test_sort_created_fetch_drops_the_pr(self):
        # A newest-created single page sliced to `limit` never places #1 in
        # the window, so the arm above discriminates on the sort key.
        changed, body = self.stub(_REPO, "pulls", {
            "state": "closed", "sort": "created", "direction": "desc",
            "per_page": "100",
        })
        raw = json.loads(body)
        merged = [pr for pr in raw if pr.get("merged_at")][:30]
        self.assertNotIn(1, [pr["number"] for pr in merged])


class TestChurnBurstPagesForWindow(_HermeticCase):
    """A label-sweep churn burst that dominates page 1 by updated_at is paged
    past to find the true newest-merged window."""

    def setUp(self):
        super().setUp()
        rows = []
        for n in range(1, 101):
            # Merged long ago, but updated_at bumped recently by a sweep —
            # sorts ahead of the real window on updated_at alone.
            rows.append(_pr(n, created=n, merged=100 + n, updated=10_000_000))
        for n in range(9001, 9031):
            rows.append(_pr(n, created=n, merged=9_000_000 + n, updated=9_000_000 + n))
        self.stub = _PagingStub(rows)
        patcher = patch.object(_mod, "conditional_get", side_effect=self.stub)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_real_window_found_past_the_churn_page(self):
        out = fetch_recent_merged_prs(_REPO)
        self.assertIsNotNone(out)
        numbers = {pr["number"] for pr in out}
        self.assertEqual(numbers, set(range(9001, 9031)),
                          "result must be exactly the 30 real merges, no churn rows")
        self.assertEqual(self.stub.pages_served, 2)


class TestCapHitReturnsListNeverNone(_HermeticCase):
    """Exhausting the 3-page cap still returns a list (the newest `limit`
    merges among the rows actually seen); the warn escalates then quiets."""

    def setUp(self):
        super().setUp()
        # 400 rows, all sharing one updated_at so the completeness check can
        # never fire early (every page tail looks equally "fresh"), forcing
        # all 3 pages before the cap and leaving 100 rows never fetched.
        rows = [_pr(n, created=n, merged=1000 + n, updated=10_000_000)
                for n in range(1, 401)]
        self.stub = _PagingStub(rows)
        patcher = patch.object(_mod, "conditional_get", side_effect=self.stub)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_cap_hit_returns_best_effort_list(self):
        with patch.object(_mod, "log") as mock_log:
            out = fetch_recent_merged_prs(_REPO)
        self.assertIsNotNone(out)
        self.assertEqual(len(out), 30)
        # Only rows 1..300 are ever fetched (3 pages); the true global top 30
        # (301..400) is unreachable, so the best-effort answer is 271..300.
        self.assertEqual({pr["number"] for pr in out}, set(range(271, 301)))
        self.assertEqual(self.stub.pages_served, 3)
        mock_log.assert_called_once()
        self.assertIn("window incomplete after 3 pages", mock_log.call_args[0][0])
        alert = self.alerts_dir / "state-scout-recent-merged-jakildev-IrredenEngine"
        self.assertIn("consecutive_ticks=1\n", alert.read_text())

    def test_repeat_ticks_stay_quiet_and_rewrite_the_alert(self):
        with patch.object(_mod, "log") as mock_log:
            for _ in range(3):
                fetch_recent_merged_prs(_REPO)
        mock_log.assert_called_once()
        alert = self.alerts_dir / "state-scout-recent-merged-jakildev-IrredenEngine"
        self.assertIn("consecutive_ticks=3\n", alert.read_text())
        # A complete window clears the streak and the alert with one line.
        self.stub._rows = self.stub._rows[:50]
        with patch.object(_mod, "log") as mock_log:
            fetch_recent_merged_prs(_REPO)
        mock_log.assert_called_once()
        self.assertIn("window complete again", mock_log.call_args[0][0])
        self.assertFalse(alert.exists())


class TestShapeAndOrderUnchanged(_HermeticCase):
    """Record shape (5 keys) and ascending-by-number order."""

    def setUp(self):
        super().setUp()
        rows = [_pr(1, created=0, merged=1_000_000)]
        for n in range(2, 132):
            rows.append(_pr(n, created=n, merged=1000 + n))
        patcher = patch.object(_mod, "conditional_get",
                               side_effect=_PagingStub(rows))
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_record_shape_and_ascending_order(self):
        out = fetch_recent_merged_prs(_REPO)
        self.assertIsNotNone(out)
        for pr in out:
            self.assertEqual(
                set(pr.keys()),
                {"number", "title", "headRefName", "baseRefName", "mergedAt"})
        numbers = [pr["number"] for pr in out]
        self.assertEqual(numbers, sorted(numbers))
        self.assertEqual(len(numbers), len(set(numbers)))


class TestDedupeAcrossPageBoundary(_HermeticCase):
    """sort=updated is not stable between the page-1 and page-2 fetch
    instants, so a PR can be served on both pages; a window row served twice
    never yields a duplicate record (a plain list would return 9030 twice
    and drop 9001)."""

    def setUp(self):
        super().setUp()
        # 99 churn rows fill page 1 down to its tail, which is the newest
        # real merge (9030); page 2 holds the other 29 real merges.
        rows = []
        for n in range(1, 100):
            rows.append(_pr(n, created=n, merged=100 + n, updated=10_000_000))
        for n in range(9001, 9031):
            rows.append(_pr(n, created=n, merged=9_000_000 + n, updated=9_000_000 + n))
        self.stub = _PagingStub(rows)
        tail = _pr(9030, created=9030, merged=9_009_030, updated=9_009_030)

        def _flaky_call(repo_slug, path, params=None, **kwargs):
            changed, body = self.stub(repo_slug, path, params=params, **kwargs)
            if params.get("page") == "2":
                # 9030 (the page-1 tail) shifts onto page 2 between fetch
                # instants and is served on both pages.
                body = json.dumps([tail] + json.loads(body))
            return (changed, body)

        patcher = patch.object(_mod, "conditional_get", side_effect=_flaky_call)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_no_duplicate_numbers_in_result(self):
        out = fetch_recent_merged_prs(_REPO)
        self.assertIsNotNone(out)
        self.assertEqual([pr["number"] for pr in out], list(range(9001, 9031)))
        self.assertEqual(self.stub.pages_served, 2)


if __name__ == "__main__":
    unittest.main()
