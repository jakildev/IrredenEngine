#!/usr/bin/env python3
"""Windows coverage for the quiet-window process model.

The owner-token, MSYS process-map, park runtime-root, CPU-time, parker-row
and reaped-child cases run on every host; the classes gated on native Windows
drive real MSYS processes.
"""

import argparse
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import threading
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


SECOND_RUNTIME = Path("C:/msys64/")
PARKER_START = 133_000_000_000_000_000
MSYS_PS_HEADER = MSYS_PS.splitlines(keepends=True)[0]


def ps_exe(runtime_root):
    return str(runtime_root / "usr" / "bin" / "ps.exe")


class NativeWindowsOs:
    """The helper's `os` as a parker sees it under the 600 shell; the rest is the real module."""

    name = "nt"

    @staticmethod
    def getppid():
        return 600

    def __getattr__(self, attr):
        return getattr(os, attr)


class ParkRuntimeRootsTest(unittest.TestCase):
    """cmd_park takes one runtime root from the lease owner token and one from --runtime-root."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.helper = load_helper(Path(temp.name))
        self.helper.atomic_json(
            self.helper.lease_path("pane") / "lease.json",
            {"tag": "pane", "holder_pid": 5, "owner_token": f"{HOLDER} {RUNTIME}"},
        )
        for patch in (
            mock.patch.object(self.helper, "os", NativeWindowsOs()),
            mock.patch.object(self.helper.platform, "system", return_value="Windows"),
            mock.patch.object(self.helper, "win32_parents", return_value=dict(WIN32_PARENTS)),
            mock.patch.object(self.helper, "windows_native_alive", return_value=True),
            mock.patch.object(self.helper, "windows_process_start", return_value=PARKER_START),
        ):
            patch.start()
            self.addCleanup(patch.stop)

    def park(self, run):
        """One held sample then off; returns every parker state written and stderr."""
        args = argparse.Namespace(
            tag="pane",
            measurement_root=20,
            measurement_root_token=f"{ROOT} {RUNTIME}",
            runtime_root=str(SECOND_RUNTIME),
            launcher_token="run-a",
        )
        held = {
            "state": "held",
            "records": [{"state": "held", "settle_sample_seconds": 0.0, "settle_cpu": 0.5}],
        }
        written = []
        real_atomic_json = self.helper.atomic_json

        def record(path, value):
            if path.parent.name == "parkers":
                written.append(dict(value))
            real_atomic_json(path, value)

        stderr = io.StringIO()
        with mock.patch.object(self.helper.subprocess, "run", side_effect=run), \
                mock.patch.object(self.helper, "status_payload",
                                  side_effect=[held, {"state": "off"}, {"state": "off"}]), \
                mock.patch.object(self.helper, "atomic_json", side_effect=record), \
                mock.patch.object(self.helper.sys, "stderr", stderr):
            self.assertEqual(self.helper.cmd_park(args), 0)
        return written, stderr.getvalue()

    def test_msys_only_ancestor_is_retained_from_either_runtime_root(self):
        for live in (RUNTIME, SECOND_RUNTIME):
            with self.subTest(table=str(live)):
                calls = []

                def run(argv, **kwargs):
                    calls.append(argv[0])
                    text = MSYS_PS if argv[0] == ps_exe(live) else MSYS_PS_HEADER
                    return subprocess.CompletedProcess(argv, 0, stdout=text, stderr="")

                sampled = mock.Mock(return_value=(0.0, 1.0, 2.0))
                with mock.patch.object(self.helper, "sample_cores", sampled):
                    written, warning = self.park(run)
                self.assertEqual(warning, "")
                self.assertEqual(written[0]["measurement_root"], ROOT)
                self.assertEqual(written[0]["launcher_token"], "run-a")
                self.assertEqual(written[0]["start_time"], PARKER_START)
                self.assertEqual(sorted(calls), sorted([ps_exe(RUNTIME), ps_exe(SECOND_RUNTIME)]))
                holder, excluded, _, runtime_roots = sampled.call_args.args
                self.assertEqual(holder, HOLDER)
                self.assertIn(ROOT, excluded)
                self.assertEqual(set(runtime_roots), {RUNTIME, SECOND_RUNTIME})

    def test_unreadable_runtime_table_drops_the_root_and_samples_busy(self):
        for outcome in (
            FileNotFoundError("ps.exe"),
            subprocess.TimeoutExpired("ps.exe", 5),
            subprocess.CalledProcessError(1, "ps.exe"),
        ):
            with self.subTest(outcome=type(outcome).__name__), \
                    mock.patch.object(self.helper, "cpu_time", return_value=0.0):
                written, warning = self.park(mock.Mock(side_effect=outcome))
                self.assertIn(f"ignoring non-ancestor IR_QUIET_MEASURE_ROOT={ROOT}", warning)
                self.assertEqual(written[0]["measurement_root"], 0)
                self.assertEqual(written[1]["state"], "busy")


class FakeKernel32:
    def __init__(self, openable, readable=None, starts=None):
        self.openable = openable
        self.readable = openable if readable is None else readable
        self.starts = starts or {}
        self.closed = []

    def OpenProcess(self, access, inherit, pid):
        return 1000 + pid if pid in self.openable else 0

    def GetProcessTimes(self, handle, *times):
        if handle - 1000 not in self.readable:
            return 0
        times[0]._obj.value = self.starts.get(handle - 1000, 0)
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


class ParkerRowsTest(unittest.TestCase):
    """Parker rows on a Windows host, every pid alive unless a case says otherwise."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.helper = load_helper(Path(temp.name))
        self.parkers = self.helper.lease_path("pane") / "parkers"
        self.alive = mock.Mock(return_value=True)
        for patch in (
            mock.patch.object(self.helper, "os", NativeWindowsOs()),
            mock.patch.object(self.helper, "pid_alive", self.alive),
        ):
            patch.start()
            self.addCleanup(patch.stop)

    def write(self, pid, **fields):
        row = {"pid": pid, "owner_token": str(pid), "state": "parked", **fields}
        self.helper.atomic_json(self.parkers / f"{pid}.json", row)

    def drop(self, token):
        args = argparse.Namespace(tag="pane", launcher_token=token)
        self.assertEqual(self.helper.cmd_park_drop(args), 0)
        return sorted(int(entry.stem) for entry in self.parkers.glob("*.json"))

    def test_park_drop_removes_only_the_launchers_row(self):
        self.write(11, launcher_token="run-a")
        self.write(12, launcher_token="run-b")
        self.assertEqual(self.drop("run-a"), [12])

    def test_park_drop_sweeps_dead_writers_whatever_their_token(self):
        self.write(11, launcher_token="run-a")
        self.write(12, launcher_token="run-b")
        self.alive.side_effect = lambda pid, token="": pid != 12
        self.assertEqual(self.drop("run-c"), [11])

    def test_empty_token_drops_no_live_row(self):
        self.write(11, launcher_token="")
        self.write(12)
        self.assertEqual(self.drop(""), [11, 12])

    def test_park_drop_without_a_lease_is_a_no_op(self):
        self.assertEqual(self.drop("run-a"), [])

    def test_recycled_pid_is_not_a_live_parker(self):
        self.write(11, start_time=PARKER_START)
        self.write(12, start_time=PARKER_START)
        self.write(13)
        fake = FakeKernel32({11, 12, 13}, starts={11: PARKER_START, 12: PARKER_START + 1})
        with mock.patch.object(self.helper, "kernel32", return_value=fake):
            live = self.helper.live_parkers(self.helper.lease_path("pane"))
        self.assertEqual(sorted(row["pid"] for row in live), [11, 13])

    def test_recycled_pid_row_is_swept_by_park_drop(self):
        self.write(11, start_time=PARKER_START)
        self.write(12, start_time=PARKER_START)
        fake = FakeKernel32({11, 12}, starts={11: PARKER_START, 12: PARKER_START + 1})
        with mock.patch.object(self.helper, "kernel32", return_value=fake):
            self.assertEqual(self.drop("run-a"), [11])


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


