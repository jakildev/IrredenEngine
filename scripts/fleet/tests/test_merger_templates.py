"""Contract tests for the merger comment templates in docs/agents/merger-templates.md.

The merger fills these by hand, so the test renders each one the way the
doc says to and checks what downstream readers parse: the step 5d dedup's
`SHA pair:` line, the `— fleet merger` selector, and the verbatim
`fleet-pr fork-of` lines.
"""

import re
import sys
import unittest
from pathlib import Path

DOC = Path(__file__).resolve().parents[3] / "docs/agents/merger-templates.md"

MASTER_SHA = "a" * 40
HEAD_SHA = "b" * 40
UPSTREAM_SHA = "c" * 40
BOUNDARY_SHA = "d" * 40
SIGN_OFF = "— fleet merger"
SHA_PAIR = re.compile(r"^SHA pair: master=([0-9a-f]{40}) × PR=([0-9a-f]{40})$", re.M)
PLACEHOLDER = re.compile(r"<([A-Za-z][\w-]*)>")

COMMON = {
    "N": "12",
    "baseRefName": "master",
    "this-headRefName": "feat-b",
}
SEMANTIC = dict(COMMON, **{
    "file1": "docs/a.md",
    "file2": "docs/b.md",
    "sha": "1234567",
    "subj": "subject",
    "master-tip-sha": MASTER_SHA,
    "pr-head-sha": HEAD_SHA,
    "merged-N": "21",
    "boundary-sha": BOUNDARY_SHA,
    "merged-parent-line": f"merged-parent: 21 {BOUNDARY_SHA}",
})
FORK = dict(COMMON, **{
    "upstream-N": "11",
    "upstream-headRefName": "feat-x",
    "upstream-tip-sha": UPSTREAM_SHA,
    "fork-of-line": f"fork-of: 11 feat-x {UPSTREAM_SHA}",
})


def section(text, heading):
    start = text.index(f"\n## § {heading}")
    end = text.find("\n## ", start + 1)
    return text[start:end if end != -1 else len(text)]


def fenced_blocks(text):
    return re.findall(r"^```\n(.*?)^```$", text, re.M | re.S)


def render(template, fields):
    def sub(match):
        if match.group(1) not in fields:
            raise AssertionError(f"unsubstituted placeholder {match.group(0)}")
        return fields[match.group(1)]
    return PLACEHOLDER.sub(sub, template)


def with_paragraph(body, paragraph):
    """The doc's insertion rule: paragraph, one blank line, then `SHA pair:`."""
    at = re.search(r"^SHA pair: ", body, re.M).start()
    return body[:at] + paragraph.rstrip("\n") + "\n\n" + body[at:]


class TemplateCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        doc = DOC.read_text(encoding="utf-8")
        cls.semantic_text = section(doc, "semantic-conflict")
        cls.fork_text = section(doc, "fork-of-other-pr")

    def blocks(self, text, count):
        found = fenced_blocks(text)
        self.assertEqual(len(found), count, "fenced template blocks in the section")
        return found

    def assert_sha_pair_last(self, body):
        self.assertTrue(re.search(SIGN_OFF, body), "merger selector must match")
        matches = SHA_PAIR.findall(body)
        self.assertEqual(matches, [(MASTER_SHA, HEAD_SHA)])
        lines = [line for line in body.splitlines() if line.strip()]
        self.assertEqual(lines[-1], SIGN_OFF)
        self.assertRegex(lines[-2], SHA_PAIR)

    def test_semantic_conflict_without_merged_parent(self):
        body = render(fenced_blocks(self.semantic_text)[0], SEMANTIC)
        self.assert_sha_pair_last(body)
        self.assertNotIn("merged-parent:", body)

    def test_semantic_conflict_with_merged_parent(self):
        semantic, paragraph = self.blocks(self.semantic_text, 2)
        body = render(with_paragraph(semantic, paragraph), SEMANTIC)
        self.assert_sha_pair_last(body)
        self.assertIn(f"\nmerged-parent: 21 {BOUNDARY_SHA}\n", body)
        self.assertIn(f"git rebase --onto origin/master {BOUNDARY_SHA} feat-b\n", body)

    def test_fork_of_other_pr_quotes_the_tool_line(self):
        (fork,) = self.blocks(self.fork_text, 1)
        body = render(fork, FORK)
        self.assertTrue(re.search(SIGN_OFF, body))
        self.assertIn(f"\nfork-of: 11 feat-x {UPSTREAM_SHA}\n", body)
        self.assertIn(f"git rebase --onto origin/master {UPSTREAM_SHA} feat-b\n", body)

    def test_each_template_names_the_command_that_produces_its_line(self):
        command = "`fleet-pr fork-of <N> --repo <engine|game>`"
        self.assertIn(command, self.fork_text)
        self.assertIn(command, self.semantic_text)


if __name__ == "__main__":
    if not DOC.is_file():
        print(f"SKIP: subject under test missing at {DOC}", file=sys.stderr)
        sys.exit(3)
    unittest.main()
