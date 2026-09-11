"""lint_comment_refs.py: comment detection per language family and the
shrink-only baseline. Hermetic: the scanner is pointed at a temp tree and a
temp baseline; nothing reads the real repository."""
import importlib.util
import io
import json
import re
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

    def test_shell(self):
        self.assertEqual(lines("# see #1234\n", "shell"), [1])
        self.assertEqual(lines("echo hi  # #1234\n", "shell"), [1])
        self.assertEqual(lines("echo '#1234' # #4321\n", "shell"), [1])
        self.assertEqual(lines('x="a\\"#1234"\n', "shell"), [])
        self.assertEqual(lines("echo 'a\\' # #1234\n", "shell"), [1],
                         "a shell single quote takes no escapes")
        self.assertEqual(lines("x=$'a\\'b' # #1234\n", "shell"), [1],
                         "`$'...'` is the one single-quoted form that does")
        self.assertEqual(lines("url=https://h/pull#1234\n", "shell"), [],
                         "a `#` mid-word opens no comment")
        self.assertEqual(lines("echo \\#1234\n", "shell"), [])
        self.assertEqual(lines("x='\n#1234\n'\necho\n", "shell"), [],
                         "a shell quoted string spans lines")

    def test_shell_here_documents_are_not_comments(self):
        self.assertEqual(lines("cat <<'EOF'\n#1234\nEOF\n", "shell"), [])
        self.assertEqual(lines("cat <<EOF\n#1234\nEOF\necho done\n", "shell"), [])
        self.assertEqual(lines('cat <<"EOF"\n#1234\nEOF\n', "shell"), [])
        self.assertEqual(lines("\tcat <<-EOF\n\t#1234\n\tEOF\n", "shell"), [])
        self.assertEqual(lines("cmd <<A <<B\n#1111\nA\n#2222\nB\n", "shell"), [],
                         "two documents open on one line, bodies in order")
        self.assertEqual(lines('grep x <<< "$v" # #1234\n', "shell"), [1],
                         "`<<<` is a here-string, not a here-document")
        self.assertEqual(lines("n=$(( 1 << 3 ))\n# #1234\n3\n", "shell"), [2],
                         "an arithmetic left shift opens no here-document, so "
                         "it cannot swallow the lines after it")

    def test_shell_embedded_source_is_still_scanned(self):
        self.assertEqual(lines("python3 <<'PY'\n# see #1234\nPY\n", "shell"), [2])
        self.assertEqual(lines("python3 <<'PY'\nx = \"#1234\"\nPY\n", "shell"), [])
        self.assertEqual(lines("A=1 \\\n  python3 <<'PY'\n# #1234\nPY\n", "shell"), [3],
                         "the command word may sit on a continued line")
        self.assertEqual(
            lines("cat > s <<'EOF'\n#!/usr/bin/env bash\n# see #1234\nEOF\n", "shell"),
            [3], "a document whose body is a script declares itself by shebang")
        self.assertEqual(
            lines("cat > s.bat <<'EOF'\n@echo off\nrem #1234\nEOF\n", "shell"), [])
        self.assertEqual(lines("python3 -c '\n# see #1234\n'\n", "shell"), [2])
        self.assertEqual(lines("python3 -c '\nx = \"#1234\"\n'\n", "shell"), [])
        self.assertEqual(lines("python3 -c $'\n# see #1234\n'\n", "shell"), [2],
                         "bash and zsh quote an escaped program as `$'...'`")
        self.assertEqual(lines("python3 -c$'\n# see #1234\n'\n", "shell"), [2],
                         "the space before the program argument is optional")
        self.assertEqual(lines('python3 -c $"\n# see #1234\n"\n', "shell"), [2],
                         '`$"..."` is bash\'s other quoting prefix, and it '
                         "opens the program argument just the same")
        self.assertEqual(lines('sudo -u root python3 -c $"\n# see #1234\n"\n',
                               "shell"), [2],
                         "the prefix spelling survives an option-bearing "
                         "command prefix")
        self.assertEqual(lines("python3 script.py $'#1234'\n", "shell"), [],
                         "only the `-c` argument is source; an interpreter's "
                         "other `$'...'` arguments stay values")
        self.assertEqual(lines('python3 script.py $"#1234"\n', "shell"), [],
                         'the same boundary holds for `$"..."`')
        self.assertEqual(lines("msg='release #1234'\n", "shell"), [])
        self.assertEqual(lines("grep -c '#1234' f\n", "shell"), [],
                         "`-c` on a non-interpreter is not a program string")

    def test_cmake(self):
        self.assertEqual(lines("set(X 1234) # #1234\n", "cmake"), [1])
        self.assertEqual(lines("set(message [=[\n#1234\n]=])\n", "cmake"), [],
                         "a bracket argument is a value, not a comment")
        self.assertEqual(lines('set(X "\n#1234\n")\n', "cmake"), [],
                         "a quoted argument spans lines")
        self.assertEqual(lines("#[[\nsee #1234 here\n]]\nset(X 1)\n", "cmake"), [2],
                         "a bracket comment is a comment for every line it spans")
        self.assertEqual(lines('#[[ note ]] set(X "#1234")\n', "cmake"), [],
                         "a bracket comment ends where its bracket does, so the "
                         "code after it on the same line is still code")

    def test_powershell(self):
        self.assertEqual(lines("$x = 1 # #1234\n", "powershell"), [1])
        self.assertEqual(lines("<#\nsee #1234 here\n#>\n$x = 1\n", "powershell"), [2],
                         "a block comment is a comment for every line it spans")
        self.assertEqual(lines('<# note #> $x = "#1234"\n', "powershell"), [],
                         "a block comment ends where `#>` does, so the code "
                         "after it on the same line is still code")
        self.assertEqual(lines('$t = @"\nhe said "#1234" here\n"@\n$x = 1\n',
                                "powershell"), [],
                         "a here-string is a value, not a comment, and an "
                         "unpaired quote inside it does not end it")
        self.assertEqual(lines("$t = @'\nit's #1234 here\n'@\n$x = 1\n",
                               "powershell"), [])

    def test_starlark_reads_as_python(self):
        self.assertEqual(lines('"""doc\n#1234\n"""\nx = 1\n', "python"), [])
        self.assertEqual(lines("# #1234\nx = 1\n", "python"), [1])

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
            "a.sh": "shell", "scripts/fleet/completions/fleet-run.bash": "shell",
            "scripts/fleet/completions/irreden-fleet.zsh": "shell",
            "a.ps1": "powershell", "a.cmake": "cmake", "x/CMakeLists.txt": "cmake",
            "a.bzl": "python", "BUILD.bazel": "python",
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
        self.assertEqual(lint.comment_family("engine/tools/bin/ir-build"), "shell")
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


