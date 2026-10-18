"""No suite launches a bare `"bash"` argv.

On native Windows, `CreateProcess` searches System32 before PATH, and there
`bash.exe` is the WSL launcher: a bare `"bash"` first argv element runs the
child in WSL with none of the suite's hermetic environment. Suites resolve
bash with `shutil.which("bash")` instead.

The detector parses each suite and flags a process-launch call whose argv
(first positional or `args=`) starts with the string `"bash"` — directly, via
a name bound to `"bash"`, via a name bound to such a list, or as the left
operand of a `+` concatenation. Name bindings are tracked module-wide without
scoping, so a reused name can only over-report. A list that is only data (a
policy pattern handed to `json.dumps`) is not a launch.
"""
import ast
import sys
import unittest
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
LAUNCHERS = {"run", "Popen", "call", "check_call", "check_output"}


def _bindings(tree):
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    yield target.id, node.value
        elif isinstance(node, ast.AnnAssign) and node.value is not None:
            if isinstance(node.target, ast.Name):
                yield node.target.id, node.value


def bare_bash_launches(source):
    tree = ast.parse(source)
    bindings = list(_bindings(tree))
    bare_names = {
        name for name, value in bindings
        if isinstance(value, ast.Constant) and value.value == "bash"
    }
    argv_names = set()

    def is_bare(expr):
        if isinstance(expr, ast.Constant):
            return expr.value == "bash"
        return isinstance(expr, ast.Name) and expr.id in bare_names

    def is_bash_argv(expr):
        if isinstance(expr, (ast.List, ast.Tuple)):
            return bool(expr.elts) and is_bare(expr.elts[0])
        if isinstance(expr, ast.BinOp) and isinstance(expr.op, ast.Add):
            return is_bash_argv(expr.left)
        return isinstance(expr, ast.Name) and expr.id in argv_names

    # Fixpoint so `a = ["bash"]; b = a + ["-c"]` binds b regardless of walk order.
    changed = True
    while changed:
        changed = False
        for name, value in bindings:
            if name not in argv_names and is_bash_argv(value):
                argv_names.add(name)
                changed = True

    hits = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        name = func.attr if isinstance(func, ast.Attribute) else getattr(func, "id", "")
        if name not in LAUNCHERS:
            continue
        argv = node.args[0] if node.args else next(
            (kw.value for kw in node.keywords if kw.arg == "args"), None)
        if argv is not None and is_bash_argv(argv):
            hits.append(node.lineno)
    return sorted(hits)


class BareBashArgv(unittest.TestCase):
    def test_positive_fixture_is_flagged(self):
        source = (
            "import subprocess\n"
            "subprocess.run(\n"
            "    ['bash', '-c', 'true'],\n"
            "    check=True)\n"
        )
        self.assertEqual(bare_bash_launches(source), [2])

    def test_indirect_bare_argv_fixtures_are_flagged(self):
        source = (
            "import subprocess\n"
            "argv = ['bash', '-c', 'true']\n"
            "subprocess.run(argv)\n"
            "subprocess.Popen(args=('bash', '-c', 'true'))\n"
            "shell = 'bash'\n"
            "subprocess.call([shell, '-c', 'true'])\n"
            "base: list = ['bash']\n"
            "full = base + ['-c', 'true']\n"
            "subprocess.check_call(full)\n"
            "subprocess.check_output(['bash'] + ['-c', 'true'])\n"
        )
        self.assertEqual(bare_bash_launches(source), [3, 4, 6, 9, 10])

    def test_resolved_bash_and_data_lists_are_not_flagged(self):
        source = (
            "import json, shutil, subprocess\n"
            "subprocess.run([shutil.which('bash') or 'bash', '-c', 'true'])\n"
            "json.dumps(['bash', 'scripts/perf/perf_grid_matrix.sh'])\n"
            "pattern = ['bash', 'scripts/perf/perf_grid_matrix.sh']\n"
            "json.dumps(pattern)\n"
            "argv = [shutil.which('bash') or 'bash', '-c', 'true']\n"
            "subprocess.run(argv)\n"
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
