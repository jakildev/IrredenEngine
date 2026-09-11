"""lint_comment_refs.py: comment detection per language family and the
shrink-only baseline. Hermetic: the scanner is pointed at a temp tree and a
temp baseline; nothing reads the real repository."""
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

SUBJECT = Path(__file__).resolve().parents[2] / "lint_comment_refs.py"
if not SUBJECT.is_file():
    print("SKIP: lint_comment_refs.py subject absent", file=sys.stderr)
    sys.exit(3)

_spec = importlib.util.spec_from_file_location("lint_comment_refs", SUBJECT)
lint = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lint)


class CommentStart(unittest.TestCase):
    def test_slash_family(self):
        self.assertEqual(lint.comment_start("int x;  // #1234", "slash"), 8)
        self.assertEqual(lint.comment_start("   /// #1234", "slash"), 3)
        self.assertEqual(lint.comment_start(" * inside a block #1234", "slash"), 1)
        self.assertEqual(lint.comment_start("int x = 1234;", "slash"), -1)

    def test_hash_family(self):
        self.assertEqual(lint.comment_start("x = 1  # #1234", "hash"), 7)
        self.assertEqual(lint.comment_start("#1234: deferred", "hash"), 0)
        self.assertEqual(lint.comment_start("#!/usr/bin/env python3", "hash"), -1)
        self.assertEqual(lint.comment_start("x = 1234", "hash"), -1)

    def test_dash_family(self):
        self.assertEqual(lint.comment_start("local n = 1 -- #1234", "dash"), 12)
        self.assertEqual(lint.comment_start("local n = 1234", "dash"), -1)


class Scanning(unittest.TestCase):
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

    def add(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        self.files[rel] = True

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = lint.main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    def test_reference_in_code_is_not_a_hit(self):
        self.add("engine/a.cpp", "int issue = 1234;\nconst char *s = \"#1234\";\n")
        self.assertEqual(lint.scan_file("engine/a.cpp", "slash"), [])

    def test_clean_tree_passes_with_no_baseline(self):
        self.add("engine/a.cpp", "int x = 1;  // a real contract note\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("none above its budget", out)

    def test_new_reference_fails_and_names_the_line(self):
        self.add("engine/a.cpp", "int x = 1;\nint y = 2;  // added for #1234\n")
        rc, out, err = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("engine/a.cpp:2:", out)
        self.assertIn("budget 0", out)
        self.assertIn("--update-baseline", err)

    def test_baseline_budget_holds_and_shrinks_only(self):
        self.add("scripts/tool.py", "# #1111\n# #2222\nx = 1\n")
        rc, _, _ = self.run_main("--update-baseline", "--allow-raise")
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {"scripts/tool.py": 2})
        rc, _, _ = self.run_main()
        self.assertEqual(rc, 0, "within budget passes")

        self.add("scripts/tool.py", "# #1111\n# #2222\n# #3333\nx = 1\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1, "a third reference exceeds the recorded budget")
        self.assertIn("budget 2", out)
        rc, _, err = self.run_main("--update-baseline")
        self.assertEqual(rc, 1, "an update may not raise a budget")
        self.assertIn("refused to raise", err)
        self.assertEqual(json.loads(self.baseline.read_text()), {"scripts/tool.py": 2})

        self.add("scripts/tool.py", "x = 1\n")
        rc, out, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {},
                         "a swept file leaves the baseline")

    def test_skipped_and_untyped_paths_are_ignored(self):
        self.add("engine/render/third_party/vendor.hpp", "// vendored #1234\n")
        self.add("docs/notes.md", "see #1234\n")
        self.add("engine/b.hpp", "// keeper\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("0 reference line(s)", out)


if __name__ == "__main__":
    unittest.main()
