"""Hermetic authentication handoff and independent admission gates."""

import importlib.machinery
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import fleet_gh_fallback as fallback  # noqa: E402


class Routing(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("FLEET_", "GH_", "GITHUB_", "BASH_FUNC_"))}
        self.env.update(HOME=str(self.root), FLEET_CONF="/dev/null", FLEET_UP_CONF="/dev/null",
                        FLEET_ENGINE_ROOT=str(self.root / "repo"),
                        FLEET_GAME_ROOT=str(self.root / "absent"),
                        FLEET_STATE_DIR=str(self.root / "state"),
                        FLEET_CLAIMS_DIR=str(self.root / "claims"),
                        FLEET_RESERVATIONS_DIR=str(self.root / "reservations"),
                        FLEET_SESSIONS_DIR=str(self.root / "sessions"),
                        GH_CONFIG_DIR=str(self.root / "gh-config"), GH_HOST="invalid.invalid",
                        GH_TOKEN="ghs_synthetic-inherited", GITHUB_TOKEN="ghs_synthetic-other",
                        PATH=str(self.bin) + os.pathsep + self.env["PATH"])
        self.stub("fleet-gh-token", "printf 'ghs_synthetic-fresh\\n'")
        self.env["FLEET_GH_TOKEN_BIN"] = str(self.bin / "fleet-gh-token")
        self.stub("gh", '''case "${GH_TOKEN:-${GITHUB_TOKEN:-}}" in
  ghs_*) echo app ;; *) echo user ;;
esac''')
        self.stub("ir-acquire", "exit 0")
        self.usage = self.root / "state" / "usage"
        self.usage.mkdir(parents=True)
        self.now = int(time.time())
        for identity in ("app", "user"):
            for pool in ("core", "graphql"):
                self.sample(identity, pool)

    def stub(self, name, body):
        path = self.bin / name
        path.write_text("#!/usr/bin/env bash\n" + body + "\n")
        path.chmod(0o755)

    def shell(self, command, **env):
        return subprocess.run(["bash", "-c", command], env={**self.env, **env},
                              capture_output=True, text=True, timeout=30, cwd=self.root)

    def sample(self, identity, pool, utilization=0.1, **fields):
        sample = dict(identity=identity, rateLimitType="github_" + pool,
                      utilization=utilization, observed_at=self.now,
                      resetsAt=self.now + 3000, limit=5000, remaining=4500)
        sample.update(fields)
        path = self.usage / f"github-{identity}-{pool}.json"
        path.write_text(json.dumps(sample))
        return path

    def gate(self, scope):
        proc = self.shell('"$SUBJECT" --gate-status "$SCOPE"',
                          SUBJECT=str(SCRIPTS / "fleet-dispatcher"), SCOPE=scope)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return proc.stdout.strip()

    def test_shim_user_selection_cannot_refresh_inherited_app_token(self):
        proc = self.shell('bash "$SHIM" api user', SHIM=str(SCRIPTS / "gh"),
                          FLEET_GH_IDENTITY="user")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.strip(), "user")

    def test_claim_inherits_lane_and_preserves_dispatch_identity(self):
        command = '''set --
FLEET_CLAIM_LIB=1 source "$SUBJECT"
gh api user
printf '%s\\n' "$FLEET_DISPATCH_ID"
'''
        for identity in ("app", "user"):
            proc = self.shell(command, SUBJECT=str(SCRIPTS / "fleet-claim"),
                              FLEET_GH_IDENTITY=identity, FLEET_DISPATCH_ID="dispatch-test")
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stdout.splitlines(), [identity, "dispatch-test"])

    def test_failed_app_mint_does_not_fall_back_to_user(self):
        self.stub("fleet-gh-token", "exit 1")
        command = 'FLEET_CLAIM_LIB=1 source "$SUBJECT"; gh api user'
        app = self.shell(command, SUBJECT=str(SCRIPTS / "fleet-claim"),
                         FLEET_GH_IDENTITY="app")
        self.assertNotEqual(app.returncode, 0)
        self.assertEqual(app.stdout, "")
        user = self.shell(command, SUBJECT=str(SCRIPTS / "fleet-claim"),
                          FLEET_GH_IDENTITY="user")
        self.assertEqual(user.returncode, 0, user.stderr)
        self.assertEqual(user.stdout.strip(), "user")

    def test_failed_app_mint_exit_is_not_a_refused_claim(self):
        self.stub("fleet-gh-token", "exit 1")
        proc = self.shell('bash "$SUBJECT" check 9001', SUBJECT=str(SCRIPTS / "fleet-claim"),
                          FLEET_GH_IDENTITY="app")
        self.assertNotIn(proc.returncode, (0, 1), proc.stderr)
        self.assertEqual(proc.returncode, 4, proc.stderr)

    def test_app_minter_defaults_to_sibling_outside_path(self):
        lib = self.root / "lib"
        lib.mkdir()
        shutil.copy2(SCRIPTS / "fleet-common.sh", lib / "fleet-common.sh")
        token_bin = lib / "fleet-gh-token"
        token_bin.write_text("#!/usr/bin/env bash\nprintf 'ghs_sibling\\n'\n")
        token_bin.chmod(0o755)
        env = dict(self.env)
        env.pop("FLEET_GH_TOKEN_BIN")
        env["PATH"] = os.pathsep.join(p for p in env["PATH"].split(os.pathsep)
                                      if p != str(self.bin))
        command = ('source "$COMMON"; fleet_select_github_identity app; '
                   'printf "%s\\n" "$GH_TOKEN"')
        proc = subprocess.run(
            ["bash", "-c", command],
            env={**env, "COMMON": str(lib / "fleet-common.sh")}, capture_output=True,
            text=True, timeout=30, cwd=self.root)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.strip(), "ghs_sibling")

    def test_app_wall_does_not_close_user_gate(self):
        for pool in ("core", "graphql"):
            self.sample("app", pool, 1.0)
            self.assertTrue(self.gate("shared").startswith("open:"))
            self.assertTrue(self.gate("all").startswith("open"))
            self.assertIn(f"github_{pool}[app]", self.gate("daemon"))
            self.sample("app", pool)

    def test_user_wall_does_not_close_daemon_gate(self):
        for pool in ("core", "graphql"):
            self.sample("user", pool, 1.0)
            self.assertIn(f"github_{pool}[user]", self.gate("shared"))
            self.assertIn(f"github_{pool}[user]", self.gate("all"))
            self.assertTrue(self.gate("daemon").startswith("open:"))
            self.sample("user", pool)

    def dispatch_loop_command(self):
        source = (SCRIPTS / "fleet-dispatcher").read_text()
        functions = []
        for name in ("usage_gate_open", "main"):
            match = re.search(rf"^{name}\(\) \{{\n.*?^\}}", source, re.M | re.S)
            self.assertIsNotNone(match, name)
            functions.append(match.group())
        return 'source "$COMMON"\n' + "\n".join(functions) + '''
log() { :; }
log_gate_transition() { :; }
describe_config() { :; }
acquire_dispatcher_lock() { return 0; }
write_pid_file() { :; }
clear_tmux_github_identity() { :; }
cleanup_stale_dispatches() { :; }
advance_main_clone() { :; }
check_source_reload() { :; }
rearm_due_merger() { :; }
rearm_periodic_meta_triggers() { :; }
log_claude_gate_transition() { :; }
log_scoped_gate_transition() { :; }
read_dispatch_mode() { echo live; }
session_exists() { return 0; }
usage_gate_status() { "$SUBJECT" --gate-status "$@"; }
dispatch_role() { echo "$1" >> "$LAUNCH_LOG"; }
SLEEP_COUNT=0
sleep() {
    SLEEP_COUNT=$((SLEEP_COUNT + 1))
    if (( SLEEP_COUNT >= ${STOP_AFTER_SLEEPS:-1} )); then
        touch "$SHUTDOWN_FLAG"
    fi
}
STATE_DIR="$FLEET_STATE_DIR"
USAGE_DIR="$STATE_DIR/usage"
LOG_FILE="$STATE_DIR/dispatcher.log"
TRIGGERS_DIR="$STATE_DIR/triggers"
DISPATCH_DIR="$STATE_DIR/dispatch"
RATE_LIMIT_DIR="$STATE_DIR/rate-limit"
ENGINE="$FLEET_ENGINE_ROOT"
GAME="$FLEET_GAME_ROOT"
DAEMON_SURFACE_HASH=""
TIMEOUT_CMD=timeout
POLL_INTERVAL_SECONDS=1
PERIODIC_REARM_INTERVAL_SECONDS=60
DISPATCHED_ROLES=(worker)
main
'''

    def run_dispatch_loop(self, **env):
        log = self.root / "launches"
        log.write_text("")
        shutdown = self.root / "shutdown"
        shutdown.unlink(missing_ok=True)
        proc = self.shell(self.dispatch_loop_command(),
                          SUBJECT=str(SCRIPTS / "fleet-dispatcher"),
                          COMMON=str(SCRIPTS / "fleet-common.sh"),
                          FLEET_LIB_DIR=str(SCRIPTS), FLEET_RUNTIMES="codex",
                          FLEET_ALERTS_DIR=str(self.root / "alerts"),
                          LAUNCH_LOG=str(log), SHUTDOWN_FLAG=str(shutdown), **env)
        return proc, log

    def test_real_dispatch_loop_uses_user_admission(self):
        cases = ((identity, pool, count) for identity, count in (("app", 1), ("user", 0))
                 for pool in ("core", "graphql"))
        for identity, pool, count in cases:
            self.sample(identity, pool, 1.0)
            proc, log = self.run_dispatch_loop()
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(len(log.read_text().splitlines()), count, identity)
            self.sample(identity, pool)

    def test_failed_app_mint_skips_tick_without_stopping_dispatcher(self):
        self.stub("fleet-gh-token", "exit 1")
        proc, log = self.run_dispatch_loop(STOP_AFTER_SLEEPS="3")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(log.read_text(), "")
        alert = self.root / "alerts" / "dispatcher-app-auth"
        self.assertIn("3 consecutive ticks", alert.read_text())

    def test_missing_stale_wrong_identity_fail_only_their_lane(self):
        for fields in ({"observed_at": self.now - 7200}, {"identity": "app"}):
            path = self.sample("user", "core")
            record = json.loads(path.read_text())
            path.write_text(json.dumps({**record, **fields}))
            self.assertIn("closed:github_core[user] missing or stale", self.gate("shared"))
            self.assertTrue(self.gate("daemon").startswith("open:"))
        self.sample("user", "core").unlink()
        self.assertIn("missing or stale", self.gate("shared"))

    def test_refusals_do_not_overwrite_or_borrow_the_other_reset(self):
        self.sample("app", "graphql", resetsAt=self.now + 1000)
        self.sample("user", "graphql", resetsAt=self.now + 2000)
        for identity, token in (("app", "ghs_synthetic"), ("user", "")):
            with patch.dict(os.environ, {**self.env, "GH_TOKEN": token,
                                         "FLEET_GH_IDENTITY": identity}, clear=True):
                fallback.latch_refusal("synthetic refusal", self.usage, self.now)
        records = list(self.usage.glob("*.rejected.json"))
        self.assertEqual(len(records), 2)
        resets = {json.loads(p.read_text())["identity"]: json.loads(p.read_text())["resetsAt"]
                  for p in records}
        self.assertEqual(resets, {"app": self.now + 1000, "user": self.now + 2000})
        self.assertIn("rejected", self.gate("shared"))

    def test_status_names_every_lane_and_ignores_stale_legacy_breach(self):
        legacy = dict(identity="user", rateLimitType="github_graphql", utilization=1.0,
                      observed_at=self.now - 7200, resetsAt=self.now + 1000)
        (self.usage / "github-graphql.json").write_text(json.dumps(legacy))
        proc = self.shell('bash "$SUBJECT" --json', SUBJECT=str(SCRIPTS / "fleet-gate-status"),
                          FLEET_GH_APP_ID="1", FLEET_GH_APP_INSTALLATION_ID="2",
                          FLEET_GH_APP_KEY_PATH="/synthetic")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        data = json.loads(proc.stdout)
        self.assertEqual(data["gate"], "open")
        lanes = data.get("github_lane_identities", {})
        for lane in ("scout", "dispatcher", "queue-ingest", "cleanup", "reconcile",
                     "stalled-sweep", "pre-claim", "completion"):
            self.assertEqual(lanes.get(lane), "app", lane)
        for lane in ("dispatched-pane", "babysit", "pane-claim-mutation", "human-shell"):
            self.assertEqual(lanes.get(lane), "user", lane)
        self.assertEqual(data["github_lane_gates"]["pane"]["state"], "open")
        proc = self.shell('bash "$SUBJECT"', SUBJECT=str(SCRIPTS / "fleet-gate-status"),
                          FLEET_GH_APP_ID="1", FLEET_GH_APP_INSTALLATION_ID="2",
                          FLEET_GH_APP_KEY_PATH="/synthetic")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        for lane in lanes:
            self.assertIn(lane, proc.stdout)

    def test_reservation_survives_app_to_user_resume_and_release(self):
        def claim(args, identity):
            return self.shell('bash "$SUBJECT" ' + args,
                              SUBJECT=str(SCRIPTS / "fleet-claim"), FLEET_GH_IDENTITY=identity)

        self.assertEqual(claim("reserve 9001 pool-1", "app").returncode, 0)
        self.assertEqual(claim("reservation-of pool-1", "user").stdout.strip(), "9001")
        self.assertNotEqual(claim("reserve 9002 pool-1", "user").returncode, 0)
        self.assertEqual(claim("release-worktree pool-1", "user").returncode, 0)
        self.assertEqual(claim("reservation-of pool-1", "app").stdout.strip(), "")

    def test_maintenance_children_inherit_app_identity(self):
        from test_scout_degraded_fetch import _mod, _ScoutTickHarness

        charged = {}
        real_popen = subprocess.Popen

        def spawn(argv, *args, **kwargs):
            words = [str(word) for word in argv]
            lane = next((word for word in words if word in ("cleanup", "reconcile")),
                        Path(words[-1]).name)
            if any(word.endswith("fleet-stalled-sweep") for word in words):
                lane = "stalled-sweep"
            if any(word.endswith("fleet-queue-ingest") for word in words):
                lane = "queue-ingest"
            with patch.object(subprocess, "Popen", real_popen):
                proc = subprocess.run(["bash", str(self.bin / "gh"), "api", "user"],
                                      env=kwargs.get("env"), capture_output=True,
                                      text=True, timeout=10)
            charged[lane] = proc.stdout.strip()

        with patch.dict(os.environ, {**self.env, "FLEET_GH_IDENTITY": "app"}, clear=True), \
                patch.object(_mod, "USAGE_DIR", self.usage):
            _ScoutTickHarness()._tick(str(self.root), ["issue-1"], False, [], popen=spawn)
        self.assertEqual(charged, {lane: "app" for lane in
                                  ("cleanup", "reconcile", "stalled-sweep", "queue-ingest")})

    def test_wrapper_selects_user_before_launch_for_every_target_kind(self):
        self.stub("git", 'if [[ "$*" == *--show-toplevel* ]]; then pwd; fi')
        log = self.root / "auth.log"
        self.stub("tmux", 'gh api user >> "$AUTH_LOG"')
        for name in ("fleet-runtime", "ir-acquire", "fleet-quiet-wait", "fleet-claim"):
            self.stub(name, "exit 0")
        for kind in ("task", "review", "feedback", "conflict", "planreview", "plan"):
            log.write_text("")
            proc = self.shell('bash "$SUBJECT" pane-1 gpt-6-astra high worker "" live '
                              '"target=$KIND:engine:9001" codex fable',
                              SUBJECT=str(SCRIPTS / "fleet-dispatch-wrap"), KIND=kind,
                              FLEET_DISPATCH_PRINT_LAUNCH="1", AUTH_LOG=str(log))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("resumed=0", proc.stdout)
            self.assertTrue(log.read_text().splitlines())
            self.assertEqual(set(log.read_text().splitlines()), {"user"})

    def scout(self):
        loader = importlib.machinery.SourceFileLoader(
            "identity_scout", str(SCRIPTS / "fleet-state-scout"))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        scout = importlib.util.module_from_spec(spec)
        with patch.dict(os.environ, self.env, clear=True):
            loader.exec_module(scout)
        scout.USAGE_DIR = self.usage
        return scout

    def test_scout_core_and_graphql_probes_keep_pools_separate(self):
        scout = self.scout()
        seen = []

        def run(cmd, **kwargs):
            env = kwargs.get("env") or os.environ
            identity = "app" if env.get("GH_TOKEN", "").startswith("ghs_") else "user"
            seen.append(identity)
            used = 120 if identity == "app" else 40
            if "--include" in cmd:
                out = (f"HTTP/2.0 200 OK\nX-RateLimit-Limit: 5000\nX-RateLimit-Used: {used}\n"
                       f"X-RateLimit-Reset: {self.now + 3000}\nX-RateLimit-Resource: core\n\n{{}}")
            elif "graphql" in cmd:
                out = json.dumps({"data": {"rateLimit": {"limit": 5000, "used": used,
                                  "remaining": 5000 - used, "resetAt": "2030-01-01T00:00:00Z"}}})
            else:
                out = '{"resources":{}}'
            return subprocess.CompletedProcess(cmd, 0, out, "")

        with patch.dict(os.environ, {**self.env, "FLEET_GH_IDENTITY": "app"}, clear=True), \
                patch.object(scout.subprocess, "run", side_effect=run), \
                patch.object(scout, "last_rate_limit", return_value=None):
            scout.sample_github_rate_limit()
        self.assertIn("app", seen)
        self.assertIn("user", seen)
        for identity, remaining in (("app", 4880), ("user", 4960)):
            for pool in ("core", "graphql"):
                record = json.loads((self.usage / f"github-{identity}-{pool}.json").read_text())
                self.assertEqual(record["remaining"], remaining)

    def test_user_only_follower_probes_core_without_local_rest_traffic(self):
        scout = self.scout()
        self.assertTrue(hasattr(scout, "_sample_identity_core"))
        with patch.dict(os.environ, {**self.env, "FLEET_GH_IDENTITY": "user"}, clear=True), \
                patch.object(scout, "_sample_github_search"), \
                patch.object(scout, "_sample_github_core"), \
                patch.object(scout, "_sample_github_graphql"), \
                patch.object(scout, "last_rate_limit", return_value=None), \
                patch.object(scout, "_sample_identity_core") as probe:
            scout.sample_github_rate_limit()
        self.assertEqual(probe.call_count, 1)
        self.assertEqual(probe.call_args.args[0]["FLEET_GH_IDENTITY"], "user")

    def test_scout_gates_collection_on_app_only(self):
        scout = self.scout()
        for identity in ("user", "app"):
            self.sample(identity, "graphql", 1.0)
            with patch.dict(os.environ, {**self.env, "FLEET_GH_IDENTITY": "app"}, clear=True), \
                    patch.object(scout, "_refresh_gh_token", return_value=True), \
                    patch.object(scout, "sample_github_rate_limit"), \
                    patch.object(scout, "_spawn_failed"), patch.object(scout, "_spawn_ok"), \
                    patch.object(scout, "build_state",
                                 side_effect=RuntimeError("poll reached")) as poll:
                if identity == "user":
                    with self.assertRaisesRegex(RuntimeError, "poll reached"):
                        scout.tick_once()
                else:
                    try:
                        scout.tick_once()
                    except RuntimeError as exc:
                        self.fail(f"App exhaustion did not stop polling: {exc}")
                    poll.assert_not_called()
            self.sample(identity, "graphql")


if __name__ == "__main__":
    unittest.main()
