"""Hermetic tests for fleet-pr-closes-lint."""

import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINT = ROOT / "fleet-pr-closes-lint"
WORKFLOW = ROOT.parents[1] / ".github/workflows/pr-closes-lint.yml"
FIXTURES = json.loads(
    (Path(__file__).resolve().parent / "pr_closes_lint_fixtures.json").read_text(encoding="utf-8")
)
PR = FIXTURES["pr3138"]
NARRATIVE = "steward to close #2321, re-anchor"
REWORDED = "steward to close issue 2321, re-anchor"


def reworded_commits():
    commits = [dict(c) for c in PR["commits"]]
    assert NARRATIVE in commits[1]["message"]
    commits[1]["message"] = commits[1]["message"].replace(NARRATIVE, REWORDED)
    return commits


class LintCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.env = os.environ.copy()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, text):
        path = self.dir / name
        path.write_text(text, encoding="utf-8")
        return str(path)

    def run_lint(self, *args, cwd=None):
        return subprocess.run(
            [sys.executable, str(LINT), *args],
            capture_output=True, text=True, timeout=30,
            cwd=cwd or self.dir, env=self.env, check=False,
        )

    def explicit(self, messages, body="", title="docs: fixture title", extra=()):
        return self.run_lint(
            "--messages-file", self.write("messages.json", json.dumps(messages)),
            "--title", title,
            "--body-file", self.write("body.md", body),
            *extra,
        )

    def assertExit(self, result, code):
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)


class IncidentReplay(LintCase):
    def test_positive_control_reports_the_narrative_ref_from_commit_two(self):
        result = self.explicit(PR["commits"], PR["body"], PR["title"])
        self.assertExit(result, 1)
        undeclared = [ln for ln in result.stdout.splitlines() if ln.startswith("UNDECLARED")]
        self.assertEqual(len(undeclared), 1, result.stdout)
        self.assertIn("#2321", undeclared[0])
        self.assertIn("commit 2/2 cdce82544", undeclared[0])

    def test_negative_control_reworded_sentence_reports_nothing(self):
        result = self.explicit(reworded_commits(), PR["body"], PR["title"])
        self.assertExit(result, 0)
        self.assertNotIn("UNDECLARED", result.stdout)

    def test_the_two_arms_differ_in_that_sentence_only(self):
        before = PR["commits"][1]["message"].split("\n")
        after = reworded_commits()[1]["message"].split("\n")
        self.assertEqual(len(before), len(after))
        changed = [(a, b) for a, b in zip(before, after) if a != b]
        self.assertEqual(len(changed), 1)
        self.assertEqual(changed[0][0].replace(NARRATIVE, REWORDED), changed[0][1])
        self.assertEqual(PR["commits"][0], reworded_commits()[0])

    def test_amendment_commit_one_alone_passes_and_both_fail(self):
        self.assertExit(self.explicit(PR["commits"][:1], PR["body"], PR["title"]), 0)
        self.assertExit(self.explicit(PR["commits"], PR["body"], PR["title"]), 1)


class RawVersusStripped(LintCase):
    def test_backticked_keyword_in_a_commit_message_is_reported(self):
        result = self.explicit([FIXTURES["commit_backticked"]])
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED #1824", result.stdout)

    def test_the_same_text_as_a_pr_body_is_not_reported(self):
        result = self.explicit(["chore: clean message"], FIXTURES["commit_backticked"])
        self.assertIn("would close: (none)", result.stdout)
        self.assertExit(result, 0)

    def test_negated_keyword_before_a_ref_is_reported(self):
        result = self.explicit([FIXTURES["commit_negated"]])
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED #1271", result.stdout)

    def test_keyword_wrapped_onto_the_next_line_is_reported(self):
        result = self.explicit([FIXTURES["commit_wrapped"]])
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED #2547", result.stdout)

    def test_colon_after_the_keyword_is_reported(self):
        self.assertExit(self.explicit(["fix: thing\n\nFixes: #41"]), 1)

    def test_keyword_in_the_title_is_reported_with_source_title(self):
        result = self.explicit(["chore: clean"], title="thing (closes #42)")
        self.assertExit(result, 1)
        self.assertIn("[title line 1]", result.stdout)


