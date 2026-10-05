"""fleet-jobs: detached survival, exact exit, pane isolation, identity safety,
and the fixed-profile boundary.

Every job runs a fixture: the subject is copied into a temp checkout whose
adjacent engine/tools/bin/ir-build, scripts/fleet/tests/run_all.sh and
scripts/render-verify.py are stubs, so the real profiles resolve fixtures.
State lives under a temp FLEET_STATE_DIR and HOME; the live ~/.fleet is never
read or written.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import json
import os
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-jobs"
if not SUBJECT.is_file():
    print("SKIP: scripts/fleet/fleet-jobs absent", file=sys.stderr)
    sys.exit(3)
# Only NaturalExit, BreakawayRefusal and the Windows* classes run on native
# Windows (the job-object arm); the rest of the suite drives POSIX process
# groups and signals.
posix_only = unittest.skipIf(os.name == "nt", "POSIX process-group harness")

IR_BUILD = r"""#!/usr/bin/env bash
case "$1" in
  hold) echo "line one"; while [ ! -e "$2" ]; do sleep 0.05; done; echo "line two"; exit 7 ;;
  tree) sleep 300 & echo "$!" > "$2.tmp"; echo "$$" >> "$2.tmp"; mv "$2.tmp" "$2"; wait ;;
  orphan) "$3" -c "$ORPHAN" "$2" "$4" & while [ ! -e "$2" ]; do sleep 0.05; done; exit 0 ;;
  *) echo "ir-build $*"; exit 0 ;;
esac
"""
# Ignores SIGTERM, records its own native pid, then writes to the job log
# until something kills it. "session" leaves the job's process group and
# drops the inherited environment, so only the log descriptor ties it to the
# job; "quiet-session" leaves the group and closes the log, so only the
# environment does; "daemon" drops all three, so only the supervisor's
# anchor on its own tree does. "relay" answers SIGTERM by starting a "late"
# process, which SIGTERM ends, and exiting.
ORPHAN = r"""import os, signal, subprocess, sys, time
mode = sys.argv[2] if len(sys.argv) > 2 else "group"
if mode == "relay":
    def relay(*_):
        subprocess.Popen([sys.executable, "-c", os.environ["ORPHAN"], sys.argv[1] + ".late",
                          "late"])
        os._exit(0)
    signal.signal(signal.SIGTERM, relay)
if mode in ("quiet-session", "daemon"):
    devnull = os.open(os.devnull, os.O_WRONLY)
    os.dup2(devnull, 1)
    os.dup2(devnull, 2)
if mode in ("session", "quiet-session", "daemon"):
    os.setsid()
if mode in ("session", "daemon"):
    os.execve(sys.executable, [sys.executable, "-c", os.environ["ORPHAN"], sys.argv[1],
                               "scrubbed"], {})
if mode not in ("relay", "late") and hasattr(signal, "SIGTERM"):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
with open(sys.argv[1] + ".tmp", "w") as handle:
    handle.write(str(os.getpid()))
os.replace(sys.argv[1] + ".tmp", sys.argv[1])
while True:
    print("tick", flush=True)
    time.sleep(0.1)
