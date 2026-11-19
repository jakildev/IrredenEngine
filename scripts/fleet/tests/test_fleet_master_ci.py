"""Hermetic cases for the GitHub-backed red-master incident tracker."""

import importlib.util
import json
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SUBJECT = ROOT / "fleet" / "fleet_master_ci.py"
sys.path.insert(0, str(SUBJECT.parent))
SPEC = importlib.util.spec_from_file_location("fleet_master_ci_subject", SUBJECT)
if SUBJECT.exists():
    tracker = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(tracker)
else:
    tracker = types.SimpleNamespace(
        fleet_github=types.SimpleNamespace(run=None),
        SUITES_ANNOTATION_TITLE="fleet-tests failed suites",
    )

RUN = {
    "databaseId": 700,
    "headSha": "a" * 40,
    "createdAt": "2026-11-18T05:00:00Z",
    "url": "https://example.invalid/runs/700",
}
DETAILS = {
    "failed_jobs": ["fleet-tests", "gate"],
    "failed_suites": ["test_one.py@123", "test_two.sh@456"],
    "has_skipped_job": False,
}


def _done(stdout="", returncode=0, stderr=""):
    return subprocess.CompletedProcess([], returncode, stdout=stdout, stderr=stderr)


class FakeGitHub:
    def __init__(self, issues=None, comments=None):
        self.issues = list(issues or [])
        self.comments = {int(key): list(value) for key, value in (comments or {}).items()}
        self.calls = []
        self.sent = []
        self.fail_lookup = False

    def __call__(self, args, **_kwargs):
        self.calls.append(list(args))
        if args[:3] == ["api", "--paginate", "--slurp"]:
            endpoint = args[3]
            if "issues?state=all" in endpoint:
                if self.fail_lookup:
                    return _done(returncode=1, stderr="lookup failed")
                return _done(json.dumps([self.issues]))
            if "/comments?" in endpoint:
                number = int(endpoint.split("/issues/")[1].split("/")[0])
                return _done(json.dumps([self.comments.get(number, [])]))
            if "/actions/runs/" in endpoint:
                jobs = [
                    {"id": 1, "name": "fleet-tests", "conclusion": "failure"},
                    {"id": 2, "name": "gate", "conclusion": "failure"},
                ]
                return _done(json.dumps([{"jobs": jobs}]))
            if "/check-runs/" in endpoint:
                annotations = []
                if "/1/" in endpoint:
                    annotations = [{
                        "title": tracker.SUITES_ANNOTATION_TITLE,
                        "message": "test_one.py@123 test_two.sh@456",
                    }]
                return _done(json.dumps([annotations]))
        if args[:2] == ["issue", "create"]:
            body = Path(args[args.index("--body-file") + 1]).read_text(encoding="utf-8")
            self.sent.append(("create", body, list(args)))
            return _done("https://github.com/jakildev/IrredenEngine/issues/900\n")
        if args[:2] == ["issue", "comment"]:
            number = int(args[2])
            body = Path(args[args.index("--body-file") + 1]).read_text(encoding="utf-8")
            self.sent.append(("comment", body, list(args)))
            self.comments.setdefault(number, []).append({"body": body})
            return _done("https://example.invalid/comment\n")
        raise AssertionError(f"unexpected call: {args}")


def issue(number=900, state="open", body=None):
    return {
        "number": number,
        "state": state,
        "body": body or tracker._workflow_marker("fleet-tests.yml"),
    }


class TrackerCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def call(self, fake, run=RUN, conclusion="red"):
        with patch.object(tracker.fleet_github, "run", fake):
            return tracker.upsert_tracker(
                "fleet-tests.yml", run, DETAILS, conclusion, ROOT.parent
            )

    def test_first_red_creates_one_approved_no_plan_tracker_with_evidence(self):
        fake = FakeGitHub()
        self.assertEqual(self.call(fake), 900)
        self.assertEqual([kind for kind, _body, _args in fake.sent], ["create"])
        _kind, body, args = fake.sent[0]
        self.assertIn(tracker._workflow_marker("fleet-tests.yml"), body)
        self.assertIn(tracker._run_marker(RUN, "red"), body)
        self.assertIn(RUN["url"], body)
        self.assertIn(RUN["headSha"], body)
        self.assertIn("`fleet-tests`", body)
        self.assertIn("`test_one.py@123`", body)
        self.assertEqual(
            [args[index + 1] for index, value in enumerate(args) if value == "--label"],
            ["fleet:master-red", "fleet:agent-approved", "fleet:no-plan"],
        )

    def test_second_red_comments_once_and_the_same_run_is_idempotent(self):
        first_marker = tracker._run_marker(RUN, "red")
        fake = FakeGitHub([issue(body=tracker._workflow_marker("fleet-tests.yml")
                                      + "\n" + first_marker)])
        second = dict(RUN, databaseId=701, headSha="b" * 40)
        self.assertEqual(self.call(fake, second), 900)
        self.assertEqual([kind for kind, _body, _args in fake.sent], ["comment"])
        self.assertEqual(self.call(fake, second), 900)
        self.assertEqual([kind for kind, _body, _args in fake.sent], ["comment"])

    def test_a_run_recorded_on_a_closed_tracker_does_not_open_another(self):
        body = tracker._workflow_marker("fleet-tests.yml") + "\n" + tracker._run_marker(RUN, "red")
        fake = FakeGitHub([issue(state="closed", body=body)])
        self.assertIsNone(self.call(fake))
        self.assertEqual(fake.sent, [])

    def test_green_comments_once_after_red_and_never_closes(self):
        body = tracker._workflow_marker("fleet-tests.yml") + "\n" + tracker._run_marker(RUN, "red")
        fake = FakeGitHub([issue(body=body)])
        green = dict(RUN, databaseId=702, headSha="c" * 40)
        self.assertEqual(self.call(fake, green, "green"), 900)
        self.assertEqual([kind for kind, _body, _args in fake.sent], ["comment"])
        self.assertIn(tracker._run_marker(green, "green"), fake.sent[0][1])
        later = dict(RUN, databaseId=703, headSha="d" * 40)
        self.assertEqual(self.call(fake, later, "green"), 900)
        self.assertEqual(len(fake.sent), 1)
        self.assertFalse(any("--state" in args for args in fake.calls))

    def test_tracker_lookup_miss_fails_closed(self):
        fake = FakeGitHub()
        fake.fail_lookup = True
        with self.assertRaises(tracker.MasterCiError):
            self.call(fake)
        self.assertEqual(fake.sent, [])

    def test_failed_job_and_suite_annotation_details_are_read(self):
        fake = FakeGitHub()
        with patch.object(tracker.fleet_github, "run", fake):
            self.assertEqual(tracker.failed_run_details(700), DETAILS)


if __name__ == "__main__":
    unittest.main()