class CommandWord(unittest.TestCase):
    """Only the command word decides whether a `-c` string or a here-document
    is interpreter source; interpreter-shaped arguments to another command
    are data."""

    def test_interpreter_named_as_an_argument_is_data(self):
        for line in ("echo python3 -c '# see #1234'\n",
                     "printf '%s' python3 -c '# #1234'\n",
                     "command echo python3 -c '# #1234'\n",
                     "echo bash <<'EOF'\n# #1234\nEOF\n",
                     "grep python3 -c '#1234' f\n"):
            self.assertEqual(lines(line, "shell"), [], line)

    def test_interpreter_as_the_command_word_is_source(self):
        for line in ("python3 -c '# #1234'\n",
                     "command python3 -c '# #1234'\n",
                     "/usr/bin/env python3 -c '# #1234'\n",
                     "FOO=1 python3 -u -c '# #1234'\n",
                     "x=1; python3 -c '# #1234'\n",
                     "echo hi | python3 -c '# #1234'\n",
                     "timeout 30 python3 -c '# #1234'\n",
                     "if true; then python3 -c '# #1234'; fi\n",
                     "sudo bash <<'EOF'\n# #1234\nEOF\n"):
            self.assertEqual(lines(line, "shell"), [1] if "EOF" not in line else [2], line)

    def test_prefix_option_operand_is_not_the_command_word(self):
        """A prefix option's separate operand reads as a command word unless it
        is consumed with its option, hiding the interpreter behind it."""
        for line in ("sudo -u root python3 -c '# #1234'\n",
                     "env -u FLEET_ROLE python3 -c '# #1234'\n",
                     "timeout -s KILL 30 python3 -c '# #1234'\n",
                     "xargs -E END python3 -c '# #1234'\n",
                     "sudo -u root bash <<'EOF'\n# #1234\nEOF\n"):
            self.assertEqual(lines(line, "shell"), [1] if "EOF" not in line else [2], line)

    def test_an_option_without_an_operand_consumes_nothing(self):
        """`-E` takes no operand and `--user=root` carries its own, so neither
        may swallow the interpreter that follows it."""
        for line in ("sudo -E python3 -c '# #1234'\n",
                     "sudo --user=root python3 -c '# #1234'\n"):
            self.assertEqual(lines(line, "shell"), [1], line)

    def test_the_word_after_a_consumed_operand_still_decides(self):
        """Consuming `-u root` reaches the command word, it does not assume the
        next interpreter-shaped word anywhere on the line is one."""
        self.assertEqual(lines("sudo -u root echo python3 -c '# #1234'\n", "shell"), [])