"""
# Starts ORPHAN at once and exits as soon as it is running.
SPAWNER = r"""import os, subprocess, sys, time
subprocess.Popen([sys.executable, "-c", os.environ["ORPHAN"], sys.argv[1]],
                 stdout=subprocess.DEVNULL, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
while not os.path.exists(sys.argv[1]):
    time.sleep(0.05)
"""
RUN_ALL = '#!/usr/bin/env bash\necho "run_all $*"\nexit 0\n'
RENDER_VERIFY = r"""import os, sys, time
hold = [a[5:] for a in sys.argv if a.startswith("hold=")]
while hold and not os.path.exists(hold[0]):
    time.sleep(0.05)
print("render-verify", *sys.argv[1:])
sys.exit(3)
"""


# Holds the first _supervise process that finds the flag file, so its job's
# startup window closes before it can take the job.
SLOW_SUPERVISOR = r"""import atexit, os, sys, time
flag = os.environ.get("SLOW_SUPERVISOR", "")
if "_supervise" in sys.argv and flag:
    try:
        os.remove(flag)
    except FileNotFoundError:
        pass
    else:
        time.sleep(float(os.environ["SLOW_SUPERVISOR_DELAY"]))
        atexit.register(lambda: open(flag + ".done", "w").close())
"""


def git(cwd, *args, stdin=None):
    return subprocess.run(["git", "-C", str(cwd), *args], input=stdin, check=True,
                          capture_output=True, text=True).stdout.strip()


def init_repo(path):
    """A repository with one empty commit, so linked worktrees can be added."""
    subprocess.run(["git", "init", "-q", str(path)], check=True)
    tree = git(path, "mktree", stdin="")
    commit = git(path, "-c", "user.name=fleet-test", "-c", "user.email=fleet-test@invalid",
                 "commit-tree", tree, "-m", "fixture")
    git(path, "update-ref", "HEAD", commit)
    return path


def load_subject(path=SUBJECT):
    loader = importlib.machinery.SourceFileLoader("fleet_jobs", str(path))
    spec = importlib.util.spec_from_loader("fleet_jobs", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def wait_for(predicate, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    return predicate()


def gone(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


class JobsCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name).resolve()
        # Synthetic, unique pane names, so a check of the real ~/.fleet can
        # prove these panes never reached it. Each pane is a registered
        # .claude/worktrees/<pane> checkout; self.repo is pane A's.
        tag = secrets.token_hex(3)
        self.pane_a, self.pane_b = f"zz-test-a-{tag}", f"zz-test-b-{tag}"
        self.main = init_repo(self.root / "clone")
        self.repo = self.checkout(self.pane_a)
        git(self.main, "worktree", "add", "-q", "--detach", str(self.repo))
        (self.repo / "scripts/fleet/tests").mkdir(parents=True)
        (self.repo / "engine/tools/bin").mkdir(parents=True)
        shutil.copy2(SUBJECT, self.repo / "scripts/fleet/fleet-jobs")
        shutil.copy2(SUBJECT.with_name("fleet_codex_doctor.py"), self.repo / "scripts/fleet")
        for rel, text in (("engine/tools/bin/ir-build", IR_BUILD),
                          ("scripts/fleet/tests/run_all.sh", RUN_ALL),
                          ("scripts/render-verify.py", RENDER_VERIFY)):
            (self.repo / rel).write_text(text)
            (self.repo / rel).chmod(0o755)
        self.clone(self.checkout(self.pane_b))
        self.state = self.root / "state"
        self.subject = self.repo / "scripts/fleet/fleet-jobs"

    def tearDown(self):
        # Every live job, not only the ones a test expected to start: a
        # mutated guard admits extra jobs that would otherwise outlive the run.
        for pane in (self.pane_a, self.pane_b):
            for line in self.jobs(["list", "--json"], pane=pane).stdout.splitlines():
                job = json.loads(line)
                if job["status"] in ("starting", "running"):
                    self.jobs(["kill", job["id"]], pane=pane)
        self._tmp.cleanup()

    def checkout(self, pane):
        return self.root / "clone/.claude/worktrees" / pane

    def clone(self, path, main=None):
        """A linked worktree of main (the fixture engine repository) at path."""
        git(main or self.main, "worktree", "add", "-q", "--detach", str(path))
        return self.lookalike(path, init=False)

    def lookalike(self, path, init=True):
        shutil.copytree(self.repo, path, ignore=shutil.ignore_patterns(".git"),
                        dirs_exist_ok=True)
        if init:
            subprocess.run(["git", "init", "-q", str(path)], check=True)
        return path

    def env(self, pane):
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(("FLEET_", "IRREDEN_", "IR_"))}
        env.update(HOME=str(self.root / "home"), FLEET_STATE_DIR=str(self.state),
                   ORPHAN=ORPHAN)
        if pane is not None:
            env["FLEET_ASSIGNED_WORKTREE"] = str(self.checkout(pane))
        return env

    def jobs(self, args, pane=None, cwd=None, stdin=None):
        pane = self.pane_a if pane is None else pane
        return subprocess.run([sys.executable, str(self.subject), *args],
                              cwd=cwd or self.checkout(pane),
                              env=self.env(pane), capture_output=True, text=True, timeout=60,
                              input=stdin)

    def start(self, args, pane=None):
        result = self.jobs(["start", *args], pane=pane)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.split()[1]

    def status(self, job, pane=None):
        result = self.jobs(["status", "--json", job], pane=pane)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def job_dirs(self):
        return sorted(p for p in self.state.glob("jobs/*/*") if p.is_dir())


@posix_only
class Lifecycle(JobsCase):
    def test_job_survives_launcher_group_kill_and_wait_returns_exact_exit(self):
        release = self.root / "release"
        launcher = subprocess.Popen(
            ["bash", "-c", f'"{sys.executable}" "{self.subject}" start build -- hold '
                           f'"{release}"; exec sleep 60'],
            cwd=self.repo, env=self.env(self.pane_a), stdout=subprocess.PIPE, text=True,
            start_new_session=True)
        first = launcher.stdout.readline()
        self.assertTrue(first.startswith("started "), first)
        job = first.split()[1]
        # Runtime cleanup: the launching invocation's whole process group dies.
        os.killpg(launcher.pid, signal.SIGKILL)
        launcher.wait()
        launcher.stdout.close()
        time.sleep(0.3)
        self.assertEqual(self.status(job)["status"], "running")
        release.touch()
        waited = self.jobs(["wait", job])
        self.assertEqual(waited.returncode, 7, waited.stderr)
        self.assertEqual(waited.stdout, "line one\nline two\n")
        self.assertIn(f"{job} failed exit=7", waited.stderr.splitlines()[-1])
        final = self.status(job)
        self.assertEqual((final["status"], final["exit_code"]), ("failed", 7))

    def test_wait_timeout_reports_running_without_ending_the_job(self):
        release = self.root / "release"
        job = self.start(["build", "--", "hold", str(release)])
        waited = self.jobs(["wait", "--quiet", "--timeout", "1", job])
        self.assertEqual(waited.returncode, 124)
        self.assertIn("still running", waited.stderr)
        self.assertEqual(waited.stdout, "")
        release.touch()
        self.assertEqual(self.jobs(["wait", "--quiet", job]).returncode, 7)

    def test_kill_reaps_child_and_descendant(self):
        pids = self.root / "pids"
        job = self.start(["build", "--name", "tree-fixture", "--", "tree", str(pids)])
        self.assertTrue(wait_for(pids.exists))
        descendant, child = (int(x) for x in pids.read_text().split())
        self.assertFalse(gone(descendant))
        killed = self.jobs(["kill", job])
        self.assertEqual(killed.returncode, 0, killed.stderr)
        self.assertIn(" killed ", killed.stdout)
        self.assertTrue(wait_for(lambda: gone(child) and gone(descendant)),
                        f"child {child} or descendant {descendant} survived kill")
        final = self.status(job)
        self.assertEqual(final["status"], "killed")
        self.assertEqual(final["name"], "tree-fixture")
        self.assertEqual(final["exit_code"], 128 + signal.SIGTERM)

    def test_profiles_positive_fire(self):
        cases = (
            (["build", "--", "--target", "format-changed"], 0, "ir-build --target format-changed"),
            (["fleet-tests", "--only", "test_x"], 0, "run_all --only test_x"),
            (["fleet-tests", "--only=test_y"], 0, "run_all --only test_y"),
            (["fleet-tests"], 0, "run_all"),
            (["render-verify", "--name", "rv", "--", "--target", "IRAnalyticOracle",
              "--no-build", "--warmup=3", "--demo-arg", "--occlusion-cull"], 3,
             "render-verify --target IRAnalyticOracle --no-build --warmup=3 "
             "--demo-arg --occlusion-cull"))
        for args, code, line in cases:
            with self.subTest(args=args):
                job = self.start(args)
                waited = self.jobs(["wait", job])
                self.assertEqual(waited.returncode, code, waited.stderr)
                lines = waited.stdout.splitlines()
                if args[0] == "render-verify":
                    self.assertTrue(lines[0].startswith("[fleet-jobs] display: "), lines)
                    lines = lines[1:]
                self.assertEqual("\n".join(lines).strip(), line)

    def test_one_live_render_verify_per_pane(self):
        release = self.root / "release"
        held = ["render-verify", "--", "--target", "IRAnalyticOracle",
                "--demo-arg", f"hold={release}"]
        first = self.start(held)
        self.assertEqual(self.status(first)["status"], "running")
        refused = self.jobs(["start", *held])
        self.assertEqual(refused.returncode, 1, refused.stdout)
        self.assertIn(f"render-verify job {first} is still running", refused.stderr)
        self.assertEqual(len(self.job_dirs()), 1)
        other_pane = self.start(held, pane=self.pane_b)
        build = self.start(["build", "--", "plain"])
        self.assertEqual(self.jobs(["wait", "--quiet", build]).returncode, 0)
        release.touch()
        for pane, job in ((self.pane_a, first), (self.pane_b, other_pane)):
            self.assertEqual(self.jobs(["wait", "--quiet", job], pane=pane).returncode, 3)
        after = self.start(["render-verify", "--", "--target", "IRAnalyticOracle"])
        self.assertEqual(self.jobs(["wait", "--quiet", after]).returncode, 3)

    def test_concurrent_render_verify_starts_admit_exactly_one(self):
        release = self.root / "release"
        held = ["start", "render-verify", "--", "--target", "IRAnalyticOracle",
                "--demo-arg", f"hold={release}"]
        # Finished decoys lengthen every client's admission scan, so the
        # clients overlap inside it rather than one finishing before the next.
        pane = self.state / "jobs" / self.pane_a
        for i in range(400):
            decoy = pane / f"20250101T000000Z-render-verify-{i:06x}"
            decoy.mkdir(parents=True)
            (decoy / "meta.json").write_text(json.dumps(
                {"id": decoy.name, "profile": "render-verify", "status": "succeeded",
                 "exit_code": 0}))
        clients = [subprocess.Popen([sys.executable, str(self.subject), *held],
                                    cwd=self.repo, env=self.env(self.pane_a),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                   for _ in range(8)]
        outputs = [c.communicate(timeout=60)[0] for c in clients]
        codes = [c.returncode for c in clients]
        self.assertEqual(sorted(codes), [0] + [1] * 7)
        admitted = [out.split()[1] for out, code in zip(outputs, codes) if code == 0]
        self.assertEqual(len(self.job_dirs()), 401)
        release.touch()
        self.assertEqual(self.jobs(["wait", "--quiet", admitted[0]]).returncode, 3)

    def test_supervisor_past_the_startup_window_never_runs(self):
        window = 3
        self.subject.write_text(self.subject.read_text().replace(
            "STARTUP_TIMEOUT = 30.0", f"STARTUP_TIMEOUT = {window}.0"))
        site = self.root / "site"
        site.mkdir()
        (site / "sitecustomize.py").write_text(SLOW_SUPERVISOR)
        slow = self.root / "slow-supervisor"
        slow.touch()
        env = dict(self.env(self.pane_a), PYTHONPATH=str(site), SLOW_SUPERVISOR=str(slow),
                   SLOW_SUPERVISOR_DELAY=str(window * 2))
        start = [sys.executable, str(self.subject), "start", "render-verify", "--",
                 "--target", "IRAnalyticOracle"]
        late = subprocess.run(start, cwd=self.repo, env=env, capture_output=True, text=True,
                              timeout=60)
        self.assertEqual((late.returncode, late.stdout), (1, ""))
        self.assertIn(f"did not report within {window}s", late.stderr)
        [late_dir] = self.job_dirs()
        # Admitted while the late supervisor still sleeps.
        admitted = subprocess.run(start, cwd=self.repo, env=env, capture_output=True,
                                  text=True, timeout=60)
        self.assertEqual(admitted.returncode, 0, admitted.stderr)
        self.assertFalse(Path(f"{slow}.done").exists())
        job = admitted.stdout.split()[1]
        self.assertEqual(self.jobs(["wait", "--quiet", job]).returncode, 3)
        self.assertTrue(wait_for(lambda: Path(f"{slow}.done").exists(), timeout=window * 4))
        self.assertEqual((late_dir / "job.log").read_bytes(), b"", "the late job ran")
        status = self.status(late_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 127))
        self.assertIn("did not report", status["reason"])


class NaturalExit(JobsCase):
    MODES = ("group",) if os.name == "nt" else ("group", "session", "quiet-session", "daemon")

    def test_exit_drains_a_term_resistant_descendant_before_terminal_status(self):
        for mode in self.MODES:
            with self.subTest(mode=mode):
                pidfile = self.root / f"orphan-{mode}"
                job = self.start(["build", "--", "orphan", str(pidfile), sys.executable, mode])
                waited = self.jobs(["wait", job])
                self.assertEqual(waited.returncode, 0, waited.stderr)
                self.assertIn(f"{job} succeeded exit=0", waited.stderr.splitlines()[-1])
                if mode not in ("quiet-session", "daemon"):
                    self.assertIn("tick", waited.stdout)
                descendant = int(pidfile.read_text())
                survivor = load_subject().identity(descendant)
                if survivor:
                    os.kill(descendant, getattr(signal, "SIGKILL", signal.SIGTERM))
                self.assertIsNone(survivor,
                                  f"{mode} descendant {descendant} outlived the terminal status")
                log = self.state / "jobs" / self.pane_a / job / "job.log"
                final = log.read_bytes()
                self.assertEqual(final.decode().splitlines(), waited.stdout.splitlines())
                time.sleep(1)
                self.assertEqual(log.read_bytes(), final, "the log grew after the terminal status")

    @posix_only
    def test_a_process_started_mid_drain_is_signalled_when_found(self):
        subject = load_subject()
        pidfile = self.root / "orphan-relay"
        began = time.monotonic()
        job = self.start(["build", "--", "orphan", str(pidfile), sys.executable, "relay"])
        waited = self.jobs(["wait", job])
        elapsed = time.monotonic() - began
        self.assertEqual(waited.returncode, 0, waited.stderr)
        # SIGTERM can end the late process before it records itself.
        late_file = Path(f"{pidfile}.late")
        if late_file.exists():
            late = int(late_file.read_text())
            survivor = subject.identity(late)
            if survivor:
                os.kill(late, signal.SIGKILL)
            self.assertIsNone(survivor, f"late process {late} outlived the terminal status")
        # Only a SIGTERM sent when a pass finds it ends the late process
        # before SIGKILL's grace runs out.
        self.assertLess(elapsed, subject.CANCEL_GRACE,
                        "the late process was left for SIGKILL instead of signalled when found")


@posix_only
class DrainFailsClosed(JobsCase):
    def test_drain_is_true_only_once_the_tree_is_empty(self):
        subject = load_subject()
        for killable in (True, False):
            with self.subTest(killable=killable):
                child = subprocess.Popen([sys.executable, "-c", "pass"], start_new_session=True)
                member = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
                try:
                    ident = wait_for(lambda: subject.identity(member.pid))
                    unkillable = mock.patch.object(subject, "_signal_verified",
                                                   lambda members, sig: None)
                    # job_members of this process would reach every child of the
                    # test runner on Linux.
                    with mock.patch.object(subject, "CANCEL_GRACE", 0.5), \
                            mock.patch.object(subject, "job_members", lambda pgid, marks: []), \
                            unkillable if not killable else contextlib.nullcontext():
                        drained = subject._drain_tree(child, None, [(member.pid, ident)], None)
                    self.assertIs(drained, killable)
                    self.assertIsNotNone(child.returncode, "the drain left the child unreaped")
                finally:
                    member.kill()
                    member.wait()

    def test_undrained_job_is_failed_whatever_the_child_returned(self):
        subject = load_subject(self.subject)
        job_dir = self.state / "jobs" / self.pane_a / "20260101T000000Z-build-fedcba"
        job_dir.mkdir(parents=True)
        (job_dir / "job.log").touch()
        (job_dir / "meta.json").write_text(json.dumps(
            {"id": job_dir.name, "pane": self.pane_a, "profile": "build", "name": "build",
             "cwd": str(self.repo), "created_at": "2026-01-01T00:00:00Z",
             "created_epoch": time.time(), "status": "starting", "exit_code": None}))
        spec = {"id": job_dir.name, "cwd": str(self.repo), "args": ["--", "probe"]}

        def undrained(child, job, members, marks):
            child.wait()
            return False

        with mock.patch.dict(os.environ, self.env(self.pane_a), clear=True), \
                mock.patch.object(subject.sys, "stdin", io.StringIO(json.dumps(spec))), \
                mock.patch.object(subject.signal, "signal"), \
                mock.patch.object(subject, "anchor_tree", lambda: None), \
                mock.patch.object(subject, "_drain_tree", undrained):
            self.assertEqual(subject.supervise(), 0)
        self.assertEqual((job_dir / "job.log").read_text(), "ir-build probe\n")
        status = self.status(job_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 125))
        self.assertIn("outlived SIGKILL", status["reason"])
        waited = self.jobs(["wait", "--quiet", job_dir.name])
        self.assertEqual(waited.returncode, 125, waited.stderr)


class WindowsDrainFailsClosed(unittest.TestCase):
    """The job-object drain counts a failed job query as live, not as empty."""

    class Kernel32:
        def __init__(self, active):
            self.active, self.queries = active, 0

        def TerminateJobObject(self, job, code):
            return 1

        def QueryInformationJobObject(self, job, info_class, info, size, returned):
            self.queries += 1
            if self.active is None:
                return 0
            info._obj.active = self.active
            return 1

    def test_drain_is_true_only_for_a_job_read_as_empty(self):
        subject = load_subject()
        for active, drained in ((0, True), (2, False), (None, False)):
            with self.subTest(active=active):
                kernel32 = self.Kernel32(active)
                child = subprocess.Popen([sys.executable, "-c", "pass"])
                with mock.patch.object(subject, "IS_WINDOWS", True), \
                        mock.patch.object(subject, "_kernel32", lambda: kernel32), \
                        mock.patch.object(subject, "CANCEL_GRACE", 0.5), \
                        mock.patch.object(subject, "POLL", 0.05):
                    self.assertIs(subject._drain_tree(child, object(), [], None), drained)
                self.assertIsNotNone(child.returncode, "the drain left the child unreaped")
                if not drained:
                    self.assertGreater(kernel32.queries, 1, "the drain gave up before its grace")


@unittest.skipUnless(os.name == "nt", "native-Windows job-object arm")
class WindowsContainment(unittest.TestCase):
    def test_child_starts_nothing_outside_its_job(self):
        subject = load_subject()
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = Path(tmp) / "descendant"
            child = subject.spawn_suspended([sys.executable, "-c", SPAWNER, str(pidfile)],
                                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                            env={**os.environ, "ORPHAN": ORPHAN})
            # Long enough for a running child to start its descendant and exit
            # before job assignment.
            time.sleep(3)
            self.assertFalse(pidfile.exists(), "the child ran before it was in its job")
            job = subject.contain(child)
            self.assertEqual(child.wait(timeout=30), 0)
            descendant = int(pidfile.read_text())
            self.assertIsNotNone(subject.identity(descendant))
            subject._drain_tree(child, job, [], None)
            survivor = subject.identity(descendant)
            if survivor:
                subprocess.run(["taskkill", "/F", "/PID", str(descendant)], capture_output=True)
            self.assertIsNone(survivor, f"descendant {descendant} outlived the job drain")


class BreakawayRefusal(JobsCase):
    """A start whose supervisor cannot leave the caller's job fails closed."""

    def test_refused_breakaway_is_not_retried_and_records_failed(self):
        subject = load_subject(self.subject)
        breakaway, calls = 0x01000000, []

        def popen(argv, **kwargs):
            calls.append(kwargs.get("creationflags", 0))
            if calls[-1] & breakaway:
                exc = PermissionError(13, "Access is denied")
                exc.winerror = 5
                raise exc
            raise AssertionError("supervisor relaunched without CREATE_BREAKAWAY_FROM_JOB")

        # The patched-out admission_lock is what creates the pane directory.
        (self.state / "jobs" / self.pane_a).mkdir(parents=True)
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, self.env(self.pane_a), clear=True), \
                mock.patch.object(subject, "IS_WINDOWS", True), \
                mock.patch.object(subject, "admission_lock",
                                  lambda root: contextlib.nullcontext()), \
                mock.patch.object(subject.subprocess, "DETACHED_PROCESS", 0x8, create=True), \
                mock.patch.object(subject.subprocess, "CREATE_NEW_PROCESS_GROUP", 0x200,
                                  create=True), \
                mock.patch.object(subject.subprocess, "Popen", popen), \
                mock.patch.object(subject, "pane_name", lambda cwd: self.pane_a), \
                contextlib.redirect_stderr(stderr), \
                self.assertRaises(SystemExit) as exited:
            subject.cmd_start(["build", "--", "x"], self.repo)
        self.assertEqual(exited.exception.code, 1)
        self.assertEqual(len(calls), 1, "the refused launch was retried")
        self.assertTrue(calls[0] & breakaway)
        self.assertIn("forbids breakaway", stderr.getvalue())
        [job_dir] = self.job_dirs()
        status = self.status(job_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 127))
        self.assertIn("forbids breakaway", status["reason"])
        waited = self.jobs(["wait", job_dir.name])
        self.assertEqual(waited.returncode, 127, waited.stderr)


