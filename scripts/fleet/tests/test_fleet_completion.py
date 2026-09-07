"""Tests for fleet_completion.py — the completion contract on a dispatch target.

Pure verdicts against fixtures (the dispatcher fetches, this decides), the
`declined:` grammar, and the CLI line the dispatcher parses.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import fleet_completion as fc  # noqa: E402

DISPATCHED = 1788714000  # 2026-09-06T17:00:00Z
LABEL = "fleet:claim-mac-pool-3"
HOST_AGENT = "mac-pool-3"


def comment(body, created="2026-09-06T18:00:00Z"):
    return {"created_at": created, "body": body}


class DeclineGrammar(unittest.TestCase):
    def test_plain_line_matches_and_yields_the_reason(self):
        self.assertEqual(
            fc.declined_since([comment("declined: worker/opus @mac-pool-3 needs a mac host")],
                              HOST_AGENT, DISPATCHED),
            "needs a mac host")

    def test_bold_and_missing_reason(self):
        self.assertEqual(
            fc.declined_since([comment("**declined:** worker/opus @mac-pool-3")],
                              HOST_AGENT, DISPATCHED),
            "no reason given")

    def test_other_agent_and_quoted_grammar_do_not_count(self):
        comments = [
            comment("declined: worker/opus @mac-pool-4 not mine"),
            comment("run `declined: x @mac-pool-3 y` to walk away"),
            comment("the grammar is\n```\ndeclined: x @mac-pool-3 y\n```"),
            comment("see: declined: worker/opus @mac-pool-3 mid-line"),
        ]
        self.assertIsNone(fc.declined_since(comments, HOST_AGENT, DISPATCHED))

    def test_comment_from_before_dispatch_does_not_count(self):
        self.assertIsNone(
            fc.declined_since([comment("declined: worker/opus @mac-pool-3 old",
                                       "2026-09-06T16:00:00Z")],
                              HOST_AGENT, DISPATCHED))

    def test_camel_case_created_at_and_unparseable_stamps(self):
        self.assertEqual(
            fc.declined_since([{"createdAt": "2026-09-06T18:00:00Z",
                                "body": "declined: a/b @mac-pool-3 ok"}],
                              HOST_AGENT, DISPATCHED),
            "ok")
        self.assertIsNone(
            fc.declined_since([{"createdAt": "garbage", "body": "declined: a/b @mac-pool-3 ok"}],
                              HOST_AGENT, DISPATCHED))


class Verdicts(unittest.TestCase):
    def test_released_label_is_finished(self):
        self.assertEqual(fc.assess(LABEL, ["fleet:queued"], [], HOST_AGENT, DISPATCHED)[0],
                         fc.VERDICT_FINISHED)

    def test_standing_label_with_no_record_is_abandoned(self):
        verdict, detail = fc.assess(LABEL, [LABEL], [], HOST_AGENT, DISPATCHED)
        self.assertEqual(verdict, fc.VERDICT_ABANDONED)
        self.assertIn(LABEL, detail)

    def test_decline_wins_over_the_standing_label(self):
        verdict, detail = fc.assess(
            LABEL, [LABEL], [comment("declined: worker/opus @mac-pool-3 needs mac")],
            HOST_AGENT, DISPATCHED)
        self.assertEqual(verdict, fc.VERDICT_DECLINED)
        self.assertEqual(detail, "declined this iteration: needs mac")

    def test_an_open_pr_referencing_the_task_means_the_label_rides_it(self):
        by_branch = [{"number": 50, "headRefName": "claude/42-fix", "body": ""}]
        by_body = [{"number": 50, "headRefName": "claude/other", "body": "Closes #42"}]
        for prs in (by_branch, by_body):
            self.assertEqual(
                fc.assess(LABEL, [LABEL], [], HOST_AGENT, DISPATCHED,
                          open_prs=prs, number=42, repo="engine")[0],
                fc.VERDICT_FINISHED)
        unrelated = [{"number": 50, "headRefName": "claude/43-fix", "body": ""}]
        self.assertEqual(
            fc.assess(LABEL, [LABEL], [], HOST_AGENT, DISPATCHED,
                      open_prs=unrelated, number=42, repo="engine")[0],
            fc.VERDICT_ABANDONED)

    def test_a_parked_pr_still_counts_as_coverage(self):
        # The iteration produced a PR; the work is on it, not in a worktree
        # to salvage — the opposite of abandoned.
        parked = [{"number": 50, "headRefName": "claude/42-fix", "body": "",
                   "labels": [{"name": "fleet:parked"}]}]
        self.assertEqual(
            fc.assess(LABEL, [LABEL], [], HOST_AGENT, DISPATCHED,
                      open_prs=parked, number=42, repo="engine")[0],
            fc.VERDICT_FINISHED)

    def test_no_pr_list_means_no_coverage_test(self):
        self.assertEqual(
            fc.assess("fleet:amending-mac-pool-3", ["fleet:amending-mac-pool-3"], [],
                      HOST_AGENT, DISPATCHED)[0],
            fc.VERDICT_ABANDONED)

    def test_label_names_accepts_both_github_spellings(self):
        self.assertEqual(fc.label_names([{"name": "a"}, "b", {"x": 1}]), ["a", "b", ""])


class Cli(unittest.TestCase):
    def _run(self, issue, comments, prs=None, **extra):
        with tempfile.TemporaryDirectory() as tmp:
            paths = {}
            for name, data in (("issue", issue), ("comments", comments), ("prs", prs)):
                if data is None:
                    continue
                paths[name] = os.path.join(tmp, name + ".json")
                with open(paths[name], "w", encoding="utf-8") as handle:
                    json.dump(data, handle)
            script = os.path.join(os.path.dirname(fc.__file__), "fleet_completion.py")
            argv = [sys.executable, script,
                    "assess", "--claim-label", LABEL, "--host-agent", HOST_AGENT,
                    "--since", str(DISPATCHED), "--issue-json", paths["issue"],
                    "--comments-json", paths["comments"]]
            if "prs" in paths:
                argv += ["--prs-json", paths["prs"]]
            for key, value in extra.items():
                argv += [f"--{key}", str(value)]
            out = subprocess.run(argv, capture_output=True, text=True, check=True).stdout
            return out.rstrip("\r\n").split("\t")

    def test_line_carries_verdict_updated_at_and_detail(self):
        issue = {"labels": [{"name": LABEL}], "updated_at": "2026-09-06T18:30:00Z"}
        self.assertEqual(
            self._run(issue, [comment("declined: worker/opus @mac-pool-3 needs mac")]),
            ["declined", "2026-09-06T18:30:00Z", "declined this iteration: needs mac"])

    def test_pr_list_and_number_drive_the_coverage_test(self):
        issue = {"labels": [{"name": LABEL}], "updated_at": "x"}
        prs = [{"number": 50, "headRefName": "claude/42-fix", "body": ""}]
        self.assertEqual(self._run(issue, [], prs, number=42, repo="engine")[0], "finished")
        self.assertEqual(self._run(issue, [], None, number=42)[0], "abandoned")

    def test_a_bare_label_array_is_accepted(self):
        self.assertEqual(self._run(["fleet:queued"], []), ["finished", "", f"{LABEL} released"])


if __name__ == "__main__":
    unittest.main()
