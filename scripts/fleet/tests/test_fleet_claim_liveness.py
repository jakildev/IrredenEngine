"""Table tests for fleet_claim_liveness.verdict — the one keep/reap predicate
every sweep of a fleet:amending-* / fleet:claim-* label consults.

Every arm is driven from temp directories and an injected ``now``, so no case
depends on the wall clock or the live ~/.fleet. Each keep case is paired with
the control that differs in exactly the one signal the keep rests on.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUBJECT = HERE.parent / "fleet_claim_liveness.py"
if not SUBJECT.exists():
    print(f"SKIP: subject not found: {SUBJECT}", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(HERE.parent))
import fleet_claim_liveness as lv  # noqa: E402

NOW = 1_800_000_000
TTL = 1800
HOST = "mac"


class _Fixture:
    """A sandboxed host: heartbeats, dispatch records, dispatch-current,
    reservations, amend snapshots and FS claim locks under one temp root."""

    def __init__(self, root):
        self.root = Path(root)
        self.dirs = {k: str(self.root / k) for k in
                     ("heartbeats", "state", "reservations", "amend_snapshots", "claims")}
        for d in self.dirs.values():
            Path(d).mkdir(parents=True, exist_ok=True)
        (self.root / "state" / "dispatch").mkdir()
        (self.root / "state" / "dispatch-current").mkdir()

    def heartbeat(self, agent, age):
        p = Path(self.dirs["heartbeats"]) / agent
        p.write_text("")
        os.utime(p, (NOW - age, NOW - age))

    def current(self, agent, dispatch_id):
        (self.root / "state" / "dispatch-current" / agent).write_text(dispatch_id + "\n")

    def dispatch(self, pane, agent, target):
        rec = {"role": "worker", "pane": pane, "target": target, "agent": agent}
        (self.root / "state" / "dispatch" / f"pane-{pane}.json").write_text(json.dumps(rec))

    def reserve(self, agent, task_id):
        (Path(self.dirs["reservations"]) / f"{agent}.json").write_text(
            json.dumps({"task_id": str(task_id)}))

    def claim_lock(self, slug, owner, dispatch_id=None):
        d = Path(self.dirs["claims"]) / slug
        d.mkdir()
        (d / "owner").write_text(owner + "\n")
        if dispatch_id is not None:
            (d / "dispatch_id").write_text(dispatch_id + "\n")

    def snapshot(self, pr, agent, dispatch_id, epoch=NOW - 5000):
        (Path(self.dirs["amend_snapshots"]) / f"{pr}.json").write_text(json.dumps(
            {"pr": pr, "agent": agent, "dispatch_id": dispatch_id, "acquired_epoch": epoch}))

    def verdict(self, label, number, ns="engine", ttl=TTL, host=HOST):
        return lv.verdict(label, ns, number, this_host=host, ttl=ttl, now=NOW,
                          dirs=self.dirs)


class ParseLabel(unittest.TestCase):
    def test_parses_both_kinds_and_dashed_agents(self):
        self.assertEqual(lv.parse_claim_label("fleet:claim-mac-pool-1"), ("claim", "mac", "pool-1"))
        self.assertEqual(lv.parse_claim_label("fleet:amending-windows-opus-worker-2"),
                         ("amending", "windows", "opus-worker-2"))

    def test_rejects_other_prefixes_and_unknown_hosts(self):
        for label in ("fleet:reviewing-mac-pool-1", "fleet:claim-bsd-pool-1",
                      "fleet:claim-mac-", "fleet:sweep-cooldown"):
            self.assertIsNone(lv.parse_claim_label(label), label)


class ClaimVerdicts(unittest.TestCase):
    """fleet:claim-* on an issue."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.fx = _Fixture(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_fresh_heartbeat_with_matching_id_is_live(self):
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D1")
        self.fx.heartbeat("pool-9", 60)
        code, detail = self.fx.verdict("fleet:claim-mac-pool-9", 500)
        self.assertEqual((code, detail["arm"]), (lv.LIVE, "heartbeat"))

    def test_control_heartbeat_older_than_ttl_cannot_vouch(self):
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D1")
        self.fx.heartbeat("pool-9", TTL + 1)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.CANNOT_VOUCH)

    def test_the_calling_sweeps_ttl_is_the_heartbeat_window(self):
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D1")
        self.fx.heartbeat("pool-9", 3600)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500, ttl=1800)[0],
                         lv.CANNOT_VOUCH)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500, ttl=7200)[0], lv.LIVE)

    def test_live_dispatch_record_keeps_a_claim_with_a_stale_heartbeat(self):
        self.fx.claim_lock("500", "pool-9", "preclaim")
        self.fx.dispatch(3, "pool-9", "task:engine:500")
        code, detail = self.fx.verdict("fleet:claim-mac-pool-9", 500)
        self.assertEqual(code, lv.LIVE)
        self.assertEqual((detail["arm"], detail["target"]), ("dispatch", "task:engine:500"))

    def test_stack_target_with_a_base_suffix_counts(self):
        self.fx.dispatch(3, "pool-9", "stack:engine:500:3800")
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.LIVE)

    def test_control_dispatch_by_another_agent_or_item_or_namespace_does_not(self):
        self.fx.dispatch(3, "pool-8", "task:engine:500")
        self.fx.dispatch(4, "pool-9", "task:engine:501")
        self.fx.dispatch(5, "pool-9", "task:game:500")
        self.fx.dispatch(6, "pool-9", "feedback:engine:500")
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.CANNOT_VOUCH)

    def test_fresh_heartbeat_with_superseded_id_and_no_reservation_cannot_vouch(self):
        # The pane-renewal guard: a later role in the same pane refreshes the
        # heartbeat, so the heartbeat alone must not keep a dead claim.
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        code, detail = self.fx.verdict("fleet:claim-mac-pool-9", 500)
        self.assertEqual((code, detail["arm"]), (lv.CANNOT_VOUCH, "identity-mismatch"))

    def test_reservation_naming_the_item_restores_identity(self):
        # A reservation resume runs under a new dispatch id; the reserved pane
        # is dispatched only to resume this item.
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.fx.reserve("pool-9", 500)
        code, detail = self.fx.verdict("fleet:claim-mac-pool-9", 500)
        self.assertEqual((code, detail["arm"]), (lv.LIVE, "heartbeat+reservation"))

    def test_control_reservation_naming_another_item_does_not(self):
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.fx.reserve("pool-9", 777)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.CANNOT_VOUCH)

    def test_absent_ids_leave_the_heartbeat_to_decide(self):
        self.fx.claim_lock("500", "pool-9")           # no dispatch_id stamped
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.LIVE)
        self.fx.claim_lock("501", "pool-7", "D1")     # no dispatch-current
        self.fx.heartbeat("pool-7", 10)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-7", 501)[0], lv.LIVE)

    def test_the_preclaim_sentinel_is_an_id_that_matches_no_dispatch(self):
        self.fx.claim_lock("500", "pool-9", "preclaim")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.CANNOT_VOUCH)

    def test_a_superseded_task_claim_is_never_confirmed_dead(self):
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D2")
        self.assertNotEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.DEAD)

    def test_a_lock_owned_by_another_agent_carries_no_identity_for_this_one(self):
        self.fx.claim_lock("500", "pool-3", "D1")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 500)[0], lv.LIVE)

    def test_game_namespace_reads_the_game_slug(self):
        self.fx.claim_lock("game-45", "pool-9", "D1")
        self.fx.current("pool-9", "D2")
        self.fx.heartbeat("pool-9", 10)
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 45, ns="game")[0],
                         lv.CANNOT_VOUCH)
        self.fx.dispatch(2, "pool-9", "task:game:45")
        self.assertEqual(self.fx.verdict("fleet:claim-mac-pool-9", 45, ns="game")[0], lv.LIVE)

    def test_cross_host_is_never_judged_here(self):
        # A same-basename local pane is live on every signal; none of them are
        # the windows pane's.
        self.fx.claim_lock("500", "pool-9", "D1")
        self.fx.current("pool-9", "D1")
        self.fx.heartbeat("pool-9", 1)
        self.fx.dispatch(1, "pool-9", "task:engine:500")
        code, detail = self.fx.verdict("fleet:claim-windows-pool-9", 500)
        self.assertEqual((code, detail["arm"]), (lv.CANNOT_VOUCH, "cross-host"))

    def test_unparsable_label_cannot_vouch(self):
        self.assertEqual(self.fx.verdict("fleet:reviewing-mac-pool-9", 500)[0], lv.CANNOT_VOUCH)


