"""Hermetic NO-DISPLAY classification tests for ir-run and its verifiers."""

from __future__ import annotations

import argparse
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPTS.parent
IR_RUN = REPO_ROOT / "engine/tools/bin/ir-run"
sys.path.insert(0, str(SCRIPTS))

import verify_common  # noqa: E402


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


gui_verify = _load("no_display_gui_verify", "gui-verify.py")
render_verify = _load("no_display_render_verify", "render-verify.py")
light_verify = _load("no_display_light_verify", "light-verify.py")
NO_DISPLAY_OUTPUT = "ir-run: RESULT=NO-DISPLAY exe=stub exit=69\n"


class IrRunNoDisplayTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build"
        self.build.mkdir()

    def write_stub(self, name: str, body: str) -> Path:
        path = self.build / name
        path.write_text(f"#!/usr/bin/env bash\n{body}\n")
        path.chmod(0o755)
        return path

    def run_ir(self, name: str, *, timeout: int, grace: int) -> subprocess.CompletedProcess[str]:
        env = os.environ | {
            "IR_LOCK_ROOT": str(self.build / "locks"),
            "IR_RUN_NO_DISPLAY_GRACE": str(grace),
        }
        return subprocess.run(
            [str(IR_RUN), "--build-dir", str(self.build), "--timeout", str(timeout), name],
            text=True,
            capture_output=True,
            env=env,
            check=False,
        )

    def test_persistent_zero_monitor_run_fast_fails_and_stops_child(self) -> None:
        pid_file = self.build / "stub.pid"
        self.write_stub(
            "zero-monitors",
            f"echo $$ > {pid_file}\necho 'Discovered 0 display monitors'\nexec sleep 30",
        )
        started = time.monotonic()
        result = self.run_ir("zero-monitors", timeout=120, grace=1)

        self.assertEqual(result.returncode, verify_common.EXIT_ENV_FAIL)
        self.assertIn("RESULT=NO-DISPLAY exe=zero-monitors exit=69", result.stderr)
        self.assertLess(time.monotonic() - started, 10)
        pid = int(pid_file.read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_other_monitor_count_keeps_watchdog_behavior(self) -> None:
        self.write_stub("one-monitor", "echo 'Discovered 1 display monitors'\nexec sleep 30")
        result = self.run_ir("one-monitor", timeout=1, grace=0)

        self.assertEqual(result.returncode, 0)
        self.assertIn("RESULT=ALIVE-TIMEOUT", result.stdout)

    def test_clean_exit_inside_grace_keeps_clean_verdict(self) -> None:
        self.write_stub("zero-then-clean", "echo 'Discovered 0 display monitors'\nsleep .1\nexit 0")
        result = self.run_ir("zero-then-clean", timeout=5, grace=2)

        self.assertEqual(result.returncode, 0)
        self.assertIn("RESULT=CLEAN", result.stdout)
        self.assertNotIn("RESULT=NO-DISPLAY", result.stderr)


class VerifierNoDisplayTest(unittest.TestCase):
    def test_gui_verify_exits_with_env_fail(self) -> None:
        with patch.object(sys, "argv", ["gui-verify.py", "IRFake", "--no-build"]), \
                patch.object(verify_common, "run_capture", return_value=(69, NO_DISPLAY_OUTPUT)), \
                redirect_stdout(io.StringIO()) as captured:
            with self.assertRaises(SystemExit) as exit_code:
                gui_verify.main()

        self.assertEqual(exit_code.exception.code, verify_common.EXIT_ENV_FAIL)
        self.assertEqual(captured.getvalue().count("ENV-FAIL:"), 1)

    def test_render_verify_exits_with_env_fail_before_extra_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            demo = root / "creations/demos/fake"
            manifest = demo / "test/references/manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"target": "IRFake", "shots": ["shot"]}')
            args = argparse.Namespace(
                no_build=True, warmup=None, timeout=10, demo_arg=[],
                update_references=False, force=True,
            )
            with patch.object(
                render_verify.verify_common, "find_exe", return_value=root / "fake"
            ), \
                    patch.object(
                        render_verify,
                        "_capture_pass",
                        return_value=(69, "", verify_common.NO_DISPLAY),
                    ) as capture, \
                    redirect_stdout(io.StringIO()) as captured:
                result = render_verify._verify_one(
                    args=args, worktree=root, build_dir=root / "build", backend="test",
                    target="IRFake", demo_dir=demo,
                )

        self.assertEqual(result["rc"], verify_common.EXIT_ENV_FAIL)
        self.assertEqual(capture.call_count, 1)
        self.assertEqual(captured.getvalue().count("ENV-FAIL:"), 1)

    def test_light_verify_exits_with_env_fail_without_second_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "build/lighting/IRLightingEmissive"
            exe.parent.mkdir(parents=True)
            exe.touch()
            with patch.object(
                light_verify.verify_common, "detect_worktree_root", return_value=root
            ), \
                    patch.object(
                        light_verify.verify_common, "detect_backend", return_value="test"
                    ), \
                    patch.object(light_verify.verify_common, "find_exe", return_value=exe), \
                    patch.object(
                        light_verify.verify_common,
                        "run_pass",
                        return_value=(69, NO_DISPLAY_OUTPUT, []),
                    ) as run_pass, \
                    redirect_stdout(io.StringIO()) as captured:
                result = light_verify.main(["--no-build"])

        self.assertEqual(result, verify_common.EXIT_ENV_FAIL)
        self.assertEqual(run_pass.call_count, 1)
        self.assertEqual(captured.getvalue().count("ENV-FAIL:"), 1)


if __name__ == "__main__":
    unittest.main()
