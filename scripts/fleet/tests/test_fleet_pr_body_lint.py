"""Hermetic tests for fleet-pr-body-lint."""

import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINT = ROOT / "fleet-pr-body-lint"
ISSUE = 2563
CRITERIA = [f"Criterion {number}" for number in range(1, 7)]
PLAN = "## Plan: fixture\n\n### Acceptance criteria\n\n" + "\n".join(
    f"{number}. {criterion}" for number, criterion in enumerate(CRITERIA, 1)
)


def snapshot(number=ISSUE, body="", comments=None, complete=True):
    return {
        "number": number,
        "title": "Fixture issue",
        "body": body,
        "comments": comments if comments is not None else [{"body": PLAN}],
        "comments_complete": complete,
    }


def evidence_body(rows=6, number=ISSUE, extra=""):
    data = "\n".join(
        f"| {index}. criterion | check {index} | observed {index} |" for index in range(1, rows + 1)
    )
    return (
        "## Summary\n\nFixture.\n\n## Acceptance evidence\n"
        "| Criterion | Check run | Observed |\n"
        "|---|---|---|\n"
        f"{data}\n{extra}\n\nCloses #{number}\n"
    )


class FleetPrBodyLintTests(unittest.TestCase):
    def run_lint(self, body, issue=None, args=None, env=None, issue_number=None):
        with tempfile.TemporaryDirectory() as temp:
            issue_data = issue or snapshot()
            issue_path = Path(temp) / "issue.json"
            issue_path.write_text(json.dumps(issue_data), encoding="utf-8")
            command = [
                str(LINT),
                str(issue_number or issue_data.get("number", ISSUE)),
                "--issue-json",
                str(issue_path),
            ]
            command.extend(args or [])
            return subprocess.run(
                command,
                input=body,
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )

    def test_historical_original_bodies_fail_and_amended_bodies_pass(self):
        # Offline snapshots of the decisive body shapes from:
        # PR #2898, original 2026-08-05T01:20:07Z / amended 2026-08-07T07:02:17Z
        # https://github.com/jakildev/IrredenEngine/pull/2898
        # PR #2897, original 2026-08-05T01:12:12Z / amended 2026-08-05T02:55:04Z
        # https://github.com/jakildev/IrredenEngine/pull/2897
        for number in (2488, 2563):
            issue = snapshot(number=number)
            issue["comments"] = [{"body": PLAN}]
            result = self.run_lint(
                f"Claiming issue. Work in progress.\n\nCloses #{number}\n",
                issue=issue,
                args=["--repo", "engine"],
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("required=6, present=0", result.stdout)

            amended = evidence_body(number=number).replace(
                "observed 2", "deferred: deviation explained; reviewer grades vocabulary"
            )
            result = self.run_lint(amended, issue=issue)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("required=6, present=6", result.stdout)

    def test_row_count_positive_fire_and_source_diagnostics(self):
        # Derived #2897 fixture: retain historical evidence rows 1, 3, and 4.
        result = self.run_lint(evidence_body(rows=3))
        self.assertEqual(result.returncode, 1)
        self.assertIn("required=6, present=3, shortfall=3", result.stdout)
        for criterion in CRITERIA:
            self.assertIn(criterion, result.stdout)
        self.assertEqual(self.run_lint(evidence_body()).returncode, 0)

    def test_criteria_list_forms_nested_continuations_and_single_paragraph(self):
        forms = [
            "1. one\n   - nested\n   continuation\n2. two",
            "- one\n  continuation\n- two",
            "- [x] one\n- [ ] two",
            "one criterion continued on one paragraph",
        ]
        for block in forms:
            with self.subTest(block=block):
                issue = snapshot(
                    comments=[{"body": f"## Plan\n\n### Acceptance criteria\n{block}"}]
                )
                expected = 1 if block.startswith("one") else 2
                result = self.run_lint(evidence_body(rows=expected), issue=issue)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f"required={expected}", result.stdout)

    def test_body_only_absent_and_empty_criteria(self):
        body_issue = snapshot(body="**Acceptance criteria**:\n- first\n- second", comments=[])
        self.assertEqual(self.run_lint(evidence_body(2), issue=body_issue).returncode, 0)
        no_criteria = snapshot(body="No acceptance field.", comments=[])
        self.assertIn(
            "no acceptance criteria", self.run_lint(evidence_body(0), issue=no_criteria).stdout
        )
        empty = snapshot(body="**Acceptance criteria**:", comments=[])
        self.assertEqual(self.run_lint(evidence_body(0), issue=empty).returncode, 2)
        mixed = snapshot(
            comments=[
                {
                    "body": (
                        "## Plan\n\n### Acceptance criteria\n"
                        "- first\n\nunsupported second paragraph"
                    )
                }
            ]
        )
        self.assertEqual(self.run_lint(evidence_body(1), issue=mixed).returncode, 2)

    def test_replan_review_and_corrections_follow_thread_authority(self):
        comments = [
            {"body": "## Plan\n\n### Acceptance criteria\n1. obsolete"},
            {"body": PLAN},
            {"body": "## Plan review — sound\n\n### Acceptance criteria\n1. fake"},
            {"body": "## Plan corrections\n\nCorrected `some/path`."},
        ]
        result = self.run_lint(evidence_body(6), issue=snapshot(comments=comments))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("required=6", result.stdout)
        self.assertIn(
            "present=3", self.run_lint(evidence_body(3), issue=snapshot(comments=comments)).stdout
        )

        comments.append(
            {"body": "## Plan corrections\n\n### Acceptance criteria\n1. a\n2. b\n3. c"}
        )
        self.assertIn(
            "required=3", self.run_lint(evidence_body(3), issue=snapshot(comments=comments)).stdout
        )
        comments.extend(
            [
                {"body": "## Plan corrections\n\nOnly a measurement changed."},
                {"body": "## Plan corrections\n\n### Acceptance criteria\n- a\n- b\n- c\n- d"},
            ]
        )
        result = self.run_lint(evidence_body(3), issue=snapshot(comments=comments))
        self.assertEqual(result.returncode, 1)
        self.assertIn("required=4", result.stdout)

        comments.append({"body": "## Plan corrections\n\n### Acceptance criteria\n"})
        self.assertEqual(
            self.run_lint(evidence_body(4), issue=snapshot(comments=comments)).returncode, 2
        )

    def test_ignored_regions_crlf_escaped_pipes_and_table_controls(self):
        fake = (
            "> Closes #2563\r\n```\r\nCloses #2563\r\n## Acceptance evidence\r\n"
            "| Criterion | Check | Observed |\r\n|---|---|---|\r\n| fake | x | y |\r\n```\r\n"
            "<!-- Closes #2563 -->\r\n"
        )
        self.assertIn("no standalone Closes", self.run_lint(fake).stdout)
        body = evidence_body(6).replace("criterion |", r"criterion \| detail |")
        self.assertEqual(self.run_lint(body.replace("\n", "\r\n")).returncode, 0)

    def test_multiple_closing_issues_require_scoped_tables(self):
        body = evidence_body(6) + "\nCloses #2488\n"
        self.assertEqual(self.run_lint(body).returncode, 1)
        rows = "\n".join(f"| c{i} | run | seen |" for i in range(6))
        scoped = (
            "## Acceptance evidence\n\n### Issue #2563\n"
            "| Criterion | Grade | Evidence |\n|:---|:---:|---:|\n"
            f"{rows}\n\n### Issue #2488\n| Criterion | Check | Observed |\n|---|---|---|\n"
            "| other | run | seen |\n\nCloses #2563\nCloses #2488\n"
        )
        self.assertEqual(self.run_lint(scoped).returncode, 0)

    def test_cached_json_validation_and_no_network(self):
        with tempfile.TemporaryDirectory() as temp:
            marker = Path(temp) / "network-called"
            gh = Path(temp) / "gh"
            gh.write_text(f"#!/bin/sh\ntouch '{marker}'\nexit 99\n", encoding="utf-8")
            gh.chmod(gh.stat().st_mode | stat.S_IXUSR)
            env = os.environ.copy()
            env["PATH"] = f"{temp}:{env['PATH']}"
            result = self.run_lint(evidence_body(), env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(marker.exists())

        broken_cases = (
            (snapshot(number=1), ISSUE),
            (snapshot(complete=False), None),
            ({"number": ISSUE}, None),
        )
        for broken, issue_number in broken_cases:
            self.assertEqual(
                self.run_lint(evidence_body(), issue=broken, issue_number=issue_number).returncode,
                2,
            )

    def test_live_fetch_paginates_and_matches_cached_mode(self):
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            calls = temp_path / "calls"
            gh = temp_path / "gh"
            late_plan = json.dumps([[{"body": "ordinary"}], [{"body": PLAN}]])
            gh.write_text(
                "#!/usr/bin/env python3\n"
                "import json, pathlib, sys\n"
                f"pathlib.Path({str(calls)!r}).open('a').write(' '.join(sys.argv[1:]) + '\\n')\n"
                "if sys.argv[1:3] == ['issue', 'view']:\n"
                f"    print(json.dumps({{'number': {ISSUE}, 'title': 'x', 'body': ''}}))\n"
                "elif sys.argv[1] == 'api':\n"
                f"    print({late_plan!r})\n"
                "else:\n"
                "    raise SystemExit(2)\n",
                encoding="utf-8",
            )
            gh.chmod(gh.stat().st_mode | stat.S_IXUSR)
            env = os.environ.copy()
            env["PATH"] = f"{temp}:{env['PATH']}"
            result = subprocess.run(
                [str(LINT), str(ISSUE), "--write-issue-json", str(temp_path / "snapshot.json")],
                input=evidence_body(),
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("required=6, present=6", result.stdout)
            written = json.loads((temp_path / "snapshot.json").read_text(encoding="utf-8"))
            self.assertTrue(written["comments_complete"])
            self.assertEqual(len(written["comments"]), 2)
            call_text = calls.read_text(encoding="utf-8")
            self.assertIn("api --paginate --slurp", call_text)
            self.assertIn("per_page=100", call_text)

    def test_live_fetch_failure_is_exit_two(self):
        with tempfile.TemporaryDirectory() as temp:
            gh = Path(temp) / "gh"
            gh.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            gh.chmod(gh.stat().st_mode | stat.S_IXUSR)
            env = os.environ.copy()
            env["PATH"] = f"{temp}:{env['PATH']}"
            result = subprocess.run(
                [str(LINT), str(ISSUE)],
                input=evidence_body(),
                text=True,
                capture_output=True,
                env=env,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("GitHub issue fetch failed", result.stderr)

    def test_publication_gate_blocks_short_and_parse_error_but_allows_complete(self):
        flow = (ROOT.parents[1] / "docs/agents/skills/commit-and-push.md").read_text(
            encoding="utf-8"
        )
        procedure = (
            ROOT.parents[1] / ".claude/skills/commit-and-push/procedures/stackable-on.md"
        ).read_text(encoding="utf-8")
        self.assertIn('fleet-pr-body-lint "$closes_n"', flow)
        self.assertIn("--write-issue-json", flow)
        self.assertLess(flow.index('fleet-pr-body-lint "$closes_n"'), flow.index("Tokenize both"))
        self.assertIn("--body-file .pr-body.md", procedure)
        self.assertIn('if [[ -n "$existing" ]]', procedure)

        marker = "PUBLISHED"
        for body, expected in ((evidence_body(3), False), (evidence_body(6), True)):
            result = self.run_lint(body)
            published = marker if result.returncode == 0 else ""
            self.assertEqual(bool(published), expected)


if __name__ == "__main__":
    if not LINT.is_file():
        print(f"SKIP: subject under test missing at {LINT}", file=sys.stderr)
        sys.exit(3)
    unittest.main()
