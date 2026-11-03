"""Tests for fleet-media-push (publish a screenshot dir to the orphan media branch).

Pins the contract attach-screenshots relies on:
  - the printed SHA is the remote media tip and carries the files at the
    same docs/pr-screenshots/ path; other branches' directories survive
  - the worktree, index and HEAD are untouched
  - republishing identical files makes no commit and prints the current tip
  - a push rejected because another worker advanced media is rebuilt on the
    new tip, keeping both workers' directories
  - --exists answers 0 / 1 from the remote branch
  - paths outside docs/pr-screenshots/ and a missing media branch are usage errors
"""
import importlib.machinery
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / "fleet-media-push"


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


def load_tool():
    loader = importlib.machinery.SourceFileLoader("fleet_media_push", str(TOOL))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class MediaPushTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.remote = base / "remote.git"
        git(base, "init", "--quiet", "--bare", str(self.remote))
        self.seed = self.clone("seed", init=True)
        self.work = self.clone("work")

    def tearDown(self):
        self.tmp.cleanup()

    def clone(self, name, init=False):
        path = Path(self.tmp.name) / name
        if init:
            git(self.tmp.name, "init", "--quiet", "-b", "master", str(path))
            git(path, "remote", "add", "origin", str(self.remote))
        else:
            git(self.tmp.name, "clone", "--quiet", str(self.remote), str(path))
        git(path, "config", "user.name", "t")
        git(path, "config", "user.email", "t@example.com")
        if init:
            (path / ".gitignore").write_text("/docs/pr-screenshots/\n")
            git(path, "add", ".gitignore")
            git(path, "commit", "--quiet", "-m", "master")
            git(path, "push", "--quiet", "origin", "master")
            git(path, "checkout", "--quiet", "--orphan", "media")
            git(path, "rm", "--quiet", "-r", "--cached", ".")
            self.write(path, "docs/pr-screenshots/older/a-after.png", b"older")
            git(path, "add", "-f", "docs/pr-screenshots/older")
            git(path, "commit", "--quiet", "-m", "seed")
            git(path, "push", "--quiet", "origin", "media")
            git(path, "checkout", "--quiet", "-f", "master")
        return path

    @staticmethod
    def write(root, rel, data):
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def run_tool(self, *args, cwd=None):
        return subprocess.run([sys.executable, str(TOOL), *args], cwd=cwd or self.work,
                              capture_output=True, text=True)

    def media_files(self, ref="media"):
        return set(git(self.remote, "ls-tree", "-r", "--name-only", ref).splitlines())

    def test_publish_prints_the_remote_tip_and_keeps_other_dirs(self):
        self.write(self.work, "docs/pr-screenshots/claude/x/shot-after.png", b"after")
        self.write(self.work, "docs/pr-screenshots/claude/x/nested/README.md", b"notes")
        head, status = git(self.work, "rev-parse", "HEAD"), git(self.work, "status", "--porcelain")
        result = self.run_tool("docs/pr-screenshots/claude/x")
        self.assertEqual(result.returncode, 0, result.stderr)
        sha = result.stdout.strip()
        self.assertEqual(sha, git(self.remote, "rev-parse", "media"))
        self.assertEqual(self.media_files(), {
            "docs/pr-screenshots/older/a-after.png",
            "docs/pr-screenshots/claude/x/shot-after.png",
            "docs/pr-screenshots/claude/x/nested/README.md"})
        shot = f"{sha}:docs/pr-screenshots/claude/x/shot-after.png"
        self.assertEqual(git(self.remote, "show", shot), "after")
        self.assertEqual(git(self.work, "rev-parse", "HEAD"), head)
        self.assertEqual(git(self.work, "status", "--porcelain"), status)

    def test_republishing_identical_files_makes_no_commit(self):
        self.write(self.work, "docs/pr-screenshots/claude/x/shot-after.png", b"after")
        first = self.run_tool("docs/pr-screenshots/claude/x").stdout.strip()
        second = self.run_tool("docs/pr-screenshots/claude/x")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout.strip(), first)
        self.assertEqual(git(self.remote, "rev-parse", "media"), first)

    def test_rejected_push_is_rebuilt_on_the_new_tip(self):
        tool = load_tool()
        racer = self.clone("racer")
        self.write(racer, "docs/pr-screenshots/claude/racer/r.png", b"racer")
        self.write(self.work, "docs/pr-screenshots/claude/x/shot-after.png", b"after")
        real_fetch, calls = tool.fetch_tip, []

        def fetch_then_race(root, remote, branch):
            tip = real_fetch(root, remote, branch)
            if not calls:
                subprocess.run([sys.executable, str(TOOL), "docs/pr-screenshots/claude/racer"],
                               cwd=racer, check=True, capture_output=True)
            calls.append(tip)
            return tip

        tool.fetch_tip = fetch_then_race
        sha = tool.publish(self.work.resolve(), "origin", "media",
                           str(self.work / "docs/pr-screenshots/claude/x"))
        self.assertEqual(len(calls), 2)
        self.assertNotEqual(calls[0], calls[1])
        self.assertEqual(sha, git(self.remote, "rev-parse", "media"))
        self.assertEqual(git(self.remote, "rev-parse", f"{sha}^"), calls[1])
        self.assertIn("docs/pr-screenshots/claude/racer/r.png", self.media_files())
        self.assertIn("docs/pr-screenshots/claude/x/shot-after.png", self.media_files())

    def test_exists_reads_the_remote_branch(self):
        self.write(self.work, "docs/pr-screenshots/claude/x/shot-after.png", b"after")
        self.assertEqual(self.run_tool("--exists", "docs/pr-screenshots/claude/x").returncode, 1)
        self.assertEqual(self.run_tool("--exists", "docs/pr-screenshots/older").returncode, 0)
        self.run_tool("docs/pr-screenshots/claude/x")
        self.assertEqual(self.run_tool("--exists", "docs/pr-screenshots/claude/x").returncode, 0)

    def test_usage_errors(self):
        self.write(self.work, "elsewhere/shot.png", b"x")
        outside = self.run_tool("elsewhere")
        self.assertEqual(outside.returncode, 2)
        self.assertIn("not under docs/pr-screenshots/", outside.stderr)
        empty = self.work / "docs/pr-screenshots/claude/empty"
        empty.mkdir(parents=True)
        self.assertEqual(self.run_tool("docs/pr-screenshots/claude/empty").returncode, 2)
        self.write(self.work, "docs/pr-screenshots/claude/x/shot.png", b"x")
        missing = self.run_tool("--branch", "no-such-branch", "docs/pr-screenshots/claude/x")
        self.assertEqual(missing.returncode, 2)
        self.assertIn("media branch", missing.stderr)


if __name__ == "__main__":
    unittest.main()
