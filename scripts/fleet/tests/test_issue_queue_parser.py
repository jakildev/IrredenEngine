"""Tests for the issue-based task queue parser in fleet-state-scout (T-381).

Covers the pure-function helpers — _parse_issue_field, _build_task_id_map,
_convert_blocked_by — and the label/body precedence rules inside
fetch_task_queue's per-issue loop (model, owner, in_progress, area).
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


class BuildTaskIdMap(unittest.TestCase):
    def test_maps_issue_to_task_id_and_fields(self):
        md = (
            "- [~] **fleet: scout reads issues** — summary\n"
            "  - **ID:** T-381\n"
            "  - **Model:** opus\n"
            "  - **Issue:** #1215\n"
            "  - **Blocked by:** #1214\n"
            "- [ ] **next task**\n"
            "  - **ID:** T-382\n"
            "  - **Issue:** #1216\n"
            "  - **Model:** sonnet\n"
        )
        m = _mod._build_task_id_map(md)
        self.assertEqual(m[1215]["id"], "T-381")
        self.assertEqual(m[1215]["model"], "opus")
        self.assertEqual(m[1215]["blocked_by"], "#1214")
        self.assertEqual(m[1216]["id"], "T-382")
        self.assertEqual(m[1216]["model"], "sonnet")

    def test_skips_entries_missing_id_or_issue(self):
        md = (
            "- [ ] **no id**\n"
            "  - **Issue:** #999\n"
            "- [ ] **no issue**\n"
            "  - **ID:** T-099\n"
        )
        self.assertEqual(_mod._build_task_id_map(md), {})

    def test_empty_input(self):
        self.assertEqual(_mod._build_task_id_map(""), {})
        self.assertEqual(_mod._build_task_id_map(None), {})


class ConvertBlockedBy(unittest.TestCase):
    def test_translates_issue_refs_to_task_ids(self):
        issue_to_id = {1214: "T-380", 1215: "T-381"}
        self.assertEqual(
            _mod._convert_blocked_by("#1214", issue_to_id), "T-380"
        )
        self.assertEqual(
            _mod._convert_blocked_by("#1214, #1215", issue_to_id),
            "T-380, T-381",
        )

    def test_unknown_issue_passes_through(self):
        self.assertEqual(_mod._convert_blocked_by("#9999", {}), "#9999")

    def test_handles_none_and_sentinels(self):
        self.assertEqual(_mod._convert_blocked_by("(none)", {}), "(none)")
        self.assertEqual(_mod._convert_blocked_by("", {}), "(none)")
        self.assertEqual(_mod._convert_blocked_by("—", {}), "(none)")

    def test_preserves_non_issue_text(self):
        self.assertEqual(
            _mod._convert_blocked_by("free-text reason", {}),
            "free-text reason",
        )


class FetchTaskQueueDispatch(unittest.TestCase):
    """Exercise the per-issue loop with synthetic gh output.

    Stubs run_capture so the test doesn't shell out to gh / git.
    """
    def _run(self, issues, tasks_md=""):
        captured = []
        def fake_run_capture(cmd, **kwargs):
            captured.append(cmd)
            if cmd[0] == "gh":
                import json
                return json.dumps(issues)
            if cmd[0] == "git":
                return tasks_md
            return ""
        original = _mod.run_capture
        _mod.run_capture = fake_run_capture
        try:
            return _mod.fetch_task_queue("jakildev/IrredenEngine", _mod.ENGINE)
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

    def test_task_id_pulled_from_tasks_md_during_transition(self):
        md = (
            "- [ ] **render: task**\n"
            "  - **ID:** T-385\n"
            "  - **Issue:** #100\n"
        )
        out = self._run([{
            "number": 100, "title": "render: task",
            "labels": [{"name": "fleet:queued"}],
            "body": "",
        }], tasks_md=md)
        self.assertEqual(out["open"][0]["id"], "T-385")
        self.assertEqual(out["open"][0]["title"], "T-385")

    def test_blocked_by_translated_via_id_map(self):
        md = (
            "- [ ] **a**\n  - **ID:** T-100\n  - **Issue:** #50\n"
            "- [ ] **b**\n  - **ID:** T-200\n  - **Issue:** #100\n"
        )
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Blocked by:** #50",
        }], tasks_md=md)
        self.assertEqual(out["open"][0]["blocked_by"], "T-100")

    def test_empty_gh_output_returns_empty_sections(self):
        out = self._run([])
        self.assertEqual(out["open"], [])
        self.assertEqual(out["in_progress"], [])
        self.assertEqual(out["done"], [])


if __name__ == "__main__":
    unittest.main()
