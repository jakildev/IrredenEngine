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
import pathlib
import tempfile
import unittest

_SCRIPT = pathlib.Path(__file__).parent.parent / "fleet-claude-stream"
_loader = importlib.machinery.SourceFileLoader("fleet_claude_stream", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_claude_stream", _loader)


class _StreamModuleCase(unittest.TestCase):
    """A fresh fleet-claude-stream module over a scratch state dir."""

    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()
        self._usage_dir = pathlib.Path(self._tmpdir.name) / "usage"
        self._usage_dir.mkdir()
        self._mod = importlib.util.module_from_spec(_spec)
        _loader.exec_module(self._mod)
        self._mod.USAGE_DIR = self._usage_dir

    def tearDown(self):
        self._tmpdir.cleanup()


class LatchUsageObservation(_StreamModuleCase):

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


class RecordIterationResult(_StreamModuleCase):
    """handle_result writes the iteration's cost / turns / duration to
    FLEET_ITERATION_RESULT (module constant ITERATION_RESULT) — the record
    fleet-dispatcher folds into its completion line and iterations ledger."""

    RESULT = {"type": "result", "subtype": "success", "is_error": False,
              "num_turns": 17, "total_cost_usd": 1.23456789, "duration_ms": 4321,
              "session_id": "abc-123"}

    def setUp(self):
        super().setUp()
        self._path = pathlib.Path(self._tmpdir.name) / "iteration-results" / "pane-1.json"
        self._mod.emit = lambda s: None   # keep the pane render out of the test output

    def test_result_event_writes_the_record(self):
        self._mod.ITERATION_RESULT = str(self._path)
        self._mod.handle_result(dict(self.RESULT))
        data = json.loads(self._path.read_text())
        self.assertEqual(data["subtype"], "success")
        self.assertEqual(data["turns"], 17)
        self.assertEqual(data["cost_usd"], 1.234568)
        self.assertEqual(data["duration_s"], 4.3)
        self.assertEqual(data["session_id"], "abc-123")
        self.assertIsInstance(data["finished_at"], int)

    def test_error_result_keeps_its_subtype(self):
        self._mod.ITERATION_RESULT = str(self._path)
        self._mod.handle_result(dict(self.RESULT, is_error=True, subtype="error_max_turns",
                                     result="boom"))
        self.assertEqual(json.loads(self._path.read_text())["subtype"], "error_max_turns")

    def test_unset_path_writes_nothing(self):
        self._mod.ITERATION_RESULT = None
        self._mod.handle_result(dict(self.RESULT))
        self.assertFalse(self._path.exists())
        self.assertEqual([p.name for p in pathlib.Path(self._tmpdir.name).iterdir()], ["usage"])

if __name__ == "__main__":
    unittest.main()
