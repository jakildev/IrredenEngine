"""Tests for _fetch_prs_graphql's open-PR fetch window (#2743).

`gh pr list` defaults to --limit 30 and truncates past it silently, keeping the
NEWEST 30 by number. `repos.<repo>.prs[]` is the fleet's authoritative in-flight
signal, so the truncation dropped the OLDEST open PRs — the long-lived stalled
ones — out of every role's projection, the feedback and semantic-conflict lanes,
and the in-flight / stackable enrichers.

The control that inverts is TestOpenPrFetchWindow: it stubs run_capture with a
fake that honors --limit the way gh does, so the fetch's own behaviour decides
the verdict. Asserting `"--limit" in argv` would only be a change-detector on
the fix's own artifact (the #2723 trap), and an enrich-layer fixture cannot
discriminate at all — enrich_inflight_pr_tasks receives prs[] directly, so it
passes at any fetch limit. TestInflightMatchPastTheDefaultWindow is therefore
recorded as downstream-harm coverage, not as the control.

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

# gh's own documented default, the value that made the truncation silent.
_GH_DEFAULT_LIMIT = 30

# Wide enough that gh's default truncates it, narrow enough to stay legible.
_FIXTURE_COUNT = 40
_FIRST_PR = 1000


def _pr(n):
    return {
        "number": n,
        "title": f"pr {n}",
        "headRefName": f"claude/{n - 500}-some-task",
        "headRefOid": f"{n:040d}",
        "baseRefName": "master",
        "author": {"login": "jakildev"},
        "labels": [{"name": "fleet:approved"}],
        "mergeable": "MERGEABLE",
        "isDraft": False,
        "reviews": [],
        "updatedAt": "2026-08-07T00:00:00Z",
        "body": "",
    }


_FIXTURE = [_pr(n) for n in range(_FIRST_PR, _FIRST_PR + _FIXTURE_COUNT)]


def _fake_gh(argv, cwd=None):
    """Stand in for `gh pr list`, modelling its ARGUMENT PARSING, not just its
    endpoint (scripts/fleet/CLAUDE.md).

    Fails closed on any argv shape it does not model, so a future rewrite of
    the call site cannot quietly slip past the window this suite guards.
    """
    if argv[:3] != ["gh", "pr", "list"]:
        raise AssertionError(f"unmodelled command: {argv!r}")
    for required in ("--repo", "--state", "--json"):
        if required not in argv:
            raise AssertionError(f"gh pr list requires {required}: {argv!r}")

    if "--limit" in argv:
        raw = argv[argv.index("--limit") + 1]
        # The real binary rejects a non-integer limit before issuing a request.
        if not raw.lstrip("-").isdigit() or int(raw) < 1:
            raise AssertionError(f"gh rejects --limit {raw!r}")
        limit = int(raw)
    else:
        limit = _GH_DEFAULT_LIMIT

    # gh returns newest-first and keeps the first `limit` — which is why the
    # OLDEST open PRs are the ones that vanish.
    newest_first = sorted(_FIXTURE, key=lambda pr: pr["number"], reverse=True)
    return json.dumps(newest_first[:limit])


class TestStubFidelity(unittest.TestCase):
    """The stub must be able to express the bug, or every arm below is vacuous."""

    def test_stub_truncates_to_ghs_default_when_limit_absent(self):
        without = json.loads(_fake_gh(["gh", "pr", "list", "--repo", _REPO,
                                       "--state", "open", "--json", "number"]))
        self.assertEqual(len(without), _GH_DEFAULT_LIMIT,
                         "stub must reproduce gh's silent default-30 truncation")

    def test_stub_drops_the_oldest_prs_first(self):
        without = json.loads(_fake_gh(["gh", "pr", "list", "--repo", _REPO,
                                       "--state", "open", "--json", "number"]))
        kept = {pr["number"] for pr in without}
        oldest = _FIRST_PR
        newest = _FIRST_PR + _FIXTURE_COUNT - 1
        self.assertNotIn(oldest, kept, "the oldest open PR is what falls out")
        self.assertIn(newest, kept)

    def test_stub_fails_closed_on_an_unmodelled_command(self):
        with self.assertRaises(AssertionError):
            _fake_gh(["gh", "issue", "list", "--repo", _REPO])


class TestOpenPrFetchWindow(unittest.TestCase):
    """The behavioural control. Red on the pre-fix tree (30 != 40)."""

    def test_fetch_returns_every_open_pr_past_ghs_default(self):
        with patch.object(_mod, "run_capture", side_effect=_fake_gh):
            prs = _mod._fetch_prs_graphql(_REPO)
        self.assertEqual(
            len(prs), _FIXTURE_COUNT,
            f"open-PR fetch kept {len(prs)} of {_FIXTURE_COUNT} — the list is "
            "truncated at gh's default and the oldest PRs are invisible")

    def test_the_oldest_open_pr_survives_the_fetch(self):
        # The count assertion alone would pass a fetch that returned 40 wrong
        # records; name the PR whose disappearance is the actual harm.
        with patch.object(_mod, "run_capture", side_effect=_fake_gh):
            prs = _mod._fetch_prs_graphql(_REPO)
        self.assertIn(_FIRST_PR, {pr["number"] for pr in prs},
                      "the oldest open PR is the first one gh drops")

    def test_fetch_still_sorts_ascending_by_number(self):
        # The widened list must stay quiescent for the projection hash.
        with patch.object(_mod, "run_capture", side_effect=_fake_gh):
            prs = _mod._fetch_prs_graphql(_REPO)
        numbers = [pr["number"] for pr in prs]
        self.assertEqual(numbers, sorted(numbers))


class TestTruncationGuard(unittest.TestCase):
    """The guard converts a silent narrowing into an operator-visible one.

    These arms name OPEN_PR_FETCH_LIMIT, which does not exist on the pre-fix
    tree, so they error rather than fail there — they are the fix's own
    contract, not part of the inverting control above.
    """

    def _fetch_capturing_log(self, limit):
        seen = []
        with patch.object(_mod, "OPEN_PR_FETCH_LIMIT", limit), \
                patch.object(_mod, "run_capture", side_effect=_fake_gh), \
                patch.object(_mod, "log", side_effect=seen.append):
            prs = _mod._fetch_prs_graphql(_REPO)
        return prs, seen

    def test_guard_fires_when_the_result_reaches_the_cap(self):
        prs, logged = self._fetch_capturing_log(5)
        self.assertEqual(len(prs), 5)
        self.assertTrue(
            any("may be truncated" in m for m in logged),
            f"a capped result must warn; logged={logged!r}")

    def test_guard_is_silent_below_the_cap(self):
        # Drive the SHIPPED constant, not a copy of it — a hardcoded 200 here
        # would keep passing if the shipped limit were lowered under the tree's
        # real open-PR count, which is the state the guard exists to report.
        prs, logged = self._fetch_capturing_log(_mod.OPEN_PR_FETCH_LIMIT)
        self.assertEqual(len(prs), _FIXTURE_COUNT)
        self.assertFalse([m for m in logged if "may be truncated" in m],
                         f"an untruncated result must not warn; logged={logged!r}")

    def test_shipped_limit_clears_the_default_it_replaces(self):
        self.assertGreater(_mod.OPEN_PR_FETCH_LIMIT, _GH_DEFAULT_LIMIT)


class TestInflightMatchPastTheDefaultWindow(unittest.TestCase):
    """Downstream-harm coverage, NOT a control.

    enrich_inflight_pr_tasks takes prs[] directly, so this passes at any fetch
    limit. It documents what the truncation cost: a task shadowed by an open PR
    read as owner-free and claimable (#2376).
    """

    def test_task_shadowed_by_an_old_pr_is_tagged_inflight(self):
        old_pr = _FIXTURE[0]
        issue = old_pr["headRefName"].split("/")[1].split("-")[0]
        state = {"repos": {"engine": {
            "prs": [dict(p, labels=["fleet:approved"]) for p in _FIXTURE],
            "tasks": {"open": [{"id": f"#{issue}", "issue": f"#{issue}",
                                "owner": "free"}]},
        }}}
        _mod.enrich_inflight_pr_tasks(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIsNotNone(
            task.get("inflight_pr"),
            "a task whose issue is shadowed by an open PR must not read free")
        self.assertEqual(task["inflight_pr"]["number"], old_pr["number"])


if __name__ == "__main__":
    unittest.main()
