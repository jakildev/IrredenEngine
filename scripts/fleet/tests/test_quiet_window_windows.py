#!/usr/bin/env python3
"""Native-Windows owner-token coverage for the quiet-window process model."""

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
HELPER_PATH = REPO_ROOT / "engine" / "tools" / "lib" / "quiet_window.py"


def load_helper(lock_root: Path):
    os.environ["IR_LOCK_ROOT"] = str(lock_root)
    spec = importlib.util.spec_from_file_location("quiet_window", HELPER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OwnerTokenTest(unittest.TestCase):
    def test_owner_token_selects_native_pid(self):
        with tempfile.TemporaryDirectory() as temp:
            helper = load_helper(Path(temp))
            self.assertEqual(helper.windows_native_pid(17, "42 msys-root"), 42)
            self.assertEqual(helper.windows_native_pid(17), 17)

    def test_liveness_uses_owner_token_on_windows(self):
        with tempfile.TemporaryDirectory() as temp:
            helper = load_helper(Path(temp))
            probed = []
            helper.windows_native_alive = lambda pid: probed.append(pid) or True
            with mock.patch.object(helper.os, "name", "nt"):
                self.assertTrue(helper.pid_alive(17, "42 msys-root"))
            self.assertEqual(probed, [42])


@unittest.skipUnless(os.name == "nt", "requires native Windows Python")
class WindowsOwnerTokenTest(unittest.TestCase):
    def test_msys_owner_token_drives_liveness_and_tree_root(self):
        msys_bash = Path(sys.executable).parents[2] / "usr" / "bin" / "bash.exe"
        if not msys_bash.is_file():
            self.skipTest("no MSYS2 bash beside the interpreter")
        command = "echo $$ $(cat /proc/$$/winpid); sleep 30; true"
        process = subprocess.Popen(
            [str(msys_bash), "-c", command], stdout=subprocess.PIPE, text=True
        )
        try:
            msys_pid, winpid = map(int, process.stdout.readline().split())
            with tempfile.TemporaryDirectory() as temp:
                helper = load_helper(Path(temp))
                token = f"{winpid} msys-root"
                self.assertEqual(helper.windows_native_pid(msys_pid, token), winpid)
                self.assertTrue(helper.pid_alive(msys_pid, token))
                snapshot, _ = helper.tree_snapshot(
                    helper.windows_native_pid(msys_pid, token), set()
                )
                self.assertIn(winpid, snapshot)
        finally:
            process.kill()
            process.wait(timeout=30)
        self.assertFalse(helper.pid_alive(msys_pid, token))


if __name__ == "__main__":
    if os.name != "nt":
        print("quiet_window_windows: skipped (requires native Windows Python)")
    unittest.main()