class AmendingVerdicts(unittest.TestCase):
    """fleet:amending-* on a PR: the same predicate plus the pre-claim grace
    and the confirmed-dead arm."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.fx = _Fixture(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_superseded_dispatch_is_confirmed_dead(self):
        self.fx.snapshot(900, "pool-2", "D1")
        self.fx.current("pool-2", "D2")
        self.fx.heartbeat("pool-2", 5)
        code, detail = self.fx.verdict("fleet:amending-mac-pool-2", 900)
        self.assertEqual(code, lv.DEAD)
        self.assertEqual((detail["owner_dispatch"], detail["superseded_by"]), ("D1", "D2"))

    def test_live_feedback_dispatch_outranks_a_stale_heartbeat(self):
        self.fx.snapshot(900, "pool-2", "D1")
        self.fx.current("pool-2", "D1")
        self.fx.heartbeat("pool-2", TTL * 10)
        self.fx.dispatch(2, "pool-2", "feedback:engine:900")
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.LIVE)

    def test_control_a_task_dispatch_does_not_vouch_for_an_amend(self):
        self.fx.snapshot(900, "pool-2", "D1")
        self.fx.current("pool-2", "D1")
        self.fx.heartbeat("pool-2", TTL * 10)
        self.fx.dispatch(2, "pool-2", "task:engine:900")
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.CANNOT_VOUCH)

    def test_fresh_preclaim_is_live_and_never_superseded(self):
        self.fx.snapshot(900, "pool-2", "preclaim", epoch=NOW - 30)
        self.fx.current("pool-2", "D7")
        code, detail = self.fx.verdict("fleet:amending-mac-pool-2", 900)
        self.assertEqual((code, detail["arm"]), (lv.LIVE, "preclaim"))

    def test_preclaim_past_its_grace_needs_the_dispatch_record(self):
        self.fx.snapshot(900, "pool-2", "preclaim", epoch=NOW - 900)
        self.fx.current("pool-2", "D7")
        self.fx.heartbeat("pool-2", 5)
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.CANNOT_VOUCH)
        self.fx.dispatch(2, "pool-2", "feedback:engine:900")
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.LIVE)

    def test_matching_dispatch_and_fresh_heartbeat_is_live(self):
        self.fx.snapshot(900, "pool-2", "D1")
        self.fx.current("pool-2", "D1")
        self.fx.heartbeat("pool-2", 5)
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.LIVE)

    def test_no_snapshot_leaves_the_heartbeat_to_decide(self):
        self.fx.heartbeat("pool-2", 5)
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.LIVE)
        self.fx.heartbeat("pool-2", TTL + 5)
        self.assertEqual(self.fx.verdict("fleet:amending-mac-pool-2", 900)[0], lv.CANNOT_VOUCH)


class Cli(unittest.TestCase):
    """The bash seam: one LF-terminated tab line, exit 0; misuse prints
    nothing on stdout and exits non-zero, so it can never read as a verdict."""

    def _run(self, *args, env=None):
        return subprocess.run([sys.executable, str(SUBJECT), *args], capture_output=True,
                              env={**os.environ, **(env or {})})

    def test_prints_one_lf_line_with_placeholders_for_empty_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            fx = _Fixture(tmp)
            fx.claim_lock("500", "pool-9", "D1")
            fx.dispatch(3, "pool-9", "task:engine:500")
            flags = []
            for key, flag in (("heartbeats", "--heartbeats-dir"), ("state", "--state-dir"),
                              ("reservations", "--reservations-dir"),
                              ("amend_snapshots", "--amend-snapshots-dir"),
                              ("claims", "--claims-dir")):
                flags += [flag, fx.dirs[key]]
            p = self._run("verdict", "fleet:claim-mac-pool-9", "engine", "500", "mac", "1800",
                          *flags, "--now", str(NOW))
            self.assertEqual(p.returncode, 0, p.stderr)
            # Byte-level: a CR would ride into bash's last `read` field.
            self.assertEqual(p.stdout, b"0\tdispatch\t-\t-\ttask:engine:500\n")

    def test_usage_error_prints_no_verdict(self):
        p = self._run("verdict", "fleet:claim-mac-pool-9", "engine", "x", "mac", "1800")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(p.stdout, b"")
        p = self._run("verdict", "fleet:claim-mac-pool-9", "engine", "500", "mac", "1800",
                      "--state-dir")
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(p.stdout, b"")


if __name__ == "__main__":
    unittest.main()
