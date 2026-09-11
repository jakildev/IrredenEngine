"""fleet-health reads only the fleet dir it is pointed at and names the waste.

Fixture: the 2026-09-09 boot as the dispatcher, fleet-rebase, and scout logs
recorded it — four LLM merger iterations in eight minutes, every one a no-op,
each preceded by a tier-0 run whose llm_remaining counted human-owned PRs —
plus one Codex-attributed dispatch and a state.json with unstamped PRs.
"""

import datetime as dt
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager, redirect_stdout
from pathlib import Path

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-health"
if not SUBJECT.is_file():
    print("SKIP: fleet-health subject absent", file=sys.stderr)
    sys.exit(3)

import importlib.util  # noqa: E402

spec = importlib.util.spec_from_loader("fleet_health", loader=None)
fleet_health = importlib.util.module_from_spec(spec)
exec(compile(SUBJECT.read_text(), str(SUBJECT), "exec"), fleet_health.__dict__)  # noqa: S102


def _line(ts, source, msg):
    return f"[{ts} {source}] {msg}"


# The fixture's dispatcher boot. Every log line below sits on this day, so a
# window measured from real wall-clock now drifts off the fixture rather than
# tracking it (see #3132). Tests exercising a relative --since derive their
# pinned clock from this instant instead of naming a date.
FIXTURE_BOOT = "2026-09-09T04:40:51Z"


