"""Hermetic tests for fleet-pr-body-lint."""

import json
import os
import shutil
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
        # Fixture A: a body whose evidence rows were amended after the original.
        # https://github.com/jakildev/IrredenEngine/pull/2898
        # Fixture B: a body amended within hours of the original.
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
        # Derived from fixture B: retain historical evidence rows 1, 3, and 4.
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
            "- one\n\n  loose continuation\n- two",
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

    def test_documented_gate_controls_both_publication_paths_and_corrections(self):
        flow = (ROOT.parents[1] / "docs/agents/skills/commit-and-push.md").read_text(
            encoding="utf-8"
        )
        procedure = (
            ROOT.parents[1] / ".claude/skills/commit-and-push/procedures/stackable-on.md"
        ).read_text(encoding="utf-8")
        gate = flow.split("### 8a.", 1)[1].split("```bash\n", 1)[1].split("```", 1)[0]
        gate = gate.replace('"<N from the drafted body>"', str(ISSUE)).replace(
            "--repo <repo>", "--repo engine"
        )
        publish = procedure.split("## Open (or reconcile)", 1)[1].split("```bash\n", 1)[1]
        publish = publish.split("```", 1)[0].replace("<claude|codex>", "codex")

        correction_cases = {
            "non-acceptance": (
                [{"body": PLAN}, {"body": "## Plan corrections\n\nPath only."}],
                6,
                True,
            ),
            "complete-replacement": (
                [
                    {"body": PLAN},
                    {
                        "body": (
                            "## Plan corrections\n\n### Acceptance criteria\n"
                            "1. a\n2. b\n3. c"
                        )
                    },
                ],
                3,
                True,
            ),
            "replacement-shortfall": (
                [
                    {"body": PLAN},
                    {
                        "body": (
                            "## Plan corrections\n\n### Acceptance criteria\n"
                            "1. a\n2. b\n3. c\n4. d"
                        )
                    },
                ],
                3,
                False,
            ),
            "empty-replacement": (
                [{"body": PLAN}, {"body": "## Plan corrections\n\n### Acceptance criteria\n"}],
                6,
                False,
            ),
        }
        for name, (comments, rows, should_publish) in correction_cases.items():
            for existing in (False, True):
                with self.subTest(case=name, path="edit" if existing else "create"):
                    self.run_documented_publication(
                        gate, publish, comments, rows, existing, should_publish
                    )

    def run_documented_publication(
        self, gate, publish, comments, rows, existing, should_publish
    ):
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            bin_path = temp_path / "bin"
            bin_path.mkdir()
            (temp_path / ".pr-body.md").write_text(evidence_body(rows), encoding="utf-8")
            (temp_path / "comments.json").write_text(json.dumps([comments]), encoding="utf-8")

            lint_link = bin_path / "fleet-pr-body-lint"
            lint_link.symlink_to(LINT)
            gh = bin_path / "gh"
            gh.write_text(
                "#!/usr/bin/env python3\n"
                "import json, os, pathlib, sys\n"
                "args = sys.argv[1:]\n"
                "if args[:2] == ['issue', 'view']:\n"
                "    expected = ['issue', 'view', '2563', '--repo', "
                "'jakildev/IrredenEngine', '--json', 'number,title,body']\n"
                "    if args != expected: raise SystemExit(90)\n"
                "    print(json.dumps({'number': 2563, 'title': 'Fixture issue', 'body': ''}))\n"
                "elif args[:1] == ['api']:\n"
                "    expected = ['api', '--paginate', '--slurp', "
                "'repos/jakildev/IrredenEngine/issues/2563/comments?per_page=100']\n"
                "    if args != expected: raise SystemExit(91)\n"
                "    print(pathlib.Path(os.environ['COMMENTS']).read_text())\n"
                "elif args[:2] == ['pr', 'list']:\n"
                "    expected = ['pr', 'list', '--head', 'codex/example', '--state', "
                "'open', '--json', 'url', '-q', '.[0].url']\n"
                "    if args != expected:\n"
                "        raise SystemExit(92)\n"
                "    if os.environ['EXISTING'] == '1': print('https://example.test/pull/1')\n"
                "elif args[:2] == ['pr', 'edit']:\n"
                "    expected = ['pr', 'edit', 'https://example.test/pull/1', '--base', "
                "'master', '--add-label', 'fleet:author-codex', '--body-file', '.pr-body.md']\n"
                "    if args != expected: raise SystemExit(93)\n"
                "    pathlib.Path(os.environ['MARKER']).write_text('edit')\n"
                "elif args[:2] == ['pr', 'create']:\n"
                "    expected = ['pr', 'create', '--base', 'master', '--label', "
                "'fleet:wip', '--label', 'fleet:author-codex', '--title', "
                "'<scope>: <title> (#<N>)', '--body-file', '.pr-body.md']\n"
                "    if args != expected: raise SystemExit(94)\n"
                "    pathlib.Path(os.environ['MARKER']).write_text('create')\n"
                "else:\n"
                "    raise SystemExit(95)\n",
                encoding="utf-8",
            )
            jq = bin_path / "jq"
            jq.write_text(
                "#!/usr/bin/env python3\n"
                "import json, sys\n"
                "if sys.argv[1:] != ['-r', '.title', '.issue-2563.json']: raise SystemExit(96)\n"
                "print(json.load(open(sys.argv[-1]))['title'])\n",
                encoding="utf-8",
            )
            git = bin_path / "git"
            git.write_text(
                "#!/bin/sh\n"
                "[ \"$*\" = 'branch --show-current' ] || exit 97\n"
                "printf 'codex/example\\n'\n",
                encoding="utf-8",
            )
            for executable in (gh, jq, git):
                executable.chmod(executable.stat().st_mode | stat.S_IXUSR)

            marker = temp_path / "published"
            env = os.environ.copy()
            env.update(
                {
                    "PATH": f"{bin_path}:{env['PATH']}",
                    "COMMENTS": str(temp_path / "comments.json"),
                    "EXISTING": "1" if existing else "0",
                    "MARKER": str(marker),
                }
            )
            script = "set -e\nbase=master\n" + gate + publish
            result = subprocess.run(
                [shutil.which("bash") or "bash", "-c", script],
                cwd=temp_path,
                capture_output=True,
                text=True,
                timeout=10,
                env=env,
                check=False,
            )
            self.assertEqual(marker.exists(), should_publish, result.stdout + result.stderr)
            if should_publish:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(marker.read_text(), "edit" if existing else "create")
            else:
                self.assertIn(result.returncode, (1, 2), result.stdout + result.stderr)


if __name__ == "__main__":
    if not LINT.is_file():
        print(f"SKIP: subject under test missing at {LINT}", file=sys.stderr)
        sys.exit(3)
    unittest.main()
