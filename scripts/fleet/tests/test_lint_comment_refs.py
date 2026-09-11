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


def lines(text, family):
    return [n for n, _ in lint.scan_text(text, family)]


class Tokenizer(unittest.TestCase):
    def test_slash_comments(self):
        self.assertEqual(lines("int x;  // #1234\n", "slash"), [1])
        self.assertEqual(lines("   /// #1234\n", "slash"), [1])
        self.assertEqual(lines("/* block\n   #1234 inside\n */ int y; // #5678\n", "slash"), [2, 3])
        self.assertEqual(lines("int x = 1234;\n#include \"a.hpp\" // #1234\n", "slash"), [2])

    def test_slash_strings_are_not_comments(self):
        self.assertEqual(lines('value = "#1234";\n', "slash"), [])
        self.assertEqual(lines('auto value = "// #1234";\n', "slash"), [])
        self.assertEqual(lines('auto s = "a\\"#1234"; int z;\n', "slash"), [])
        self.assertEqual(lines("char c = '\"'; // #999\n", "slash"), [1])
        self.assertEqual(lines("int a = 1'000'000; // #1234\n", "slash"), [1])

    def test_slash_raw_strings(self):
        self.assertEqual(lines('auto s = R"lua(-- #1234 // #4321)lua";\n', "slash"), [])
        self.assertEqual(lines('auto s = R"(#1234)"; // #2222\n', "slash"), [1])
        self.assertEqual(lines('auto s = u8R"x(\n#1234\n)x"; int z;\n', "slash"), [])

    def test_python(self):
        self.assertEqual(lines("x = 1  # #1234\n", "python"), [1])
        self.assertEqual(lines("#1234: deferred\n", "python"), [1])
        self.assertEqual(lines('value = "#1234"\n', "python"), [])
        self.assertEqual(lines("s = 'it\\'s #1234'\n", "python"), [])
        self.assertEqual(lines('"""doc\n#1234 in a docstring\n"""\nx = 1\n', "python"), [])
        self.assertEqual(lines("#!/usr/bin/env python3\nx = 1\n", "python"), [])

    def test_hash(self):
        self.assertEqual(lines('local value = "-- #1234"\n', "hash"), [])
        self.assertEqual(lines("echo '#1234' # #4321\n", "hash"), [1])
        self.assertEqual(lines('x="a\\"#1234"\n', "hash"), [])
        self.assertEqual(lines("echo 'a\\' # #1234\n", "hash"), [1],
                         "a shell single quote takes no escapes")
        self.assertEqual(lines("set(X 1234) # #1234\n", "hash"), [1])

    def test_dash(self):
        self.assertEqual(lines("local n = 1 -- #1234\n", "dash"), [1])
        self.assertEqual(lines("local n = 1234\n", "dash"), [])
        self.assertEqual(lines('local value = "-- #1234"\n', "dash"), [])
        self.assertEqual(lines("local s = [[\n#1234 long string\n]]\n", "dash"), [])
        self.assertEqual(lines("--[[ block\n#1234\n]] local q = 1\n", "dash"), [2])
        self.assertEqual(lines("--[==[ #1111 ]==] -- #2222\n", "dash"), [1])


class Families(unittest.TestCase):
    def test_every_supported_class_selects_a_family(self):
        expected = {
            "a.hpp": "slash", "a.cpp": "slash", "a.h": "slash", "a.cc": "slash",
            "a.c": "slash", "a.tpp": "slash", "a.inl": "slash",
            "engine/render/src/metal/metal_cocoa_bridge.mm": "slash",
            "a.glsl": "slash", "a.metal": "slash",
            "a.py": "python",
            "a.sh": "hash", "scripts/fleet/completions/fleet-run.bash": "hash",
            "scripts/fleet/completions/irreden-fleet.zsh": "hash",
            "a.ps1": "hash", "a.cmake": "hash", "x/CMakeLists.txt": "hash",
            "a.bzl": "hash", "BUILD.bazel": "hash",
            "a.lua": "dash",
        }
        for rel, family in expected.items():
            self.assertEqual(lint.comment_family(rel), family, rel)

    def test_untyped_and_skipped_paths(self):
        for rel in ("docs/x.md", "a.png", "a.json", "a.yml",
                    "engine/render/third_party/x.cpp", "third_party/x.py",
                    "build/x.cpp", "engine/render/include/irreden/render/gl_wrap/x.h",
                    ".gitignore"):
            self.assertIsNone(lint.comment_family(rel), rel)


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

    def seed(self, counts, path=None):
        (path or self.baseline).write_text(json.dumps(counts))

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = lint.main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    def test_extensionless_interpreter_files_are_scanned(self):
        self.add("engine/tools/bin/ir-build", "#!/usr/bin/env bash\nset -e # #1234\n")
        self.add("scripts/fleet/fleet-up", "#!/usr/bin/env python3\nx = '#1234'\n")
        self.assertEqual(lint.comment_family("engine/tools/bin/ir-build"), "hash")
        self.assertEqual(lint.comment_family("scripts/fleet/fleet-up"), "python")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("engine/tools/bin/ir-build:2:", out)
        self.assertNotIn("fleet-up", out)

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
        self.seed({"scripts/tool.py": 2})
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

        self.add("scripts/tool.py", "# #1111\nx = 1\n")
        rc, _, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {"scripts/tool.py": 1})

        self.add("scripts/tool.py", "x = 1\n")
        rc, out, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 0)
        self.assertEqual(json.loads(self.baseline.read_text()), {},
                         "a swept file leaves the baseline")

    def test_no_flag_can_raise_a_budget(self):
        self.add("scripts/tool.py", "# #1111\nx = 1\n")
        rc, _, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 1)
        self.assertFalse(self.baseline.exists() and json.loads(self.baseline.read_text()))
        with self.assertRaises(SystemExit):
            self.run_main("--update-baseline", "--allow-raise")

    def test_ci_checks_against_the_base_branch_baseline(self):
        self.add("scripts/tool.py", "# #1111\n# #2222\nx = 1\n")
        self.seed({"scripts/tool.py": 2}, self.baseline)
        base = self.root / "base-baseline.json"
        self.seed({"scripts/tool.py": 1}, base)
        rc, _, _ = self.run_main()
        self.assertEqual(rc, 0, "the committed baseline was raised to cover the new line")
        rc, out, _ = self.run_main("--baseline", str(base))
        self.assertEqual(rc, 1, "the base branch's budget still applies")
        self.assertIn("budget 1", out)

    def test_skipped_and_untyped_paths_are_ignored(self):
        self.add("engine/render/third_party/vendor.hpp", "// vendored #1234\n")
        self.add("docs/notes.md", "see #1234\n")
        self.add("engine/b.hpp", "// keeper\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0)
        self.assertIn("0 reference line(s)", out)


if __name__ == "__main__":
    unittest.main()