def fixture_clock(hours):
    """An ISO-8601 instant `hours` after the fixture's dispatcher boot."""
    base = dt.datetime.fromisoformat(FIXTURE_BOOT.replace("Z", "+00:00"))
    return (base + dt.timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%SZ")


@contextmanager
def pinned_clock(instant):
    """Pin the report's notion of now for the duration of the block."""
    os.environ["FLEET_HEALTH_NOW"] = instant
    try:
        yield
    finally:
        os.environ.pop("FLEET_HEALTH_NOW", None)


DISPATCHER_LOG = "\n".join([
    _line("2026-09-09T04:40:51Z", "dispatcher",
          "started (pid=61197, interval=10s, rearm=300s, "
          "timeout_cmd=gtimeout)"),
    _line("2026-09-09T04:40:53Z", "dispatcher",
          "merger: spawned tier-0 fleet-rebase (LLM pass only if it "
          "re-arms)"),
    _line("2026-09-09T04:40:53Z", "dispatcher", "dispatching epic-steward -> %28"),
    _line("2026-09-09T04:41:04Z", "dispatcher", "dispatching merger -> %30"),
    _line("2026-09-09T04:41:26Z", "dispatcher",
          "dispatching sonnet-reviewer -> %31 [target=review:engine:3103]"),
    _line("2026-09-09T04:43:41Z", "dispatcher",
          "dispatch for merger on %30 completed (pane returned to shell, "
          "outcome=no)"),
    _line("2026-09-09T04:44:17Z", "dispatcher",
          "dispatching worker -> %32 [class=opus effort=high "
          "target=conflict:engine:3081]"),
    _line("2026-09-09T04:44:17Z", "dispatcher",
          "merger: spawned tier-0 fleet-rebase (LLM pass only if it "
          "re-arms)"),
    _line("2026-09-09T04:44:28Z", "dispatcher", "dispatching merger -> %30"),
    _line("2026-09-09T04:45:20Z", "dispatcher",
          "dispatch for merger on %30 completed (pane returned to shell, "
          "outcome=no)"),
    _line("2026-09-09T04:45:53Z", "dispatcher",
          "dispatch for sonnet-reviewer on %31 completed (pane returned "
          "to shell, outcome=yes, target=review:engine:3103, "
          "verdict=finished)"),
    _line("2026-09-09T04:46:14Z", "dispatcher",
          "merger: spawned tier-0 fleet-rebase (LLM pass only if it "
          "re-arms)"),
    _line("2026-09-09T04:46:26Z", "dispatcher", "dispatching merger -> %31"),
    _line("2026-09-09T04:47:40Z", "dispatcher",
          "dispatch for merger on %31 completed (pane returned to shell, "
          "outcome=no)"),
    _line("2026-09-09T04:48:00Z", "dispatcher",
          "merger: spawned tier-0 fleet-rebase (LLM pass only if it "
          "re-arms)"),
    _line("2026-09-09T04:48:12Z", "dispatcher", "dispatching merger -> %31"),
    _line("2026-09-09T04:48:46Z", "dispatcher",
          "dispatch for merger on %31 completed (pane returned to shell, "
          "outcome=no)"),
    _line("2026-09-09T05:04:04Z", "dispatcher",
          "dispatching sonnet-reviewer -> %31 [target=review:game:900] "
          "runtime=codex"),
    _line("2026-09-09T05:07:31Z", "dispatcher",
          "dispatch for worker on %32 completed (pane returned to shell, "
          "outcome=yes, target=conflict:engine:3081, verdict=finished)"),
    _line("2026-09-09T05:08:30Z", "dispatcher",
          "dispatch for sonnet-reviewer on %31 completed (pane returned "
          "to shell, outcome=yes, target=review:game:900, "
          "verdict=finished, runtime=codex)"),
    _line("2026-09-09T05:09:00Z", "dispatcher",
          "worker: 3 consecutive dispatches claimed nothing "
          "(class=sonnet) — standing down; scout re-arms on new work"),
]) + "\n"

REBASE_LOG = """\
[2026-09-09T04:40:59Z fleet-rebase] done: attempted=2 cleared=1 merged=0 llm_remaining=5
[2026-09-09T04:40:59Z fleet-rebase] re-armed merger trigger for the LLM pass
[2026-09-09T04:44:21Z fleet-rebase] done: attempted=1 cleared=1 merged=0 llm_remaining=4
[2026-09-09T04:44:21Z fleet-rebase] re-armed merger trigger for the LLM pass
[2026-09-09T04:46:17Z fleet-rebase] done: attempted=1 cleared=1 merged=0 llm_remaining=5
[2026-09-09T04:46:17Z fleet-rebase] re-armed merger trigger for the LLM pass
[2026-09-09T04:48:04Z fleet-rebase] done: attempted=1 cleared=1 merged=0 llm_remaining=4
[2026-09-09T04:48:04Z fleet-rebase] re-armed merger trigger for the LLM pass
"""

SCOUT_LOG = """\
[2026-09-09T04:41:19Z state-scout] triggered: sonnet-reviewer
[2026-09-09T04:42:16Z state-scout] no triggers (all projections unchanged)
[2026-09-09T04:44:11Z state-scout] triggered: worker, merger
[2026-09-09T04:46:04Z state-scout] triggered: merger
[2026-09-09T04:47:58Z state-scout] triggered: queue-manager(claim-cleanup), merger, epic-steward
"""

STATE_JSON = {
    "generated_at": "2026-09-09T05:10:00Z",
    "repos": {
        "engine": {"prs": [
            {"number": 3081, "labels": ["fleet:semantic-conflict"], "mergeable": "CONFLICTING"},
            {"number": 3103, "labels": ["fleet:approved", "fleet:author-codex"],
             "mergeable": "MERGEABLE"},
        ]},
        "game": {"prs": [
            {"number": 901, "labels": [{"name": "fleet:approved"}], "mergeable": "MERGEABLE"},
        ]},
    },
}


def build_fleet_dir(root, conf="FLEET_RUNTIMES=\"claude,codex\"\nFLEET_CROSS_PROVIDER_REVIEW=1\n"):
    root = Path(root)
    (root / "logs").mkdir(parents=True)
    (root / "logs" / "dispatcher.log").write_text(DISPATCHER_LOG)
    (root / "logs" / "fleet-rebase.log").write_text(REBASE_LOG)
    (root / "logs" / "state-scout.log").write_text(SCOUT_LOG)
    state = root / "state"
    for d in ("dispatch", "empty-streak", "runtime-problems", "runtime-cooldown"):
        (state / d).mkdir(parents=True)
    (state / "dispatcher.pid").write_text(str(os.getpid()))
    (state / "scout.pid").write_text("999999999")
    (state / "dispatch-mode").write_text("live\n")
    (state / "state.json").write_text(json.dumps(STATE_JSON))
    (state / "empty-streak" / "merger__sonnet").write_text("1\n")
    # A pre-`runtime` record for a still-running epic-steward; its pool
    # sidecar names the provider.
    (state / "dispatch" / "pane-28.json").write_text(json.dumps({
        "role": "epic-steward", "pane": "%28", "class": "opus",
        "dispatched_at": "2026-09-09T04:40:53Z", "dispatched_epoch": 1788928853,
        "claim_marker": 1, "agent": "pool-1"}))
    (root / "sessions").mkdir()
    (root / "sessions" / "pool-1.session.json").write_text(json.dumps({
        "session_id": "x", "role": "epic-steward", "runtime": "claude",
        "created_epoch": 1788928853}))
    (root / "alerts").mkdir()
    (root / "alerts" / "witness-roster.stuck").write_text("roster=opus-architect count=4033\n")
    (root / "fleet-up.conf").write_text(conf)
    return root


class Env(unittest.TestCase):
    """Every path is derived from --fleet-dir; nothing reads the caller's ~/.fleet."""

    def setUp(self):
        self._saved = {k: os.environ.pop(k, None) for k in
                       ("FLEET_STATE_DIR", "FLEET_SESSIONS_DIR", "FLEET_ALERTS_DIR",
                        "FLEET_CONF", "FLEET_DIR", "FLEET_HEALTH_NOW")}
        self.tmp = tempfile.TemporaryDirectory()
        self.root = build_fleet_dir(Path(self.tmp.name) / "fleet")

    def tearDown(self):
        self.tmp.cleanup()
        for k, v in self._saved.items():
            # Pop first: a test that pins one of these must not leak it into
            # the next test when the caller's environment had it unset.
            os.environ.pop(k, None)
            if v is not None:
                os.environ[k] = v

    def run_report(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = fleet_health.main(["--fleet-dir", str(self.root), "--json", *args])
        return rc, json.loads(out.getvalue())

    def run_report_at(self, instant, *args):
        """run_report with the report's notion of now pinned to `instant`."""
        with pinned_clock(instant):
            return self.run_report(*args)


class Dispatches(Env):
    def test_window_defaults_to_the_dispatcher_boot(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["window"]["since"], "2026-09-09T04:40:51Z")

    def test_merger_no_ops_are_counted_and_warned(self):
        rc, rep = self.run_report()
        merger = rep["roles"]["merger"]
        self.assertEqual((merger["dispatches"], merger["productive"], merger["no_op"]), (4, 0, 4))
        self.assertEqual(merger["targeted"], 0)
        self.assertIn("merger: 4 of 4 completed dispatches did no work", rep["warnings"])
        self.assertEqual(rc, 1)

    def test_productive_roles_do_not_warn(self):
        rc, rep = self.run_report()
        rev = rep["roles"]["sonnet-reviewer"]
        self.assertEqual((rev["productive"], rev["no_op"], rev["targeted"]), (2, 0, 2))
        self.assertFalse(any(w.startswith("sonnet-reviewer:") for w in rep["warnings"]))

    def test_runtime_attribution(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["roles"]["sonnet-reviewer"]["runtimes"], {"codex": 1, "unknown": 1})
        # Running iteration with a pre-`runtime` record: sidecar by agent name.
        self.assertEqual(rep["roles"]["epic-steward"]["runtimes"], {"claude": 1})
        self.assertEqual(rep["roles"]["epic-steward"]["running"], 1)
        live = rep["live"][0]
        self.assertEqual((live["pane"], live["runtime"]), ("%28", "claude"))
        self.assertEqual(rep["runtimes"]["dispatch_split"]["codex"], 1)

    def test_durations(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["roles"]["merger"]["max_s"], 157)
        self.assertEqual(rep["roles"]["merger"]["no_op_median_s"], 63)

    def test_over_cap_launches_are_tallied_per_role(self):
        # An elastic-cap launch carries `over-cap=<cap>` after its target in
        # the same bracket; the target must still parse and the launch pair
        # normally with its completion.
        log = self.root / "logs" / "dispatcher.log"
        log.write_text(log.read_text() + "\n".join([
            _line("2026-09-09T05:05:00Z", "dispatcher",
                  "dispatching worker -> %33 [class=opus effort=high "
                  "target=task:engine:3110 over-cap=1] runtime=claude"),
            _line("2026-09-09T05:06:00Z", "dispatcher",
                  "dispatch for worker on %33 completed (pane returned to "
                  "shell, outcome=yes, target=task:engine:3110, "
                  "verdict=finished, runtime=claude)"),
        ]) + "\n")
        rc, rep = self.run_report()
        worker = rep["roles"]["worker"]
        self.assertEqual((worker["dispatches"], worker["over_cap"], worker["productive"]),
                         (2, 1, 2))
        over = [d for d in rep["dispatches"] if d["over_cap"]]
        self.assertEqual([(d["pane"], d["class"], d["target"], d["seconds"]) for d in over],
                         [("%33", "opus", "task:engine:3110", 60)])
        self.assertEqual(rep["roles"]["merger"]["over_cap"], 0)
        text = fleet_health.render_text(rep)
        self.assertIn("over-cap=1", text)
        self.assertNotIn("over-cap=0", text)


class TriggersAndLadder(Env):
    def test_trigger_sources(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["triggers"]["scout"]["merger"], 3)
        self.assertEqual(rep["triggers"]["scout"]["queue-manager"], 1)
        self.assertEqual(rep["triggers"]["tier0_spawns"], 4)
        self.assertEqual(rep["triggers"]["standdowns"], {"worker": 1})

    def test_merger_ladder_waste_is_named(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["rebase"]["runs"], 4)
        self.assertEqual(rep["rebase"]["llm_rearms"], 4)
        self.assertEqual(rep["rebase"]["totals"]["llm_remaining"], 18)
        self.assertTrue(any(w.startswith("merger ladder: tier-0 re-armed the LLM pass 4x")
                            for w in rep["warnings"]), rep["warnings"])

    def test_ladder_warning_needs_both_halves(self):
        # Re-arms with a productive LLM iteration are not waste.
        log = self.root / "logs" / "dispatcher.log"
        log.write_text(log.read_text().replace(
            "on %31 completed (pane returned to shell, outcome=no)",
            "on %31 completed (pane returned to shell, outcome=yes)", 1))
        rc, rep = self.run_report()
        self.assertFalse(any(w.startswith("merger ladder") for w in rep["warnings"]))


class Runtimes(Env):
    def test_unstamped_prs_warn_under_cross_provider_review(self):
        rc, rep = self.run_report()
        self.assertEqual(rep["runtimes"]["open_prs_unstamped"], ["engine#3081", "game#901"])
        self.assertEqual(rep["runtimes"]["open_prs_stamped"], 1)
        self.assertTrue(any("carry no fleet:author-* label" in w for w in rep["warnings"]))

    def test_unstamped_is_silent_without_codex(self):
        (self.root / "fleet-up.conf").write_text('FLEET_RUNTIMES="claude"\n')
        rc, rep = self.run_report()
        self.assertEqual(rep["runtimes"]["configured"], ["claude"])
        self.assertFalse(any("fleet:author-*" in w for w in rep["warnings"]))

    def test_routing_problems_and_gate_alerts_surface(self):
        (self.root / "state" / "runtime-problems" / "abc.json").write_text(json.dumps(
            {"key": "route:review:engine:3081", "reason": "unstamped PR", "count": 7}))
        (self.root / "alerts" / "dispatch-gate-sonnet-reviewer").write_text("reason=route-failed\n")
        rc, rep = self.run_report()
        self.assertIn("runtime routing problem: route:review:engine:3081: unstamped PR (x7)",
                      rep["warnings"])
        self.assertIn("standing dispatch-gate alert: dispatch-gate-sonnet-reviewer",
                      rep["warnings"])

    def test_codex_cooldown(self):
        (self.root / "state" / "runtime-cooldown" / "codex.json").write_text(
            json.dumps({"until": 4102444800}))
        rc, rep = self.run_report()
        self.assertEqual(rep["runtimes"]["codex_cooldown_until"], 4102444800)
        self.assertIn("codex provider is in cooldown", rep["warnings"])


class DaemonsAndWindow(Env):
    def test_dead_scout_pid_warns(self):
        rc, rep = self.run_report()
        self.assertTrue(rep["daemons"]["dispatcher"]["alive"])
        self.assertFalse(rep["daemons"]["scout"]["alive"])
        self.assertIn("state-scout is not running", rep["warnings"])

    def test_since_accepts_durations_and_iso(self):
        rc, rep = self.run_report("--since", "2026-09-09T05:00:00Z")
        self.assertEqual(set(rep["roles"]), {"sonnet-reviewer"})
        # A duration is measured from now, so the arm only means anything with
        # now pinned relative to the fixture: seven hours after the boot puts
        # every logged dispatch inside a 1-day window on any calendar date
        # (see #3132).
        rc, rep = self.run_report_at(fixture_clock(7), "--since", "1d")
        self.assertIn("merger", rep["roles"])

    def test_relative_window_drops_the_fixture_once_the_clock_moves_past_it(self):
        # The standing positive control for the arm above: the same "1d" that
        # reaches the fixture at boot+7h must stop reaching it at boot+48h.
        rc, rep = self.run_report_at(fixture_clock(48), "--since", "1d")
        self.assertNotIn("merger", rep["roles"])
        # The emptiness above is also what a real clock produces, so assert the
        # window boundary itself — boot+48h minus a day is a value only the
        # injected clock can yield.
        self.assertEqual(rep["window"]["since"], fixture_clock(24))

    def test_bad_pinned_clock_is_a_usage_error(self):
        out = io.StringIO()
        with pinned_clock("yesterday"), redirect_stdout(out):
            rc = fleet_health.main(["--fleet-dir", str(self.root), "--json"])
        self.assertEqual(rc, 2)

    def test_bad_since_is_a_usage_error(self):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = fleet_health.main(["--fleet-dir", str(self.root), "--since", "yesterday"])
        self.assertEqual(rc, 2)

    def test_clean_fleet_exits_zero(self):
        for name in ("dispatcher.log", "fleet-rebase.log", "state-scout.log"):
            (self.root / "logs" / name).write_text(
                "[2026-09-09T04:40:51Z dispatcher] started (pid=1)\n"
                if name == "dispatcher.log" else "")
        (self.root / "state" / "scout.pid").write_text(str(os.getpid()))
        (self.root / "fleet-up.conf").write_text('FLEET_RUNTIMES="claude"\n')
        rc, rep = self.run_report()
        self.assertEqual(rep["warnings"], [])
        self.assertEqual(rc, 0)

    def test_text_rendering_via_cli(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("FLEET_")}
        env["HOME"] = self.tmp.name
        res = subprocess.run([sys.executable, str(SUBJECT), "--fleet-dir", str(self.root)],
                             capture_output=True, text=True, env=env, timeout=30)
        self.assertEqual(res.returncode, 1, res.stderr)
        self.assertIn("## Merger ladder", res.stdout)
        self.assertIn("merger: 4 of 4 completed dispatches did no work", res.stdout)
        self.assertIn("witness-roster.stuck", res.stdout)


if __name__ == "__main__":
    unittest.main()
