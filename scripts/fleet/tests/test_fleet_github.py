"""fleet_github: the shared GitHub CLI resolver and its per-attempt accounting.

Hermetic: every `gh` here is a stub in a temp dir (on native Windows, the
`gh.bat` + extensionless-Python twin lib_hermetic.sh describes), every event
root is a temp dir, and each test hands the resolver an explicit env dict, so
nothing reads the caller's PATH, ~/.fleet, or GitHub.
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

FLEET_DIR = Path(__file__).resolve().parent.parent
SUBJECT = FLEET_DIR / "fleet_github.py"
if not SUBJECT.is_file():
    print("SKIP: fleet_github.py absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(FLEET_DIR))
import fleet_github  # noqa: E402

STUB_BODY = r'''
import json, os, sys
tag = os.path.basename(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.buffer.write(("OUT %s " % tag).encode()
                        + json.dumps(sys.argv[1:]).encode() + b" \xc3\xa9\x00\n")
sys.stderr.buffer.write(b"ERR \xe2\x9c\x93\n")
sys.exit(int(os.environ.get("STUB_EXIT", "0")))
'''

SECRETS = ("secret-owner", "secret-repo", "4242", "TITLE-SECRET", "BODY-SECRET",
           "https://github.com/secret-owner/secret-repo/issues/4242", "ghp_TOKENSECRET")


def make_gh(directory, body=STUB_BODY):
    """An executable `gh` in `directory`; returns the path `shutil.which` finds."""
    d = Path(directory)
    d.mkdir(parents=True, exist_ok=True)
    script = d / "gh"
    # An absolute interpreter: tests narrow PATH to the stub dirs alone.
    shebang = "#!/usr/bin/env python3\n" if os.name == "nt" else f"#!{sys.executable}\n"
    script.write_text(shebang + body, encoding="utf-8", newline="\n")
    script.chmod(0o755)
    if os.name == "nt":
        bat = d / "gh.bat"
        bat.write_text('@"%s" "%%~dp0gh" %%*\r\n' % sys.executable, encoding="utf-8")
        return str(bat)
    return str(script)


def events_in(root):
    root = Path(root)
    if not root.is_dir():
        return []
    return sorted(p for p in root.rglob("*") if p.is_file())


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.real = make_gh(self.root / "real")
        self.stub = make_gh(self.root / "stub")
        self.launcher_dir = self.root / "launcher"
        self.launcher = make_gh(self.launcher_dir)
        self.events = self.root / "events"
        fleet_github._warned.clear()

    def tearDown(self):
        self.tmp.cleanup()

    def env(self, *path_dirs, accounting=True, **extra):
        env = {"PATH": os.pathsep.join(str(d) for d in path_dirs)}
        if accounting:
            env.update({
                "FLEET_GH_ACCOUNTING": "1", "FLEET_GH_REAL": self.real,
                "FLEET_GH_LAUNCHER": self.launcher, "FLEET_GH_EVENT_ROOT": str(self.events),
            })
        env.update(extra)
        return env


class Classification(unittest.TestCase):
    CASES = [
        (["pr", "view", "1"], ("graphql", "read")),
        (["pr", "list", "--json", "number"], ("graphql", "read")),
        (["issue", "view", "7", "--comments"], ("graphql", "read")),
        (["pr", "diff", "3"], ("graphql", "read")),
        (["issue", "edit", "2", "--add-label", "x"], ("graphql", "write")),
        (["pr", "comment", "2", "--body", "x"], ("graphql", "write")),
        (["issue", "create", "--title", "t"], ("graphql", "write")),
        (["pr", "frobnicate"], ("other", "unknown")),
        (["api", "graphql", "-f", "query=query { viewer { login } }"], ("graphql_api", "read")),
        (["api", "graphql", "-f", "query=mutation { x }"], ("graphql_api", "write")),
        (["api", "repos/o/r/pulls?per_page=100&page=2"], ("rest", "read")),
        (["api", "--paginate", "repos/o/r/issues"], ("rest", "read")),
        (["api", "-X", "PATCH", "repos/o/r/pulls/1", "--input", "-"], ("rest", "write")),
        (["api", "--method=DELETE", "repos/o/r/issues/1/labels/x"], ("rest", "write")),
        (["api", "repos/o/r/issues/1/labels", "-f", "labels[]=x"], ("rest", "write")),
        (["api", "-H", "Accept: x", "repos/o/r"], ("rest", "read")),
        (["auth", "token"], ("other", "unknown")),
        ([], ("other", "unknown")),
    ]

    def test_route_and_access(self):
        for args, want in self.CASES:
            with self.subTest(args=args):
                self.assertEqual(fleet_github.classify(args), want)

    def test_buckets_reconcile(self):
        seen = {fleet_github.bucket(*fleet_github.classify(a)) for a, _ in self.CASES}
        self.assertEqual(seen, set(fleet_github.BUCKETS))

    def test_refusal_then_rest_replay_is_two_attempts(self):
        rows = fleet_github.summarize([
            {"actor": "scout", **dict(zip(("route", "access"),
                                          fleet_github.classify(["issue", "view", "7"])))},
            {"actor": "scout", **dict(zip(("route", "access"),
                                          fleet_github.classify(["api", "repos/o/r/issues/7"])))},
        ])
        self.assertEqual(rows["scout"]["graphql_read"], 1)
        self.assertEqual(rows["scout"]["rest"], 1)
        self.assertEqual(rows["scout"]["total"], 2)


class Resolution(Base):
    def test_explicit_override_wins_and_is_never_counted(self):
        res = fleet_github.resolve("MY_GH_BIN", self.env(self.real_dir(), MY_GH_BIN=self.stub))
        self.assertFalse(res.accounted)
        self.assertEqual(Path(res.argv[-1]).stem, "gh")
        self.assertIn("stub", res.argv[-1])

    def real_dir(self):
        return Path(self.real).parent

    def test_path_prepended_stub_wins_uncounted(self):
        res = fleet_github.resolve(env=self.env(Path(self.stub).parent, self.real_dir()))
        self.assertFalse(res.accounted)
        self.assertIn("stub", res.argv[-1])

    def test_same_file_as_pinned_real_is_counted(self):
        res = fleet_github.resolve(env=self.env(self.real_dir()))
        self.assertTrue(res.accounted)
        self.assertIn("real", res.argv[-1])

    def test_launcher_first_is_counted_and_runs_the_pinned_real(self):
        res = fleet_github.resolve(env=self.env(self.launcher_dir, self.real_dir()))
        self.assertTrue(res.accounted)
        self.assertIn("real", res.argv[-1], "the pinned real runs, never the launcher itself")

    def test_python_path_starts_no_extra_interpreter(self):
        argv = fleet_github.argv(["pr", "view", "1"], env=self.env(self.real_dir()))
        want = fleet_github._launch_argv(self.real)
        self.assertEqual(argv[:len(want)], want)
        if os.name != "nt":
            self.assertEqual(argv[0], self.real, "argv[0] is gh itself, not python")

    def test_disabled_accounting_counts_nothing(self):
        res = fleet_github.resolve(env=self.env(self.real_dir(), accounting=False))
        self.assertFalse(res.accounted)

    def test_unreadable_pin_is_loud_not_silent(self):
        env = self.env(self.real_dir(), FLEET_GH_REAL=str(self.root / "gone" / "gh.exe"))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            res = fleet_github.resolve(env=env)
        self.assertFalse(res.accounted)
        self.assertIn("cannot compare", err.getvalue())
        self.assertIn("uncounted", err.getvalue())

    def test_missing_gh_is_not_found(self):
        res = fleet_github.resolve(env=self.env(self.root / "empty"))
        self.assertFalse(res.found)

    def test_launcher_view_skips_itself_when_accounting_is_off(self):
        env = self.env(self.launcher_dir, self.real_dir(), accounting=False,
                       FLEET_GH_LAUNCHER=self.launcher)
        res = fleet_github.resolve(env=env, skip_launcher=True)
        self.assertFalse(res.accounted)
        self.assertIn("real", res.argv[-1], "a disabled launcher delegates past its own entry")


class Events(Base):
    def test_privacy_only_metadata_is_written(self):
        args = ["issue", "create", "--repo", "secret-owner/secret-repo", "--title", "TITLE-SECRET",
                "--body", "BODY-SECRET https://github.com/secret-owner/secret-repo/issues/4242",
                "--token", "ghp_TOKENSECRET"]
        path = fleet_github.record(args, self.env(FLEET_ROLE="worker", FLEET_RUNTIME="claude"))
        text = Path(path).read_text(encoding="utf-8")
        for secret in SECRETS:
            self.assertNotIn(secret, text)
        event = json.loads(text)
        self.assertEqual(set(event), {"ts", "actor", "runtime", "route", "access"})
        self.assertEqual((event["actor"], event["runtime"], event["route"], event["access"]),
                         ("worker", "claude", "graphql", "write"))

    def test_actor_precedence(self):
        cases = [({"FLEET_GH_ACTOR": "scout", "FLEET_ROLE": "worker"}, "scout"),
                 ({"FLEET_ROLE": "opus-reviewer"}, "opus-reviewer"), ({}, "unknown")]
        for extra, want in cases:
            with self.subTest(want=want):
                self.assertEqual(fleet_github.actor_of(extra), want)

    def test_concurrent_writers_never_share_or_tear_a_file(self):
        code = ("import sys; sys.path.insert(0, sys.argv[1]); import fleet_github as g\n"
                "for _ in range(40): g.record(['pr', 'view', '1'], "
                "{'FLEET_GH_EVENT_ROOT': sys.argv[2], 'FLEET_ROLE': sys.argv[3]})\n")
        actors = ("worker", "sonnet-reviewer", "opus-reviewer", "unknown-ish", "architect")
        procs = [subprocess.Popen([sys.executable, "-c", code, str(FLEET_DIR), str(self.events), a])
                 for a in actors]
        for p in procs:
            self.assertEqual(p.wait(timeout=120), 0)
        files = events_in(self.events)
        self.assertEqual(len(files), 200)
        self.assertFalse([f for f in files if f.name.startswith(".")], "no temp file left behind")
        counts = {}
        for f in files:
            actor = json.loads(f.read_text(encoding="utf-8"))["actor"]
            counts[actor] = counts.get(actor, 0) + 1
        self.assertEqual(counts, dict.fromkeys(actors, 40))

    def test_reader_skips_temp_torn_and_foreign_files(self):
        import datetime as dt
        now = 1790000000.0
        fleet_github.record(["pr", "view", "1"], self.env(FLEET_ROLE="worker"), now=now)
        hour = next(p for p in self.events.iterdir())
        (hour / ".x.json.tmp").write_text('{"ts": "2026-09-21T14:13:20Z"}', encoding="utf-8")
        (hour / "torn.json").write_text('{"ts": "2026-09-21T14:1', encoding="utf-8")
        (hour / "notes.txt").write_text("x", encoding="utf-8")
        since = dt.datetime.fromtimestamp(now - 60, dt.timezone.utc)
        until = dt.datetime.fromtimestamp(now + 60, dt.timezone.utc)
        got = list(fleet_github.iter_events(self.events, since, until))
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0]["actor"], "worker")

    def test_reader_survives_a_shard_pruned_after_discovery(self):
        import datetime as dt
        import shutil
        from unittest import mock
        now = 1790000000.0
        for hours_ago in (72, 0):
            fleet_github.record(["pr", "view", "1"], self.env(FLEET_ROLE="worker"),
                                now=now - hours_ago * 3600)
        old = min(p for p in self.events.iterdir())
        real_iterdir = Path.iterdir

        def prune_then_list(path):
            # A concurrent writer's prune() lands between iter_events' is_dir()
            # and its listing of this shard.
            if path == old:
                shutil.rmtree(path)
            return real_iterdir(path)

        since = dt.datetime.fromtimestamp(now - 100 * 3600, dt.timezone.utc)
        until = dt.datetime.fromtimestamp(now + 60, dt.timezone.utc)
        with mock.patch.object(Path, "iterdir", autospec=True, side_effect=prune_then_list):
            got = list(fleet_github.iter_events(self.events, since, until))
        self.assertFalse(old.exists())
        self.assertEqual(len(got), 1)

    def test_retention_prunes_only_closed_old_hours(self):
        now = 1790000000.0
        for hours_ago in (0, 47, 49, 72):
            fleet_github.record(["pr", "view", "1"], self.env(), now=now - hours_ago * 3600)
        (self.events / "not-an-hour").mkdir()
        removed = fleet_github.prune(self.events, now=now, retention_hours=48)
        self.assertEqual(len(removed), 2)
        left = {p.name for p in self.events.iterdir()}
        self.assertIn("not-an-hour", left)
        self.assertEqual(len(events_in(self.events)), 2)

    def test_unwritable_root_warns_and_never_raises(self):
        blocker = self.root / "file-not-dir"
        blocker.write_text("x", encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertIsNone(fleet_github.record(["pr", "view", "1"],
                                                  {"FLEET_GH_EVENT_ROOT": str(blocker / "sub")}))
        self.assertIn("cannot write an accounting event", err.getvalue())


class Launcher(Base):
    """`fleet_github.py launch`: the shell launcher's core, as a child process."""

    def launch(self, env, *args, stub_exit="0"):
        full = {**os.environ, **env, "STUB_EXIT": stub_exit}
        for key in fleet_github.ACCOUNTING_ENV:
            if key not in env:
                full.pop(key, None)
        return subprocess.run([sys.executable, str(SUBJECT), "launch", *args], env=full,
                              capture_output=True, timeout=60)

    def direct(self, gh, *args, stub_exit="0"):
        return subprocess.run([*fleet_github._launch_argv(gh), *args],
                              env={**os.environ, "STUB_EXIT": stub_exit},
                              capture_output=True, timeout=60)

    def test_counted_launch_is_byte_transparent(self):
        args = ("api", "repos/o/r/pulls?per_page=100&page=2")
        got = self.launch(self.env(self.real_dir()), *args, stub_exit="7")
        want = self.direct(self.real, *args, stub_exit="7")
        self.assertEqual(got.returncode, 7)
        self.assertEqual(got.stdout, want.stdout)
        self.assertEqual(got.stderr, want.stderr)
        self.assertIn(b'"repos/o/r/pulls?per_page=100&page=2"', got.stdout)
        self.assertEqual(len(events_in(self.events)), 1)

    def real_dir(self):
        return Path(self.real).parent

    def test_stub_first_runs_the_stub_and_writes_nothing(self):
        got = self.launch(self.env(Path(self.stub).parent, self.real_dir()), "pr", "view", "1")
        self.assertEqual(got.returncode, 0)
        self.assertTrue(got.stdout.startswith(b"OUT stub "), got.stdout)
        self.assertEqual(events_in(self.events), [])

    def test_missing_gh_exits_127(self):
        got = self.launch(self.env(self.root / "empty"), "pr", "view", "1")
        self.assertEqual(got.returncode, 127)
        self.assertIn(b"gh: command not found", got.stderr)


if __name__ == "__main__":
    unittest.main()
