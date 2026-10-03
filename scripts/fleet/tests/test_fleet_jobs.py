"""fleet-jobs: detached survival, exact exit, pane isolation, identity safety,
and the fixed-profile boundary.

Every job runs a fixture: the subject is copied into a temp checkout whose
adjacent engine/tools/bin/ir-build, scripts/fleet/tests/run_all.sh and
scripts/render-verify.py are stubs, so the real profiles resolve fixtures.
State lives under a temp FLEET_STATE_DIR and HOME; the live ~/.fleet is never
read or written.
"""

import importlib.machinery
import importlib.util
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

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-jobs"
if not SUBJECT.is_file():
    print("SKIP: scripts/fleet/fleet-jobs absent", file=sys.stderr)
    sys.exit(3)
if os.name == "nt":
    # The native-Windows arm (creation-time identity, taskkill tree) is
    # covered by the Windows host smoke lane, not this POSIX harness.
    print("fleet-jobs suite: POSIX-only; Windows arm runs in host smoke", file=sys.stderr)
    sys.exit(0)

IR_BUILD = r"""#!/usr/bin/env bash
case "$1" in
  hold) echo "line one"; while [ ! -e "$2" ]; do sleep 0.05; done; echo "line two"; exit 7 ;;
  tree) sleep 300 & echo "$!" > "$2.tmp"; echo "$$" >> "$2.tmp"; mv "$2.tmp" "$2"; wait ;;
  *) echo "ir-build $*"; exit 0 ;;
esac
"""
RUN_ALL = '#!/usr/bin/env bash\necho "run_all $*"\nexit 0\n'
RENDER_VERIFY = r"""import os, sys, time
hold = [a[5:] for a in sys.argv if a.startswith("hold=")]
while hold and not os.path.exists(hold[0]):
    time.sleep(0.05)
print("render-verify", *sys.argv[1:])
sys.exit(3)
"""


def load_subject():
    loader = importlib.machinery.SourceFileLoader("fleet_jobs", str(SUBJECT))
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
        self.repo = self.root / "repo"
        (self.repo / "scripts/fleet/tests").mkdir(parents=True)
        (self.repo / "engine/tools/bin").mkdir(parents=True)
        shutil.copy2(SUBJECT, self.repo / "scripts/fleet/fleet-jobs")
        for rel, text in (("engine/tools/bin/ir-build", IR_BUILD),
                          ("scripts/fleet/tests/run_all.sh", RUN_ALL),
                          ("scripts/render-verify.py", RENDER_VERIFY)):
            (self.repo / rel).write_text(text)
            (self.repo / rel).chmod(0o755)
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        self.state = self.root / "state"
        self.subject = self.repo / "scripts/fleet/fleet-jobs"
        # Synthetic, unique pane names, so a check of the real ~/.fleet can
        # prove these panes never reached it.
        tag = secrets.token_hex(3)
        self.pane_a, self.pane_b = f"zz-test-a-{tag}", f"zz-test-b-{tag}"
        self.started = []

    def tearDown(self):
        for pane, job in self.started:
            self.jobs(["kill", job], pane=pane)
        self._tmp.cleanup()

    def env(self, pane):
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(("FLEET_", "IRREDEN_", "IR_"))}
        env.update(HOME=str(self.root / "home"), FLEET_STATE_DIR=str(self.state))
        if pane is not None:
            env["FLEET_ASSIGNED_WORKTREE"] = str(self.root / "wt" / pane)
        return env

    def jobs(self, args, pane=None, cwd=None, stdin=None):
        pane = self.pane_a if pane is None else pane
        return subprocess.run([sys.executable, str(self.subject), *args], cwd=cwd or self.repo,
                              env=self.env(pane), capture_output=True, text=True, timeout=60,
                              input=stdin)

    def start(self, args, pane=None):
        result = self.jobs(["start", *args], pane=pane)
        self.assertEqual(result.returncode, 0, result.stderr)
        job = result.stdout.split()[1]
        self.started.append((pane or self.pane_a, job))
        return job

    def status(self, job, pane=None):
        result = self.jobs(["status", "--json", job], pane=pane)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def job_dirs(self):
        return sorted(self.state.glob("jobs/*/*"))


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
        self.started.append((self.pane_a, job))
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
                self.assertEqual(waited.stdout.strip(), line)

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
        worktree = self.root / "clone/.claude/worktrees/pool-zz"
        shutil.copytree(self.repo, worktree, ignore=shutil.ignore_patterns(".git"))
        subprocess.run(["git", "init", "-q", str(worktree)], check=True)
        result = subprocess.run([sys.executable, str(worktree / "scripts/fleet/fleet-jobs"),
                                 "start", "fleet-tests"], cwd=worktree, env=self.env(None),
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(list(self.state.glob("jobs/pool-zz/*/meta.json")))
        unregistered = subprocess.run([sys.executable, str(self.subject), "list"],
                                      cwd=self.repo, env=self.env(None),
                                      capture_output=True, text=True, timeout=60)
        self.assertEqual(unregistered.returncode, 2)
        self.assertIn("no pane", unregistered.stderr)


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