class DeclaredSet(LintCase):
    def test_commit_keyword_declared_by_a_body_link_line_passes(self):
        self.assertExit(self.explicit(["fix: x\n\nCloses #43"], "Summary.\n\nCloses #43\n"), 0)

    def test_commit_keyword_with_a_body_refs_line_fails(self):
        self.assertExit(self.explicit(["fix: x\n\nCloses #43"], "Summary.\n\nRefs #43\n"), 1)

    def test_link_line_forms(self):
        body = "- Fixes #44, closes #45 and resolves jakildev/irreden#46.\n"
        msgs = ["a\n\nfixes #44", "b\n\ncloses #45", "c\n\nresolves jakildev/irreden#46"]
        self.assertExit(self.explicit(msgs, body), 0)

    def test_link_line_inside_a_fence_does_not_declare(self):
        self.assertExit(self.explicit(["fix: x\n\nCloses #43"], "```\nCloses #43\n```\n"), 1)

    def oracle_file(self, number):
        oracle = [{"number": number, "repository": {"name": "IrredenEngine",
                                                    "owner": {"login": "jakildev"}}}]
        return ("--closing-refs-file", self.write("oracle.json", json.dumps(oracle)))

    def test_mid_sentence_body_keyword_without_a_link_line_fails(self):
        # GitHub's closingIssuesReferences lists a narrative body ref too.
        result = self.explicit(["chore: clean"], "This PR does not close #47 yet.\n",
                               extra=self.oracle_file(47))
        self.assertExit(result, 1)
        self.assertIn("[body line 1]", result.stdout)

    def test_oracle_only_ref_fails(self):
        result = self.explicit(["chore: clean"], "Refs issue 48.\n",
                               extra=self.oracle_file(48))
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED #48  [oracle]", result.stdout)


class CrossRepo(LintCase):
    def test_foreign_ref_is_reported_with_its_slug(self):
        result = self.explicit(["chore: x\n\ncloses someone/Other#5"])
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED someone/other#5", result.stdout)

    def test_bare_and_qualified_own_repo_spellings_compare_equal(self):
        body = "Closes jakildev/IrredenEngine#49\n"
        self.assertExit(self.explicit(["fix: x\n\nfixes #49"], body), 0)
        self.assertExit(self.explicit(["fix: x\n\nfixes jakildev/irredenengine#49"],
                                      "Closes #49\n"), 0)

    def test_game_repo_resolves_bare_refs_to_its_own_slug(self):
        result = self.explicit(["fix: x\n\nfixes #50"], "Closes jakildev/irreden#50\n",
                               extra=("--repo", "game"))
        self.assertExit(result, 0)


class Coverage(LintCase):
    def test_zero_commits_is_exit_two(self):
        result = self.explicit([])
        self.assertExit(result, 2)
        self.assertIn("-- scanned title + 0 commit message(s) + body", result.stderr)

    def test_coverage_line_on_every_run(self):
        result = self.explicit(["a", "b"])
        self.assertIn("-- scanned title + 2 commit message(s) + body", result.stderr)

    def test_unreadable_body_file_is_exit_two(self):
        result = self.run_lint("--messages-file", self.write("m.json", '["a"]'),
                               "--title", "t", "--body-file", str(self.dir / "missing.md"))
        self.assertExit(result, 2)

    def test_malformed_messages_file_is_exit_two(self):
        self.assertExit(self.run_lint("--messages-file", self.write("m.json", "{}"),
                                      "--title", "t", "--body-file", self.write("b", "")), 2)

    def test_title_and_body_required_without_pr(self):
        self.assertExit(self.run_lint("--messages-file", self.write("m.json", '["a"]'),
                                      "--title", "t"), 2)

    def test_usage_error_is_exit_two(self):
        self.assertExit(self.run_lint("--bogus"), 2)


GH_STUB = r'''#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
fx = json.load(open(os.environ["STUB_FIXTURE"]))
log = open(os.environ["STUB_LOG"], "a")
log.write(json.dumps(args) + "\n")
slug = "jakildev/IrredenEngine"
if args == ["api", f"repos/{slug}/pulls/3138"]:
    print(json.dumps({"title": fx["title"], "body": fx["body"]}))
elif args == ["api", "--paginate", "--slurp", f"repos/{slug}/pulls/3138/commits?per_page=100"]:
    pages = [[{"sha": c["sha"], "commit": {"message": c["message"]}}] for c in fx["commits"]]
    print(json.dumps(pages))
elif args == ["pr", "view", "3138", "--repo", slug, "--json", "closingIssuesReferences"]:
    if os.environ.get("STUB_ORACLE_FAIL"):
        sys.stderr.write("GraphQL: API rate limit exceeded\n")
        sys.exit(1)
    print(json.dumps({"closingIssuesReferences": []}))
else:
    sys.stderr.write("unknown command or flag for stub: %r\n" % (args,))
    sys.exit(1)
'''


