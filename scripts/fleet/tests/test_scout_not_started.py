"""Tests for the scout's never-started reading and its standing alert.

A red PR whose failed checks are jobs GitHub refused to start (`failure`, no
steps, a failure-level annotation) carries that annotation as `not_started`
on its record; a standing alert per repo raises it. Every `gh` call goes
through a router that raises on a miss, so a path the subject was not meant to
read fails the test instead of reaching GitHub.
"""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from test_scout_degraded_fetch import _mod, _ScoutTickHarness

REPO = "example/repo"
SHA = "1" * 40
BILLING = ("The job was not started because recent account payments have failed or "
           "your spending limit needs to be increased. Please check the "
           "'Billing & plans' section in your settings")
BILLING_TEXT = BILLING.replace("'", '"')
NOTICE = {"annotation_level": "notice", "message": "ubuntu-latest migration"}
REFUSAL = {"annotation_level": "failure", "message": BILLING}


def _check(job_id, conclusion="FAILURE"):
    return {"__typename": "CheckRun", "status": "COMPLETED", "conclusion": conclusion,
            "detailsUrl": f"https://github.com/{REPO}/actions/runs/9/job/{job_id}"}


def _pr(number, rollup):
    return {"number": number, "title": "t", "headRefName": f"b{number}", "headRefOid": SHA,
            "baseRefName": "master", "author": {"login": "bot"}, "labels": [],
            "mergeable": "MERGEABLE", "isDraft": False, "reviews": [],
            "updatedAt": "2026-10-24T00:00:00Z", "body": "", "statusCheckRollup": rollup}


def _job_path(job_id):
    return f"repos/{REPO}/actions/jobs/{job_id}"


def _annotations_path(job_id):
    return f"repos/{REPO}/check-runs/{job_id}/annotations?per_page=100"


class _FakeGh:
    """`run_capture` stand-in: `gh pr list` serves `prs`, `gh api <path>` serves
    `api[path]` (None models a failed call), and any other path raises."""

    def __init__(self, prs, api):
        self.prs, self.api, self.paths = prs, api, []

    def __call__(self, argv, cwd=None):
        if argv[:3] == ["gh", "pr", "list"]:
            return json.dumps(self.prs)
        if argv[:2] == ["gh", "api"]:
            self.paths.append(argv[2])
            reply = self.api[argv[2]]
            return None if reply is None else json.dumps(reply)
        raise AssertionError(f"unexpected command {argv}")

    def jobs_read(self):
        return [p for p in self.paths if "/actions/jobs/" in p]


class _FakeHeadChecks:
    """`conditional_get` stand-in serving a head's two REST check surfaces a
    page at a time; a surface given as None models an unparseable page."""

    def __init__(self, runs, statuses=()):
        self.lists = {"check-runs": ("check_runs", runs), "status": ("statuses", statuses)}

    def __call__(self, repo, path, params=None, cache_dir=None):
        key, items = self.lists[path.rsplit("/", 1)[1]]
        if items is None:
            return True, None
        per = _mod.HEAD_CHECKS_PER_PAGE
        page = int(params["page"])
        return True, json.dumps({key: list(items)[(page - 1) * per:page * per]})


def _run(job_id, conclusion="failure"):
    return {"id": job_id, "status": "completed", "conclusion": conclusion}


class _ReadingCase(unittest.TestCase):

    def setUp(self):
        _mod._not_started_memo.clear()
        self.addCleanup(_mod._not_started_memo.clear)
        _mod._rollup_denied_until.pop(REPO, None)

    def fetch(self, prs, api):
        gh = _FakeGh(prs, api)
        with patch.object(_mod, "run_capture", gh), patch.object(_mod, "log"):
            return _mod._fetch_prs_graphql(REPO), gh

    def fetch_denied(self, head_checks, api):
        _mod._rollup_denied_until[REPO] = 4102444800
        self.addCleanup(_mod._rollup_denied_until.pop, REPO, None)
        with patch.object(_mod, "conditional_get", head_checks):
            return self.fetch([_pr(1, None)], api)

    @staticmethod
    def refused(job_id):
        return {_job_path(job_id): {"id": job_id, "conclusion": "failure", "steps": []},
                _annotations_path(job_id): [NOTICE, REFUSAL]}


