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

`WorkflowKeepsRunnerStatusTest` reads render-harness-tests.yml itself: a step
that pipes the runner (`run_all.sh | tee`) must run with pipefail, or the
job goes green on any suite failure.
"""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))

import verify_common  # noqa: E402  (needs the sys.path insert above)

_RUN_ALL = Path(__file__).resolve().parent / "run_all.sh"
_WORKFLOW = (_SCRIPTS.parent / ".github" / "workflows"
             / "render-harness-tests.yml")

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
        [verify_common.resolve_bash(), str(_RUN_ALL), str(directory)],
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

    def test_pass_line_carries_the_test_count(self):
        with tempfile.TemporaryDirectory() as d:
            _write(Path(d), "test_a.py", _TRIVIAL_CASE)
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("PASS  test_a.py (1 tests)", r.stdout)

    def test_all_skipped_suite_passes_but_says_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            _write(Path(d), "test_skips.py",
                   "import unittest\n\n\n"
                   "class T(unittest.TestCase):\n"
                   "    @unittest.skip('fixture')\n"
                   "    def test_skipped(self):\n"
                   "        pass\n\n\n"
                   "if __name__ == '__main__':\n"
                   "    unittest.main()\n")
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("PASS  test_skips.py (1 tests, 1 skipped)", r.stdout)

    def test_suite_that_ran_no_tests_fails(self):
        with tempfile.TemporaryDirectory() as d:
            _write(Path(d), "test_good.py", _TRIVIAL_CASE)
            _write(Path(d), "test_noop.py", "print('no unittest.main() here')\n")
            r = _run_runner(Path(d))
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("FAIL  test_noop.py (no tests ran)", r.stdout)
            self.assertIn("test_noop.py", r.stderr)
            self.assertIn("1 passed, 1 failed", r.stdout)

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
    # The regression lock for per-process isolation.
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


# ----------------------------------------------------------------------
# The workflow must keep the runner's exit status.
# ----------------------------------------------------------------------

_STEP_START = re.compile(r"^(\s*)- ")
_PIPE = re.compile(r"(?<!\|)\|(?!\|)")


def _steps(workflow: str) -> list[str]:
    """Split a workflow's text into its `- ` list-item blocks (steps)."""
    blocks: list[list[str]] = []
    indent = None
    for line in workflow.splitlines():
        if line.lstrip().startswith("#"):
            continue
        m = _STEP_START.match(line)
        if m and (indent is None or len(m.group(1)) == indent) \
                and "name:" in line:
            indent = len(m.group(1))
            blocks.append([line])
        elif blocks:
            blocks[-1].append(line)
    return ["\n".join(b) for b in blocks]


def masked_runner_steps(workflow: str) -> list[str]:
    """Steps that pipe run_all.sh's output without pipefail.

    A bare `run:` executes under `bash -e {0}`; GitHub adds `-o pipefail`
    only when the step names `shell: bash`. Without either, a
    `run_all.sh | tee` step takes tee's status and every suite failure
    reads green.
    """
    masked = []
    for step in _steps(workflow):
        run = step.split("run:", 1)[1] if "run:" in step else ""
        if "run_all.sh" not in run or not _PIPE.search(run):
            continue
        if re.search(r"^\s*shell:\s*bash\s*$", step, re.M) \
                or "pipefail" in run:
            continue
        masked.append(step.splitlines()[0].strip())
    return masked


class WorkflowKeepsRunnerStatusTest(unittest.TestCase):

    def test_workflow_invokes_the_runner(self):
        # Non-vacuity: the check below passes trivially if no step runs it.
        self.assertIn("run_all.sh", _WORKFLOW.read_text(encoding="utf-8"))

    def test_piped_runner_step_keeps_its_exit_status(self):
        self.assertEqual(
            masked_runner_steps(_WORKFLOW.read_text(encoding="utf-8")), [])

    def test_mutation_dropping_shell_bash_is_caught(self):
        text = _WORKFLOW.read_text(encoding="utf-8")
        mutated = re.sub(r"^\s*shell:\s*bash\s*\n", "", text, flags=re.M)
        self.assertNotEqual(mutated, text, "mutation found nothing to drop")
        self.assertEqual(len(masked_runner_steps(mutated)), 1)

    def test_logical_or_is_not_a_pipe(self):
        step = ("    steps:\n"
                "      - name: Run\n"
                "        run: bash scripts/tests/run_all.sh || exit 1\n")
        self.assertEqual(masked_runner_steps(step), [])

    def test_explicit_pipefail_in_run_is_accepted(self):
        step = ("    steps:\n"
                "      - name: Run\n"
                "        run: set -o pipefail; bash scripts/tests/run_all.sh"
                " | tee x.log\n")
        self.assertEqual(masked_runner_steps(step), [])


if __name__ == "__main__":
    unittest.main()
