"""fleet-state-scout spawns its sibling scripts through the interpreter their
shebang names.

On native Windows an extensionless script cannot be exec'd, so the scout
wraps it. A python script handed to bash is read as shell: its docstring
runs as commands, and a backtick-quoted name in that prose (`fleet-state-scout`
in fleet-stalled-sweep's) is a command substitution that launches a second
scout every hour. The wrapper must pick this interpreter for a python
shebang and bash for everything else; off Windows the script runs itself.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
if not _SCRIPT.is_file():
    print("SKIP: fleet-state-scout subject absent", file=sys.stderr)
    sys.exit(3)
_SWEEP = Path(__file__).parent.parent / "fleet-stalled-sweep"

os.environ.setdefault("PYTHONUTF8", "1")
from test_scout_degraded_fetch import _mod  # noqa: E402

argv_for = _mod._fleet_script_argv


class ScriptArgv(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.python_script = root / "py-tool"
        self.python_script.write_text(
            '#!/usr/bin/env python3\n"""`fleet-state-scout` spawns it."""\n')
        self.bash_script = root / "sh-tool"
        self.bash_script.write_text("#!/usr/bin/env bash\necho hi\n")
        self.bare = root / "bare-tool"
        self.bare.write_text("echo no shebang\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_windows_runs_a_python_script_with_this_interpreter(self):
        argv = argv_for([str(self.python_script), "--repo", "x"], platform="win32")
        self.assertEqual(argv[0], sys.executable)
        self.assertEqual(argv[1], str(self.python_script).replace("\\", "/"))
        self.assertEqual(argv[2:], ["--repo", "x"])

    def test_windows_runs_a_bash_script_with_bash(self):
        argv = argv_for([str(self.bash_script)], platform="win32")
        self.assertNotEqual(argv[0], sys.executable)
        self.assertTrue(Path(argv[0]).name.startswith("bash"), argv)
        self.assertEqual(argv[1], str(self.bash_script).replace("\\", "/"))

    def test_windows_runs_a_shebangless_file_with_bash(self):
        argv = argv_for([str(self.bare)], platform="win32")
        self.assertTrue(Path(argv[0]).name.startswith("bash"), argv)

    def test_other_platforms_run_the_script_itself(self):
        for script in (self.python_script, self.bash_script):
            self.assertEqual(argv_for([str(script), "a"], platform="linux"), [str(script), "a"])

    def test_the_hourly_sweep_is_never_handed_to_bash_on_windows(self):
        if not _SWEEP.is_file():
            self.skipTest("fleet-stalled-sweep absent")
        argv = argv_for(["fleet-stalled-sweep", "--repo", "jakildev/x"], platform="win32")
        self.assertEqual(argv[0], sys.executable)
        self.assertTrue(argv[1].endswith("/fleet-stalled-sweep"), argv)

    def test_a_bare_sibling_name_resolves_beside_the_scout(self):
        argv = argv_for(["fleet-gh-token"], platform="linux")
        self.assertEqual(Path(argv[0]).parent, _SCRIPT.parent)


if __name__ == "__main__":
    unittest.main()