class Check07Scope(unittest.TestCase):
    """The executed ratchet and `simplify` Check 7 are the two halves of one
    rule, so they must cover the same population. Not hermetic by design: the
    drift this pins is between the real checker and the real check doc."""

    CHECK = (Path(__file__).resolve().parents[3]
             / ".claude/skills/simplify/checks/check-07-reference-comments.md")
    SKILL = (Path(__file__).resolve().parents[3]
             / ".claude/skills/simplify/SKILL.md")

    def setUp(self):
        if not self.CHECK.is_file():
            self.skipTest("check-07 doc absent")
        self.doc = self.CHECK.read_text()
        self.globs = re.findall(r"glob:\s*'([^']+)'", self.doc)
        self.assertTrue(self.globs, "the doc names no Grep glob")

    def test_every_family_extension_is_inside_the_check_glob(self):
        listed = set()
        for g in self.globs:
            m = re.search(r"\{([^}]*)\}", g)
            if m:
                listed.update("." + e.strip() for e in m.group(1).split(","))
        wanted = (lint.SLASH_COMMENT_EXTS | lint.PYTHON_EXTS
                  | lint.SHELL_COMMENT_EXTS | lint.POWERSHELL_COMMENT_EXTS
                  | lint.CMAKE_COMMENT_EXTS | lint.DASH_COMMENT_EXTS
                  | {Path(n).suffix for n in lint.CMAKE_COMMENT_NAMES})
        self.assertEqual(sorted(wanted - listed), [],
                         "a class the ratchet counts that Check 7 never greps")

    def test_the_glob_carries_a_marker_for_every_comment_syntax(self):
        pattern = re.search(r"pattern:\s*'([^']+)'", self.doc).group(1)
        for ext, marker in ((".lua", "--"), (".py", "#"), (".hpp", "//")):
            self.assertIn(marker, pattern,
                          f"{ext} is in the glob but its comment marker is not "
                          "in the pattern, so those files are swept blind")

    def test_extensionless_executables_have_a_glob_arm(self):
        arms = [g for g in self.globs if "{" not in g]
        uncovered = sorted(
            p for p in lint.tracked_files()
            if Path(p).suffix == "" and lint.comment_family(p)
            and not any(p.startswith(a.rstrip("*")) for a in arms))
        self.assertEqual(uncovered, [],
                         f"extensionless interpreter files outside {arms}")

    def test_the_skill_index_row_defers_to_the_checker(self):
        if not self.SKILL.is_file():
            self.skipTest("SKILL.md absent")
        row = next(ln for ln in self.SKILL.read_text().splitlines()
                   if "check-07-reference-comments.md" in ln)
        self.assertIn("lint_comment_refs.py", row,
                      "the §2b trigger row re-enumerates the population "
                      "instead of deferring to the checker that defines it")


if __name__ == "__main__":
    unittest.main()
