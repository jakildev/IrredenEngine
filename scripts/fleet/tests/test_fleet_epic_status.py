"""fleet-epic-status reads each issue's plan state from its `## Plan` comment
through `gh issue view <N> --repo <slug> --json comments`, for the umbrella and
every child, and reports it in both the JSON and the human output.

The `gh` stub models that argument vector exactly and raises on any other call
shape, so a fallback to a checkout probe or a drifted request shape fails the
suite instead of passing silently. Hermetic: no live GitHub, no live ~/.fleet.
"""
import importlib.machinery
import importlib.util
import io
import json
import subprocess
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-epic-status"
_loader = importlib.machinery.SourceFileLoader("fleet_epic_status", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_epic_status", _loader)
mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(mod)

SLUG = "jakildev/IrredenEngine"
UMBRELLA = 1661


class _GhStub:
    """`gh issue view <N> --repo <slug> --json comments`; `comments` maps issue
    number -> the comment bodies gh would return."""

    def __init__(self, comments):
        self.comments = comments
        self.calls = []

    def __call__(self, argv, **kwargs):
        self.calls.append(argv)
        expected_shape = (len(argv) == 8 and argv[:3] == ["gh", "issue", "view"]
                          and argv[4:6] == ["--repo", SLUG]
                          and argv[6:8] == ["--json", "comments"])
        if not expected_shape:
            raise AssertionError(f"unmodeled gh invocation: {argv!r}")
        number = int(argv[3])
        if number not in self.comments:
            raise AssertionError(f"fixture has no comments for #{number}")
        payload = {"comments": [{"body": b} for b in self.comments[number]]}
        return subprocess.CompletedProcess(argv, 0, stdout=json.dumps(payload), stderr="")


class PlanCommentExists(unittest.TestCase):
    def test_plan_heading_is_the_only_signal(self):
        stub = _GhStub({
            1: ["## Plan: do the thing\n\nsteps"],
            2: ["### Plan corrections\n- fix"],
            3: ["Discussion mentioning ## Plan mid-line", "## Triage"],
            4: [],
            5: ["  ## Plan\nindented heading still counts"],
        })
        with patch.object(mod.subprocess, "run", stub):
            self.assertTrue(mod.plan_comment_exists(SLUG, 1))
            self.assertTrue(mod.plan_comment_exists(SLUG, 2), "a deeper Plan heading counts")
            self.assertFalse(mod.plan_comment_exists(SLUG, 3))
            self.assertFalse(mod.plan_comment_exists(SLUG, 4))
            self.assertTrue(mod.plan_comment_exists(SLUG, 5))
        self.assertEqual(len(stub.calls), 5, "one live view per issue, nothing else")

    def test_gh_failure_reads_as_no_plan(self):
        def failing(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 1, stdout="", stderr="boom")
        err = io.StringIO()
        with patch.object(mod.subprocess, "run", failing), patch.object(mod.sys, "stderr", err):
            self.assertFalse(mod.plan_comment_exists(SLUG, 9))
        self.assertIn("gh failed", err.getvalue())

    def test_no_checkout_probe_remains(self):
        self.assertFalse(hasattr(mod, "find_repo_root"))
        self.assertFalse(hasattr(mod, "PLANS_DIR"))


class Dashboard(unittest.TestCase):
    """main() end to end with the issue fetches stubbed at their seams; only
    the plan probe reaches the gh stub."""

    def setUp(self):
        self.umbrella = {
            "number": UMBRELLA, "title": "engine: an epic", "state": "OPEN",
            "labels": [{"name": "fleet:epic"}],
            "body": "## Children\n\n- [ ] #1662 — first\n- [ ] #1663 — second\n",
        }
        self.children = [
            {"number": 1662, "title": "engine: first (P1)", "state": "OPEN",
             "labels": [{"name": "fleet:task"}, {"name": "fleet:opus"}],
             "body": "**Model:** opus\n**Part of epic:** #1661\n**Blocked by:** (none)\n"},
            {"number": 1663, "title": "engine: second (P2)", "state": "OPEN",
             "labels": [{"name": "fleet:task"}, {"name": "fleet:sonnet"}],
             "body": "**Model:** sonnet\n**Part of epic:** #1661\n**Blocked by:** #1662\n"},
        ]
        self.stub = _GhStub({
            UMBRELLA: ["## Plan: an epic\n\n### Scope\n..."],
            1662: ["## Plan: first\n\n### Scope\n..."],
            1663: ["## Triage\napproved", "just a comment"],
        })

    def run_main(self, *argv):
        out = io.StringIO()
        with patch.object(mod.subprocess, "run", self.stub), \
                patch.object(mod, "fetch_issue", lambda slug, n: dict(self.umbrella)), \
                patch.object(mod, "discover_children",
                             lambda slug, u, state: list(self.children)), \
                patch.object(mod, "load_state_cache", lambda: ({}, False)), \
                patch.object(mod, "prs_for_child_live", lambda slug, n: []), \
                patch.object(mod, "repo_slug", lambda key: SLUG), \
                patch.object(mod.sys, "argv", ["fleet-epic-status", str(UMBRELLA), *argv]), \
                redirect_stdout(out):
            rc = mod.main()
        return rc, out.getvalue()

    def test_json_reports_plan_state_for_umbrella_and_children(self):
        rc, out = self.run_main("--json")
        self.assertEqual(rc, 0)
        result = json.loads(out)
        self.assertTrue(result["umbrella"]["has_plan"])
        by_number = {c["number"]: c["has_plan"] for c in result["children"]}
        self.assertEqual(by_number, {1662: True, 1663: False})
        probed = sorted(int(argv[3]) for argv in self.stub.calls)
        self.assertEqual(probed, [UMBRELLA, 1662, 1663],
                         "exactly one plan probe per issue, umbrella included")

    def test_human_output_shows_plan_column(self):
        rc, out = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("  Plan:    present", out)
        rows = {int(line.split()[0].lstrip("#")): line
                for line in out.splitlines() if line.strip().startswith("#166")}
        self.assertRegex(rows[1662], r"\bY\b")
        self.assertRegex(rows[1663], r"\bN\b")

    def test_umbrella_without_plan_comment(self):
        self.stub.comments[UMBRELLA] = ["## Steward ledger\n\nreconciled-through: 2026-09-11"]
        rc, out = self.run_main("--json")
        self.assertFalse(json.loads(out)["umbrella"]["has_plan"])


if __name__ == "__main__":
    unittest.main()
