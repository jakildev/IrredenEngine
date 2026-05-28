"""Tests for fleet-claude-stream's latch_usage_observation.

Covers:
  - allowed_warning event (has utilization) => file written with correct value
  - allowed event (no utilization) after warning => existing file NOT overwritten
  - junk "?" rateLimitType => no file written (no _.json created)
  - valid type with no utilization from the start => no file written
"""
import importlib.machinery
import importlib.util
import json
import os
import pathlib
import tempfile
import unittest

_SCRIPT = pathlib.Path(__file__).parent.parent / "fleet-claude-stream"
_loader = importlib.machinery.SourceFileLoader("fleet_claude_stream", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_claude_stream", _loader)
_mod = importlib.util.module_from_spec(_spec)


class LatchUsageObservation(unittest.TestCase):
    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self._usage_dir = pathlib.Path(self._tmpdir.name) / "usage"
        self._usage_dir.mkdir()
        _loader.exec_module(_mod)
        _mod.USAGE_DIR = self._usage_dir

    def tearDown(self):
        self._tmpdir.cleanup()

    def _read(self, name):
        p = self._usage_dir / f"{name}.json"
        if not p.exists():
            return None
        return json.loads(p.read_text())

    def test_warning_event_writes_utilization(self):
        _mod.latch_usage_observation(
            "five_hour",
            {"utilization": 0.85, "resetsAt": "2026-05-28T00:00:00Z"},
        )
        data = self._read("five_hour")
        self.assertIsNotNone(data)
        self.assertEqual(data["utilization"], 0.85)
        self.assertEqual(data["rateLimitType"], "five_hour")

    def test_allowed_event_preserves_warning_value(self):
        # First: latch a warning observation.
        _mod.latch_usage_observation(
            "five_hour",
            {"utilization": 0.90, "resetsAt": "2026-05-28T00:00:00Z"},
        )
        # Then: an "allowed" event arrives with no utilization field.
        _mod.latch_usage_observation(
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
        _mod.latch_usage_observation(
            "?",
            {"resetsAt": "2026-05-28T00:00:00Z"},
        )
        # Neither _.json nor any other file should be created.
        files = list(self._usage_dir.iterdir())
        self.assertEqual(files, [], "junk '?' type must not create any file")

    def test_no_utilization_on_first_event_writes_nothing(self):
        _mod.latch_usage_observation(
            "five_hour",
            {"resetsAt": "2026-05-28T00:00:00Z"},
        )
        self.assertIsNone(self._read("five_hour"), "no file when first event has no utilization")


if __name__ == "__main__":
    unittest.main()
