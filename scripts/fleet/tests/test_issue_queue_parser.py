"""Tests for the issue-based task queue parser in fleet-state-scout
(T-381, T-392).

Covers the pure-function helper `_parse_issue_field` and the label/body
precedence rules inside `fetch_task_queue`'s per-issue loop (model, owner,
in_progress, area, blocked_by, task identity).
"""
import importlib.machinery
import importlib.util
import json
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


class ParseBlockedBy(unittest.TestCase):
    """`_parse_blocked_by` prefers the `**Blocked by:**` field but falls back
    to free-form `Blocked on #N` / `Blocked on PR-x` header prose (#1326).
    """
    def test_prefers_canonical_field(self):
        body = "**Blocked by:** #50\n\n## Blocked on #99\n"
        self.assertEqual(_mod._parse_blocked_by(body), "#50")

    def test_falls_back_to_header_prose(self):
        self.assertEqual(_mod._parse_blocked_by("## Blocked on #1300\n"), "#1300")

    def test_header_prose_plain_line(self):
        self.assertEqual(_mod._parse_blocked_by("Blocked on #1300 merging\n"),
                         "#1300 merging")

    def test_header_prose_bold(self):
        self.assertEqual(_mod._parse_blocked_by("**Blocked on #1300**\n"), "#1300")

    def test_header_prose_pr_token(self):
        self.assertEqual(_mod._parse_blocked_by("## Blocked on PR-x\n"), "PR-x")

    def test_header_prose_without_ref_is_not_a_blocker(self):
        # Incidental prose must not gate the task forever.
        self.assertEqual(_mod._parse_blocked_by("Blocked on the redesign\n"), "")

    def test_line_not_starting_with_blocked_ignored(self):
        self.assertEqual(
            _mod._parse_blocked_by("This was blocked on #99 last week.\n"), "")

    def test_empty_body(self):
        self.assertEqual(_mod._parse_blocked_by(""), "")
        self.assertEqual(_mod._parse_blocked_by(None), "")

    def test_inline_bold_mid_line_extracts_ref(self):
        # #1423: colon and value inside one bold span, mid-line after · separators.
        line = "**Part of epic:** #104 · **Phase 3 of 4** · **Blocked by: #106 (Phase 2)**"
        result = _mod._parse_blocked_by(line)
        self.assertIn("#106", result)

    def test_inline_bold_gate_blocks_open_ref(self):
        # Variant used in fleet-claim acceptance test: value must surface so
        # check_blockers can reject the claim.
        body = "**Part of epic:** #104 · **Phase 4 of 4** · **Blocked by: #107 (Phase 3)**\n"
        result = _mod._parse_blocked_by(body)
        self.assertIn("#107", result)

    def test_inline_bold_none_value_is_unblocked(self):
        body = "**Part of epic:** #104 · **Blocked by: (none)**\n"
        self.assertEqual(_mod._parse_blocked_by(body), "")

    def test_canonical_form_not_affected_by_inline_pattern(self):
        # The inline pattern must not fire on canonical **Blocked by:** form
        # (bold closes at colon; space+value follows outside the bold span).
        body = "**Blocked by:** #50\n"
        self.assertEqual(_mod._parse_blocked_by(body), "#50")

    def test_inline_bold_combined_with_canonical(self):
        # Both forms present — union both refs.
        body = "**Blocked by:** #50\n**Part of epic:** #1 · **Blocked by: #60 (Phase 2)**\n"
        result = _mod._parse_blocked_by(body)
        self.assertIn("#50", result)
        self.assertIn("#60", result)


class FetchTaskQueueDispatch(unittest.TestCase):
    """Exercise the per-issue loop with synthetic gh output.

    Stubs conditional_get so the test doesn't hit the GitHub REST API.
    """
    def _run(self, issues):
        # fetch_task_queue now reads the queue via _rest_list -> conditional_get
        # (REST + ETag cache), not run_capture. Patch that seam so the test stays
        # hermetic: a mock miss must never fall through to the live GitHub API or
        # mutate the shared ~/.fleet ETag cache the production scout uses (#2227).
        payload = json.dumps(issues)
        original = _mod.conditional_get
        _mod.conditional_get = lambda *a, **k: (True, payload)
        try:
            return _mod.fetch_task_queue("jakildev/IrredenEngine")
        finally:
            _mod.conditional_get = original

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

    def test_blocked_by_recovers_header_prose(self):
        # #1326: a dependency declared only as a `Blocked on #N` header must
        # surface as blocked, not (none).
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}],
            "body": "## Scope\n\n## Blocked on #1300\n\nMore.\n",
        }])
        self.assertEqual(out["open"][0]["blocked_by"], "#1300")

    def test_blocked_flag_set_from_label(self):
        # #1527: the fleet:blocked label surfaces as task["blocked"] so workers
        # see the state directly and the unblock projection can act on it.
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}, {"name": "fleet:blocked"}],
            "body": "**Blocked by:** #50",
        }])
        self.assertTrue(out["open"][0]["blocked"])

    def test_blocked_flag_false_without_label(self):
        out = self._run([{
            "number": 100, "title": "b",
            "labels": [{"name": "fleet:queued"}],
            "body": "**Blocked by:** (none)",
        }])
        self.assertFalse(out["open"][0]["blocked"])

    def test_empty_gh_output_returns_empty_sections(self):
        out = self._run([])
        self.assertEqual(out["open"], [])
        self.assertEqual(out["in_progress"], [])
        self.assertEqual(out["done"], [])


if __name__ == "__main__":
    unittest.main()
