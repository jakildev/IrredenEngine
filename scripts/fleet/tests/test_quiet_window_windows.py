#!/usr/bin/env python3
"""Windows coverage for the quiet-window process model.

The owner-token, MSYS process-map, CPU-time and reaped-child cases run on
every host; the classes gated on native Windows drive real MSYS processes.
"""

import importlib.util
import io
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


# The issue's probe, as Win32 creator links: the parker python (700) was
# started by a live shell (600) whose creator (550) is an exited exec stub,
# so the walk dies before the measurement root shell (100).
ROOT, MATRIX, OUTSIDER, HOLDER = 100, 900, 950, 50
WIN32_PARENTS = {
    700: 600,
    600: 500,
    500: 550,
    ROOT: HOLDER,
    MATRIX: 560,
    OUTSIDER: HOLDER,
    HOLDER: 1,
}
# The same runtime's `ps`: MSYS pid, MSYS ppid, PGID, WINPID. The exec'd
# shell (MSYS 21, Windows 500) and the matrix (MSYS 30, Windows 900) both
# hang under the root shell (MSYS 20, Windows 100).
MSYS_PS = """\
      PID    PPID    PGID     WINPID   TTY         UID    STIME COMMAND
       20       1      20        100  ?         197609 15:22:37 /usr/bin/bash
       21      20      20        500  ?         197609 15:22:37 /usr/bin/bash
       22      21      20        700  ?         197609 15:22:37 /mingw64/bin/python3
I      30      20      20        900  pty1      197609 15:22:37 /usr/bin/bash
"""
RUNTIME = Path("C:/Program Files/Git/")


def patch_windows(helper, ps_text=MSYS_PS):
    calls = []

    def fake_run(argv, **kwargs):
        calls.append(argv)
        return subprocess.CompletedProcess(argv, 0, stdout=ps_text, stderr="")

    stack = [
        mock.patch.object(helper.platform, "system", return_value="Windows"),
        mock.patch.object(helper, "win32_parents", return_value=dict(WIN32_PARENTS)),
        mock.patch.object(helper.subprocess, "run", side_effect=fake_run),
    ]
    return stack, calls


class MsysProcessMapTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.helper = load_helper(Path(temp.name))
        patches, self.ps_calls = patch_windows(self.helper)
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def resolve(self, root):
        stderr = io.StringIO()
        with mock.patch.object(self.helper.os, "getppid", return_value=600), \
                mock.patch.object(self.helper.sys, "stderr", stderr):
            resolved = self.helper.resolve_measurement_root(root, (RUNTIME,))
        return resolved, stderr.getvalue()

    def test_root_reached_only_through_msys_parentage_is_accepted(self):
        resolved, warning = self.resolve(ROOT)
        self.assertEqual(resolved, ROOT)
        self.assertEqual(warning, "")

    def test_sibling_root_is_rejected_with_the_existing_message(self):
        resolved, warning = self.resolve(OUTSIDER)
        self.assertEqual(resolved, 0)
        self.assertIn(f"ignoring non-ancestor IR_QUIET_MEASURE_ROOT={OUTSIDER}", warning)

    def test_win32_links_alone_do_not_reach_the_root(self):
        with mock.patch.object(self.helper.os, "getppid", return_value=600), \
                mock.patch.object(self.helper.sys, "stderr", io.StringIO()):
            self.assertEqual(self.helper.resolve_measurement_root(ROOT, ()), 0)

    def test_measurement_subtree_is_excluded_from_the_sampled_members(self):
        with mock.patch.object(self.helper, "cpu_time", return_value=1.0):
            whole, _ = self.helper.tree_snapshot(HOLDER, set(), (RUNTIME,))
            members, _ = self.helper.tree_snapshot(HOLDER, {ROOT}, (RUNTIME,))
        self.assertIn(MATRIX, whole)
        self.assertIn(700, whole)
        self.assertEqual(set(members), {HOLDER, OUTSIDER})

    def test_runtime_root_with_a_space_is_one_argv_element(self):
        self.helper.process_parents((RUNTIME,))
        self.assertEqual(self.ps_calls, [[str(RUNTIME / "usr" / "bin" / "ps.exe")]])

    def test_status_letter_rows_parse_like_plain_rows(self):
        plain = MSYS_PS.replace("I      30", "       30")
        self.assertEqual(self.helper.parse_msys_ps(MSYS_PS), self.helper.parse_msys_ps(plain))
        self.assertEqual(self.helper.parse_msys_ps(MSYS_PS)[30], (20, MATRIX))

    def test_unreadable_runtime_table_fails_closed(self):
        for outcome in (
            FileNotFoundError("ps.exe"),
            subprocess.TimeoutExpired("ps.exe", 5),
            subprocess.CalledProcessError(1, "ps.exe"),
        ):
            with self.subTest(outcome=type(outcome).__name__), \
                    mock.patch.object(self.helper.subprocess, "run", side_effect=outcome):
                with self.assertRaises((OSError, subprocess.SubprocessError)):
                    self.helper.process_parents((RUNTIME,))
                self.assertEqual(self.resolve(ROOT)[0], 0)


class FakeKernel32:
    def __init__(self, openable, readable=None):
        self.openable = openable
        self.readable = openable if readable is None else readable
        self.closed = []

    def OpenProcess(self, access, inherit, pid):
        return 1000 + pid if pid in self.openable else 0

    def GetProcessTimes(self, handle, *times):
        if handle - 1000 not in self.readable:
            return 0
        times[2]._obj.value = 30_000_000
        times[3]._obj.value = 20_000_000
        return 1

    def CloseHandle(self, handle):
        self.closed.append(handle)
        return 1


class WindowsCpuTimeTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.helper = load_helper(Path(temp.name))
        patch = mock.patch.object(self.helper.platform, "system", return_value="Windows")
        patch.start()
        self.addCleanup(patch.stop)

    def test_cpu_time_reads_user_and_kernel_time(self):
        fake = FakeKernel32({HOLDER})
        with mock.patch.object(self.helper, "kernel32", return_value=fake):
            self.assertAlmostEqual(self.helper.cpu_time(HOLDER), 5.0)
        self.assertEqual(fake.closed, [1000 + HOLDER])

    def snapshot(self, fake):
        with mock.patch.object(self.helper, "kernel32", return_value=fake), \
                mock.patch.object(
                    self.helper, "process_parents", return_value={OUTSIDER: HOLDER}
                ):
            return self.helper.tree_snapshot(HOLDER, set())[0]

    def test_member_that_cannot_be_opened_or_read_is_skipped(self):
        unopenable = FakeKernel32({HOLDER})
        unreadable = FakeKernel32({HOLDER, OUTSIDER}, readable={HOLDER})
        for name, fake in (("open", unopenable), ("read", unreadable)):
            with self.subTest(failure=name):
                self.assertEqual(set(self.snapshot(fake)), {HOLDER})
        self.assertIn(1000 + OUTSIDER, unreadable.closed)

    def test_missing_holder_still_raises(self):
        with self.assertRaises(ProcessLookupError):
            self.snapshot(FakeKernel32({OUTSIDER}))


class ReapedChildTest(unittest.TestCase):
    def sample(self, system):
        with tempfile.TemporaryDirectory() as temp:
            helper = load_helper(Path(temp))
            snapshots = iter(
                [({HOLDER: 1.0, 60: 4.0}, {60: HOLDER}), ({HOLDER: 3.0}, {})]
            )
            with mock.patch.object(helper.platform, "system", return_value=system), \
                    mock.patch.object(helper, "tree_snapshot",
                                      side_effect=lambda *args: next(snapshots)), \
                    mock.patch.object(helper.time, "sleep"), \
                    mock.patch.object(helper.time, "time", side_effect=[0.0, 1.0]):
                return helper.sample_cores(HOLDER, set(), 1.0)[0]

    def test_reaped_child_is_subtracted_where_parents_absorb_it(self):
        for system in ("Linux", "Darwin"):
            with self.subTest(system=system):
                self.assertEqual(self.sample(system), 0.0)

    def test_reaped_child_is_not_subtracted_on_windows(self):
        self.assertEqual(self.sample("Windows"), 2.0)


@unittest.skipUnless(os.name == "nt", "requires native Windows Python")
class WindowsOwnerTokenTest(unittest.TestCase):
    def test_msys_owner_token_drives_liveness_and_tree_root(self):
        msys_bash = Path(sys.executable).parents[2] / "usr" / "bin" / "bash.exe"
        if not msys_bash.is_file():
            self.skipTest("no MSYS2 bash beside the interpreter")
        # The builtin read: a non-login shell's PATH can name another runtime's
        # cat, which cannot see this runtime's /proc.
        command = "read -r w < /proc/$$/winpid; echo $$ $w; sleep 30; true"
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
            process.stdout.close()
        self.assertFalse(helper.pid_alive(msys_pid, token))

    def test_exec_chain_links_backgrounded_python_to_the_top_shell(self):
        msys_root = Path(sys.executable).parents[2]
        msys_bash = msys_root / "usr" / "bin" / "bash.exe"
        if not msys_bash.is_file():
            self.skipTest("no MSYS2 bash beside the interpreter")
        with tempfile.TemporaryDirectory() as temp:
            script = Path(temp) / "chain.sh"
            script.write_text(
                "#!/usr/bin/env bash\n"
                '"$1" -c "import os, time; print(os.getpid(), flush=True); time.sleep(30)" &\n'
                "wait\n",
                newline="\n",
            )
            command = (
                'read -r w < /proc/$$/winpid; echo "$w"; '
                f'echo x | "{script.as_posix()}" "{Path(sys.executable).as_posix()}"'
            )
            process = subprocess.Popen(
                [str(msys_bash), "-c", command], stdout=subprocess.PIPE, text=True
            )
            try:
                top = int(process.stdout.readline())
                leaf = int(process.stdout.readline())
                helper = load_helper(Path(temp))
                parents = helper.process_parents((msys_root,))
                children = {}
                for child, parent in parents.items():
                    children.setdefault(parent, []).append(child)
                self.assertIn(leaf, helper.descendants(top, children))
                cursor, chain = leaf, set()
                while cursor > 0 and cursor not in chain:
                    chain.add(cursor)
                    cursor = parents.get(cursor, 0)
                self.assertIn(top, chain)
                win32_only = helper.win32_parents()
                cursor, chain = leaf, set()
                while cursor > 0 and cursor not in chain:
                    chain.add(cursor)
                    cursor = win32_only.get(cursor, 0)
                self.assertNotIn(top, chain, "Win32 links alone reach the top shell")
            finally:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                               capture_output=True)
                process.kill()
                process.wait(timeout=30)
                process.stdout.close()


if __name__ == "__main__":
    if os.name != "nt":
        print("quiet_window_windows: skipped (requires native Windows Python)")
    unittest.main()
