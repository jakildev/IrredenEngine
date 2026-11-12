"""Hermetic coverage for the scout's dispatch-liveness standing alert."""

import importlib.machinery
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout_liveness", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout_liveness", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class TestDispatchLiveness(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.alerts = Path(self.tmp.name) / "alerts"
        self.state = Path(self.tmp.name) / "state"
        self.projections = self.state / "projections"
        self.state.mkdir()
        self.projections.mkdir()
        self.logs = []
        _mod._dispatch_liveness_since.clear()
        _mod._spawn_fail_streak.clear()
        self.patches = [
            patch.object(_mod, "_alerts_dir", return_value=self.alerts),
            patch.object(_mod, "log", side_effect=self.logs.append),
            patch.object(_mod, "STATE_DIR", self.state),
            patch.object(_mod, "PROJECTIONS_DIR", self.projections),
        ]
        for item in self.patches:
            item.start()

    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        self.tmp.cleanup()

    def tick(self, now, targets=None, **overrides):
        readings = {
            "dispatcher_alive": True,
            "inflight": False,
            "gate_open": True,
            "mode_live": True,
        }
        readings.update(overrides)
        _mod.check_dispatch_liveness_lane(
            "worker", targets or {
                "dispatchable": ["task:engine:4979"], "withheld": []},
            now, **readings)

    def test_fires_at_boundary_and_stays_quiet_while_rewriting(self):
        self.tick(1000)
        self.tick(2799)
        self.assertFalse((self.alerts / "dispatch-liveness-worker").exists())
        self.tick(2800)
        path = self.alerts / "dispatch-liveness-worker"
        self.assertTrue(path.exists())
        first = path.read_text().splitlines()[0]
        self.assertIn("lane=worker reason=idle-with-work", first)
        self.assertIn("since=1970-01-01T00:16:40Z", first)
        self.assertIn("targets=task:engine:4979", first)
        self.assertEqual(len([line for line in self.logs if "ESCALATION" in line]), 1)
        before = len(self.logs)
        self.tick(2801)
        self.assertEqual(len(self.logs), before)
        self.assertIn("task:engine:4979", path.read_text())

    def test_withheld_target_carries_reason_and_expiry(self):
        targets = {"dispatchable": [], "withheld": [{
            "target": "review:engine:42", "reason": "GPU lease held",
            "expires_at": "2026-11-11T22:00:00Z",
        }]}
        self.tick(1000, targets)
        self.tick(2800, targets)
        text = (self.alerts / "dispatch-liveness-worker").read_text()
        self.assertIn("withheld=1", text)
        self.assertIn("reason='GPU lease held'", text)
        self.assertIn("expires_at=2026-11-11T22:00:00Z", text)

    def test_inflight_and_empty_targets_clear_with_one_recovery(self):
        self.tick(1000)
        self.tick(2800)
        path = self.alerts / "dispatch-liveness-worker"
        self.tick(2801, inflight=True)
        self.assertFalse(path.exists())
        self.assertEqual(len([line for line in self.logs if "liveness recovered" in line]), 1)
        self.tick(3000)
        self.tick(4800)
        self.assertTrue(path.exists())
        self.tick(4801, {"dispatchable": [], "withheld": []})
        self.assertFalse(path.exists())
        self.assertEqual(len([line for line in self.logs if "liveness recovered" in line]), 2)

    def test_expected_holds_and_pre_boundary_do_not_fire(self):
        cases = (({}, 2799), ({"gate_open": False}, 2800),
                 ({"mode_live": False}, 2800), ({"inflight": True}, 2800))
        for readings, second_tick in cases:
            with self.subTest(readings=readings, second_tick=second_tick):
                _mod._dispatch_liveness_since.clear()
                _mod._spawn_fail_streak.clear()
                self.tick(1000)
                self.tick(second_tick, **readings)
                self.assertFalse((self.alerts / "dispatch-liveness-worker").exists())

    def test_dead_dispatcher_fires_immediately_without_targets(self):
        self.tick(1000, {"dispatchable": [], "withheld": []},
                  dispatcher_alive=False, gate_open=False, inflight=True)
        first = (self.alerts / "dispatch-liveness-worker").read_text().splitlines()[0]
        self.assertIn("reason=dispatcher-dead", first)
        self.assertIn("targets=", first)

    def test_dispatcher_pid_reader_distinguishes_live_dead_and_unknown(self):
        pid_path = self.state / "dispatcher.pid"
        self.assertIsNone(_mod._dispatcher_pid_alive())
        pid_path.write_text("123\n")
        with patch.object(_mod.sys, "platform", "linux"), \
             patch.object(_mod.os, "kill", return_value=None):
            self.assertTrue(_mod._dispatcher_pid_alive())
        with patch.object(_mod.sys, "platform", "linux"), \
             patch.object(_mod.os, "kill", side_effect=ProcessLookupError):
            self.assertFalse(_mod._dispatcher_pid_alive())
        with patch.object(_mod.sys, "platform", "linux"), \
             patch.object(_mod.os, "kill", side_effect=PermissionError):
            self.assertIsNone(_mod._dispatcher_pid_alive())
        pid_path.write_text("not-a-pid\n")
        self.assertIsNone(_mod._dispatcher_pid_alive())

    def test_inflight_reader_requires_a_complete_state_directory(self):
        directory = self.state / "dispatch"
        self.assertIsNone(_mod._dispatch_inflight_roles())
        directory.mkdir()
        (directory / "worker.json").write_text(json.dumps({"role": "worker"}))
        (directory / "empty.json").write_text("{}")
        self.assertEqual(_mod._dispatch_inflight_roles(), {"worker"})
        (directory / "broken.json").write_text("{")
        self.assertIsNone(_mod._dispatch_inflight_roles())

    def test_gate_reader_accepts_usage_shapes_and_quiet_holds(self):
        cases = (
            ("open", "off", True),
            ("open:Codex util=10%", "off", True),
            ("closed:GitHub quota", None, False),
            ("open", "waiting owner=benchmark", False),
            ("open", "draining owner=benchmark", False),
            ("open", "held owner=benchmark", False),
            ("open", "linger owner=benchmark", False),
            ("unexpected", None, None),
        )
        for usage, quiet, expected in cases:
            with self.subTest(usage=usage, quiet=quiet):
                results = [subprocess.CompletedProcess([], 0, usage, "")]
                if quiet is not None:
                    results.append(subprocess.CompletedProcess([], 0, quiet, ""))
                with patch.object(_mod.subprocess, "run", side_effect=results):
                    self.assertEqual(_mod._dispatch_gate_open(), expected)

        failed = subprocess.CompletedProcess([], 1, "", "failure")
        with patch.object(_mod.subprocess, "run", return_value=failed):
            self.assertIsNone(_mod._dispatch_gate_open())
        with patch.object(_mod.subprocess, "run", side_effect=OSError):
            self.assertIsNone(_mod._dispatch_gate_open())

    def test_gate_status_reader_returns_only_successful_nonempty_output(self):
        completed = subprocess.CompletedProcess([], 0, "open\n", "")
        with patch.object(_mod.subprocess, "run", return_value=completed) as run:
            self.assertEqual(_mod._dispatch_gate_status("all"), "open")
            self.assertEqual(run.call_args.args[0][-2:], ["--gate-status", "all"])
        empty = subprocess.CompletedProcess([], 0, "\n", "")
        with patch.object(_mod.subprocess, "run", return_value=empty):
            self.assertIsNone(_mod._dispatch_gate_status("quiet"))
        failed = subprocess.CompletedProcess([], 1, "open\n", "failure")
        with patch.object(_mod.subprocess, "run", return_value=failed):
            self.assertIsNone(_mod._dispatch_gate_status("all"))
        with patch.object(_mod.subprocess, "run", side_effect=OSError):
            self.assertIsNone(_mod._dispatch_gate_status("all"))

    def test_lane_runner_reads_real_state_and_clears_on_quiet_hold(self):
        (self.state / "dispatch-mode").write_text("live\n")
        (self.state / "dispatcher.pid").write_text("123\n")
        (self.state / "dispatch").mkdir()
        quiet_status = ["off"]

        def run_stub(argv, **_kwargs):
            names = {Path(part).name for part in argv}
            if "fleet-dispatcher" in names:
                scope = argv[-1]
                output = "open" if scope == "all" else quiet_status[0]
                return subprocess.CompletedProcess(argv, 0, output + "\n", "")
            if "fleet_task_class.py" in names:
                lane = argv[-1]
                output = json.dumps({
                    "dispatchable": [f"task:engine:{lane}"], "withheld": []})
                return subprocess.CompletedProcess(argv, 0, output, "")
            raise AssertionError(f"unexpected command: {argv}")

        env = {"FLEET_LIVENESS_STALL_SECS": "0", "FLEET_LIVENESS_NOW": "1000"}
        with patch.object(_mod.sys, "platform", "linux"), \
             patch.object(_mod.os, "kill", return_value=None), \
             patch.object(_mod.subprocess, "run", side_effect=run_stub), \
             patch.dict(os.environ, env, clear=False):
            _mod.run_dispatch_liveness_lanes()
            for lane in _mod.DISPATCH_LIVENESS_ROLES:
                self.assertTrue((self.alerts / f"dispatch-liveness-{lane}").exists())
            quiet_status[0] = "held owner=benchmark"
            _mod.run_dispatch_liveness_lanes()
            for lane in _mod.DISPATCH_LIVENESS_ROLES:
                self.assertFalse((self.alerts / f"dispatch-liveness-{lane}").exists())

    def test_unreadable_resolver_preserves_existing_state_and_alert(self):
        self.tick(1000)
        self.tick(2800)
        path = self.alerts / "dispatch-liveness-worker"
        before = path.read_text()
        with patch.object(_mod, "_dispatcher_pid_alive", return_value=True), \
             patch.object(_mod, "_dispatch_inflight_roles", return_value=set()), \
             patch.object(_mod, "_dispatch_gate_open", return_value=True), \
             patch.object(_mod, "_dispatch_liveness_targets", return_value=None):
            (self.state / "dispatch-mode").write_text("live\n")
            _mod.run_dispatch_liveness_lanes()
        self.assertEqual(path.read_text(), before)


if __name__ == "__main__":
    unittest.main()
