"""lint_instruction_size.py: class caps per path shape and the shrink-only
baseline. Hermetic: the scanner is pointed at a temp tree and a temp
baseline; nothing reads the real repository."""
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

SUBJECT = Path(__file__).resolve().parents[2] / "lint_instruction_size.py"
if not SUBJECT.is_file():
    print("SKIP: lint_instruction_size.py subject absent", file=sys.stderr)
    sys.exit(3)

_spec = importlib.util.spec_from_file_location("lint_instruction_size", SUBJECT)
lint = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lint)


class TempTree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.baseline = self.root / "baseline.json"
        self.files = {}
        self.p = []
        for target, value in (("REPO", self.root), ("BASELINE", self.baseline)):
            patcher = patch.object(lint, target, value)
            patcher.start()
            self.p.append(patcher)
        patcher = patch.object(lint, "tracked_files", lambda: list(self.files))
        patcher.start()
        self.p.append(patcher)

    def tearDown(self):
        for patcher in self.p:
            patcher.stop()
        self.tmp.cleanup()

    def add(self, rel, n_lines):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(f"line {i}\n" for i in range(n_lines)))
        self.files[rel] = True

    def link(self, rel, target):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)
        self.files[rel] = True

    def seed(self, budgets):
        self.baseline.write_text(json.dumps(budgets))

    def read_baseline(self):
        return json.loads(self.baseline.read_text())

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = lint.main(list(argv))
        return rc, out.getvalue(), err.getvalue()


class Classes(TempTree):
    def test_every_path_shape_selects_its_cap(self):
        expected = {
            "CLAUDE.md": 200,
            "engine/render/CLAUDE.md": 200,
            "creations/demos/shape_debug/CLAUDE.md": 200,
            "AGENTS.md": 400,
            ".claude/skills/simplify/SKILL.md": 500,
            ".claude/skills/simplify/checks/check-07-reference-comments.md": 400,
            ".claude/skills/optimize/reference/big_wins.md": 400,
            ".claude/rules/cpp-ecs.md": 200,
            ".claude/agents/review-acceptance.md": 120,
            ".claude/commands/role-worker.md": 300,
            "docs/agents/FLEET.md": 400,
            "docs/agents/skills/review-pr.md": 400,
        }
        for rel, cap in expected.items():
            self.assertEqual(lint.class_cap(rel), cap, rel)

    def test_paths_outside_the_population(self):
        for rel in ("README.md", "docs/design/skill-sharing.md", "engine/render/README.md",
                    ".claude/settings.json", ".claude/rules/README.md.bak",
                    ".claude/rules/nested/x.md", ".claude/commands/nested/x.md",
                    ".claude/agents/nested/x.md", ".claude/other/x.md",
                    "docs/agents/.archive/audit-claude-md.md", "docs/agents/state.json",
                    "engine/CLAUDE.md.orig"):
            self.assertIsNone(lint.instruction_class(rel), rel)

    def test_symlinks_are_skipped(self):
        self.add(".claude/commands/role-merger.md", 10)
        self.link(".claude/agents/role-merger.md", "../commands/role-merger.md")
        self.link(".claude/agents/role-retired.md", "../commands/role-retired.md")
        self.assertIsNone(lint.instruction_class(".claude/agents/role-merger.md"))
        self.assertIsNone(lint.instruction_class(".claude/agents/role-retired.md"),
                          "a dangling link is still a link")
        counts, _ = lint.scan_tree()
        self.assertEqual(list(counts), [".claude/commands/role-merger.md"],
                         "the target is counted once, at its own path")

    def test_archive_is_skipped(self):
        self.add("docs/agents/.archive/audit-claude-md.md", 900)
        self.add("docs/agents/FLEET.md", 10)
        counts, _ = lint.scan_tree()
        self.assertEqual(list(counts), ["docs/agents/FLEET.md"])

    def test_an_unterminated_last_line_counts(self):
        path = self.root / "engine/CLAUDE.md"
        path.parent.mkdir(parents=True)
        path.write_text("one\ntwo")
        self.assertEqual(lint.count_lines("engine/CLAUDE.md"), 2)


class Scanning(TempTree):
    def test_under_budget_passes(self):
        self.add("engine/CLAUDE.md", 150)
        self.add(".claude/skills/simplify/SKILL.md", 500)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, "a file at its cap is within budget")
        self.assertIn("none above its budget", out)

    def test_new_file_over_its_class_cap_fails_with_the_cap(self):
        self.add(".claude/agents/review-new.md", 121)
        rc, out, err = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn(".claude/agents/review-new.md: 121 lines, budget 120", out)
        self.assertIn("by hand", err)
        self.assertIn("lint_instruction_size_baseline.json", err)

    def test_over_budget_fails_and_names_only_the_offender(self):
        self.add("docs/agents/FLEET.md", 1000)
        self.add("engine/CLAUDE.md", 100)
        self.seed({"docs/agents/FLEET.md": 999})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("docs/agents/FLEET.md: 1000 lines, budget 999", out)
        self.assertNotIn("engine/CLAUDE.md", out)

    def test_recorded_budget_holds(self):
        self.add("docs/agents/FLEET.md", 1000)
        self.seed({"docs/agents/FLEET.md": 1000})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("1 above the class cap under a recorded budget", out)

    def test_update_baseline_lowers_refuses_to_raise_and_drops(self):
        self.add("engine/CLAUDE.md", 300)
        self.seed({"engine/CLAUDE.md": 300})
        rc, _, _ = self.run_main()
        self.assertEqual(rc, 0, "within budget passes")

        self.add("engine/CLAUDE.md", 301)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1, "one line past the recorded budget fails")
        self.assertIn("budget 300", out)
        rc, _, err = self.run_main("--update-baseline")
        self.assertEqual(rc, 1, "an update may not raise a budget")
        self.assertIn("refused to raise", err)
        self.assertIn("engine/CLAUDE.md: 300 -> 301", err)
        self.assertEqual(self.read_baseline(), {"engine/CLAUDE.md": 300})

        self.add("engine/CLAUDE.md", 250)
        rc, out, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertIn("1 file(s) above their class cap", out)
        self.assertEqual(self.read_baseline(), {"engine/CLAUDE.md": 250})

        self.add("engine/CLAUDE.md", 200)
        rc, _, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertEqual(self.read_baseline(), {},
                         "a file back at its cap leaves the baseline")
        rc, _, _ = self.run_main()
        self.assertEqual(rc, 0, "and passes on the cap alone")

    def test_no_flag_can_raise_a_budget(self):
        self.add("engine/CLAUDE.md", 201)
        rc, _, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 1)
        self.assertEqual(self.read_baseline(), {},
                         "a new over-cap file gets no entry from a refused update")
        with self.assertRaises(SystemExit):
            self.run_main("--update-baseline", "--allow-raise")

    def test_a_deleted_file_leaves_the_baseline(self):
        self.add("engine/CLAUDE.md", 10)
        self.seed({"engine/retired/CLAUDE.md": 500})
        rc, _, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertEqual(self.read_baseline(), {})

    def test_seeding_records_only_files_above_their_cap(self):
        self.add("engine/CLAUDE.md", 250)
        self.add("docs/agents/FLEET.md", 10)
        self.add(".claude/commands/role-worker.md", 300)
        lint.write_baseline(lint.scan_tree()[0])
        self.assertEqual(self.read_baseline(), {"engine/CLAUDE.md": 250})


if __name__ == "__main__":
    unittest.main()