class PrAdapter(LintCase):
    def setUp(self):
        super().setUp()
        bin_dir = self.dir / "bin"
        bin_dir.mkdir()
        gh = bin_dir / "gh"
        gh.write_text(GH_STUB, encoding="utf-8")
        gh.chmod(gh.stat().st_mode | stat.S_IXUSR)
        self.log = self.dir / "gh.log"
        self.env.update({
            "PATH": f"{bin_dir}{os.pathsep}{self.env['PATH']}",
            "STUB_FIXTURE": self.write("fixture.json", json.dumps(PR)),
            "STUB_LOG": str(self.log),
        })

    def calls(self):
        return [json.loads(ln) for ln in self.log.read_text().splitlines()]

    def test_pr_mode_fetches_paged_commits_and_reports_the_incident(self):
        result = self.run_lint("--pr", "3138")
        self.assertExit(result, 1)
        self.assertIn("UNDECLARED #2321  [commit 2/2 cdce82544 line 4]", result.stdout)
        self.assertIn(["api", "--paginate", "--slurp",
                       "repos/jakildev/IrredenEngine/pulls/3138/commits?per_page=100"],
                      self.calls())

    def test_pr_mode_body_override_declares_the_ref(self):
        result = self.run_lint("--pr", "3138", "--body-file", self.write("b.md", "Closes #2321\n"))
        self.assertExit(result, 0)

    def test_failed_oracle_read_is_a_note_not_a_failure(self):
        self.env["STUB_ORACLE_FAIL"] = "1"
        result = self.run_lint("--pr", "3138", "--body-file", self.write("b.md", "Closes #2321\n"))
        self.assertExit(result, 0)
        self.assertIn("oracle skipped", result.stderr)

    def test_stub_fidelity_rejects_an_unpaged_commit_fetch(self):
        result = subprocess.run(
            ["gh", "api", "repos/jakildev/IrredenEngine/pulls/3138/commits"],
            capture_output=True, text=True, env=self.env, check=False)
        self.assertNotEqual(result.returncode, 0)


class RemedyExecutes(LintCase):
    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo, capture_output=True, text=True,
                              env=self.env, check=True).stdout.strip()

    def setUp(self):
        super().setUp()
        self.env.update({
            "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.test",
            "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.test",
        })
        self.repo = self.dir / "repo"
        self.repo.mkdir()
        self.git("init", "-q", "-b", "master")
        (self.repo / "a.txt").write_text("base\n")
        self.git("add", "a.txt")
        self.git("commit", "-q", "-m", "base")
        self.git("update-ref", "refs/remotes/origin/master", "HEAD")
        self.git("checkout", "-q", "-b", "feature")
        (self.repo / "a.txt").write_text("one\n")
        self.git("commit", "-q", "-am", "docs: first\n\nA steward may close #51 later.")
        self.narrative_sha = self.git("rev-parse", "HEAD")
        (self.repo / "b.txt").write_text("two\n")
        self.git("add", "b.txt")
        self.git("commit", "-q", "-m", "docs: second")
        self.body = self.write("body.md", "Refs issue 51.\n")

    def lint_base(self):
        return self.run_lint("--base", "master", "--title", "docs: fixture",
                             "--body-file", self.body, cwd=self.repo)

    def test_older_commit_is_named_then_the_documented_collapse_clears_it(self):
        result = self.lint_base()
        self.assertExit(result, 1)
        self.assertIn(f"commit 1/2 {self.narrative_sha[:9]}", result.stdout)
        tree = self.git("rev-parse", "HEAD^{tree}")
        fork = self.git("merge-base", "HEAD", "origin/master")
        self.git("reset", "--soft", fork)
        self.git("commit", "-q", "-m", "docs: collapsed, clean message")
        self.assertExit(self.lint_base(), 0)
        self.assertEqual(self.git("rev-parse", "HEAD^{tree}"), tree)

    def test_base_mode_combines_with_a_declaring_body(self):
        self.body = self.write("body.md", "Summary.\n\nCloses #51\n")
        self.assertExit(self.lint_base(), 0)

    def test_base_mode_with_no_branch_commits_is_exit_two(self):
        self.git("checkout", "-q", "master")
        self.assertExit(self.lint_base(), 2)


class WorkflowShape(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text(encoding="utf-8")

    def test_triggers_cover_push_and_edit(self):
        match = re.search(r"^\s+types:\s*\[([^\]]*)\]", self.text, re.M)
        self.assertIsNotNone(match)
        types = {t.strip() for t in match.group(1).split(",")}
        self.assertTrue({"opened", "reopened", "synchronize", "edited"} <= types, types)

    def test_no_paths_filter(self):
        self.assertIsNone(re.search(r"^\s+paths(-ignore)?:", self.text, re.M))

    def test_pr_text_is_never_interpolated_into_a_run_script(self):
        self.assertNotRegex(self.text, r"github\.event\.pull_request\.(title|body)")

    def test_invokes_the_tool_by_path_in_pr_mode(self):
        self.assertIn("scripts/fleet/fleet-pr-closes-lint --pr", self.text)


if __name__ == "__main__":
    if not LINT.is_file():
        print(f"SKIP: subject under test missing at {LINT}", file=sys.stderr)
        sys.exit(3)
    unittest.main()
