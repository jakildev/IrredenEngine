"""Unit + integration tests for lint_python_registry.py (#2859).

`extend-include`'s registry is asserted against a **derived** population, not
just spot-checked: every case here proves one half of the symmetric
comparison (unregistered population member vs. ghost registry row), so
drift in either direction fails loudly instead of being silently absorbed.

  - parse_extend_include: extracts the row set from ruff.toml prose
  - is_python_shebang: extension-less + python shebang, both conditions
    required; extension present, no shebang, and unreadable-file all clean
  - derive_population: filters a tracked-file list down to shebang files
  - diff_registry: population-vs-registry set diff, both directions
  - main(): end-to-end over a synthetic tree + ruff.toml (both directions,
    then a clean match)
  - the committed repo's own registry passes (post-#2859 acceptance)

stdlib-only; every fixture is written under a TemporaryDirectory (no
network, no repo mutation). The synthetic-tree tests build a throwaway git
repo via `git init` so `tracked_scripts_files` (a real `git ls-files` call)
is exercised end-to-end rather than stubbed.
"""
import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import lint_python_registry as lint

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent


def _run_main(root):
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return lint.main(["lint_python_registry.py", str(root)])


class ParseExtendInclude(unittest.TestCase):
    def test_extracts_rows_from_toml_prose(self):
        text = (
            '# comment\n'
            'extend-include = [\n'
            '    "scripts/fleet/fleet-issue",\n'
            '    "scripts/fleet/fleet-pr",\n'
            ']\n'
            '\n'
            'extend-exclude = ["scripts/fleet/__pycache__"]\n'
        )
        self.assertEqual(
            lint.parse_extend_include(text),
            {"scripts/fleet/fleet-issue", "scripts/fleet/fleet-pr"},
        )

    def test_missing_key_returns_empty_set(self):
        self.assertEqual(lint.parse_extend_include("target-version = \"py310\"\n"), set())


class IsPythonShebang(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path

    def test_extensionless_python_shebang_matches(self):
        path = self.write("fleet-thing", "#!/usr/bin/env python3\nprint('hi')\n")
        self.assertTrue(lint.is_python_shebang(path))

    def test_bare_python_shebang_matches(self):
        path = self.write("fleet-thing2", "#!/usr/bin/python\nprint('hi')\n")
        self.assertTrue(lint.is_python_shebang(path))

    def test_extension_present_is_not_matched_even_with_shebang(self):
        # ruff's own directory scan already covers .py files.
        path = self.write("thing.py", "#!/usr/bin/env python3\nprint('hi')\n")
        self.assertFalse(lint.is_python_shebang(path))

    def test_extensionless_bash_shebang_is_not_matched(self):
        path = self.write("fleet-bashy", "#!/usr/bin/env bash\necho hi\n")
        self.assertFalse(lint.is_python_shebang(path))

    def test_extensionless_non_shebang_is_not_matched(self):
        path = self.write("README", "not a script\n")
        self.assertFalse(lint.is_python_shebang(path))

    def test_missing_file_is_not_matched(self):
        self.assertFalse(lint.is_python_shebang(self.root / "does-not-exist"))


class DerivePopulation(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path

    def test_filters_relpaths_down_to_shebang_files(self):
        self.write("scripts/fleet/fleet-thing", "#!/usr/bin/env python3\n")
        self.write("scripts/fleet/fleet-bashy", "#!/usr/bin/env bash\n")
        self.write("scripts/fleet/thing.py", "#!/usr/bin/env python3\n")
        relpaths = [
            "scripts/fleet/fleet-thing",
            "scripts/fleet/fleet-bashy",
            "scripts/fleet/thing.py",
        ]
        self.assertEqual(
            lint.derive_population(self.root, relpaths),
            {"scripts/fleet/fleet-thing"},
        )


class DiffRegistry(unittest.TestCase):
    def test_matching_sets_yield_no_diffs(self):
        s = {"scripts/fleet/a", "scripts/fleet/b"}
        self.assertEqual(lint.diff_registry(s, s), ([], []))

    def test_unregistered_population_member_is_reported(self):
        population = {"scripts/fleet/a", "scripts/fleet/b"}
        registered = {"scripts/fleet/a"}
        self.assertEqual(
            lint.diff_registry(population, registered),
            (["scripts/fleet/b"], []),
        )

    def test_ghost_registry_row_is_reported(self):
        population = {"scripts/fleet/a"}
        registered = {"scripts/fleet/a", "scripts/fleet/stale"}
        self.assertEqual(
            lint.diff_registry(population, registered),
            ([], ["scripts/fleet/stale"]),
        )

    def test_both_directions_at_once(self):
        population = {"scripts/fleet/a", "scripts/fleet/new"}
        registered = {"scripts/fleet/a", "scripts/fleet/stale"}
        self.assertEqual(
            lint.diff_registry(population, registered),
            (["scripts/fleet/new"], ["scripts/fleet/stale"]),
        )


class MainEndToEnd(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path

    def _git_init_and_add(self):
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.email", "t@example.com"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.name", "t"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)

    def test_unregistered_file_makes_main_fail(self):
        self.write("scripts/fleet/fleet-registered", "#!/usr/bin/env python3\n")
        self.write("scripts/fleet/fleet-missing", "#!/usr/bin/env python3\n")
        self.write("ruff.toml",
                    'extend-include = [\n    "scripts/fleet/fleet-registered",\n]\n')
        self._git_init_and_add()
        self.assertEqual(_run_main(self.root), 1)

    def test_ghost_row_makes_main_fail(self):
        self.write("scripts/fleet/fleet-real", "#!/usr/bin/env python3\n")
        self.write("ruff.toml",
                    'extend-include = [\n'
                    '    "scripts/fleet/fleet-real",\n'
                    '    "scripts/fleet/fleet-deleted",\n'
                    ']\n')
        self._git_init_and_add()
        self.assertEqual(_run_main(self.root), 1)

    def test_matching_registry_is_clean(self):
        self.write("scripts/fleet/fleet-real", "#!/usr/bin/env python3\n")
        self.write("scripts/fleet/thing.py", "#!/usr/bin/env python3\n")
        self.write("ruff.toml",
                    'extend-include = [\n    "scripts/fleet/fleet-real",\n]\n')
        self._git_init_and_add()
        self.assertEqual(_run_main(self.root), 0)

    def test_untracked_file_is_not_counted_as_population(self):
        # Only git-tracked files count — an untracked scratch file must not
        # force a registry addition.
        self.write("scripts/fleet/fleet-real", "#!/usr/bin/env python3\n")
        self.write("ruff.toml",
                    'extend-include = [\n    "scripts/fleet/fleet-real",\n]\n')
        self._git_init_and_add()
        self.write("scripts/fleet/fleet-scratch", "#!/usr/bin/env python3\n")
        self.assertEqual(_run_main(self.root), 0)


class CommittedTree(unittest.TestCase):
    def test_committed_ruff_toml_registry_matches_population(self):
        # Acceptance for #2859: the registry and the derived population agree
        # on the real repo tree.
        self.assertEqual(_run_main(_REPO_ROOT), 0)


if __name__ == "__main__":
    unittest.main()