class TestPrRecord(_ReadingCase):

    def test_red_pr_with_a_never_started_job_records_the_annotation(self):
        prs, _ = self.fetch([_pr(1, [_check(555)])], self.refused(555))
        self.assertEqual(prs[0]["checks"], "red")
        self.assertEqual(prs[0]["not_started"], BILLING_TEXT)
        self.assertEqual(prs[0]["schema"], 9)

    def test_red_pr_whose_job_ran_a_step_has_no_not_started_key(self):
        api = {_job_path(555): {"id": 555, "conclusion": "failure",
                                "steps": [{"name": "build"}]}}
        prs, gh = self.fetch([_pr(1, [_check(555)])], api)
        self.assertEqual(prs[0]["checks"], "red")
        self.assertNotIn("not_started", prs[0])
        self.assertEqual([p for p in gh.paths if "annotations" in p], [])

    def test_green_pr_reads_no_job(self):
        green = {"__typename": "CheckRun", "status": "COMPLETED", "conclusion": "SUCCESS",
                 "detailsUrl": f"https://github.com/{REPO}/actions/runs/9/job/7"}
        prs, gh = self.fetch([_pr(1, [green])], {})
        self.assertEqual(prs[0]["checks"], "green")
        self.assertNotIn("not_started", prs[0])
        self.assertEqual(gh.paths, [])

    def test_one_failed_check_that_ran_makes_the_pr_an_ordinary_red(self):
        api = {**self.refused(555),
               _job_path(556): {"id": 556, "conclusion": "failure", "steps": [{"name": "t"}]}}
        prs, _ = self.fetch([_pr(1, [_check(555), _check(556)])], api)
        self.assertNotIn("not_started", prs[0])

    def test_an_unreadable_job_fails_closed_to_an_ordinary_red(self):
        prs, _ = self.fetch([_pr(1, [_check(555)])], {_job_path(555): None})
        self.assertEqual(prs[0]["checks"], "red")
        self.assertNotIn("not_started", prs[0])

    def test_an_unreadable_annotation_list_fails_closed_and_is_not_memoized(self):
        api = {_job_path(555): {"id": 555, "conclusion": "failure", "steps": []},
               _annotations_path(555): None}
        prs, _ = self.fetch([_pr(1, [_check(555)])], api)
        self.assertNotIn("not_started", prs[0])
        prs, _ = self.fetch([_pr(1, [_check(555)])], self.refused(555))
        self.assertEqual(prs[0]["not_started"], BILLING_TEXT)

    def test_a_reading_is_memoized_per_failed_job_set(self):
        self.fetch([_pr(1, [_check(555)])], self.refused(555))
        prs, gh = self.fetch([_pr(1, [_check(555)])], {})
        self.assertEqual(prs[0]["not_started"], BILLING_TEXT)
        self.assertEqual(gh.paths, [])

    def test_a_timed_out_job_is_not_never_started(self):
        api = {_job_path(555): {"id": 555, "conclusion": "timed_out", "steps": []}}
        prs, _ = self.fetch([_pr(1, [_check(555, "TIMED_OUT")])], api)
        self.assertEqual(prs[0]["checks"], "red")
        self.assertNotIn("not_started", prs[0])

    def test_a_red_commit_status_is_not_covered(self):
        status = {"__typename": "StatusContext", "state": "FAILURE"}
        prs, _ = self.fetch([_pr(1, [_check(555), status])], self.refused(555))
        self.assertNotIn("not_started", prs[0])

    def test_a_failed_check_with_no_job_url_is_not_covered(self):
        node = {"__typename": "CheckRun", "status": "COMPLETED", "conclusion": "FAILURE",
                "detailsUrl": "https://ci.example.invalid/build/9"}
        prs, gh = self.fetch([_pr(1, [node])], {})
        self.assertNotIn("not_started", prs[0])
        self.assertEqual(gh.paths, [])

    def test_rollup_denied_head_reads_job_ids_from_the_rest_check_runs(self):
        prs, _ = self.fetch_denied(_FakeHeadChecks([_run(555)]), self.refused(555))
        self.assertEqual(prs[0]["checks"], "red")
        self.assertEqual(prs[0]["not_started"], BILLING_TEXT)

    def test_rollup_denied_head_with_an_unreadable_status_surface_fails_closed(self):
        prs, gh = self.fetch_denied(_FakeHeadChecks([_run(555)], None), self.refused(555))
        self.assertEqual(prs[0]["checks"], "unread")
        self.assertNotIn("not_started", prs[0])
        self.assertEqual(gh.jobs_read(), [])

    def test_rollup_denied_head_reads_a_failed_job_on_a_later_check_run_page(self):
        per = _mod.HEAD_CHECKS_PER_PAGE
        api = {}
        for job_id in range(1, per + 1):
            api.update(self.refused(job_id))
        api[_job_path(1000)] = {"id": 1000, "conclusion": "failure", "steps": [{"name": "t"}]}
        runs = [_run(job_id) for job_id in range(1, per + 1)] + [_run(1000)]
        prs, gh = self.fetch_denied(_FakeHeadChecks(runs), api)
        self.assertEqual(prs[0]["checks"], "red")
        self.assertNotIn("not_started", prs[0])
        self.assertIn(_job_path(1000), gh.jobs_read())

    def test_rollup_denied_head_still_full_at_the_page_cap_fails_closed(self):
        runs = [_run(job_id) for job_id in
                range(1, _mod.HEAD_CHECKS_PER_PAGE * _mod.HEAD_CHECKS_MAX_PAGES + 1)]
        prs, gh = self.fetch_denied(_FakeHeadChecks(runs), {})
        self.assertEqual(prs[0]["checks"], "unread")
        self.assertNotIn("not_started", prs[0])
        self.assertEqual(gh.jobs_read(), [])


