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
from fleet import fleet_display_session  # noqa: E402
from fleet.tests.fake_display import FakeDisplay, gone, wait_gone  # noqa: E402


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


# Records the display hold it ran under, then behaves as its last argument says.
HOLD_PROBE_STUB = """
holder="${FLEET_DISPLAY_HOLDER:-}"
alive=dead
[[ -n "$holder" ]] && kill -0 "$holder" 2>/dev/null && alive=alive
echo "$holder $alive $(cat "$FAKE_DISPLAY_STATE")" > "$STUB_RECORD"
case "${*: -1}" in
  fail) exit 3 ;;
  hang) exec sleep 30 ;;
  zero) echo 'Discovered 0 display monitors'; exec sleep 30 ;;
esac
exit 0
"""


class IrRunDisplayWakeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.build = root / "build"
        self.build.mkdir()
        self.record = root / "record"
        self.display = FakeDisplay(root / "display")
        stub = self.build / "IRHoldProbe"
        stub.write_text(f"#!/usr/bin/env bash\n{HOLD_PROBE_STUB}")
        stub.chmod(0o755)

    def env(self, **overrides: str) -> dict[str, str]:
        env = {k: v for k, v in os.environ.items() if not k.startswith("FLEET_DISPLAY_")}
        env |= self.display.env() | {
            "IR_LOCK_ROOT": str(self.build / "locks"),
            "IR_RUN_NO_DISPLAY_GRACE": "1",
            "STUB_RECORD": str(self.record),
        }
        return env | overrides

    def run_ir(self, *args: str, env: dict[str, str] | None = None,
               timeout: str | None = None) -> subprocess.CompletedProcess[str]:
        argv = [str(IR_RUN), "--build-dir", str(self.build)]
        if timeout is not None:
            argv += ["--timeout", timeout]
        return subprocess.run([*argv, "IRHoldProbe", *args], text=True, capture_output=True,
                              env=env or self.env(), check=False, timeout=60)

    def held_run(self) -> tuple[int, list[str]]:
        """The one caffeinate this run started, after asserting it has ended."""
        [(pid, argv)] = self.display.invocations()
        self.assertTrue(wait_gone(pid), f"display holder {pid} outlived the run")
        return pid, argv

    def test_sleeping_display_wakes_and_is_held_for_the_run(self) -> None:
        for timeout in (None, "20"):
            with self.subTest(timeout=timeout):
                self.display.log.unlink(missing_ok=True)
                self.display.set_state("1 0 1")
                result = self.run_ir("--auto-screenshot", "1", timeout=timeout)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("RESULT=CLEAN", result.stdout)
                pid, argv = self.held_run()
                self.assertEqual(argv[:3], ["-d", "-u", "-w"])
                holder, alive, *state = self.record.read_text().split()
                self.assertEqual((holder, alive, state), (str(pid), "alive", ["1", "1", "1"]))
                self.assertIn("display before wake: CoreGraphics online=1 awake=0 NSScreen=1",
                              result.stderr)
                self.assertIn("display ready after", result.stderr)

    def test_holder_is_bound_to_the_ir_run_process(self) -> None:
        proc = subprocess.Popen(
            [str(IR_RUN), "--build-dir", str(self.build), "IRHoldProbe", "--auto-screenshot",
             "1", "hang"], env=self.env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(lambda: proc.poll() is None and proc.kill())
        deadline = time.monotonic() + 20
        while not self.record.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        [(pid, argv)] = self.display.invocations()
        self.assertEqual(argv[argv.index("-w") + 1], str(proc.pid))
        # SIGKILL skips the EXIT trap: only the -w binding ends the hold.
        proc.kill()
        proc.wait()
        self.assertTrue(wait_gone(pid), f"display holder {pid} outlived a killed ir-run")

    def test_every_terminal_arm_ends_the_hold(self) -> None:
        cases = (("fail", None, 3, "RESULT=CRASH"),
                 ("fail", "20", 1, "RESULT=CRASH"),
                 ("hang", "1", 0, "RESULT=ALIVE-TIMEOUT"),
                 ("zero", "20", 69, "RESULT=NO-DISPLAY"))
        for mode, timeout, code, verdict in cases:
            with self.subTest(mode=mode, timeout=timeout):
                self.display.log.unlink(missing_ok=True)
                result = self.run_ir("--auto-screenshot", "1", mode, timeout=timeout)
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertIn(verdict, result.stdout + result.stderr)
                self.held_run()

    def test_unready_display_is_no_display_before_launch(self) -> None:
        cases = (("1 0 1", "the online display stayed asleep"),
                 ("0 0 0", "no online CoreGraphics display"),
                 ("1 1 0", "an awake CoreGraphics display but no Aqua NSScreen"))
        for state, condition in cases:
            with self.subTest(state=state):
                self.display.log.unlink(missing_ok=True)
                self.record.unlink(missing_ok=True)
                self.display.set_state(state)
                env = self.env(FLEET_DISPLAY_WAKE_TIMEOUT="0.5")
                env.pop("FAKE_WAKE_TO")
                result = self.run_ir("--auto-screenshot", "1", env=env, timeout="20")
                self.assertEqual(result.returncode, verify_common.EXIT_ENV_FAIL, result.stderr)
                self.assertIn("RESULT=NO-DISPLAY exe=IRHoldProbe exit=69", result.stderr)
                self.assertIn("display not ready after requesting a wake and waiting",
                              result.stderr)
                self.assertIn(f"{condition} (CoreGraphics online={state[0]}", result.stderr)
                self.assertFalse(self.record.exists(), "the exe ran on an unready display")
                self.held_run()

    def test_probe_failure_launches_without_waiting(self) -> None:
        env = self.env(FLEET_DISPLAY_PROBE="false", FLEET_DISPLAY_WAKE_TIMEOUT="30")
        started = time.monotonic()
        result = self.run_ir("--auto-screenshot", "1", env=env)
        self.assertLess(time.monotonic() - started, 15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("the CoreGraphics/AppKit display probe failed", result.stderr)
        self.held_run()

    def test_outer_hold_is_reused_and_left_running(self) -> None:
        outer = subprocess.Popen(["sleep", "30"])
        self.addCleanup(outer.kill)
        self.display.set_state("1 1 1")
        result = self.run_ir("--auto-screenshot", "1",
                             env=self.env(FLEET_DISPLAY_HOLDER=str(outer.pid)))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.display.invocations(), [])
        self.assertFalse(gone(outer.pid), "ir-run ended a hold it did not own")
        self.assertEqual(self.record.read_text().split()[:2], [str(outer.pid), "alive"])

    def test_no_caffeinate_off_macos_or_outside_capture_verbs(self) -> None:
        cases = ((self.env(FLEET_DISPLAY_PLATFORM="linux"), ("--auto-screenshot", "1"), None),
                 (self.env(), (), "5"))
        for env, args, timeout in cases:
            with self.subTest(platform=env["FLEET_DISPLAY_PLATFORM"], args=args):
                result = self.run_ir(*args, env=env, timeout=timeout)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.display.invocations(), [])
                self.assertNotIn("ir-run: display", result.stderr)


