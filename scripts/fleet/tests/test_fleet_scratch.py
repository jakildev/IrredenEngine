"""Hermetic tests for fleet-scratch (dir, rm, reap) and its module.

The pane is a fake one: FLEET_ASSIGNED_WORKTREE names a temp git worktree
whose basename is unique to this run, so the scratch root the suite creates
under /tmp never belongs to a live pane. Everything created is removed in
tearDownModule. Command-level cases run the wrapper under bash; unit-level
cases inject process facts and a recording kill so that no foreign process is
needed and nothing is signalled.
"""
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "fleet-scratch"
MODULE = ROOT / "fleet_scratch.py"
if not WRAPPER.is_file() or not MODULE.is_file():
    print("SKIP: fleet-scratch subject absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(ROOT))
import fleet_scratch as fs  # noqa: E402

BASH = shutil.which("bash") or "bash"
NATIVE_WINDOWS = os.name == "nt" or sys.platform in ("cygwin", "msys")

S = {}


def _git(*args, cwd=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    subprocess.run(["git", "-c", "user.name=fixture", "-c", "user.email=fixture@example.invalid",
                    *args], cwd=cwd, env=env, check=True, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def _touch(path, text="x"):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def setUpModule():
    tmp = Path(tempfile.mkdtemp(prefix="fleet-scratch-test.")).resolve()
    pane = f"fsx-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    clone = tmp / "clone"
    clone.mkdir()
    _git("init", "-q", str(clone))
    _touch(clone / ".gitignore", "*.log\n.claude/\n")
    _touch(clone / "src" / "a.txt")
    _touch(clone / "build" / "tracked" / "keep.txt")
    _git("add", "-f", ".", cwd=clone)
    _git("commit", "-q", "-m", "fixture", cwd=clone)
    worktree = clone / ".claude" / "worktrees" / pane
    _git("worktree", "add", "-q", "--detach", str(worktree), cwd=clone)
    scratch = Path(os.path.realpath("/tmp")) / f"fleet-{pane}"
    S.update(tmp=tmp, pane=pane, clone=clone, wt=worktree, scratch=scratch,
             sibling=Path(f"{scratch}x"), outside=tmp / "outside", procs=[])
    S["outside"].mkdir()


def tearDownModule():
    for proc in S.get("procs", []):
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    for path in (S.get("scratch"), S.get("sibling")):
        if path and os.path.lexists(path):
            shutil.rmtree(path) if path.is_dir() and not path.is_symlink() else path.unlink()
    if S.get("tmp"):
        shutil.rmtree(S["tmp"], ignore_errors=True)


def run(*args, cwd=None, assigned=True, argv=None):
    env = dict(os.environ)
    env.pop(fs.ASSIGNMENT_VAR, None)
    if assigned:
        env[fs.ASSIGNMENT_VAR] = str(S["wt"])
    cmd = argv or [BASH, str(WRAPPER), *args]
    return subprocess.run(cmd, cwd=cwd or S["tmp"], env=env, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, timeout=60)


def pane():
    return fs.pane_from_env({fs.ASSIGNMENT_VAR: str(S["wt"])})


def victim(name="victim.txt"):
    return _touch(S["outside"] / name, "keep")


def scratch_file(rel):
    S["scratch"].mkdir(mode=0o700, exist_ok=True)
    return _touch(S["scratch"] / rel)


def spawn(cwd, ignore_term=False):
    code = ("import signal, sys, time\n"
            + ("signal.signal(signal.SIGTERM, signal.SIG_IGN)\n" if ignore_term else "")
            + "print('ready', flush=True)\ntime.sleep(120)\n")
    proc = subprocess.Popen([sys.executable, "-c", code], cwd=cwd, stdout=subprocess.PIPE,
                            stdin=subprocess.DEVNULL, text=True)
    S["procs"].append(proc)
    assert proc.stdout.readline().strip() == "ready"
    return proc


class Dir(unittest.TestCase):
    def test_dir_creates_prints_and_is_idempotent(self):
        first = run("dir", "caps")
        self.assertEqual(first.returncode, 0, first.stderr)
        printed = first.stdout.strip()
        self.assertEqual(printed, str(S["scratch"] / "caps"))
        self.assertTrue(os.path.isabs(printed))
        self.assertEqual(printed, os.path.realpath(printed))
        self.assertTrue(os.path.isdir(printed))
        self.assertEqual(os.stat(S["scratch"]).st_mode & 0o777, 0o700)
        before = [os.stat(p) for p in (S["scratch"], printed)]
        second = run("dir", "caps")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout.strip(), printed)
        after = [os.stat(p) for p in (S["scratch"], printed)]
        for b, a in zip(before, after):
            self.assertEqual((b.st_ino, b.st_mode, b.st_mtime_ns),
                             (a.st_ino, a.st_mode, a.st_mtime_ns))

    def test_dir_without_name_prints_root(self):
        result = run("dir")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str(S["scratch"]))

    def test_dir_name_must_be_one_component(self):
        for name in ("../escape", "a/b", "..", ".hidden"):
            self.assertEqual(run("dir", name).returncode, 2, name)
        for made in (S["scratch"].parent / "escape", S["scratch"] / "a", S["scratch"] / ".hidden"):
            self.assertFalse(os.path.exists(made), made)

    def test_dir_refuses_a_symlinked_name(self):
        S["scratch"].mkdir(mode=0o700, exist_ok=True)
        link = S["scratch"] / "planted"
        link.symlink_to(S["outside"])
        try:
            result = run("dir", "planted")
            self.assertEqual(result.returncode, 1)
            self.assertIn("not a directory", result.stderr)
        finally:
            link.unlink()


class RmPositive(unittest.TestCase):
    def test_scratch_tree(self):
        tree = S["scratch"] / "tree"
        scratch_file("tree/a/b.txt")
        scratch_file("tree/c.txt")
        result = run("rm", str(tree))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(tree.exists())

    def test_build_path(self):
        shot = _touch(S["wt"] / "build" / "creations" / "demo" / "screenshots" / "s.png")
        result = run("rm", str(shot.parent))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(shot.parent.exists())

    def test_ignored_path(self):
        log = _touch(S["wt"] / "run.log")
        result = run("rm", "run.log", cwd=S["wt"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(log.exists())

    def test_untracked_pycache(self):
        pyc = _touch(S["wt"] / "pkg" / "__pycache__" / "m.cpython.pyc")
        result = run("rm", str(pyc.parent))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(pyc.parent.exists())

    def test_absent_path_is_reported_not_refused(self):
        S["scratch"].mkdir(mode=0o700, exist_ok=True)
        result = run("rm", str(S["scratch"] / "never-made"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("absent", result.stdout)


class RmEscapes(unittest.TestCase):
    def assertRefused(self, result, arg, *survivors):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(f"'{arg}'", result.stderr)
        for path in survivors:
            self.assertTrue(os.path.lexists(path), f"{path} was deleted")

    def test_dotdot_relative(self):
        target = victim()
        S["scratch"].mkdir(mode=0o700, exist_ok=True)
        arg = os.path.relpath(target, S["scratch"])
        self.assertTrue(arg.startswith(".."))
        self.assertRefused(run("rm", arg, cwd=S["scratch"]), arg, target)

    def test_dotdot_embedded_absolute(self):
        target = victim()
        arg = f"{S['scratch']}/inner/../{os.path.relpath(target, S['scratch'])}"
        self.assertRefused(run("rm", arg), arg, target)

    def test_tmp_dotdot_ride(self):
        target = victim()
        arg = f"/tmp/fleet-{S['pane']}/../../{os.path.relpath(target, '/')}"
        self.assertRefused(run("rm", arg), arg, target)

    def test_symlink_inside_root_pointing_out(self):
        target = victim()
        S["scratch"].mkdir(mode=0o700, exist_ok=True)
        link = S["scratch"] / "out-link"
        link.symlink_to(S["outside"])
        try:
            arg = str(link / target.name)
            self.assertRefused(run("rm", arg), arg, target)
        finally:
            link.unlink()

    def test_prefix_sharing_sibling(self):
        planted = _touch(S["sibling"] / "f.txt")
        self.assertRefused(run("rm", str(planted)), str(planted), planted)
        wt_sibling = _touch(Path(f"{S['wt']}-sib") / "build" / "f.txt")
        self.assertRefused(run("rm", str(wt_sibling)), str(wt_sibling), wt_sibling)

    def test_second_argument_outside(self):
        inside = scratch_file("first.txt")
        target = victim()
        result = run("rm", str(inside), str(target))
        self.assertRefused(result, str(target), inside, target)
        self.assertIn("nothing deleted", result.stderr)

    def test_build_path_holding_tracked_file(self):
        for rel in ("build/tracked", "build", "build/tracked/keep.txt"):
            arg = str(S["wt"] / rel)
            self.assertRefused(run("rm", arg), arg, S["wt"] / "build" / "tracked" / "keep.txt")

    def test_worktree_root_and_git(self):
        for arg in (str(S["wt"]), str(S["wt"] / ".git"), f"{S['wt']}/build/../.git"):
            self.assertRefused(run("rm", arg), arg, S["wt"] / ".git")

    def test_untracked_unignored_worktree_file(self):
        stray = _touch(S["wt"] / "stray.txt")
        self.assertRefused(run("rm", str(stray)), str(stray), stray)

    def test_tracked_source(self):
        arg = str(S["wt"] / "src")
        self.assertRefused(run("rm", arg), arg, S["wt"] / "src" / "a.txt")


class RmScratchRoot(unittest.TestCase):
    """The scratch root's name is predictable in a shared /tmp, so rm accepts a
    path under it only once the root itself is a real directory the caller owns."""

    def planted_pane(self, label):
        root = S["tmp"] / f"planted-{label}"
        return root, fs.Pane(name=S["pane"], scratch_root=str(root), worktrees=(str(S["wt"]),))

    def assertNothingDeleted(self, pane, arg, *survivors):
        self.assertEqual(fs.cmd_rm(pane, [str(arg)]), fs.EXIT_REFUSED)
        for path in survivors:
            self.assertTrue(os.path.lexists(path), f"{path} was deleted")

    @unittest.skipUnless(hasattr(os, "getuid"), "ownership needs a uid")
    def test_root_owned_by_another_user_is_refused(self):
        root, planted = self.planted_pane("foreign")
        root.mkdir(mode=0o777)
        theirs = _touch(root / "theirs.txt", "keep")
        self.assertIsNotNone(fs.check_rm_target(planted, str(theirs)))
        with mock.patch.object(fs.os, "getuid", return_value=os.getuid() + 1):
            self.assertNothingDeleted(planted, theirs, theirs)
            with self.assertRaisesRegex(fs.Refusal, "belongs to another user"):
                fs.check_rm_target(planted, str(root))

    def test_absent_root_is_refused(self):
        _, planted = self.planted_pane("absent")
        with self.assertRaisesRegex(fs.Refusal, "does not exist"):
            fs.check_rm_target(planted, str(Path(planted.scratch_root) / "f.txt"))

    def test_root_that_is_a_symlink_or_file_is_refused(self):
        kept = _touch(S["outside"] / "root-link-target" / "kept.txt")
        root, planted = self.planted_pane("link")
        root.symlink_to(kept.parent)
        self.assertNothingDeleted(planted, root, root, kept)
        root.unlink()
        _touch(root, "keep")
        self.assertNothingDeleted(planted, root, root)


class RmSymlinks(unittest.TestCase):
    def test_final_symlink_is_unlinked_not_followed(self):
        kept = _touch(S["outside"] / "linked-dir" / "kept.txt")
        S["scratch"].mkdir(mode=0o700, exist_ok=True)
        link = S["scratch"] / "final-link"
        link.symlink_to(kept.parent)
        result = run("rm", str(link))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(os.path.lexists(link))
        self.assertTrue(kept.exists())

    def test_recursive_delete_does_not_follow(self):
        kept = _touch(S["outside"] / "deep-dir" / "kept.txt")
        tree = S["scratch"] / "with-link"
        scratch_file("with-link/plain.txt")
        (tree / "inner").symlink_to(kept.parent)
        result = run("rm", str(tree))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(os.path.lexists(tree))
        self.assertTrue(kept.exists())


class TmpSpellings(unittest.TestCase):
    def test_both_spellings_are_one_root(self):
        a = scratch_file("spell-a.txt")
        b = scratch_file("spell-b.txt")
        result = run("rm", f"/tmp/fleet-{S['pane']}/spell-a.txt",
                     f"{os.path.realpath('/tmp')}/fleet-{S['pane']}/spell-b.txt")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(a.exists() or b.exists())
        self.assertEqual(pane().scratch_root, str(S["scratch"]))


@unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
class ReapPositive(unittest.TestCase):
    def setUp(self):
        self.cwd = S["scratch"] / "reap-cwd"
        self.cwd.mkdir(parents=True, exist_ok=True)

    def test_reaps_child_in_scratch_root(self):
        proc = spawn(self.cwd)
        result = run("reap", str(proc.pid))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(proc.wait(timeout=5), -signal.SIGTERM)

    def test_child_ignoring_term_gets_kill(self):
        proc = spawn(self.cwd, ignore_term=True)
        result = run("reap", str(proc.pid))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(proc.wait(timeout=5), -signal.SIGKILL)


@unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
class ReapRefusals(unittest.TestCase):
    def assertRefused(self, result, pid):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(f"'{pid}'", result.stderr)

    def test_pid_one(self):
        self.assertRefused(run("reap", "1"), 1)

    def test_child_outside_every_root(self):
        proc = spawn(S["outside"])
        self.assertRefused(run("reap", str(proc.pid)), proc.pid)
        self.assertIsNone(proc.poll())
        proc.kill()
        proc.wait()

    def test_caller_and_its_parent(self):
        cwd = S["scratch"] / "reap-cwd"
        cwd.mkdir(parents=True, exist_ok=True)
        self_case = run(cwd=cwd, argv=[BASH, "-c", 'exec "$0" "$1" reap $$', BASH, str(WRAPPER)])
        self.assertEqual(self_case.returncode, 1, self_case.stderr)
        self.assertIn("this process", self_case.stderr)
        parent_case = run(cwd=cwd, argv=[BASH, "-c", '"$0" "$1" reap $$; echo "rc=$?"',
                                         BASH, str(WRAPPER)])
        self.assertEqual(parent_case.returncode, 0, parent_case.stderr)
        self.assertIn("rc=1", parent_case.stdout)
        self.assertIn("ancestor", parent_case.stderr)

    def test_missing_pid(self):
        proc = subprocess.Popen([sys.executable, "-c", "pass"])
        proc.wait()
        self.assertRefused(run("reap", str(proc.pid)), proc.pid)

    def test_not_a_pid(self):
        for arg in ("abc", "0", "-5", ""):
            self.assertEqual(run("reap", arg).returncode, 2, arg)


class ReapUnit(unittest.TestCase):
    """Process facts injected; the recording kill shows nothing was signalled."""

    ME, PARENT, TARGET, UID = 5000, 4000, 4242, 777

    def table(self, **target):
        cwd = target.pop("cwd", str(S["scratch"]))
        home = str(S["tmp"])
        facts = {
            self.ME: fs.ProcessFacts(self.ME, self.UID, self.UID, self.PARENT, False, home, "t-me"),
            self.PARENT: fs.ProcessFacts(self.PARENT, self.UID, self.UID, 1, False, home, "t-pa"),
            self.TARGET: fs.ProcessFacts(**{"pid": self.TARGET, "uid": self.UID, "ruid": self.UID,
                                            "ppid": 1, "zombie": False, "cwd": cwd,
                                            "started": "t-original", **target}),
        }
        return facts.get

    def reap(self, pid, reader):
        sent = []
        clock = iter(range(0, 10_000)).__next__
        status = fs.cmd_reap(pane(), str(pid), reader=reader,
                             kill=lambda p, s: sent.append((p, s)), sleep=lambda _s: None,
                             clock=clock, my_pid=self.ME, my_uid=self.UID)
        return status, sent

    @unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
    def test_refusals_signal_nothing(self):
        cases = {
            "foreign euid": (self.TARGET, self.table(uid=0)),
            "foreign ruid": (self.TARGET, self.table(ruid=0)),
            "cwd outside": (self.TARGET, self.table(cwd=str(S["outside"]))),
            "cwd unreadable": (self.TARGET, self.table(cwd=None)),
            "start time unreadable": (self.TARGET, self.table(started=None)),
            "prefix sibling cwd": (self.TARGET, self.table(cwd=str(S["sibling"]))),
            "zombie": (self.TARGET, self.table(zombie=True)),
            "missing": (9999, self.table()),
            "self": (self.ME, self.table()),
            "parent": (self.PARENT, self.table()),
            "pid one": (1, self.table()),
        }
        for label, (pid, reader) in cases.items():
            with self.subTest(label):
                status, sent = self.reap(pid, reader)
                self.assertEqual(status, fs.EXIT_REFUSED)
                self.assertEqual(sent, [])

    @unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
    def test_term_then_recheck_before_kill(self):
        recycled = self.table(cwd=str(S["outside"]))
        reads = iter([self.table()] * 4 + [recycled] * 1000)
        status, sent = self.reap(self.TARGET, lambda pid: next(reads)(pid))
        self.assertEqual(status, fs.EXIT_REFUSED)
        self.assertEqual(sent, [(self.TARGET, signal.SIGTERM)])

    @unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
    def test_recycled_pid_passing_every_other_check_is_not_killed(self):
        # The replacement keeps the caller's uid and a scratch-root cwd; only its
        # start time differs. The injected clock advances one unit per call, so the
        # TERM wait reads the original exactly TERM_WAIT_S times after the three
        # reads of the first check, and the replacement first appears at the
        # re-check before SIGKILL.
        recycled = self.table(started="t-replacement")
        first_seen = 3 + int(fs.TERM_WAIT_S)
        reads = iter([self.table()] * first_seen + [recycled] * 1000)
        status, sent = self.reap(self.TARGET, lambda pid: next(reads)(pid))
        self.assertEqual(status, fs.EXIT_REFUSED)
        self.assertEqual(sent, [(self.TARGET, signal.SIGTERM)])

    @unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
    def test_pid_recycled_during_the_wait_counts_as_exited(self):
        recycled = self.table(started="t-replacement")
        reads = iter([self.table()] * 3 + [recycled] * 1000)
        status, sent = self.reap(self.TARGET, lambda pid: next(reads)(pid))
        self.assertEqual(status, fs.EXIT_OK)
        self.assertEqual(sent, [(self.TARGET, signal.SIGTERM)])

    @unittest.skipIf(NATIVE_WINDOWS, "reap refuses on a native-Windows host")
    def test_real_process_has_a_start_time(self):
        facts = fs.read_process(os.getpid())
        self.assertIsNotNone(facts)
        self.assertTrue(facts.started)
        self.assertEqual(fs.read_process(os.getpid()).started, facts.started)

    def test_native_windows_refuses(self):
        original = fs._native_windows
        fs._native_windows = lambda: True
        try:
            status, sent = self.reap(self.TARGET, self.table())
        finally:
            fs._native_windows = original
        self.assertEqual(status, fs.EXIT_REFUSED)
        self.assertEqual(sent, [])


class Identity(unittest.TestCase):
    def test_unset_assignment_exits_2_and_does_nothing(self):
        planted = scratch_file("identity.txt")
        proc = None if NATIVE_WINDOWS else spawn(S["scratch"])
        cases = [("dir", "identity-caps"), ("rm", str(planted))]
        if proc:
            cases.append(("reap", str(proc.pid)))
        for args in cases:
            result = run(*args, assigned=False)
            self.assertEqual(result.returncode, 2, args)
            self.assertIn(fs.ASSIGNMENT_VAR, result.stderr)
        self.assertFalse((S["scratch"] / "identity-caps").exists())
        self.assertTrue(planted.exists())
        if proc:
            self.assertIsNone(proc.poll())
            proc.kill()
            proc.wait()

    def test_unusable_assignments(self):
        for value in ("relative/pool-x", str(S["tmp"] / "missing-pane"), "/"):
            with self.subTest(value), self.assertRaises(fs.UsageError):
                fs.pane_from_env({fs.ASSIGNMENT_VAR: value})

    def test_twin_worktree_is_a_root(self):
        twin = S["clone"] / "creations" / "game" / ".claude" / "worktrees" / S["pane"]
        twin.mkdir(parents=True)
        try:
            self.assertEqual(pane().worktrees, (str(S["wt"]), str(twin)))
        finally:
            shutil.rmtree(S["clone"] / "creations")


class Helpers(unittest.TestCase):
    def test_is_within_needs_a_separator(self):
        self.assertTrue(fs.is_within("/a/b", "/a/b"))
        self.assertTrue(fs.is_within("/a/b/c", "/a/b"))
        self.assertFalse(fs.is_within("/a/bc", "/a/b"))
        self.assertFalse(fs.is_within("/a", "/a/b"))

    def test_native_spelling(self):
        self.assertEqual(fs.native_spelling("/c/Users/x", nt=True), "c:/Users/x")
        self.assertEqual(fs.native_spelling("/c", nt=True), "c:/")
        self.assertEqual(fs.native_spelling("/c/Users/x", nt=False), "/c/Users/x")
        self.assertEqual(fs.native_spelling("/tmp/x", nt=True), "/tmp/x")

    def test_tmp_root_on_native_windows_asks_cygpath(self):
        def fake_run(argv, **_kw):
            self.assertEqual(argv, ["cygpath", "-m", "/tmp"])
            return subprocess.CompletedProcess(argv, 0, stdout=f"{S['tmp']}\n")

        def failing_run(argv, **_kw):
            raise FileNotFoundError(argv[0])

        self.assertEqual(fs.tmp_root(nt=True, run=fake_run), str(S["tmp"]))
        self.assertIsNone(fs.tmp_root(nt=True, run=failing_run))
        self.assertEqual(fs.tmp_root(nt=False), os.path.realpath("/tmp"))

    def test_no_tmp_root_refuses_dir(self):
        no_tmp = fs.pane_from_env({fs.ASSIGNMENT_VAR: str(S["wt"])}, tmp="")
        self.assertIsNone(no_tmp.scratch_root)
        with self.assertRaises(fs.UsageError):
            fs.cmd_dir(no_tmp, "x")

    def test_help_needs_no_identity(self):
        result = run("--help", assigned=False)
        self.assertEqual(result.returncode, 0)
        self.assertIn("fleet-scratch reap <pid>", result.stdout)

    def test_unknown_verb(self):
        self.assertEqual(run("purge", "/").returncode, 2)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=1).result
    sys.exit(0 if result.wasSuccessful() else 1)
