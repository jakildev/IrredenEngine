"""Tests for fleet_not_started: the shared "GitHub never started this job" predicate.

A never-started job is `conclusion == failure` with `steps == []` AND a
failure-level annotation. Either half alone is a different condition (a
cancelled job, a job that ran and failed), so each is pinned from both sides.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import fleet_alerts  # noqa: E402
import fleet_not_started as ns  # noqa: E402

BILLING = ("The job was not started because recent account payments have failed or "
           "your spending limit needs to be increased. Please check the "
           "'Billing & plans' section in your settings")
NOTICE = "ubuntu-latest will migrate to a newer image"


def _annotations():
    return [
        {"annotation_level": "notice", "message": NOTICE},
        {"annotation_level": "failure", "message": BILLING},
    ]


class TestJobNeverStarted(unittest.TestCase):

    def test_failed_job_with_no_steps(self):
        self.assertTrue(ns.job_never_started({"conclusion": "failure", "steps": []}))

    def test_failed_job_with_a_step_is_a_real_failure(self):
        self.assertFalse(ns.job_never_started(
            {"conclusion": "failure", "steps": [{"name": "build"}]}))

    def test_cancelled_job_with_no_steps_is_not_never_started(self):
        self.assertFalse(ns.job_never_started({"conclusion": "cancelled", "steps": []}))

    def test_a_job_without_a_steps_key_is_no_reading(self):
        self.assertFalse(ns.job_never_started({"conclusion": "failure"}))


class TestNotStartedText(unittest.TestCase):

    def test_picks_the_failure_level_annotation_not_the_first(self):
        text = ns.not_started_text(_annotations())
        self.assertIsNotNone(text)
        self.assertIn("Billing & plans", text)
        self.assertNotIn(NOTICE, text)

    def test_text_round_trips_through_a_single_quoted_alert_field(self):
        text = ns.not_started_text(_annotations())
        self.assertNotIn("'", text)
        fields = fleet_alerts.parse_fields(f"annotation='{text}' repo=game")
        self.assertEqual(fields["annotation"], text)
        self.assertEqual(fields["repo"], "game")

    def test_whitespace_and_newlines_collapse(self):
        text = ns.not_started_text([{"annotation_level": "failure", "message": "a\n  b\tc"}])
        self.assertEqual(text, "a b c")

    def test_no_failure_level_annotation_is_none(self):
        self.assertIsNone(ns.not_started_text([{"annotation_level": "notice", "message": "x"}]))
        self.assertIsNone(ns.not_started_text([]))
        self.assertIsNone(ns.not_started_text(None))

    def test_a_blank_failure_message_is_none(self):
        self.assertIsNone(ns.not_started_text([{"annotation_level": "failure", "message": "  "}]))


class TestRunNotStartedText(unittest.TestCase):

    @staticmethod
    def _reader(table):
        return lambda job_id: table[job_id]

    def test_every_job_never_started(self):
        jobs = [{"id": 2, "conclusion": "failure", "steps": []},
                {"id": 1, "conclusion": "failure", "steps": []}]
        text = ns.run_not_started_text(jobs, self._reader({
            1: [{"annotation_level": "failure", "message": "first"}],
            2: [{"annotation_level": "failure", "message": "second"}]}))
        self.assertEqual(text, "first")

    def test_live_shaped_fixture_yields_the_billing_text(self):
        jobs = [{"id": 120785269774, "conclusion": "failure", "steps": []}]
        text = ns.run_not_started_text(jobs, self._reader({120785269774: _annotations()}))
        self.assertIsNotNone(text)
        self.assertIn("Billing & plans", text)

    def test_one_job_that_ran_fails_the_whole_run_closed(self):
        jobs = [{"id": 1, "conclusion": "failure", "steps": []},
                {"id": 2, "conclusion": "failure", "steps": [{"name": "build"}]}]
        self.assertIsNone(ns.run_not_started_text(jobs, self._reader({1: _annotations()})))

    def test_a_failed_job_with_steps_and_a_failure_annotation_is_not_never_started(self):
        jobs = [{"id": 1, "conclusion": "failure", "steps": [{"name": "build"}]}]
        self.assertIsNone(ns.run_not_started_text(jobs, self._reader({1: _annotations()})))

    def test_an_unreadable_annotation_response_fails_closed(self):
        jobs = [{"id": 1, "conclusion": "failure", "steps": []}]
        self.assertIsNone(ns.run_not_started_text(jobs, lambda _id: None))

    def test_a_job_without_the_annotation_fails_closed(self):
        jobs = [{"id": 1, "conclusion": "failure", "steps": []},
                {"id": 2, "conclusion": "failure", "steps": []}]
        self.assertIsNone(ns.run_not_started_text(jobs, self._reader({
            1: _annotations(), 2: [{"annotation_level": "notice", "message": NOTICE}]})))

    def test_a_run_with_no_jobs_is_no_reading(self):
        self.assertIsNone(ns.run_not_started_text([], lambda _id: _annotations()))


if __name__ == "__main__":
    unittest.main()
