"""fleet-migrate-plan-files posts each live ledger / amendment once, and only once.

The gh seam is stubbed with an in-memory GitHub: an open-issue list, per-issue
comment threads, and a record of every comment posted. A stub miss raises, so
no call shape the script does not model can fall through to the live API.
"""

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-migrate-plan-files"
if not SUBJECT.is_file():
    print("SKIP: fleet-migrate-plan-files subject absent", file=sys.stderr)
    sys.exit(3)

import importlib.util  # noqa: E402

spec = importlib.util.spec_from_loader("fleet_migrate_plan_files", loader=None)
migrate = importlib.util.module_from_spec(spec)
migrate.__dict__["__file__"] = str(SUBJECT)
exec(compile(SUBJECT.read_text(), str(SUBJECT), "exec"), migrate.__dict__)  # noqa: S102

REPO = "example/repo"

LEDGER = (
    "## Steward ledger\n\nreconciled-through: 2026-09-10\n\n"
    "### Children\n| Child | State |\n|---|---|\n| #11 | merged |\n"
)
AMENDMENTS = (
    "## Amendments\n\n### A1 — 2026-09-10 — trigger: PR #5 merged\n"
    "- **Decision:** narrower scope\n"
)
PLAN_HEAD = "# Plan: thing\n\n- **Issue:** #{n}\n\n## Scope\n\nDo the thing.\n\n"
TAIL = "## References\n\n- none\n"


class GhStub:
    """In-memory GitHub behind the script's single gh() seam."""

    def __init__(self, open_issues, comments=None):
        self.open_issues = open_issues
        self.comments = {n: list(v) for n, v in (comments or {}).items()}
        self.posted = []
        self.calls = []

    def __call__(self, args, stdin=None):
        self.calls.append(list(args))
        if args[:2] == ["issue", "list"]:
            self.assertArgs(args, ["issue", "list", "--repo", REPO, "--state", "open",
                                   "--limit", "1000", "--json", "number,labels"])
            return json.dumps([{"number": n, "labels": [{"name": lab} for lab in labels]}
                               for n, labels in self.open_issues.items()])
        if args[:2] == ["api", "--paginate"]:
            (endpoint,) = args[2:]
            prefix, _, suffix = endpoint.partition("/issues/")
            number = int(suffix.split("/")[0])
            if prefix != f"repos/{REPO}" or not suffix.endswith("/comments?per_page=100"):
                raise AssertionError(f"unmodelled gh api endpoint: {endpoint}")
            pages = [json.dumps([{"body": b} for b in self.comments.get(number, [])])]
            return "\n".join(pages)
        if args[:2] == ["issue", "comment"]:
            number = int(args[2])
            self.assertArgs(args[3:], ["--repo", REPO, "--body-file", "-"])
            if stdin is None:
                raise AssertionError("comment body must arrive on stdin")
            self.comments.setdefault(number, []).append(stdin)
            self.posted.append((number, stdin))
            return ""
        raise AssertionError(f"unmodelled gh call: {args}")

    @staticmethod
    def assertArgs(actual, expected):
        if list(actual) != expected:
            raise AssertionError(f"gh argv {actual!r} != modelled {expected!r}")


class MigrateCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.plans = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write_plan(self, number, *blocks):
        (self.plans / f"issue-{number}.md").write_text(
            PLAN_HEAD.format(n=number) + "".join(blocks), encoding="utf-8")

    def run_migrate(self, stub, dry_run=False):
        migrate.gh = stub
        out = io.StringIO()
        with redirect_stdout(out):
            tally = migrate.migrate(REPO, self.plans, dry_run, out=out)
        return tally, out.getvalue()

    def test_ledger_posted_once_on_open_epic(self):
        self.write_plan(10, LEDGER)
        stub = GhStub({10: {"fleet:epic"}})
        tally, out = self.run_migrate(stub)
        self.assertEqual(stub.posted, [(10, LEDGER)])
        self.assertEqual(tally.counts["ledgers"]["posted"], 1)
        self.assertIn("#10 (issue-10.md): posted ## Steward ledger", out)

        tally2, out2 = self.run_migrate(stub)
        self.assertEqual(len(stub.posted), 1, "second run must not repost the ledger")
        self.assertEqual(tally2.counts["ledgers"]["present"], 1)
        self.assertIn("already present — skip", out2)

    def test_amendments_posted_once_as_plan_corrections(self):
        self.write_plan(20, AMENDMENTS, TAIL)
        stub = GhStub({20: {"fleet:task"}})
        tally, out = self.run_migrate(stub)
        self.assertEqual(len(stub.posted), 1)
        number, body = stub.posted[0]
        self.assertEqual(number, 20)
        self.assertTrue(body.startswith("## Plan corrections\n"), body)
        self.assertIn("### A1 — 2026-09-10 — trigger: PR #5 merged", body)
        self.assertNotIn("## Amendments", body)
        self.assertNotIn("## References", body, "the section ends at the next ## heading")
        self.assertEqual(tally.counts["corrections"]["posted"], 1)

        tally2, _ = self.run_migrate(stub)
        self.assertEqual(len(stub.posted), 1, "second run must not repost the corrections")
        self.assertEqual(tally2.counts["corrections"]["present"], 1)

    def test_identical_existing_corrections_matches_modulo_line_endings(self):
        self.write_plan(21, AMENDMENTS)
        existing = migrate.corrections_body(AMENDMENTS).replace("\n", "\r\n").rstrip()
        stub = GhStub({21: set()}, comments={21: [existing]})
        tally, _ = self.run_migrate(stub)
        self.assertEqual(stub.posted, [])
        self.assertEqual(tally.counts["corrections"]["present"], 1)

    def test_closed_issue_skipped_without_reading_its_thread(self):
        self.write_plan(30, AMENDMENTS, LEDGER)
        stub = GhStub({99: {"fleet:epic"}})
        tally, out = self.run_migrate(stub)
        self.assertEqual(stub.posted, [])
        self.assertEqual(tally.counts["ledgers"]["closed"], 1)
        self.assertEqual(tally.counts["corrections"]["closed"], 1)
        self.assertIn("#30 (issue-30.md): issue not open — skip", out)
        self.assertFalse(any(c[:2] == ["api", "--paginate"] for c in stub.calls),
                         "a closed issue's comments are never fetched")

    def test_file_without_either_section_skipped(self):
        self.write_plan(40, TAIL)
        stub = GhStub({40: {"fleet:epic"}})
        tally, _ = self.run_migrate(stub)
        self.assertEqual(stub.posted, [])
        self.assertEqual(tally.without_section, 1)
        self.assertEqual([c[:2] for c in stub.calls], [["issue", "list"]])

    def test_ledger_needs_the_epic_label_but_amendments_do_not(self):
        self.write_plan(50, AMENDMENTS, LEDGER)
        stub = GhStub({50: {"fleet:task"}})
        tally, out = self.run_migrate(stub)
        self.assertEqual([n for n, _ in stub.posted], [50])
        self.assertTrue(stub.posted[0][1].startswith("## Plan corrections\n"))
        self.assertEqual(tally.counts["ledgers"]["not-epic"], 1)
        self.assertIn("no fleet:epic label — skip", out)

    def test_dry_run_posts_nothing(self):
        self.write_plan(60, AMENDMENTS, LEDGER)
        stub = GhStub({60: {"fleet:epic"}})
        tally, out = self.run_migrate(stub, dry_run=True)
        self.assertEqual(stub.posted, [])
        self.assertIn("would post ## Steward ledger", out)
        self.assertIn("would post ## Plan corrections", out)
        self.assertEqual(tally.counts["ledgers"]["posted"], 1)
        self.assertIn("fleet-migrate-plan-files [dry-run]: ledgers: posted=1", out)

    def test_ledger_section_stops_at_next_top_level_heading(self):
        text = PLAN_HEAD.format(n=1) + LEDGER + TAIL
        self.assertEqual(migrate.section(text, "## Steward ledger"), LEDGER)
        self.assertIsNone(migrate.section(text, "## Amendments"))

    def test_paginated_comment_stream_is_flattened(self):
        pages = json.dumps([{"body": "a"}]) + "\n" + json.dumps([{"body": "b"}])
        self.assertEqual([c["body"] for page in migrate.parse_json_stream(pages) for c in page],
                         ["a", "b"])


if __name__ == "__main__":
    unittest.main()
