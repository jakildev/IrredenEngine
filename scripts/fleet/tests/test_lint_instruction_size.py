"""lint_instruction_size.py: class caps per path shape, the shrink-only
baseline, and the `--against` split into introduced and inherited offenders.
Hermetic: the scanner is pointed at a temp tree and a temp baseline; nothing
reads the real repository. The `--against` replays build a throwaway git repo,
since the base side is read from a ref's tree."""
import importlib.util
import io
import json
import subprocess
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

sys.path.insert(0, str(SUBJECT.parent))
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


class AgainstRef(unittest.TestCase):
    """`--against`: a real git repo, the base committed first and the head
    left in the working tree, as CI measures a merge commit against its first
    parent. The rows replay a measured incident: the base already carries
    three offenders, and the head grows one of them by 20 lines, so the
    offender set is identical on both sides."""

    FLEET = "docs/agents/FLEET.md"
    COMMAND = "engine/render/CLAUDE.md"
    SCRIPTS = "scripts/fleet/CLAUDE.md"
    BUDGETS = {FLEET: 464, COMMAND: 311, SCRIPTS: 450}
    BASE_ROWS = {FLEET: 465, COMMAND: 324, SCRIPTS: 456}

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.p = []
        self.baseline = self.root / "scripts" / lint.BASELINE.name
        for target, value in (("REPO", self.root), ("BASELINE", self.baseline)):
            patcher = patch.object(lint, target, value)
            patcher.start()
            self.p.append(patcher)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.com")
        self.git("config", "user.name", "t")

    def tearDown(self):
        for patcher in self.p:
            patcher.stop()
        self.tmp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def write(self, rows, budgets=None):
        for rel, n in rows.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("".join(f"line {i}\n" for i in range(n)))
        self.baseline.parent.mkdir(parents=True, exist_ok=True)
        self.baseline.write_text(json.dumps(budgets or self.BUDGETS))
        self.git("add", "-A")

    def commit_base(self, rows, budgets=None):
        self.write(rows, budgets)
        self.git("commit", "-qm", "base")
        return self.git("rev-parse", "HEAD")

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = lint.main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    @staticmethod
    def section(out, header):
        """The offender lines printed under `header`."""
        lines = out.splitlines()
        start = lines.index(header) + 1
        rest = lines[start:]
        end = next((i for i, line in enumerate(rest) if line.endswith(":")), len(rest))
        return rest[:end]

    def test_a_worsened_inherited_offender_is_introduced(self):
        base = self.commit_base(self.BASE_ROWS)
        self.write({**self.BASE_ROWS, self.FLEET: 485})
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        self.assertEqual(self.section(out, "introduced:"),
                         [f"{self.FLEET}: 485 lines, budget 464"])
        self.assertEqual(self.section(out, "inherited:"),
                         [f"{self.COMMAND}: 324 lines, budget 311",
                          f"{self.SCRIPTS}: 456 lines, budget 450"])
        self.assertIn("beyond what", err)

    def test_a_head_equal_to_its_base_introduces_nothing(self):
        base = self.commit_base(self.BASE_ROWS)
        self.write(self.BASE_ROWS)
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 0)
        self.assertEqual(self.section(out, "introduced:"), [])
        self.assertEqual(self.section(out, "inherited:"),
                         [f"{self.FLEET}: 465 lines, budget 464",
                          f"{self.COMMAND}: 324 lines, budget 311",
                          f"{self.SCRIPTS}: 456 lines, budget 450"])
        self.assertIn("3 inherited offender(s)", err)

    def test_without_the_flag_the_same_head_is_flat_red(self):
        self.commit_base(self.BASE_ROWS)
        self.write(self.BASE_ROWS)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(out.splitlines(),
                         [f"{self.FLEET}: 465 lines, budget 464",
                          f"{self.COMMAND}: 324 lines, budget 311",
                          f"{self.SCRIPTS}: 456 lines, budget 450"])

    def test_a_partial_trim_of_an_inherited_offender_is_inherited(self):
        base = self.commit_base({self.FLEET: 470})
        self.write({self.FLEET: 466})
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 0, "a fix-forward that has not finished is not blamed")
        self.assertEqual(self.section(out, "introduced:"), [])
        self.assertEqual(self.section(out, "inherited:"),
                         [f"{self.FLEET}: 466 lines, budget 464"])

    def test_a_new_offender_is_introduced(self):
        base = self.commit_base({self.FLEET: 10})
        self.write({self.FLEET: 10, ".claude/agents/review-new.md": 121})
        rc, out, _ = self.run_main("--against", base, "--warn-band", "0")
        self.assertEqual(rc, 1)
        self.assertEqual(self.section(out, "introduced:"),
                         [".claude/agents/review-new.md: 121 lines, budget 120"])

    def test_an_introduced_excess_within_the_band_warns_and_passes(self):
        base = self.commit_base({self.FLEET: 464})
        self.write({self.FLEET: 470})
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 0)
        self.assertEqual(self.section(out, "introduced:"),
                         [f"{self.FLEET}: 470 lines, budget 464"])
        self.assertIn(f"::warning file={self.FLEET}::{self.FLEET}: 470 lines, budget 464 "
                      "(+6, within the 10-line band)", out)
        self.assertIn("annotated, not failed", err)

    def test_an_introduced_excess_past_the_band_fails(self):
        base = self.commit_base({self.FLEET: 464})
        self.write({self.FLEET: 475})
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        self.assertIn(f"::error file={self.FLEET}::{self.FLEET}: 475 lines, budget 464 "
                      "(+11, past the 10-line band)", out)
        self.assertIn("more than 10 line(s) past its budget", err)

    def test_the_band_is_on_the_head_excess_not_this_change_s_delta(self):
        # An inherited +8 that grows by 3 crosses the wall at cap + band, so a
        # series of small additions cannot creep past it.
        base = self.commit_base({self.FLEET: 472})
        self.write({self.FLEET: 475})
        rc, _, _ = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        self.write({self.FLEET: 474})
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 0)
        self.assertIn("(+10, within the 10-line band)", out)

    def test_warn_band_zero_disables_the_band(self):
        base = self.commit_base({self.FLEET: 464})
        self.write({self.FLEET: 465})
        rc, out, _ = self.run_main("--against", base, "--warn-band", "0")
        self.assertEqual(rc, 1)
        self.assertNotIn("::warning", out)

    def test_a_flat_run_has_no_band(self):
        self.commit_base({self.FLEET: 464})
        self.write({self.FLEET: 465})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertNotIn("::warning", out)

    def test_a_budget_raise_is_printed_and_is_not_an_offender(self):
        base = self.commit_base({self.FLEET: 480}, {self.FLEET: 464})
        self.write({self.FLEET: 480}, {self.FLEET: 480})
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 0, "a raised budget is a reviewed hand edit, not a regression")
        self.assertIn(f"::notice file={self.FLEET}::budget raised: {self.FLEET}: 464 -> 480",
                      out)
        self.assertNotIn("introduced:", out)

    def test_the_base_side_reads_the_budget_committed_at_the_ref(self):
        base = self.commit_base({self.FLEET: 470}, {self.FLEET: 470})
        self.write({self.FLEET: 470}, {self.FLEET: 464})
        rc, out, _ = self.run_main("--against", base, "--warn-band", "0")
        self.assertEqual(rc, 1, "a budget cut in the head makes the file a new offender")
        self.assertEqual(self.section(out, "introduced:"),
                         [f"{self.FLEET}: 470 lines, budget 464"])

    def test_a_clean_head_prints_the_flat_ok_line(self):
        base = self.commit_base(self.BASE_ROWS)
        self.write({self.FLEET: 400, self.COMMAND: 300, self.SCRIPTS: 450})
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 0)
        self.assertTrue(out.startswith("ok: 3 instruction file(s)"), out)
        self.assertNotIn("introduced:", out)

    def test_an_unreadable_ref_exits_2(self):
        self.commit_base(self.BASE_ROWS)
        rc, _, err = self.run_main("--against", "no-such-ref")
        self.assertEqual(rc, 2)
        self.assertIn("cannot measure no-such-ref", err)

    def test_against_does_not_combine_with_update_baseline(self):
        with self.assertRaises(SystemExit):
            self.run_main("--against", "HEAD", "--update-baseline")


class Split(unittest.TestCase):
    """The classification rule alone, on excess over budget."""

    def test_rule(self):
        split = sys.modules["ratchet_against"].split
        head = {"new": (5, 0), "worse": (12, 10), "same": (12, 10), "trim": (11, 10),
                "cut": (10, 8), "under": (3, 10)}
        base = {"worse": (11, 10), "same": (12, 10), "trim": (13, 10), "cut": (10, 10),
                "under": (30, 10)}
        self.assertEqual(split(head, base), (["cut", "new", "worse"], ["same", "trim"]))


if __name__ == "__main__":
    unittest.main()
