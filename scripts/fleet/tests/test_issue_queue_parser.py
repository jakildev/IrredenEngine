"""Tests for the issue-based task queue parser in fleet-state-scout
(T-381, T-392).

Covers the pure-function helper `_parse_issue_field` and the label/body
precedence rules inside `fetch_task_queue`'s per-issue loop (model, owner,
in_progress, area, blocked_by, task identity).
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class ParseIssueField(unittest.TestCase):
    def test_extracts_simple_value(self):
        body = "**Model:** opus\n**Blocked by:** #1214\n"
        self.assertEqual(_mod._parse_issue_field(body, "Model"), "opus")
        self.assertEqual(_mod._parse_issue_field(body, "Blocked by"), "#1214")

    def test_missing_field_returns_empty(self):
        self.assertEqual(_mod._parse_issue_field("**Other:** x", "Model"), "")

    def test_handles_empty_body(self):
        self.assertEqual(_mod._parse_issue_field("", "Model"), "")
        self.assertEqual(_mod._parse_issue_field(None, "Model"), "")

    def test_stops_at_newline(self):
        body = "**Model:** opus\nmore text\n"
        self.assertEqual(_mod._parse_issue_field(body, "Model"), "opus")

    def test_suggested_prefix_accepted(self):
        body = "**Suggested Model:** sonnet\n**Suggested Blocked by:** (none)\n"
        self.assertEqual(_mod._parse_issue_field(body, "Model"), "sonnet")
        self.assertEqual(
            _mod._parse_issue_field(body, "Blocked by"), "(none)"
        )

    def test_suggested_area_accepted(self):
        body = "**Suggested Area:** scripts/fleet\n"
        self.assertEqual(_mod._parse_issue_field(body, "Area"), "scripts/fleet")


class FetchTaskQueueDispatch(unittest.TestCase):
    """Exercise the per-issue loop with synthetic gh output.

    Stubs run_capture so the test doesn't shell out to gh.
    """
    def _run(self, issues):
        captured = []
        def fake_run_capture(cmd, **kwargs):
            captured.append(cmd)
            if cmd[0] == "gh":
                import json
                return json.dumps(issues)
            return ""
        original = _mod.run_capture
        _mod.run_capture = fake_run_capture
        try:
            return _mod.fetch_task_queue("jakildev/IrredenEngine")
        finally:
            _mod.run_capture = original

    def test_label_beats_body_for_model(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [{"name": "fleet:queued"}, {"name": "fleet:sonnet"}],
            "body": "**Model:** opus",
        }])
        self.assertEqual(out["open"][0]["model"], "sonnet")

    def test_body_fallback_when_no_label(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Model:** opus",
        }])
        self.assertEqual(out["open"][0]["model"], "opus")

    def test_claim_label_sets_owner_and_in_progress(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [
                {"name": "fleet:queued"},
                {"name": "fleet:claim-mac-opus-worker-1"},
            ],
            "body": "",
        }])
        self.assertEqual(len(out["open"]), 0)
        self.assertEqual(len(out["in_progress"]), 1)
        self.assertEqual(out["in_progress"][0]["owner"], "opus-worker-1")
        self.assertEqual(out["in_progress"][0]["status"], "~")

    def test_in_progress_label_alone_marks_in_progress(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [
                {"name": "fleet:queued"}, {"name": "fleet:in-progress"},
            ],
            "body": "",
        }])
        self.assertEqual(len(out["in_progress"]), 1)
        self.assertEqual(out["in_progress"][0]["owner"], "free")

    def test_area_from_label_map(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [
                {"name": "fleet:queued"}, {"name": "IRRender"},
            ],
            "body": "",
        }])
        self.assertEqual(out["open"][0]["area"], "engine/render")

    def test_area_falls_back_to_body(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Area:** docs",
        }])
        self.assertEqual(out["open"][0]["area"], "docs")

    def test_task_id_is_issue_number(self):
        out = self._run([{
            "number": 100, "title": "render: task",
            "labels": [{"name": "fleet:queued"}],
            "body": "",
        }])
        self.assertEqual(out["open"][0]["id"], "#100")
        self.assertEqual(out["open"][0]["issue"], "#100")
        self.assertEqual(out["open"][0]["title"], "#100")
        self.assertEqual(out["open"][0]["summary"], "render: task")

    def test_suggested_model_body_fallback(self):
        out = self._run([{
            "number": 100, "title": "x",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Suggested Model:** sonnet\n",
        }])
        self.assertEqual(out["open"][0]["model"], "sonnet")

    def test_blocked_by_taken_verbatim_from_body(self):
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Blocked by:** #50",
        }])
        self.assertEqual(out["open"][0]["blocked_by"], "#50")

    def test_blocked_by_defaults_to_none(self):
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}],
            "body": "",
        }])
        self.assertEqual(out["open"][0]["blocked_by"], "(none)")

    def test_empty_gh_output_returns_empty_sections(self):
        out = self._run([])
        self.assertEqual(out["open"], [])
        self.assertEqual(out["in_progress"], [])
        self.assertEqual(out["done"], [])


if __name__ == "__main__":
    unittest.main()
