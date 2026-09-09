"""Codex transport tests use synthetic events and never launch a paid model."""

import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

if not (Path(__file__).resolve().parents[1] / "fleet_codex.py").is_file():
    print("SKIP: Codex adapter subject absent", file=sys.stderr)
    sys.exit(3)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fleet_codex as codex
import fleet_codex_policy as policy


class Transport(unittest.TestCase):
    def test_stderr_quota_and_stdout_notice(self):
        for rc, error in ((1, "rate limit exceeded"), (0, "startup warning")):
            with self.subTest(rc=rc), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                worktree = root / ".claude/worktrees/pool-1"
                state = root / "state"
                args = SimpleNamespace(prepare=False, check=False, role="worker",
                                       model="gpt-6-astra", effort="xhigh", mode="live",
                                       resume="", interactive=False, print_launch=False)

                def launch(*_args, **kwargs):
                    kwargs["stderr"].write(error)
                    return Mock(stdout=iter(['notice\n', '{"type":"turn.completed"}\n']),
                                wait=Mock(return_value=rc), poll=Mock(return_value=rc))

                with patch.object(codex.Path, "cwd", return_value=worktree), \
                        patch.object(codex, "writable_roots", return_value=[str(worktree)]), \
                        patch.object(codex, "prepare"), patch.object(codex, "prompt"), \
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
            with patch.object(codex.Path, "home", return_value=root), \
                    patch.object(codex.subprocess, "run", return_value=Mock(stdout=str(common))), \
                    patch.dict(codex.os.environ, {}, clear=True):
                roots = codex.writable_roots(worktree, root / ".fleet/state")
            self.assertIn(str(worktree), roots)
            self.assertIn(str(common), roots)
            self.assertNotIn(str(root / "repo"), roots)
            for name in ("claims", "heartbeats", "molecules", "iteration-summaries"):
                self.assertIn(str(root / ".fleet" / name), roots)

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
        # role at once; that must not read as hand-authored (see #3098).
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
        args = codex.command("gpt-6-astra", "xhigh", "/work", ["/work", "/state"], "task")
        self.assertEqual(args[:2], ["codex", "exec"])
        self.assertIn('approval_policy="never"', args)
        self.assertIn('sandbox_mode="workspace-write"', args)
        self.assertNotIn("--dangerously-bypass-approvals-and-sandbox", args)
        args = codex.command("gpt-6-astra", "xhigh", "/work", ["/work"], "task", "abc")
        self.assertEqual(args[-3:], ["resume", "abc", "task"])
        self.assertNotIn("-C", args)

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
