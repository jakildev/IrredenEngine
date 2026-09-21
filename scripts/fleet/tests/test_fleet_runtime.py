"""Runtime selection must not change task identity or silently self-review."""

import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

if not (Path(__file__).resolve().parents[1] / "fleet_runtime.py").is_file():
    print("SKIP: Codex adapter subject absent", file=sys.stderr)
    sys.exit(3)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fleet_runtime as runtime


class Routing(unittest.TestCase):
    def test_targetless_role_routing(self):
        cases = (
            ({}, "merger", "sonnet", "sonnet", "high", "open",
             ("claude", "sonnet", "sonnet", "high")),
            ({}, "merger", "sonnet", "sonnet", "high", "closed",
             ("codex", "sonnet", "gpt-5.6-terra", "medium")),
            ({}, "epic-steward", "opus", "opus", "xhigh", "closed",
             ("codex", "opus", "gpt-5.6-sol", "medium")),
            ({"FLEET_CODEX_EFFORT_SONNET": "high"}, "merger", "sonnet", "sonnet",
             "high", "closed", ("codex", "sonnet", "gpt-5.6-terra", "high")),
            ({"FLEET_WORKER_RUNTIME": "claude"}, "merger", "sonnet", "sonnet",
             "high", "closed", ("claude", "sonnet", "sonnet", "high")),
            ({"FLEET_WORKER_RUNTIME": "codex"}, "merger", "sonnet", "sonnet",
             "high", "open", ("codex", "sonnet", "gpt-5.6-terra", "medium")),
            ({"FLEET_RUNTIMES": "claude"}, "merger", "sonnet", "sonnet", "high",
             "closed", ("claude", "sonnet", "sonnet", "high")),
            ({"FLEET_RUNTIMES": "codex"}, "merger", "sonnet", "sonnet", "high",
             "open", ("codex", "sonnet", "gpt-5.6-terra", "medium")),
        )
        for overrides, role, cls, model, effort, gate, expected in cases:
            with self.subTest(role=role, gate=gate, overrides=overrides):
                env = {**self.env, **overrides}
                self.assertEqual(runtime.route_role(role, cls, model, effort, gate, env),
                                 expected)

        with self.assertRaisesRegex(ValueError, "unavailable"):
            runtime.route_role("merger", "sonnet", "sonnet", "high", "closed",
                               {"FLEET_RUNTIMES": "codex",
                                "FLEET_WORKER_RUNTIME": "claude"})
        with self.assertRaisesRegex(ValueError, "target-less"):
            runtime.route_role("worker", "sonnet", "sonnet", "high", "closed", self.env)
        with self.assertRaisesRegex(ValueError, "class"):
            runtime.route_role("merger", "unknown", "sonnet", "high", "closed", self.env)

    def test_targetless_role_executable(self):
        wrapper = Path(__file__).resolve().parents[1] / "fleet-runtime"
        bash = shutil.which("bash") or "bash"
        env = {**runtime.os.environ, **self.env}
        result = subprocess.run([bash, str(wrapper), "route-role", "merger", "sonnet",
                                 "sonnet", "high", "closed"], env=env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "codex sonnet gpt-5.6-terra medium")

    def test_route_diagnostic_escalates_reappears_and_clears(self):
        with tempfile.TemporaryDirectory() as temp:
            state = Path(temp) / "state"
            alerts = Path(temp) / "alerts"
            with patch.dict(runtime.os.environ, {"FLEET_ALERTS_DIR": str(alerts)}), \
                    patch.object(runtime.sys, "stderr", io.StringIO()) as log:
                for _ in range(3):
                    runtime.routing_problem(state, "review:engine:900", "unstamped")
                self.assertEqual(len(log.getvalue().splitlines()), 2)
                alert = next(alerts.iterdir())
                alert.unlink()
                runtime.routing_problem(state, "review:engine:900", "unstamped")
                self.assertEqual(json.loads(alert.read_text())["count"], 4)
                runtime.routing_problem(state, "review:engine:900")
                self.assertFalse(alert.exists())

    def test_wrapper_resume_preserves_provider_target_and_class(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sidecar.json"
            data = dict(runtime="codex", target="task:engine:907", model="gpt-6-astra",
                        effort="xhigh", **{"class": "fable"})
            path.write_text(json.dumps(data))
            self.assertEqual(runtime.resume_route(path, self.env),
                             ("codex", "fable", "gpt-6-astra", "xhigh", "task:engine:907"))
            with self.assertRaisesRegex(ValueError, "unavailable"):
                runtime.resume_route(path, {"FLEET_RUNTIMES": "claude"})
            # Run the executable too, with no network or production-state access.
            wrapper = Path(__file__).resolve().parents[1] / "fleet-runtime"
            # Windows' CreateProcess searches System32 (the legacy WSL bash.exe
            # stub, a different filesystem namespace) before PATH, so a bare
            # "bash" can resolve to the wrong interpreter regardless of PATH
            # order; shutil.which walks PATH itself and finds the real
            # MSYS2/Git-Bash.
            bash = shutil.which("bash") or "bash"
            result = subprocess.run([bash, str(wrapper), "resume-route", str(path)],
                                    env={**runtime.os.environ, **self.env},
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(),
                             "codex fable gpt-6-astra xhigh task:engine:907")
            data["target"] = "task:engine:907;bad"
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "assignment"):
                runtime.resume_route(path, self.env)

    def setUp(self):
        self.env = {"FLEET_RUNTIMES": "claude,codex", "FLEET_CROSS_PROVIDER_REVIEW": "1"}

    def test_cross_review_both_directions(self):
        for author, reviewer in (("claude", "codex"), ("codex", "claude")):
            record = {"number": 901, "labels": [f"fleet:author-{author}"]}
            result = runtime.route({"flagged_prs": [record]}, "review:engine:901",
                                   "opus-reviewer", "", "opus", "high", self.env)
            self.assertEqual(result[0], reviewer)
            self.assertEqual(result[1], "opus")

    def test_unstamped_author_and_no_same_provider_fallback(self):
        with self.assertRaisesRegex(ValueError, "unstamped"):
            runtime.choose_runtime("review", {}, "review:engine:9", self.env)
        self.env["FLEET_RUNTIMES"] = "claude"
        with self.assertRaisesRegex(ValueError, "unavailable"):
            runtime.choose_runtime("review", {"labels": ["fleet:author-claude"]},
                                   "review:engine:9", self.env)

    def test_balanced_is_stable_and_uses_both(self):
        values = [runtime.choose_runtime("task", {}, f"task:engine:{n}", self.env)
                  for n in range(1, 100)]
        self.assertEqual(set(values), {"claude", "codex"})
        self.env["FLEET_RUNTIMES"] = "codex,claude"
        self.assertEqual(values, [runtime.choose_runtime("task", {}, f"task:engine:{n}", self.env)
                                  for n in range(1, 100)])

    def test_pin_and_class_are_independent(self):
        data = {"tasks_open": [{"id": "#905", "labels": ["fleet:runtime-codex"]}]}
        self.assertEqual(runtime.route(data, "task:engine:905", "worker", "fable",
                                       "fable", "high", self.env),
                         ("codex", "fable", "gpt-6-astra", "xhigh"))
        data["tasks_open"][0]["effort"] = "max"
        self.assertEqual(runtime.route(data, "task:engine:905", "worker", "fable",
                                       "fable", "high", self.env)[3], "max")

    def test_missing_target_and_conflicting_provenance_fail_closed(self):
        with self.assertRaises(ValueError):
            runtime.target_record({}, "task:engine:99")
        with self.assertRaises(ValueError):
            record = {"labels": ["fleet:author-claude", "fleet:author-codex"]}
            runtime.choose_runtime("review", record,
                                   "review:engine:99", self.env)

    def test_merge_target_resolves_against_merger_candidates(self):
        data = {"merger_candidates": [
            {"number": 77, "repo": "engine", "labels": ["fleet:author-codex"],
             "signal": "llm"},
            {"number": 77, "repo": "game", "labels": ["fleet:author-claude"],
             "signal": "needs-resolve"},
        ]}
        # The author's provider serves its own conflict, like `conflict`.
        self.assertEqual(runtime.route(data, "merge:engine:77", "merger", "sonnet",
                                       "sonnet", "high", self.env)[0], "codex")
        self.assertEqual(runtime.route(data, "merge:game:77", "merger", "sonnet",
                                       "sonnet", "high", self.env)[0], "claude")
        with self.assertRaisesRegex(ValueError, "missing"):
            runtime.route({"prs": data["merger_candidates"]}, "merge:engine:77", "merger",
                          "sonnet", "sonnet", "high", self.env)

    def test_namespace_collision(self):
        data = {"candidate_prs": [{"number": 8, "repo": "engine"},
                                 {"number": 8, "repo": "game", "labels": ["fleet:author-codex"]}]}
        self.assertEqual(runtime.route(data, "review:game:8", "sonnet-reviewer", "",
                                       "sonnet", "high", self.env)[0], "claude")

    def test_cooldown_does_not_use_token_usage_as_quota(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertTrue(runtime.ready(temp))
            path = Path(temp) / "runtime-cooldown"
            path.mkdir()
            (path / "codex.json").write_text('{"until": 200}')
            with patch.object(runtime.time, "time", return_value=100):
                self.assertFalse(runtime.ready(temp))
            with patch.object(runtime.time, "time", return_value=201):
                self.assertTrue(runtime.ready(temp))

    def test_stamp_has_exact_provider_and_no_live_network(self):
        with patch.dict(runtime.os.environ, {}, clear=True), \
                patch.object(runtime.subprocess, "run") as run:
            runtime.stamp("123", "example/test", "codex")
            self.assertIn("fleet:author-codex", run.call_args.args[0])
            self.assertIn("fleet:author-claude", run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