@posix_only
class StartRefusedBySupervisor(JobsCase):
    """A supervisor that refuses its job is a failed start, not a started job."""

    def refused_start(self, args, **patches):
        """cmd_start(args) with the real supervise() run in-process under patches.

        Returns (stderr, the job dir, the argvs launch was called with).
        """
        subject = load_subject(self.subject)
        ran = []

        def launch(argv, **kwargs):
            ran.append(argv)
            raise OSError("probe: the child was launched")

        class Supervisor:
            # Runs the real supervise() in-process, once the spec is written.
            def __init__(self, *args, **kwargs):
                self.stdin, self.spec, self.returncode = self, b"", None

            def write(self, data):
                self.spec += data

            def close(self):
                spec = io.StringIO(self.spec.decode())
                with contextlib.ExitStack() as stack:
                    for name, value in {"launch": launch, **patches}.items():
                        stack.enter_context(mock.patch.object(subject, name, value))
                    stack.enter_context(mock.patch.object(subject.sys, "stdin", spec))
                    stack.enter_context(mock.patch.object(subject.signal, "signal"))
                    self.returncode = subject.supervise()

            def poll(self):
                return self.returncode

        real_popen = subprocess.Popen

        def popen(argv, *args, **kwargs):
            if "_supervise" in argv:
                return Supervisor()
            return real_popen(argv, *args, **kwargs)

        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, self.env(self.pane_a), clear=True), \
                mock.patch.object(subject, "DisclaimedSupervisor", Supervisor), \
                mock.patch.object(subject.subprocess, "Popen", popen), \
                mock.patch.object(subject, "pane_name", lambda cwd: self.pane_a), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), \
                self.assertRaises(SystemExit) as exited:
            subject.cmd_start(args, self.repo)
        self.assertEqual(exited.exception.code, 1)
        self.assertEqual(stdout.getvalue(), "", "a refused job was reported started")
        [job_dir] = self.job_dirs()
        status = self.status(job_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 127))
        waited = self.jobs(["wait", job_dir.name])
        self.assertEqual(waited.returncode, 127, waited.stderr)
        return stderr.getvalue(), job_dir, ran

    def test_anchor_failure_fails_the_start(self):
        stderr, _, ran = self.refused_start(["build", "--", "x"],
                                            anchor_tree=lambda: "probe refusal")
        self.assertIn("not started: cannot contain the job: probe refusal", stderr)
        self.assertEqual(ran, [], "the child ran unanchored")

    def test_unreadable_own_identity_fails_the_start(self):
        real = load_subject(self.subject).identity
        stderr, job_dir, ran = self.refused_start(
            ["render-verify", "--", "--target", "IRAnalyticOracle"],
            anchor_tree=lambda: None,
            identity=lambda pid: None if pid == os.getpid() else real(pid))
        self.assertEqual(ran, [], "the child ran under a supervisor no reader can verify")
        self.assertIn("not started: supervisor cannot read its own start identity", stderr)
        self.assertNotIn("supervisor", json.loads((job_dir / "meta.json").read_text()))
        self.assertEqual((job_dir / "job.log").read_bytes(), b"")


