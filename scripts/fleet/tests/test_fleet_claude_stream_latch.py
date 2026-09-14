"""Tests for fleet-claude-stream's latch_usage_observation.

Covers:
  - allowed_warning event (has utilization) => file written with correct value
  - allowed event (no utilization) after warning => existing file NOT overwritten
  - junk "?" rateLimitType => no file written (no _.json created)
  - valid type with no utilization from the start => no file written
  - rejected event (the wall — carries resetsAt but NO utilization) => latched
    at 100% with status, and the wrap's FLEET_QUOTA_FLAG touched
  - the wall's result text is the independent second flag signal; an
    ordinary error result is not
"""
import importlib.machinery
import importlib.util
import io
import json
import os
import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout

_SCRIPT = pathlib.Path(__file__).parent.parent / "fleet-claude-stream"
_loader = importlib.machinery.SourceFileLoader("fleet_claude_stream", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_claude_stream", _loader)


class LatchUsageObservation(unittest.TestCase):
    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self._usage_dir = pathlib.Path(self._tmpdir.name) / "usage"
        self._usage_dir.mkdir()
        self._mod = importlib.util.module_from_spec(_spec)
        _loader.exec_module(self._mod)
        self._mod.USAGE_DIR = self._usage_dir

    def tearDown(self):
        self._tmpdir.cleanup()

    def _read(self, name):
        p = self._usage_dir / f"{name}.json"
        if not p.exists():
            return None
        return json.loads(p.read_text())

    def test_warning_event_writes_utilization(self):
        self._mod.latch_usage_observation(
            "five_hour",
            {"utilization": 0.85, "resetsAt": "2026-05-28T00:00:00Z"},
        )
        data = self._read("five_hour")
        self.assertIsNotNone(data)
        self.assertEqual(data["utilization"], 0.85)
        self.assertEqual(data["rateLimitType"], "five_hour")

    def test_allowed_event_preserves_warning_value(self):
        # First: latch a warning observation.
        self._mod.latch_usage_observation(
            "five_hour",
            {"utilization": 0.90, "resetsAt": "2026-05-28T00:00:00Z"},
        )
        # Then: an "allowed" event arrives with no utilization field.
        self._mod.latch_usage_observation(
            "five_hour",
            {"resetsAt": "2026-05-28T00:00:00Z"},
        )
        data = self._read("five_hour")
        self.assertIsNotNone(data, "file must still exist")
        self.assertEqual(
            data["utilization"],
            0.90,
            "allowed event must NOT overwrite warning utilization with null",
        )

    def test_junk_question_mark_type_skipped(self):
        self._mod.latch_usage_observation(
            "?",
            {"resetsAt": "2026-05-28T00:00:00Z"},
        )
        # Neither _.json nor any other file should be created.
        files = list(self._usage_dir.iterdir())
        self.assertEqual(files, [], "junk '?' type must not create any file")

    def test_no_utilization_on_first_event_writes_nothing(self):
        self._mod.latch_usage_observation(
            "five_hour",
            {"resetsAt": "2026-05-28T00:00:00Z"},
        )
        self.assertIsNone(self._read("five_hour"), "no file when first event has no utilization")

    # The shape the CLI emitted at the wall on 2026-05-12 (queue-ingest.log):
    # no utilization field at all. Before the rejected arm this was dropped by
    # the guard above — the gate stayed open on the last warning, and every
    # pane relaunched into the wall each tick.
    REJECTED = {
        "status": "rejected",
        "resetsAt": 1778629200,
        "rateLimitType": "five_hour",
        "overageStatus": "rejected",
        "isUsingOverage": False,
    }

    def test_rejected_event_latches_at_full_utilization(self):
        self._mod.latch_usage_observation("five_hour", dict(self.REJECTED))
        data = self._read("five_hour")
        self.assertIsNotNone(data, "a rejected event must latch even without utilization")
        self.assertEqual(data["utilization"], 1.0)
        self.assertEqual(data["status"], "rejected")
        self.assertEqual(data["resetsAt"], 1778629200, "resetsAt is what holds the gate closed")

    def test_rejected_event_overrides_an_earlier_warning(self):
        self._mod.latch_usage_observation(
            "five_hour",
            {"status": "allowed_warning", "utilization": 0.80, "resetsAt": 1778629200},
        )
        self._mod.latch_usage_observation("five_hour", dict(self.REJECTED))
        self.assertEqual(self._read("five_hour")["utilization"], 1.0)

    def test_allowed_event_preserves_rejected_observation(self):
        self._mod.latch_usage_observation("five_hour", dict(self.REJECTED))
        self._mod.latch_usage_observation(
            "five_hour", {"status": "allowed", "resetsAt": 1778629200}
        )
        self.assertEqual(self._read("five_hour")["status"], "rejected")

    def test_warning_event_carries_its_status(self):
        self._mod.latch_usage_observation(
            "seven_day",
            {"status": "allowed_warning", "utilization": 0.85, "resetsAt": 1778961600},
        )
        self.assertEqual(self._read("seven_day")["status"], "allowed_warning")


class QuotaFlag(unittest.TestCase):
    """The wrap-exported FLEET_QUOTA_FLAG: the stream's exit classification
    signal, touched on a rejected rate_limit_event and on the wall's result
    text, and on nothing else."""

    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self._flag = pathlib.Path(self._tmpdir.name) / "pane-3.quota-seen"
        self._mod = importlib.util.module_from_spec(_spec)
        _loader.exec_module(self._mod)
        self._mod.USAGE_DIR = pathlib.Path(self._tmpdir.name) / "usage"
        self._saved = os.environ.get("FLEET_QUOTA_FLAG")
        os.environ["FLEET_QUOTA_FLAG"] = str(self._flag)

    def tearDown(self):
        if self._saved is None:
            os.environ.pop("FLEET_QUOTA_FLAG", None)
        else:
            os.environ["FLEET_QUOTA_FLAG"] = self._saved
        self._tmpdir.cleanup()

    def _feed(self, *events):
        import sys
        stdin = sys.stdin
        sys.stdin = io.StringIO("".join(json.dumps(e) + "\n" for e in events))
        try:
            with redirect_stdout(io.StringIO()) as out:
                self._mod.main()
        finally:
            sys.stdin = stdin
        return out.getvalue()

    def test_rejected_event_touches_the_flag_and_names_it(self):
        out = self._feed(
            {"type": "rate_limit_event", "rate_limit_info": dict(LatchUsageObservation.REJECTED)}
        )
        self.assertTrue(self._flag.exists(), "rejected event must touch FLEET_QUOTA_FLAG")
        self.assertIn("REJECTED", out)
        self.assertTrue(
            (self._mod.USAGE_DIR / "five_hour.json").exists(), "and latch the observation"
        )

    def test_warning_event_leaves_the_flag_alone(self):
        self._feed(
            {
                "type": "rate_limit_event",
                "rate_limit_info": {
                    "status": "allowed_warning",
                    "utilization": 0.9,
                    "rateLimitType": "five_hour",
                },
            }
        )
        self.assertFalse(self._flag.exists(), "a warning is not the wall")

    def test_wall_result_text_touches_the_flag(self):
        # The result event captured alongside the rejected event on 2026-05-12.
        self._feed(
            {
                "type": "result",
                "subtype": "success",
                "is_error": True,
                "api_error_status": 429,
                "result": "You've hit your limit · resets 4:40pm (America/Los_Angeles)",
            }
        )
        self.assertTrue(self._flag.exists(), "the wall's result text is the second signal")

    def test_weekly_and_session_wordings_match(self):
        for text in (
            "You've hit your weekly limit · resets 8am",
            "You've hit your session limit · resets 3:30am",
        ):
            self._flag.unlink(missing_ok=True)
            self._feed(
                {"type": "result", "subtype": "success", "is_error": True, "result": text}
            )
            self.assertTrue(self._flag.exists(), text)

    def test_other_error_results_do_not(self):
        self._feed(
            {
                "type": "result",
                "subtype": "error_during_execution",
                "is_error": True,
                "api_error_status": 429,
                "result": (
                    "API Error: Server is temporarily limiting requests (not your usage limit)"
                ),
            }
        )
        self.assertFalse(self._flag.exists(), "a 429 throttle result is not the wall")

    def test_no_flag_env_is_harmless(self):
        os.environ.pop("FLEET_QUOTA_FLAG", None)
        self._feed(
            {"type": "rate_limit_event", "rate_limit_info": dict(LatchUsageObservation.REJECTED)}
        )
        self.assertFalse(self._flag.exists())


if __name__ == "__main__":
    unittest.main()
