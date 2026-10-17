"""Tests for the scout's master-CI watch: one standing alert per red workflow.

`gh run list --limit 1` returns the NEWEST master run, which is in flight for
the first minutes after every push, so only `failure` raises the alert and only
`success` clears it; every other conclusion leaves it as it was. A read that
could not decide (non-zero exit, bad JSON, no workflows) is never a green: it
writes nothing, clears nothing, and leaves the deadline marker unwritten so the
next tick retries and `_spawn_failed` escalates a persistent miss.

The tick harness and the `fleet_github.run` seam are shared with
test_scout_degraded_fetch; every mocked miss raises rather than reaching `gh`.
"""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from test_scout_degraded_fetch import _mod, _ScoutTickHarness

SHA = "0123456789abcdef0123456789abcdef01234567"
URL = "https://example.invalid/runs/7"
CREATED = "2026-10-15T05:37:00Z"


def _run_json(conclusion, sha=SHA):
    return json.dumps([{"conclusion": conclusion, "headSha": sha,
                        "createdAt": CREATED, "url": URL}])


def _done(stdout="[]", returncode=0, stderr=""):
    return subprocess.CompletedProcess([], returncode, stdout=stdout, stderr=stderr)


class _FakeGh:
    """`fleet_github.run` stand-in: per-workflow replies, a miss raises."""

    def __init__(self, replies):
        self.replies = replies
        self.calls = []

    def __call__(self, args, **kwargs):
        self.calls.append(list(args))
        workflow = args[args.index("--workflow") + 1]
        reply = self.replies[workflow]
        if isinstance(reply, Exception):
            raise reply
        return reply


class _MasterCiCase(_ScoutTickHarness, unittest.TestCase):

    def setUp(self):
        super().setUp()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.logs = []

    def tick(self, replies, workflows=None, served=False):
        gh = _FakeGh(replies)
        self._tick(self.tmp.name, [], degraded=False, spawns=[], logs=self.logs,
                   served=served, master_ci=(gh, workflows or sorted(replies)))
        return gh

    def alert_path(self, workflow):
        return Path(self.tmp.name) / "alerts" / f"scout-master-ci-{workflow}"

    def marker(self):
        return Path(self.tmp.name) / "seen-hashes" / _mod.MASTER_CI_MARKER_NAME

    def age_marker(self):
        self.marker().write_text(f"{int(self.marker().read_text()) - 10_000}\n")

    def spawn_alert(self):
        return self._alert(self.tmp.name, "master-ci")


class TestAlertLifecycle(_MasterCiCase):

    def test_green_run_writes_no_alert(self):
        self.tick({"ci.yml": _done(_run_json("success"))})
        self.assertFalse(self.alert_path("ci.yml").exists())
        self.assertTrue(self.marker().exists())

    def test_red_run_writes_the_exact_alert_with_the_runs_identity(self):
        gh = self.tick({"ci.yml": _done(_run_json("failure"))})
        self.assertEqual(
            self.alert_path("ci.yml").read_text(),
            f"master CI red: ci.yml\nworkflow=ci.yml\nsha={SHA}\n"
            f"created_at={CREATED}\nurl={URL}\n")
        self.assertEqual(gh.calls, [[
            "run", "list", "--repo", "jakildev/IrredenEngine", "--branch", "master",
            "--workflow", "ci.yml", "--limit", "1",
            "--json", "conclusion,headSha,createdAt,url"]])

    def test_red_then_green_clears_the_alert(self):
        self.tick({"ci.yml": _done(_run_json("failure"))})
        self.age_marker()
        self.tick({"ci.yml": _done(_run_json("success"))})
        self.assertFalse(self.alert_path("ci.yml").exists())

    def test_a_still_red_workflow_logs_once_and_a_new_red_sha_logs_again(self):
        self.tick({"ci.yml": _done(_run_json("failure"))})
        self.age_marker()
        self.tick({"ci.yml": _done(_run_json("failure"))})
        self.assertEqual(sum("is RED" in m for m in self.logs), 1, self.logs)
        self.age_marker()
        self.tick({"ci.yml": _done(_run_json("failure", sha="f" * 40))})
        self.assertEqual(sum("is RED" in m for m in self.logs), 2, self.logs)
        self.assertIn("f" * 40, self.alert_path("ci.yml").read_text())

    def test_in_flight_and_inconclusive_runs_leave_a_red_alert_standing(self):
        self.tick({"ci.yml": _done(_run_json("failure"))})
        before = self.alert_path("ci.yml").read_text()
        for conclusion in ("", "cancelled", "skipped", "neutral", "action_required"):
            with self.subTest(conclusion=conclusion):
                self.age_marker()
                self.tick({"ci.yml": _done(_run_json(conclusion, sha="e" * 40))})
                self.assertEqual(self.alert_path("ci.yml").read_text(), before)

    def test_only_the_recovered_workflow_is_cleared(self):
        red = {"a.yml": _done(_run_json("failure")), "b.yml": _done(_run_json("failure"))}
        self.tick(red)
        self.age_marker()
        self.tick({"a.yml": _done(_run_json("success")), "b.yml": _done(_run_json("failure"))})
        self.assertFalse(self.alert_path("a.yml").exists())
        self.assertTrue(self.alert_path("b.yml").exists())