class AtomicJsonSharingViolationTest(unittest.TestCase):
    """atomic_json rides out a reader holding the destination open."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.dir = Path(temp.name)
        self.helper = load_helper(self.dir)
        self.target = self.dir / "parkers" / "44792.json"
        self.helper.atomic_json(self.target, {"state": "old"})

    def leftover_temp_files(self):
        return [entry for entry in self.target.parent.iterdir() if entry.name.startswith(".")]

    def test_replace_retries_through_transient_permission_errors(self):
        real_replace = os.replace
        outcomes = [PermissionError(5, "Access is denied"), PermissionError(5, "Access is denied")]

        def flaky(source, destination):
            if outcomes:
                raise outcomes.pop(0)
            real_replace(source, destination)

        with mock.patch.object(self.helper.os, "replace", side_effect=flaky) as replace, \
                mock.patch.object(self.helper.time, "sleep") as sleep:
            self.helper.atomic_json(self.target, {"state": "new"})
        self.assertEqual(replace.call_count, 3)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(self.helper.read_json(self.target), {"state": "new"})
        self.assertEqual(self.leftover_temp_files(), [])

    def test_persistent_permission_error_raises_and_cleans_the_temp_file(self):
        with mock.patch.object(
            self.helper.os, "replace", side_effect=PermissionError(5, "Access is denied")
        ) as replace, mock.patch.object(self.helper.time, "sleep"):
            with self.assertRaises(PermissionError):
                self.helper.atomic_json(self.target, {"state": "new"})
        self.assertEqual(replace.call_count, self.helper.REPLACE_ATTEMPTS)
        self.assertEqual(self.helper.read_json(self.target), {"state": "old"})
        self.assertEqual(self.leftover_temp_files(), [])

    @unittest.skipUnless(os.name == "nt", "requires native Windows file sharing")
    def test_replace_succeeds_after_a_real_reader_releases_the_file(self):
        reader = open(self.target, encoding="utf-8")
        timer = threading.Timer(0.1, reader.close)
        timer.start()
        try:
            self.helper.atomic_json(self.target, {"state": "new"})
        finally:
            timer.join()
            reader.close()
        self.assertEqual(self.helper.read_json(self.target), {"state": "new"})


@unittest.skipUnless(os.name == "nt", "requires native Windows Python")
class WindowsOwnerTokenTest(unittest.TestCase):
    def test_process_start_identifies_one_process(self):
        with tempfile.TemporaryDirectory() as temp:
            helper = load_helper(Path(temp))
            own = helper.windows_process_start(os.getpid())
            self.assertTrue(own)
            self.assertEqual(helper.windows_process_start(os.getpid()), own)
            process = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            try:
                child = helper.windows_process_start(process.pid)
            finally:
                process.kill()
                process.wait(timeout=30)
            self.assertTrue(child)
            self.assertNotEqual(child, own)

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
