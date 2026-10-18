"""No suite launches a bare `"bash"` argv.

On native Windows, `CreateProcess` searches System32 before PATH, and there
`bash.exe` is the WSL launcher: a bare `"bash"` first argv element runs the
child in WSL with none of the suite's hermetic environment. Suites resolve
bash with `shutil.which("bash")` instead.

The detector parses each suite and flags a process-launch call whose first
argument is a list literal starting with the string `"bash"`. A list that is
only data (a policy pattern handed to `json.dumps`) is not a launch.
"""
import ast
import sys
import unittest
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
LAUNCHERS = {"run", "Popen", "call", "check_call", "check_output"}


def bare_bash_launches(source):
    hits = []
    for node in ast.walk(ast.parse(source)):
        if not isinstance(node, ast.Call) or not node.args:
            continue
        func = node.func
        name = func.attr if isinstance(func, ast.Attribute) else getattr(func, "id", "")
        argv = node.args[0]
        if name in LAUNCHERS and isinstance(argv, ast.List) and argv.elts:
            first = argv.elts[0]
            if isinstance(first, ast.Constant) and first.value == "bash":
                hits.append(node.lineno)
    return hits


class BareBashArgv(unittest.TestCase):
    def test_positive_fixture_is_flagged(self):
        source = (
            "import subprocess\n"
            "subprocess.run(\n"
            "    ['bash', '-c', 'true'],\n"
            "    check=True)\n"
        )
        self.assertEqual(bare_bash_launches(source), [2])

    def test_resolved_bash_and_data_lists_are_not_flagged(self):
        source = (
            "import json, shutil, subprocess\n"
            "subprocess.run([shutil.which('bash') or 'bash', '-c', 'true'])\n"
            "json.dumps(['bash', 'scripts/perf/perf_grid_matrix.sh'])\n"
        )
        self.assertEqual(bare_bash_launches(source), [])

    def test_no_suite_launches_a_bare_bash_argv(self):
        suites = sorted(TESTS_DIR.glob("*.py"))
        self.assertGreater(len(suites), 0)
        offenders = []
        for path in suites:
            for line in bare_bash_launches(path.read_text(encoding="utf-8")):
                offenders.append(f"{path.name}:{line}")
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    sys.exit(unittest.main())