class DisplaySessionTest(unittest.TestCase):
    def test_conditions_name_exactly_one_unmet_requirement(self) -> None:
        self.assertEqual(fleet_display_session.condition((1, 1, 1)), "ok")
        self.assertEqual(fleet_display_session.condition((0, 0, 0)), "none")
        self.assertEqual(fleet_display_session.condition((0, 1, 1)), "none")
        self.assertEqual(fleet_display_session.condition((1, 0, 1)), "asleep")
        self.assertEqual(fleet_display_session.condition((1, 0, 0)), "asleep")
        self.assertEqual(fleet_display_session.condition((1, 1, 0)), "no-aqua")

    def test_hold_waits_on_an_injected_clock(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            display = FakeDisplay(Path(tmp))
            env = display.env(timeout="5")
            env.pop("FAKE_WAKE_TO")
            now = [0.0]
            hold = fleet_display_session.hold_display(
                os.getpid(), env=env, clock=lambda: now[0],
                sleep=lambda seconds: now.__setitem__(0, now[0] + 1.0))
            self.assertFalse(hold.ready)
            self.assertEqual(hold.condition, "asleep")
            self.assertIn("waiting 5.0s", hold.lines[-1])
            hold.release()
            self.assertTrue(wait_gone(hold.holder_pid))

    def test_off_macos_does_nothing(self) -> None:
        hold = fleet_display_session.hold_display(os.getpid(),
                                                  env={"FLEET_DISPLAY_PLATFORM": "linux"})
        self.assertTrue(hold.ready)
        self.assertEqual((hold.lines, hold.holder_pid), ([], None))


class VerifierNoDisplayTest(unittest.TestCase):
    def test_gui_verify_exits_with_env_fail(self) -> None:
        with patch.object(sys, "argv", ["gui-verify.py", "IRFake", "--no-build"]), \
                patch.object(verify_common, "display_preflight", return_value=None), \
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
                    patch.object(verify_common, "display_preflight", return_value=None), \
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
                    patch.object(verify_common, "display_preflight", return_value=None), \
                    redirect_stdout(io.StringIO()) as captured:
                result = light_verify.main(["--no-build"])

        self.assertEqual(result, verify_common.EXIT_ENV_FAIL)
        self.assertEqual(run_pass.call_count, 1)
        self.assertEqual(captured.getvalue().count("ENV-FAIL:"), 1)

    def display_env(self, tmp: str, **overrides: str) -> dict[str, str]:
        env = {k: v for k, v in os.environ.items() if not k.startswith("FLEET_DISPLAY_")}
        return env | FakeDisplay(Path(tmp)).env() | overrides

    def test_preflight_wakes_and_hands_the_hold_to_fleet_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp, \
                patch.dict(os.environ, self.display_env(tmp), clear=True), \
                patch.object(verify_common.atexit, "register") as register, \
                redirect_stdout(io.StringIO()) as captured:
            self.assertIsNone(verify_common.display_preflight("[test] "))
            holder = os.environ[fleet_display_session.HOLDER_ENV]
            [(release,), _] = register.call_args
            self.assertFalse(gone(int(holder)))
            # A second target in the same verifier reuses the hold.
            self.assertIsNone(verify_common.display_preflight("[test] "))
            release()
        self.assertTrue(wait_gone(int(holder)))
        output = captured.getvalue()
        self.assertIn("[test] display ready after", output)
        self.assertIn(f"[test] display held by the outer run (caffeinate pid {holder})", output)
        self.assertNotIn("ENV-FAIL", output)

    def test_display_domain_mismatch_stops_each_verifier_before_fleet_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp, \
                patch.dict(os.environ, self.display_env(tmp, FAKE_WAKE_TO="1 1 0",
                                                        FLEET_DISPLAY_WAKE_TIMEOUT="0.5"),
                           clear=True), \
                redirect_stdout(io.StringIO()) as captured:
            self.assertEqual(verify_common.display_preflight("[test] "),
                             verify_common.EXIT_ENV_FAIL)
            self.assertNotIn(fleet_display_session.HOLDER_ENV, os.environ)
        output = captured.getvalue()
        self.assertEqual(output.count("ENV-FAIL:"), 1)
        self.assertIn("ENV-FAIL: display not ready after requesting a wake", output)
        self.assertIn("an awake CoreGraphics display but no Aqua NSScreen", output)
        self.assertIn("0 display monitors", output)

        with patch.object(sys, "argv", ["gui-verify.py", "IRFake", "--no-build"]), \
                patch.object(verify_common, "display_preflight",
                             return_value=verify_common.EXIT_ENV_FAIL), \
                patch.object(verify_common, "run_capture") as gui_run:
            with self.assertRaises(SystemExit) as exit_code:
                gui_verify.main()
        self.assertEqual(exit_code.exception.code, verify_common.EXIT_ENV_FAIL)
        gui_run.assert_not_called()

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
            with patch.object(verify_common, "display_preflight",
                              return_value=verify_common.EXIT_ENV_FAIL), \
                    patch.object(render_verify, "_capture_pass") as render_run:
                result = render_verify._verify_one(
                    args=args, worktree=root, build_dir=root / "build", backend="test",
                    target="IRFake", demo_dir=demo,
                )
        self.assertEqual(result["rc"], verify_common.EXIT_ENV_FAIL)
        render_run.assert_not_called()

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.object(
                light_verify.verify_common, "detect_worktree_root", return_value=root
            ), patch.object(
                light_verify, "_if_changed_requires_run", return_value=True
            ), patch.object(
                verify_common, "display_preflight", return_value=verify_common.EXIT_ENV_FAIL
            ), patch.object(verify_common, "run_pass") as light_run:
                result = light_verify.main(["--no-build", "--if-changed"])
        self.assertEqual(result, verify_common.EXIT_ENV_FAIL)
        light_run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