@unittest.skipUnless(sys.platform == "darwin", "macOS responsible-process anchor")
class Unanchored(JobsCase):
    def test_supervisor_not_spawned_disclaimed_never_runs_the_child(self):
        job_dir = self.state / "jobs" / self.pane_a / "20260101T000000Z-build-abcdef"
        job_dir.mkdir(parents=True)
        (job_dir / "job.log").touch()
        (job_dir / "meta.json").write_text(json.dumps(
            {"id": job_dir.name, "pane": self.pane_a, "profile": "build", "name": "build",
             "cwd": str(self.repo), "created_at": "2026-01-01T00:00:00Z",
             "created_epoch": time.time(), "status": "starting", "exit_code": None}))
        spec = {"id": job_dir.name, "cwd": str(self.repo), "args": ["--", "probe"]}
        # A plain subprocess, unlike cmd_start's launch, keeps the caller's
        # responsible process.
        result = self.jobs(["_supervise"], stdin=json.dumps(spec))
        self.assertEqual(result.returncode, 127, result.stderr)
        status = self.status(job_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 127))
        self.assertIn("cannot contain the job", status["reason"])
        self.assertEqual((job_dir / "job.log").read_bytes(), b"", "the child ran unanchored")


@unittest.skipUnless(os.name == "nt", "native-Windows job-object arm")
class WindowsBreakaway(JobsCase):
    def test_start_inside_a_no_breakaway_job_refuses_instead_of_dying_with_it(self):
        subject = load_subject()
        # A fresh job object, which does not allow breakaway, holds the
        # launcher from before it first runs.
        launcher = subject.spawn_suspended(
            [sys.executable, str(self.subject), "start", "build", "--", "x"], cwd=self.repo,
            env=self.env(self.pane_a), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        enclosing = subject.contain(launcher)
        try:
            _, err = launcher.communicate(timeout=60)
        finally:
            subject._kernel32().TerminateJobObject(enclosing, 1)
            subject._kernel32().CloseHandle(enclosing)
        self.assertEqual(launcher.returncode, 1, err)
        self.assertIn("forbids breakaway", err)
        [job_dir] = self.job_dirs()
        status = self.status(job_dir.name)
        self.assertEqual((status["status"], status["exit_code"]), ("failed", 127))


@posix_only
class Isolation(JobsCase):
    def test_panes_cannot_see_or_touch_each_other(self):
        release = self.root / "release"
        job = self.start(["build", "--", "hold", str(release)])
        meta = next(self.state.glob(f"jobs/{self.pane_a}/{job}/meta.json"))
        before = meta.read_bytes()
        listed = self.jobs(["list"], pane=self.pane_b)
        self.assertEqual((listed.returncode, listed.stdout), (0, ""))
        for verb in (["status", job], ["wait", "--timeout", "1", job], ["kill", job]):
            with self.subTest(verb=verb[0]):
                result = self.jobs(verb, pane=self.pane_b)
                self.assertEqual(result.returncode, 2)
                self.assertIn(f"no job {job} in this pane", result.stderr)
        self.assertEqual(meta.read_bytes(), before)
        self.assertFalse((meta.parent / "cancel").exists())
        self.assertEqual([p.name for p in self.state.glob("jobs/*")], [self.pane_a])
        own = self.jobs(["list"]).stdout
        self.assertIn(job, own)
        # Concurrent readers always parse a complete status document.
        readers = [subprocess.Popen([sys.executable, str(self.subject), "status", "--json", job],
                                    cwd=self.repo, env=self.env(self.pane_a),
                                    stdout=subprocess.PIPE, text=True) for _ in range(12)]
        release.touch()
        readers += [subprocess.Popen([sys.executable, str(self.subject), "list", "--json"],
                                     cwd=self.repo, env=self.env(self.pane_a),
                                     stdout=subprocess.PIPE, text=True) for _ in range(12)]
        for reader in readers:
            out, _ = reader.communicate(timeout=60)
            self.assertEqual(reader.returncode, 0)
            for line in out.splitlines():
                self.assertEqual(json.loads(line)["id"], job)
        self.assertEqual(self.jobs(["wait", "--quiet", job]).returncode, 7)
        live = Path(os.path.expanduser("~")) / ".fleet/state/jobs"
        for pane in (self.pane_a, self.pane_b):
            self.assertFalse((live / pane).exists())

    def test_pane_falls_back_to_registered_worktree_only(self):
        game = init_repo(self.main / "creations/game")
        for worktree in (self.clone(self.checkout("pool-zz")),
                         self.clone(game / ".claude/worktrees/pool-zg", main=game)):
            with self.subTest(registered=worktree):
                result = subprocess.run([sys.executable, str(self.subject), "start",
                                         "fleet-tests"], cwd=worktree, env=self.env(None),
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                job = result.stdout.split()[1]
                self.assertTrue((self.state / "jobs" / worktree.name / job).is_dir())
        unregistered = self.lookalike(self.root / "plain")
        for pane in (None, self.pane_a):
            with self.subTest(assigned=pane):
                result = subprocess.run([sys.executable, str(self.subject), "list"],
                                        cwd=unregistered, env=self.env(pane),
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 2)
                self.assertIn("no pane", result.stderr)

    def test_lookalike_checkout_cannot_select_a_pane(self):
        release = self.root / "release"
        job = self.start(["build", "--", "hold", str(release)], pane=self.pane_b)
        meta = self.state / "jobs" / self.pane_b / job / "meta.json"
        before = meta.read_bytes()
        worktrees = ".claude/worktrees"
        # The other pane's worktree admin dir, borrowed through a .git file.
        borrowed = self.main / worktrees / "pool-zb"
        borrowed.mkdir(parents=True)
        admin = git(self.checkout(self.pane_b), "rev-parse", "--absolute-git-dir")
        (borrowed / ".git").write_text(f"gitdir: {admin}\n")
        other = init_repo(self.root / "other")
        lookalikes = {
            "standalone repository elsewhere": self.lookalike(
                self.root / "elsewhere" / worktrees / self.pane_b),
            "standalone repository under the engine": self.lookalike(
                self.main / worktrees / "pool-zs"),
            "borrowed .git file": borrowed,
            "worktree of another repository": self.clone(other / worktrees / "pool-zo",
                                                         main=other),
        }
        for case, cwd in lookalikes.items():
            for verb in (["list"], ["status", job], ["kill", job],
                         ["start", "build", "--", "plain"]):
                with self.subTest(case=case, verb=verb[0]):
                    result = subprocess.run([sys.executable, str(self.subject), *verb],
                                            cwd=cwd, env=self.env(None), capture_output=True,
                                            text=True, timeout=60)
                    self.assertEqual((result.returncode, result.stdout), (2, ""))
                    self.assertIn("is not a registered worktree", result.stderr)
        self.assertEqual(meta.read_bytes(), before)
        self.assertFalse((meta.parent / "cancel").exists())
        self.assertEqual([p.name for p in self.state.glob("jobs/*")], [self.pane_b])
        release.touch()
        self.assertEqual(self.jobs(["wait", "--quiet", job], pane=self.pane_b).returncode, 7)

    def test_assignment_cannot_address_another_panes_jobs(self):
        release = self.root / "release"
        job = self.start(["build", "--", "hold", str(release)], pane=self.pane_b)
        meta = self.state / "jobs" / self.pane_b / job / "meta.json"
        before = meta.read_bytes()
        # Pane A's checkout, assigned pane B by path and by bare name.
        for assigned in (str(self.checkout(self.pane_b)), self.pane_b):
            env = dict(self.env(self.pane_a), FLEET_ASSIGNED_WORKTREE=assigned)
            for verb in (["list"], ["status", job], ["wait", "--timeout", "1", job],
                         ["kill", job], ["start", "build", "--", "plain"]):
                with self.subTest(assigned=assigned, verb=verb[0]):
                    result = subprocess.run([sys.executable, str(self.subject), *verb],
                                            cwd=self.repo, env=env, capture_output=True,
                                            text=True, timeout=60)
                    self.assertEqual((result.returncode, result.stdout), (2, ""))
                    self.assertIn(f"is not this checkout's pane {self.pane_a!r}",
                                  result.stderr)
        self.assertEqual(meta.read_bytes(), before)
        self.assertFalse((meta.parent / "cancel").exists())
        self.assertEqual([p.name for p in self.state.glob("jobs/*")], [self.pane_b])
        release.touch()
        self.assertEqual(self.jobs(["wait", "--quiet", job], pane=self.pane_b).returncode, 7)


@posix_only
class IdentitySafety(JobsCase):
    def forge(self, job, supervisor):
        meta_path = self.state / "jobs" / self.pane_a / job / "meta.json"
        meta = json.loads(meta_path.read_text())
        meta.update(status="running", exit_code=None, supervisor=supervisor,
                    child={"pid": supervisor["pid"], "identity": supervisor["identity"]})
        meta_path.write_text(json.dumps(meta))

    def test_recycled_or_unverifiable_pid_is_lost_and_never_signalled(self):
        subject = load_subject()
        victim = subprocess.Popen(["sleep", "60"])
        self.addCleanup(victim.wait)
        self.addCleanup(victim.kill)
        real = subject.identity(victim.pid)
        self.assertTrue(real)
        job = self.start(["build", "--", "plain"])
        self.assertEqual(self.jobs(["wait", "--quiet", job]).returncode, 0)
        for label, forged in (("recycled", "darwin:1.000000" if real.startswith("darwin")
                               else "linux:1"),
                              ("unverifiable", None)):
            with self.subTest(label):
                self.forge(job, {"pid": victim.pid, "identity": forged})
                self.assertEqual(self.status(job)["status"], "lost")
                killed = self.jobs(["kill", job])
                self.assertEqual(killed.returncode, 1)
                self.assertIn("nothing signalled", killed.stderr)
                waited = self.jobs(["wait", "--quiet", job])
                self.assertEqual(waited.returncode, 1)
                self.assertIn("lost", waited.stderr)
                self.assertIsNone(victim.poll(), "an unrelated live process was signalled")
                self.assertEqual(subject.identity(victim.pid), real)

    def test_identity_probe_arm(self):
        subject = load_subject()
        prefix = "darwin:" if sys.platform == "darwin" else "linux:"
        own = subject.identity(os.getpid())
        self.assertTrue(own and own.startswith(prefix), own)
        self.assertEqual(subject.identity(os.getpid()), own)
        child = subprocess.Popen(["true"])
        # Exited but unreaped: a zombie is not a live job process.
        os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOWAIT)
        self.assertIsNone(subject.identity(child.pid))
        child.wait()
        self.assertIsNone(subject.identity(child.pid))


@posix_only
class ProfileBoundary(JobsCase):
    REJECTED = (
        ["start"],
        ["start", "--", "python3", "-c", "print(1)"],
        ["start", "python3", "-c", "print(1)"],
        ["start", "git", "commit", "-m", "x"],
        ["start", "git", "push", "origin", "HEAD"],
        ["start", "bash", "-c", "true"],
        ["start", "sh -c true"],
        ["start", "./build"],
        ["start", "/bin/sh"],
        ["start", "engine/tools/bin/ir-build"],
        ["start", "nope"],
        ["start", "build", "--target", "x"],
        ["start", "build", "--only", "x", "--", "a"],
        ["start", "build", "--name", "../x", "--", "a"],
        ["start", "build", "--name=", "--", "a"],
        ["start", "fleet-tests", "--target", "x"],
        ["start", "fleet-tests", "--", "bash", "-c", "true"],
        ["start", "fleet-tests", "--only"],
        ["start", "fleet-tests", "--only="],
        ["start", "render-verify", "--target", "x"],
        ["start", "render-verify", "--", "--build-dir", "/tmp"],
        ["start", "render-verify", "--", "--build-dir=/tmp"],
        ["start", "render-verify", "--", "--target", "../x"],
        ["start", "render-verify", "--", "--target"],
        ["start", "render-verify", "--", "--only", "x"],
        ["start", "render-verify", "--", "--update-references", "--force"],
        ["start", "render-verify", "--", "/tmp/evil.py"],
        ["status", "../../etc"],
        ["frobnicate"],
    )

    def test_rejected_forms_exit_nonzero_without_state(self):
        for args in self.REJECTED:
            with self.subTest(args=args):
                result = self.jobs(args)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertEqual(self.job_dirs(), [])

    def test_supervisor_revalidates_its_spec(self):
        job_dir = self.state / "jobs" / self.pane_a / "20260101T000000Z-build-abcdef"
        job_dir.mkdir(parents=True)
        (job_dir / "meta.json").write_text(json.dumps(
            {"id": job_dir.name, "profile": "python3", "status": "starting"}))
        marker = self.root / "escaped"
        spec = {"id": job_dir.name, "cwd": str(self.repo),
                "args": ["-c", f"open({str(marker)!r}, 'w')"]}
        result = self.jobs(["_supervise"], stdin=json.dumps(spec))
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown profile", result.stderr)
        self.assertFalse(marker.exists())
        self.assertFalse((job_dir / "job.log").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