class TestFetchMiss(_MasterCiCase):

    MISSES = {
        "non-zero exit": _done("", returncode=1, stderr="HTTP 403"),
        "bad json": _done("not json"),
        "wrong shape": _done('{"conclusion": "failure"}'),
        "transport error": OSError("gh not found"),
        "timeout": subprocess.TimeoutExpired("gh", 30),
    }

    def test_a_miss_writes_no_alert_and_leaves_the_deadline_unwritten(self):
        for label, reply in self.MISSES.items():
            with self.subTest(miss=label):
                self.tick({"ci.yml": reply})
                self.assertFalse(self.alert_path("ci.yml").exists())
                self.assertFalse(self.marker().exists())

    def test_a_miss_never_clears_a_standing_red_alert(self):
        self.tick({"ci.yml": _done(_run_json("failure"))})
        before = self.alert_path("ci.yml").read_text()
        for label, reply in self.MISSES.items():
            with self.subTest(miss=label):
                self.age_marker()
                self.tick({"ci.yml": reply})
                self.assertEqual(self.alert_path("ci.yml").read_text(), before)

    def test_a_persistent_miss_escalates_to_the_spawn_alert_and_a_read_clears_it(self):
        miss = {"ci.yml": _done("", returncode=1, stderr="HTTP 403")}
        for _ in range(_mod.SPAWN_FAIL_ESCALATE_N - 1):
            self.tick(miss)
        self.assertIsNone(self.spawn_alert())
        self.tick(miss)
        self.assertIn("lane=master-ci", self.spawn_alert())
        self.tick({"ci.yml": _done(_run_json("success"))})
        self.assertIsNone(self.spawn_alert())

    def test_an_empty_result_is_a_quiet_skip_not_a_miss(self):
        for _ in range(_mod.SPAWN_FAIL_ESCALATE_N + 1):
            self.tick({"pr-only.yml": _done("[]")})
            self.age_marker()
        self.assertIsNone(self.spawn_alert())
        self.assertFalse(self.alert_path("pr-only.yml").exists())

    def test_no_workflow_files_is_a_miss(self):
        gh = _FakeGh({})
        self._tick(self.tmp.name, [], degraded=False, spawns=[], logs=self.logs,
                   master_ci=(gh, []))
        self.assertEqual(gh.calls, [])
        self.assertFalse(self.marker().exists())
        self.assertTrue(any("no workflow files" in m for m in self.logs), self.logs)

    def test_the_first_miss_breaks_the_pass_after_earlier_workflows_were_read(self):
        gh = self.tick({"a.yml": _done(_run_json("failure")),
                        "b.yml": _done("", returncode=1),
                        "c.yml": _done(_run_json("failure"))})
        self.assertTrue(self.alert_path("a.yml").exists())
        self.assertFalse(self.alert_path("c.yml").exists())
        self.assertEqual(len(gh.calls), 2)
        self.assertFalse(self.marker().exists())


class TestDeadlineMarker(_MasterCiCase):
    """One case per arm that can leave the workflow loop early."""

    def test_marker_written_after_the_success_arm(self):
        self.tick({"ci.yml": _done(_run_json("failure"))})
        self.assertTrue(self.marker().exists())

    def test_marker_written_after_the_empty_result_continue(self):
        self.tick({"pr-only.yml": _done("[]")})
        self.assertTrue(self.marker().exists())

    def test_marker_written_after_the_inconclusive_continue(self):
        self.tick({"ci.yml": _done(_run_json(""))})
        self.assertTrue(self.marker().exists())

    def test_continues_do_not_skip_the_workflows_after_them(self):
        gh = self.tick({"a.yml": _done("[]"), "b.yml": _done(_run_json("")),
                        "c.yml": _done(_run_json("failure"))})
        self.assertEqual(len(gh.calls), 3)
        self.assertTrue(self.alert_path("c.yml").exists())
        self.assertTrue(self.marker().exists())

    def test_a_tick_inside_the_interval_does_not_call_gh(self):
        self.tick({"ci.yml": _done(_run_json("success"))})
        gh = self.tick({"ci.yml": _done(_run_json("failure"))})
        self.assertEqual(gh.calls, [])
        self.assertFalse(self.alert_path("ci.yml").exists())

    def test_an_aged_marker_makes_the_lane_due_again(self):
        self.tick({"ci.yml": _done(_run_json("success"))})
        self.age_marker()
        gh = self.tick({"ci.yml": _done(_run_json("failure"))})
        self.assertEqual(len(gh.calls), 1)

    def test_the_interval_env_var_overrides_the_default(self):
        self.assertEqual(_mod._master_ci_interval_seconds(), 900)
        for value, expected in (("60", 60), ("0", 0), ("-5", 900), ("soon", 900), ("", 900)):
            with self.subTest(value=value), patch.dict(
                    "os.environ", {"FLEET_MASTER_CI_INTERVAL_SECS": value}):
                self.assertEqual(_mod._master_ci_interval_seconds(), expected)

    def test_a_follower_never_polls(self):
        gh = self.tick({"ci.yml": _done(_run_json("failure"))}, served=True)
        self.assertEqual(gh.calls, [])
        self.assertFalse(self.marker().exists())


class TestOrphanAlerts(_MasterCiCase):

    def _stale(self):
        stale = self.alert_path("gone.yml")
        stale.parent.mkdir(exist_ok=True)
        stale.write_text("master CI red: gone.yml\n")
        return stale

    def test_a_complete_pass_removes_the_alert_of_a_deleted_workflow(self):
        stale = self._stale()
        self.tick({"ci.yml": _done(_run_json("success"))})
        self.assertFalse(stale.exists())

    def test_a_pass_that_missed_keeps_every_alert(self):
        stale = self._stale()
        self.tick({"ci.yml": _done("", returncode=1)})
        self.assertTrue(stale.exists())


if __name__ == "__main__":
    unittest.main()
