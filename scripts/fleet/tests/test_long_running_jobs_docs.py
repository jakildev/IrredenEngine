"""Author and reviewer protocols route long work to one runtime contract.

FLEET-RUNTIME.md § "Long-running jobs" is the single home of the rule; the
author pipeline and the reviewer protocol (which the shared review-pr flow
inherits) point at its anchor, and its profile table matches the profiles
fleet-jobs actually accepts.
"""

import importlib.machinery
import importlib.util
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
DOCS = REPO / "docs/agents"
SUBJECT = REPO / "scripts/fleet/fleet-jobs"
ANCHOR = "FLEET-RUNTIME.md#long-running-jobs"
if not (DOCS / "FLEET-RUNTIME.md").is_file():
    print("SKIP: docs/agents/FLEET-RUNTIME.md absent", file=sys.stderr)
    sys.exit(3)


def profiles():
    # The docs are the subject; a table naming profiles of an absent runner
    # is a failure, not a skip.
    if not SUBJECT.is_file():
        return set()
    loader = importlib.machinery.SourceFileLoader("fleet_jobs_docs", str(SUBJECT))
    spec = importlib.util.spec_from_loader("fleet_jobs_docs", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return set(module.PROFILES)


def section():
    text = (DOCS / "FLEET-RUNTIME.md").read_text()
    headings = re.findall(r"^## Long-running jobs$", text, re.M)
    if len(headings) != 1:
        raise AssertionError(f"expected one Long-running jobs heading, found {len(headings)}")
    body = text.split("## Long-running jobs\n", 1)[1]
    return re.split(r"^## ", body, maxsplit=1, flags=re.M)[0]


class Routing(unittest.TestCase):
    def test_author_and_reviewer_protocols_point_at_the_runtime_anchor(self):
        for doc in ("AUTHOR-PIPELINE.md", "REVIEWER-PROTOCOL.md"):
            with self.subTest(doc=doc):
                self.assertIn(ANCHOR, (DOCS / doc).read_text())
        self.assertIn("REVIEWER-PROTOCOL.md", (DOCS / "skills/review-pr.md").read_text())

    def test_rule_lives_only_in_the_runtime_doc(self):
        for role in sorted((REPO / ".claude/commands").glob("role-*.md")):
            with self.subTest(role=role.name):
                self.assertNotIn("fleet-jobs start", role.read_text())
        for doc in ("AUTHOR-PIPELINE.md", "REVIEWER-PROTOCOL.md"):
            with self.subTest(doc=doc):
                self.assertNotIn("fleet-jobs start", (DOCS / doc).read_text())

    def test_table_names_exactly_the_supported_profiles(self):
        table = [line for line in section().splitlines() if line.startswith("|")]
        rows = [line for line in table[1:] if not line.startswith("|---")]
        named = set()
        for row in rows:
            start = row.split("|")[2]
            if "fleet-build --detach" in start:
                named.add("build")
            named.update(re.findall(r"fleet-jobs start ([a-z-]+)", start))
        self.assertEqual(named, profiles())
        self.assertEqual(len(rows), len(profiles()))

    def test_evidence_is_a_terminal_wait_and_no_arbitrary_command_claim(self):
        body = section()
        self.assertIn("**Evidence is a terminal `wait`, never a start.**", body)
        self.assertIn("there is no arbitrary-command form", body)
        for claim in (r"\bany (long )?command\b", r"\barbitrary commands? (can|may)\b",
                      r"\bfleet-jobs start --\s"):
            with self.subTest(claim=claim):
                self.assertIsNone(re.search(claim, body, re.I))


if __name__ == "__main__":
    unittest.main(verbosity=2)
