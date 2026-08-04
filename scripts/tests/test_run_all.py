"""Tests for run_all.sh — the render-harness suite runner.

The runner's whole reason to exist is that it runs each suite in its own
interpreter. A shared-interpreter runner (`python3 -m unittest discover`)
lets one suite's `sys.path.insert` satisfy the next suite's import, so a
suite that forgot the line reports green (#2825).

`test_isolation_*` below is the regression lock for exactly that: a fixture
directory whose second suite imports a module only the first suite puts on
the path. The runner must report it FAILED; the positive control asserts the
same fixture passes under a shared-interpreter discover, which is what proves
the fixture reproduces the masking rather than being trivially broken.

Fixtures are synthesized in a temp directory — nothing here touches the real
suites.
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))

_RUN_ALL = Path(__file__).resolve().parent / "run_all.sh"

_TRIVIAL_CASE = """
import unittest


class T(unittest.TestCase):
    def test_ok(self):
        self.assertTrue(True)


if __name__ == "__main__":
    unittest.main()
"""


def _run_runner(directory: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["bash", str(_RUN_ALL), str(directory)],
        capture_output=True, text=True)


def _write(directory: Path, name: str, body: str) -> None:
    (directory / name).write_text(body)


class RunAllRunnerTest(unittest.TestCase):

    def test_all_passing_exits_zero(self):
        with tempfile.TemporaryDirectory() as d:
            _write(Path(d), "test_a.py", _TRIVIAL_CASE)
            _write(Path(d), "test_b.py", _TRIVIAL_CASE)
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("2 suite(s) — 2 passed, 0 failed", r.stdout)

    def test_failing_suite_exits_nonzero_and_is_named(self):
        with tempfile.TemporaryDirectory() as d:
            _write(Path(d), "test_good.py", _TRIVIAL_CASE)
            _write(Path(d), "test_bad.py", "import sys\nsys.exit(3)\n")
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 1)
            self.assertIn("FAIL  test_bad.py (exit 3)", r.stdout)
            self.assertIn("test_bad.py", r.stderr)
            # The passing sibling must still be reported as passing — a runner
            # that aborts on first failure hides the rest of the tally.
            self.assertIn("PASS  test_good.py", r.stdout)

    def test_empty_directory_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as d:
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 1)
            self.assertIn("no test_*.py suites found", r.stderr)

    def test_missing_directory_exits_nonzero(self):
        r = _run_runner(Path("/nonexistent-dir-for-run-all-test"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("not a directory", r.stderr)

    # ------------------------------------------------------------------
    # The #2825 regression lock: per-process isolation.
    # ------------------------------------------------------------------

    def _isolation_fixture(self, d: Path) -> None:
        """A dir whose 2nd suite imports a module only the 1st puts on path."""
        (d / "lib").mkdir()
        _write(d / "lib", "fixture_mod.py", "VALUE = 1\n")
        # Alphabetically first: inserts lib/ on sys.path, then imports.
        _write(d, "test_aaa_inserter.py",
               "import sys\n"
               "from pathlib import Path\n"
               "sys.path.insert(0, str(Path(__file__).resolve().parent / 'lib'))\n"
               "import fixture_mod\n"
               + _TRIVIAL_CASE)
        # Alphabetically second: bare import, no insert of its own.
        _write(d, "test_zzz_dependent.py",
               "import fixture_mod\n" + _TRIVIAL_CASE)

    def test_isolation_runner_reports_the_dependent_suite_failed(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            self._isolation_fixture(d)
            r = _run_runner(d)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("PASS  test_aaa_inserter.py", r.stdout)
            self.assertIn("FAIL  test_zzz_dependent.py", r.stdout)
            self.assertIn("ModuleNotFoundError", r.stdout)

    def test_isolation_positive_control_shared_interpreter_hides_it(self):
        """Same fixture, one interpreter — green. This is the masking."""
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            self._isolation_fixture(d)
            r = subprocess.run(
                [sys.executable, "-m", "unittest", "discover",
                 "-s", str(d), "-t", str(d)],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("OK", r.stderr)


if __name__ == "__main__":
    unittest.main()
