"""lint_gh_launches: fleet Python launches gh only through fleet_github.

Positive fixtures are temp files fed straight to `lint()`, so the real tree's
register stays out of them; the last case runs the real tree.
"""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SUBJECT = Path(__file__).resolve().parent.parent / "lint_gh_launches.py"
if not SUBJECT.is_file():
    print("SKIP: lint_gh_launches.py absent", file=sys.stderr)
    sys.exit(3)
_spec = importlib.util.spec_from_file_location("lint_gh_launches", SUBJECT)
lint_gh = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lint_gh)

FIXTURES = {
    "scripts/fleet/bare_run.py": 'import subprocess\nsubprocess.run(["gh", "pr", "view", "1"])\n',
    "scripts/fleet/multiline": (
        "#!/usr/bin/env python3\nimport subprocess\nsubprocess.run(\n    [\n"
        '        "gh", "issue", "view",\n    ],\n)\n'),
    "scripts/fleet/which_single": (
        "#!/usr/bin/env python3\nimport shutil\nGH = shutil.which('gh') or 'gh'\n"),
    "scripts/fleet/tuple_cmd.py": 'CMD = ("gh", "api", "x")\n',
    "scripts/fleet/embedded_heredoc": (
        "#!/usr/bin/env bash\nset -e\npython3 - <<'PY'\nimport subprocess\n"
        'subprocess.run(["gh", "issue", "edit", "1"])\nPY\necho done\n'),
    "scripts/fleet/embedded_dash_c": (
        "#!/usr/bin/env bash\nprintf x | python3 -c '\nimport subprocess, sys\n"
        'subprocess.run(["gh", "pr", "list"])\n' "'\n"),
    "scripts/fleet/broken_syntax": (
        "#!/usr/bin/env python3\ndef f(:\n    subprocess.run([\"gh\", \"pr\"])\n"),
}
CLEAN = {
    "scripts/fleet/shared.py": 'import fleet_github\nfleet_github.run(["pr", "view", "1"])\n',
    "scripts/fleet/git_only.py": 'import subprocess\nsubprocess.run(["git", "status"])\n',
    "scripts/fleet/mention.py": (
        'HELP = "run gh pr view to see it"\nNAMES = ["ghost", "gh-pages"]\n'),
    "scripts/fleet/tests/test_stub.py": 'subprocess.run(["gh", "pr"])\n',
    "scripts/fleet/fleet_github.py": 'import shutil\nshutil.which("gh")\n',
    "scripts/fleet/NOTES.md": '`subprocess.run(["gh", "pr"])`\n',
}


class Fixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for rel, text in {**FIXTURES, **CLEAN}.items():
            p = self.root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def run_lint(self, exceptions=None):
        return lint_gh.lint(self.root, sorted({**FIXTURES, **CLEAN}), exceptions or {})

    def test_every_bypass_shape_fires(self):
        findings, _ = self.run_lint()
        flagged = {f.split(":", 1)[0] for f in findings}
        self.assertEqual(flagged, set(FIXTURES))

    def test_lines_point_at_the_launch(self):
        findings, _ = self.run_lint()
        self.assertIn("scripts/fleet/bare_run.py:2:", "\n".join(findings))
        self.assertIn("scripts/fleet/embedded_heredoc:5:", "\n".join(findings))
        self.assertIn("scripts/fleet/multiline:4:", "\n".join(findings))

    def test_sanctioned_and_out_of_scope_files_are_quiet(self):
        findings, _ = self.run_lint()
        for rel in CLEAN:
            self.assertNotIn(rel + ":", "\n".join(findings))

    def test_register_suppresses_and_reports_stale_entries(self):
        findings, stale = self.run_lint({"scripts/fleet/tuple_cmd.py": "data",
                                         "scripts/fleet/git_only.py": "stale"})
        self.assertNotIn("scripts/fleet/tuple_cmd.py:", "\n".join(findings))
        self.assertEqual(stale, ["scripts/fleet/git_only.py"])


class Tree(unittest.TestCase):
    def test_the_tree_has_no_bypass_and_no_stale_entry(self):
        root = SUBJECT.parent.parent.parent
        findings, stale = lint_gh.lint(root, lint_gh.tracked_files(root))
        self.assertEqual(findings, [])
        self.assertEqual(stale, [])

    def test_every_register_entry_has_a_reason(self):
        for rel, reason in lint_gh.EXCEPTIONS.items():
            self.assertTrue(reason.strip(), rel)


if __name__ == "__main__":
    unittest.main()
