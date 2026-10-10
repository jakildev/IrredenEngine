"""Codex transport tests use synthetic events and never launch a paid model."""

import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import tomllib

if not (Path(__file__).resolve().parents[1] / "fleet_codex.py").is_file():
    print("SKIP: Codex adapter subject absent", file=sys.stderr)
    sys.exit(3)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fleet_codex as codex
import fleet_codex_doctor as doctor
import fleet_codex_policy as policy


class Transport(unittest.TestCase):
    def test_batch_role_prompts_and_target_guards(self):
        for role in codex.BATCH_ROLES:
            with self.subTest(role=role):
                text = codex.prompt(role, "live", "")
                self.assertIn(f"role-{role}.md", text)
                self.assertIn("has no dispatch target", text)
                self.assertNotIn("fleet-runtime stamp", text)
                self.assertNotIn("Do not discover", text)
        merger = codex.prompt("merger", "live", "merge:engine:1")
        self.assertIn("assigned target is merge:engine:1", merger)
        self.assertNotIn("fleet-runtime stamp", merger)

    def test_batch_role_run_accepts_no_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / ".claude/worktrees/pool-1"
            args = SimpleNamespace(prepare=False, check=False, doctor=False,
                                   role="epic-steward", model="gpt-5.6-sol",
                                   effort="medium", mode="live",
                                   resume="", interactive=False, print_launch=False)

            def launch(*_args, **_kwargs):
                return Mock(stdout=iter(['{"type":"turn.completed"}\n']),
                            wait=Mock(return_value=0), poll=Mock(return_value=0))

            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                    patch.object(codex, "prepare"), patch.object(codex, "probe"), \
                    patch.object(codex, "probe_display") as display, \
                    patch.object(codex, "prompt", return_value="batch prompt"), \
                    patch.object(codex.shutil, "which", return_value="codex"), \
                    patch.object(codex.subprocess, "Popen", side_effect=launch) as popen, \
                    patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(root / "state")},
                               clear=True):
                self.assertEqual(codex.run(args), 0)
                self.assertIn("-C", popen.call_args.args[0])
                display.assert_not_called()
            args.role = "worker"
            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(root / "state")},
                               clear=True):
                with self.assertRaisesRegex(ValueError, "explicit dispatch target"):
                    codex.run(args)

    def test_merger_run_requires_and_accepts_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / ".claude/worktrees/pool-1"
            args = SimpleNamespace(prepare=False, check=False, doctor=False, role="merger",
                                   model="gpt-5.6-terra", effort="medium", mode="live",
                                   resume="", interactive=False, print_launch=True)
            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                    patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(root / "state")},
                               clear=True):
                with self.assertRaisesRegex(ValueError, "explicit dispatch target"):
                    codex.run(args)
            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                    patch.object(codex, "prompt", return_value="merger prompt"), \
                    patch.object(codex.sys, "stdout", io.StringIO()), \
                    patch.dict(codex.os.environ,
                               {"FLEET_STATE_DIR": str(root / "state"),
                                "FLEET_DISPATCH_TARGET": "merge:engine:1"}, clear=True):
                self.assertEqual(codex.run(args), 0)

    def test_batch_role_policy_allows_only_bare_lease_force_push(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            merger = policy.prepare(root, "merger").read_text()
            self.assertNotIn('["git", "push", "--force-with-lease"]', merger)
            for token in ('["git", "push", "--force"]', '["git", "push", "-f"]',
                          '["git", "push", "--delete"]',
                          '["git", "push", "origin", "HEAD:master"]'):
                self.assertIn(token, merger)
            worker = policy.rules(root, "worker")
            self.assertIn('["git", "push", "--force-with-lease"]', worker)

    def test_probe_runs_real_write_rename_cleanup_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            result = subprocess.run([sys.executable, "-c", doctor.PROBE,
                                     json.dumps([str(root / "new-state")])],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)[0]["ok"])
            self.assertEqual(list((root / "new-state").iterdir()), [])
            (root / "file").write_text("must survive")
            result = subprocess.run([sys.executable, "-c", doctor.PROBE,
                                     json.dumps([str(root / "file")])],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(json.loads(result.stdout)[0]["ok"])
            self.assertEqual((root / "file").read_text(), "must survive")

    def test_probe_passes_launch_roots_and_rejects_sandbox_startup_failure(self):
        with patch.object(doctor.subprocess, "run",
                          return_value=Mock(stdout='[{"ok":true}]', returncode=0)) as run:
            doctor.probe(Path("/work"), ["/work", "/git with spaces"])
        argv = run.call_args.args[0]
        self.assertEqual(argv[:2], ["codex", "sandbox"])
        self.assertEqual(run.call_args.kwargs["cwd"], Path("/work"))
        self.assertIn('sandbox_workspace_write.writable_roots=["/work", "/git with spaces"]',
                      argv)
        for result in (Mock(stdout="", stderr="sandbox unavailable", returncode=71),
                       Mock(stdout='[{"ok":false,"reason":"permission denied"}]',
                            returncode=1)):
            with patch.object(doctor.subprocess, "run", return_value=result):
                with self.assertRaises(ValueError):
                    doctor.probe(Path("/work"), ["/work"])

    def test_failed_preflight_cools_provider_without_launching_model(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / ".claude/worktrees/pool-1"
            args = SimpleNamespace(prepare=False, check=False, doctor=False, role="worker",
                                   model="gpt-6-astra", effort="xhigh", mode="live",
                                   resume="existing-session", interactive=False, print_launch=False)
            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                    patch.object(codex, "prepare"), patch.object(codex, "prompt"), \
                    patch.object(codex, "probe", side_effect=ValueError("index.lock denied")), \
                    patch.object(codex.shutil, "which", return_value="codex"), \
                    patch.object(codex.subprocess, "Popen") as launch, \
                    patch.object(codex.sys, "stderr", io.StringIO()), \
                    patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(root / "state"),
                                                  "FLEET_DISPATCH_TARGET": "task:engine:901"},
                               clear=True):
                self.assertEqual(codex.run(args), 2)
                launch.assert_not_called()
            data = json.loads((root / "state/runtime-cooldown/codex.json").read_text())
            self.assertEqual(data["kind"], "permissions")
            self.assertIn("index.lock denied", data["reason"])
            self.assertEqual(data["worktree"], str(worktree))

    def _run_without_display(self, role):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / ".claude/worktrees/pool-1"
            args = SimpleNamespace(prepare=False, check=False, doctor=False, role=role,
                                   model="gpt-5.6-sol", effort="high", mode="live",
                                   resume="", interactive=False, print_launch=False)
            stderr = io.StringIO()

            def launch(*_args, **_kwargs):
                return Mock(stdout=iter(['{"type":"turn.completed"}\n']),
                            wait=Mock(return_value=0), poll=Mock(return_value=0))

            with patch.object(codex.Path, "cwd", return_value=worktree), \
                    patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                    patch.object(codex, "prepare"), patch.object(codex, "prompt"), \
                    patch.object(codex, "probe"), \
                    patch.object(codex, "probe_display",
                                 side_effect=ValueError("no display session")), \
                    patch.object(codex.shutil, "which", return_value="codex"), \
                    patch.object(codex.subprocess, "Popen", side_effect=launch) as popen, \
                    patch.object(codex.sys, "stderr", stderr), \
                    patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(root / "state"),
                                                  "FLEET_DISPATCH_TARGET": "task:engine:901"},
                               clear=True):
                rc = codex.run(args)
            cooldown = root / "state/runtime-cooldown/codex.json"
            data = json.loads(cooldown.read_text()) if cooldown.is_file() else None
            return rc, popen.called, stderr.getvalue(), data

    def test_missing_display_cools_provider_without_launching_model(self):
        for role in codex.DISPLAY_ROLES:
            with self.subTest(role=role):
                rc, launched, stderr, data = self._run_without_display(role)
                self.assertEqual(rc, 2)
                self.assertFalse(launched)
                self.assertIn("no display session", stderr)
                self.assertEqual(data["kind"], "display")

    def test_reviewer_roles_launch_without_a_display(self):
        for role in ("sonnet-reviewer", "opus-reviewer"):
            with self.subTest(role=role):
                rc, launched, _stderr, data = self._run_without_display(role)
                self.assertEqual(rc, 0)
                self.assertTrue(launched)
                self.assertIsNone(data)

    def test_every_role_is_classified_for_the_display_preflight(self):
        # A new role must state whether it launches demos.
        self.assertEqual(set(codex.ROLES),
                         {*codex.DISPLAY_ROLES, *codex.BATCH_ROLES,
                          "sonnet-reviewer", "opus-reviewer", "merger"})

    def test_display_probe_reads_online_displays_on_macos_only(self):
        self.assertIsNone(doctor.probe_display("linux"))
        with patch.object(doctor.subprocess, "run",
                          return_value=Mock(stdout="2\n", stderr="")) as run:
            self.assertEqual(doctor.probe_display("darwin"), 2)
        self.assertIn("CGGetOnlineDisplayList", run.call_args.args[0][-1])
        for stdout in ("0\n", "-1000\n", ""):
            with self.subTest(stdout=stdout), \
                    patch.object(doctor.subprocess, "run",
                                 return_value=Mock(stdout=stdout, stderr="")):
                with self.assertRaisesRegex(ValueError, "no display session.*--doctor"):
                    doctor.probe_display("darwin")

    def test_display_validators_run_outside_the_sandbox(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            text = policy.rules(root, "sonnet-reviewer")
            for pattern in (["python3", "scripts/render-verify.py"],
                            ["python3", str(root / "scripts/perf/repeat_profile.py")],
                            ["bash", "scripts/perf/perf_grid_matrix.sh"],
                            ["scripts/perf/perf_grid_matrix.sh"]):
                self.assertIn(f"pattern={json.dumps(pattern)}, decision=\"allow\"", text)
            self.assertNotIn('pattern=["python3"]', text)

    def test_fleet_jobs_gets_only_the_display_profile_rule(self):
        # A settings entry for fleet-jobs must not become a bare prefix allow:
        # that would run every profile's child outside the sandbox.
        settings = json.dumps({"permissions": {"allow": [
            "Bash(fleet-jobs:*)", "Bash(fleet-build:*)"]}})
        with tempfile.TemporaryDirectory() as temp, \
                patch.object(policy.Path, "read_text", return_value=settings):
            root = Path(temp).resolve()
            for role in ("worker", "sonnet-reviewer", "opus-reviewer", "merger"):
                with self.subTest(role=role):
                    text = policy.rules(root, role)
                    self.assertNotIn('pattern=["fleet-jobs"]', text)
                    self.assertIn('pattern=["fleet-jobs", "start", "render-verify"], '
                                  'decision="allow"', text)
                    self.assertEqual(text.count('"fleet-jobs"'), 1)
                    self.assertIn('pattern=["fleet-build"], decision="allow"', text)

    @unittest.skipUnless(shutil.which("codex"), "codex CLI absent; text-level case above")
    def test_fleet_jobs_profiles_keep_their_execpolicy_decisions(self):
        cases = (
            (["fleet-jobs", "start", "render-verify", "--", "--target", "IRShapeDebug"], "allow"),
            (["fleet-jobs", "start", "fleet-tests", "--only", "x"], None),
            (["fleet-jobs", "start", "build", "--", "--target", "IRShapeDebug"], None),
            (["fleet-jobs", "start", "--", "python3", "-c", "print(1)"], None),
            (["fleet-jobs", "start", "git", "push"], None),
            (["fleet-jobs", "wait", "20260101T000000Z-build-abcdef"], None),
            (["python3", "-c", "print(1)"], None))
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            for role in ("worker", "opus-reviewer"):
                path = policy.prepare(root, role)
                review = role == "opus-reviewer"
                for cmd, expected in cases + (
                        (["git", "commit", "-m", "x"], "forbidden" if review else "allow"),
                        (["git", "push", "origin", "HEAD:x"],
                         "forbidden" if review else "allow")):
                    with self.subTest(role=role, cmd=cmd):
                        result = subprocess.run(
                            ["codex", "execpolicy", "check", "--rules", str(path), "--", *cmd],
                            check=True, capture_output=True, text=True, timeout=15)
                        self.assertEqual(json.loads(result.stdout).get("decision"), expected)

    def test_display_validator_list_covers_every_fleet_run_driver(self):
        # A new driver that launches a demo through fleet-run times out on a
        # macOS Codex worker until it is named in DISPLAY_VALIDATORS.
        repo = Path(__file__).resolve().parents[3]
        launches = {".py": (r"^if __name__ == [\"']__main__[\"']", r"[\"']fleet-run[\"']"),
                    ".sh": (r"^\s*fleet-run\s",)}
        drivers = set()
        for path in (repo / "scripts").rglob("*"):
            rel = path.relative_to(repo).as_posix()
            if (path.suffix not in launches or rel.startswith("scripts/fleet/")
                    or path.name.startswith("test_")):
                continue
            text = path.read_text()
            if all(re.search(pattern, text, re.M) for pattern in launches[path.suffix]):
                drivers.add(rel)
        self.assertTrue(drivers)
        self.assertEqual(drivers, set(policy.DISPLAY_VALIDATORS))

    def test_stderr_quota_and_stdout_notice(self):
        for rc, error in ((1, "rate limit exceeded"), (0, "startup warning")):
            with self.subTest(rc=rc), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                worktree = root / ".claude/worktrees/pool-1"
                state = root / "state"
                args = SimpleNamespace(prepare=False, check=False, doctor=False, role="worker",
                                       model="gpt-6-astra", effort="xhigh", mode="live",
                                       resume="", interactive=False, print_launch=False)

                def launch(*_args, **kwargs):
                    kwargs["stderr"].write(error)
                    return Mock(stdout=iter(['notice\n', '{"type":"turn.completed"}\n']),
                                wait=Mock(return_value=rc), poll=Mock(return_value=rc))

                with patch.object(codex.Path, "cwd", return_value=worktree), \
                        patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                        patch.object(codex, "prepare"), patch.object(codex, "prompt"), \
                        patch.object(codex, "probe"), patch.object(codex, "probe_display"), \
                        patch.object(codex.shutil, "which", return_value="codex"), \
                        patch.object(codex.subprocess, "Popen", side_effect=launch), \
                        patch.object(codex.sys, "stderr", io.StringIO()), \
                        patch.dict(codex.os.environ, {"FLEET_STATE_DIR": str(state),
                                                      "FLEET_DISPATCH_TARGET": "task:engine:901"},
                                   clear=True):
                    self.assertEqual(codex.run(args), rc)
                self.assertEqual((state / "runtime-cooldown/codex.json").exists(), bool(rc))

    def test_write_roots_cover_claim_lifecycle_without_main_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / "repo/.claude/worktrees/pool-1"
            common = root / "repo/.git"
            gitdir = common / "worktrees/pool-1"
            # `git rev-parse --absolute-git-dir --git-common-dir` prints one path
            # per line; the linked worktree's own gitdir comes first.
            rev_parse = Mock(stdout=f"{gitdir}\n{common}\n")
            with patch.object(codex.Path, "home", return_value=root), \
                    patch.object(codex.subprocess, "run", return_value=rev_parse), \
                    patch.dict(codex.os.environ, {}, clear=True):
                roots = codex.writable_roots(worktree, root / ".fleet/state")
            self.assertIn(str(worktree), roots)
            self.assertIn(str(common), roots)
            # The sandbox protects the resolved gitdir of a linked worktree even
            # when its parent .git is a writable root; it must be named itself.
            self.assertIn(str(gitdir), roots)
            self.assertNotIn(str(root / "repo"), roots)
            for name in ("claims", "heartbeats", "molecules", "iteration-summaries", "handoff"):
                self.assertIn(str(root / ".fleet" / name), roots)
            self.assertIn(str(root / ".cache" / "gh"), roots)

    def test_write_roots_put_gh_cache_under_xdg_cache_home(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            worktree = root / "repo/.claude/worktrees/pool-1"
            common = root / "repo/.git"
            rev_parse = Mock(stdout=f"{common / 'worktrees/pool-1'}\n{common}\n")
            xdg = root / "xdg"
            with patch.object(codex.Path, "home", return_value=root), \
                    patch.object(codex.subprocess, "run", return_value=rev_parse), \
                    patch.dict(codex.os.environ, {"XDG_CACHE_HOME": str(xdg)}, clear=True):
                roots = codex.writable_roots(worktree, root / ".fleet/state")
            self.assertIn(str(xdg / "gh"), roots)
            self.assertNotIn(str(root / ".cache" / "gh"), roots)

    def test_wrapper_help(self):
        wrapper = Path(__file__).resolve().parents[1] / "fleet-codex"
        # Windows' CreateProcess searches System32 (the legacy WSL bash.exe stub,
        # a different filesystem namespace) before PATH, so a bare "bash" can
        # resolve to the wrong interpreter regardless of PATH order; shutil.which
        # walks PATH itself and finds the real MSYS2/Git-Bash.
        bash = shutil.which("bash") or "bash"
        result = subprocess.run([bash, str(wrapper), "--help"], capture_output=True,
                                text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--check", result.stdout)

    def test_policy_preserves_manual_edits_and_changes_role(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            path = policy.prepare(root, "worker")
            self.assertNotIn('pattern=["python3"]', path.read_text())
            policy.prepare(root, "opus-reviewer")
            self.assertIn('pattern=["fleet-pr-amend-push"], decision="forbidden"',
                          path.read_text())
            path.write_text(path.read_text() + "# my custom edit\n")
            with self.assertRaisesRegex(ValueError, "hand-authored"):
                policy.prepare(root, "worker")
            self.assertTrue(path.read_text().endswith("# my custom edit\n"))

    def test_policy_regenerates_after_upstream_rule_shape_changes(self):
        # A settings.json allow-list change alters rules() output for every
        # role at once; that must not read as hand-authored.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            path = policy.prepare(root, "worker")
            stale = path.read_text() + (
                'prefix_rule(pattern=["fleet-retired-tool"], decision="allow")\n')
            path.write_text(stale)
            policy.prepare(root, "worker")  # must not raise "hand-authored"
            self.assertNotIn("fleet-retired-tool", path.read_text())

    def test_architect_waits_for_human(self):
        self.assertIn("Wait for the human", codex.prompt("opus-architect", "live", ""))

    def test_launch_and_resume_flags(self):
        env = {"IR_QUIET_OWNER": "pool 1", "IR_LOCK_ROOT": "/locks with spaces"}
        with patch.dict(codex.os.environ, env, clear=True):
            fresh = codex.command("gpt-6-astra", "xhigh", "/work",
                                  ["/work", "/state"], "task")
            resumed = codex.command("gpt-6-astra", "xhigh", "/work",
                                    ["/work"], "task", "abc")
        self.assertEqual(fresh[:2], ["codex", "exec"])
        self.assertIn('approval_policy="never"', fresh)
        self.assertIn('sandbox_mode="workspace-write"', fresh)
        self.assertNotIn("--dangerously-bypass-approvals-and-sandbox", fresh)
        self.assertEqual(resumed[-3:], ["resume", "abc", "task"])
        self.assertNotIn("-C", resumed)
        for args in (fresh, resumed):
            self.assertEqual(args.count("--dangerously-bypass-hook-trust"), 1)
            overrides = [value for value in args if value.startswith("hooks.PreToolUse=")]
            self.assertEqual(len(overrides), 1)
            config = tomllib.loads(overrides[0])
            group = config["hooks"]["PreToolUse"][0]
            self.assertNotIn("matcher", group)
            hook = group["hooks"][0]
            self.assertEqual(hook["type"], "command")
            self.assertEqual(hook["timeoutSec"], 1500)
            self.assertIn("IR_QUIET_OWNER='pool 1'", hook["command"])
            self.assertIn("IR_LOCK_ROOT='/locks with spaces'", hook["command"])
            expected_path = f"{codex.ROOT / 'engine/tools/bin'}:{codex.ROOT / 'scripts/fleet'}"
            self.assertIn(f"PATH={expected_path}:/usr/bin:/bin", hook["command"])
            self.assertIn(str(codex.ROOT / "scripts/fleet/fleet-quiet-wait"),
                          hook["command"])

    def test_quiet_hook_is_absent_without_owner_or_for_interactive_launch(self):
        with patch.dict(codex.os.environ, {}, clear=True):
            ownerless = codex.command("gpt-6-astra", "xhigh", "/work", ["/work"], "task")
        with patch.dict(codex.os.environ,
                        {"IR_QUIET_OWNER": "pool-1", "IR_LOCK_ROOT": "/locks"},
                        clear=True):
            interactive = codex.command("gpt-6-astra", "xhigh", "/work",
                                        ["/work"], "task", interactive=True)
        for args in (ownerless, interactive):
            self.assertNotIn("--dangerously-bypass-hook-trust", args)
            self.assertFalse(any(value.startswith("hooks.PreToolUse=") for value in args))

    def test_repository_does_not_track_codex_hook_configuration(self):
        repo = Path(__file__).resolve().parents[3]
        if not (repo / ".git").exists():
            self.skipTest("tracked-file guard requires a Git checkout")
        result = subprocess.run(["git", "-C", str(repo), "ls-files", ".codex"],
                                check=True, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.stdout.splitlines(), [])

    def test_interactive_resume_waits_for_human(self):
        args = codex.command("gpt-6-astra", "xhigh", "/work", ["/work"], "task", "abc", True)
        self.assertEqual(args[-2:], ["resume", "abc"])
        self.assertNotIn("task", args)
        self.assertNotIn("--json", args)

    def test_events_capture_thread_usage_and_quota_separately(self):
        with tempfile.TemporaryDirectory() as temp:
            state = Path(temp)
            sidecar = state / "worker.json"
            sidecar.write_text('{"target":"task:engine:17","role":"worker"}')
            self.assertFalse(codex.observe({"type": "thread.started", "thread_id": "abc-123"},
                                           sidecar, state, "gpt-6-astra"))
            data = json.loads(sidecar.read_text())
            self.assertEqual(data["session_id"], "abc-123")
            self.assertEqual(data["target"], "task:engine:17")
            codex.observe({"type": "turn.completed", "usage": {"input_tokens": 10}},
                          sidecar, state, "gpt-6-astra")
            self.assertTrue((state / "codex-usage/worker.json").is_file())
            self.assertFalse((state / "runtime-cooldown/codex.json").exists())
            event = {"type": "turn.failed", "error": {"message": "quota exceeded"}}
            self.assertTrue(codex.observe(event,
                                         sidecar, state, "gpt-6-astra"))
            self.assertTrue((state / "runtime-cooldown/codex.json").is_file())

    def test_unknown_effort_and_role_rejected(self):
        with self.assertRaises(ValueError):
            codex.command("gpt-6-astra", "ultra", "/work", [], "task")
        with self.assertRaises(ValueError):
            codex.prompt("../../bad", "live", "task:engine:1")


if __name__ == "__main__":
    unittest.main()