class TestSchemaGate(_ReadingCase):

    def test_a_schema_8_record_is_not_reused_by_the_304_path(self):
        stale = {"number": 1, "schema": 8, "labels": [], "checks": "green",
                 "headRefOid": SHA, "baseRefName": "master", "mergeable": "MERGEABLE"}
        gh = _FakeGh([], {})
        with patch.object(_mod, "run_capture", gh), patch.object(_mod, "log"), \
                patch.object(_mod, "conditional_get", return_value=(False, "[]")), \
                patch.object(_mod, "_check_state_moved", return_value=(False, set())):
            result = _mod.fetch_prs(REPO, prev=[stale])
        self.assertEqual(result, [])

    def test_a_schema_9_record_is_reused_without_a_list_fetch(self):
        current = {"number": 1, "schema": 9, "labels": [], "checks": "green",
                   "headRefOid": SHA, "baseRefName": "master", "mergeable": "MERGEABLE"}
        gh = _FakeGh([], {})
        body = json.dumps([{"number": 1}])
        with patch.object(_mod, "run_capture", gh), patch.object(_mod, "log"), \
                patch.object(_mod, "conditional_get", return_value=(False, body)), \
                patch.object(_mod, "_check_state_moved", return_value=(False, set())):
            result = _mod.fetch_prs(REPO, prev=[current])
        self.assertEqual(result, [current])


def _state(prs, degraded=()):
    state = {"repos": {"game": {"prs": prs}}}
    if degraded:
        state["degraded"] = list(degraded)
    return state


class TestAlertLane(unittest.TestCase):

    ESCALATE_N = 3

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        _mod._spawn_fail_streak.clear()
        self.addCleanup(_mod._spawn_fail_streak.clear)
        self.clock = ["2026-10-24T05:00:00Z"]
        for patcher in (
                patch.object(_mod, "_alerts_dir", lambda: Path(self.tmp.name)),
                patch.object(_mod, "SPAWN_FAIL_ESCALATE_N", self.ESCALATE_N),
                patch.object(_mod, "log"),
                patch.object(_mod.time, "strftime", lambda *_a: self.clock[0])):
            patcher.start()
            self.addCleanup(patcher.stop)

    @property
    def alert(self):
        return Path(self.tmp.name) / "scout-actions-not-starting-game"

    def tick(self, state, now=None):
        if now:
            self.clock[0] = now
        _mod.run_actions_not_starting_lane(state)

    def refused(self):
        return _state([{"number": 9, "not_started": BILLING_TEXT},
                       {"number": 4, "not_started": "other text"}, {"number": 12}])

    def healthy(self):
        return _state([{"number": 9}])

    def escalate(self):
        for minute in range(self.ESCALATE_N):
            self.tick(self.refused(), f"2026-10-24T05:0{minute}:00Z")

    def test_nothing_is_written_before_the_escalate_threshold(self):
        for _ in range(self.ESCALATE_N - 1):
            self.tick(self.refused())
        self.assertFalse(self.alert.exists())

    def test_the_alert_names_repo_since_and_the_lowest_numbered_prs_annotation(self):
        self.escalate()
        self.assertEqual(
            self.alert.read_text(),
            "actions not starting repo=game since=2026-10-24T05:02:00Z annotation='other text'\n")

    def test_the_alert_is_rewritten_every_later_tick_with_since_preserved(self):
        self.escalate()
        self.tick(_state([{"number": 3, "not_started": "newer text"}]), "2026-10-24T06:00:00Z")
        self.assertEqual(
            self.alert.read_text(),
            "actions not starting repo=game since=2026-10-24T05:02:00Z "
            "annotation='newer text'\n")

    def test_the_alert_round_trips_a_quote_free_annotation(self):
        self.escalate()
        self.tick(_state([{"number": 3, "not_started": BILLING_TEXT}]))
        fields = _mod.fleet_alerts.parse_fields(self.alert.read_text())
        self.assertEqual(fields["annotation"], BILLING_TEXT)

    def test_a_healthy_pass_clears_the_alert_and_the_streak(self):
        self.escalate()
        self.tick(self.healthy())
        self.assertFalse(self.alert.exists())
        for _ in range(self.ESCALATE_N - 1):
            self.tick(self.refused())
        self.assertFalse(self.alert.exists())

    def test_a_zero_open_pr_pass_changes_nothing(self):
        self.escalate()
        before = self.alert.read_text()
        self.tick(_state([]))
        self.assertEqual(self.alert.read_text(), before)

    def test_a_degraded_pr_list_changes_nothing_in_either_direction(self):
        self.escalate()
        before = self.alert.read_text()
        self.tick(_state([{"number": 9}], degraded=["game.prs"]))
        self.assertEqual(self.alert.read_text(), before)
        self.tick(_state([{"number": 3, "not_started": "stale reading"}],
                         degraded=["game.prs"]), "2026-10-24T07:00:00Z")
        self.assertEqual(self.alert.read_text(), before)

    def test_a_degraded_reading_does_not_count_toward_escalation(self):
        for _ in range(self.ESCALATE_N):
            self.tick(_state([{"number": 3, "not_started": "x"}], degraded=["game.prs"]))
        self.assertFalse(self.alert.exists())

    def test_a_restart_keeps_the_original_since(self):
        self.escalate()
        _mod._spawn_fail_streak.clear()
        for _ in range(self.ESCALATE_N):
            self.tick(self.refused(), "2026-10-24T09:00:00Z")
        self.assertIn("since=2026-10-24T05:02:00Z", self.alert.read_text())


class TestLaneWiring(_ScoutTickHarness, unittest.TestCase):

    def run_tick(self, served):
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(_mod, "run_actions_not_starting_lane") as lane:
            self._tick(tmp, [], degraded=False, spawns=[], served=served)
        return lane

    def test_the_authoritative_poller_runs_the_lane(self):
        self.assertTrue(self.run_tick(served=False).called)

    def test_a_follower_serving_a_leader_bundle_does_not(self):
        self.assertFalse(self.run_tick(served=True).called)


if __name__ == "__main__":
    unittest.main()
