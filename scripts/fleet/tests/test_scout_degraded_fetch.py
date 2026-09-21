"""Tests for degraded-fetch handling in fleet-state-scout.

Covers: failed fetch → last-known-good preserved + degraded marker;
clean empty fetch → not degraded; no-previous-state first-run fallback;
a `200 []` over a populated label-filtered slice held for one tick and
written through only when the next tick repeats it;
the degraded SKIP in the scout-spawned lanes leaving pending work intact
(#2965); and periodic claim cleanup independent of queue projection changes.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

collect_state = _mod.collect_state
STATE_FILE = _mod.STATE_FILE

_SAMPLE_PR = {"number": 42, "title": "foo", "headRefName": "feat/foo",
              "baseRefName": "master", "author": "bot", "labels": [],
              "mergeable": "MERGEABLE", "isDraft": False, "reviews": [],
              "updatedAt": "2026-06-11T00:00:00Z"}

_SAMPLE_TASK_QUEUE = {
    "open": [{"id": "#99", "title": "t", "status": " ", "model": "sonnet",
              "owner": "free", "blocked_by": "(none)", "blocked": False,
              "area": None, "effort": None, "issue": "#99"}],
    "in_progress": [],
    "done": [],
}


class TestScoutDegradedFetch(unittest.TestCase):

    def setUp(self):
        # collect_state also fetches engine plan_review, which these tests don't
        # otherwise stub. fetch_plan_review now goes through conditional_get
        # (REST + ETag cache), so leaving it live would hit the real GitHub API
        # and write the shared ~/.fleet ETag cache on every run — the same
        # hermeticity hazard the #2227 review flagged for fetch_task_queue.
        # Stub it to a clean empty result so the degraded assertions below key
        # only on the fetcher each test deliberately fails.
        patcher = patch.object(_mod, "fetch_plan_review", return_value=[])
        patcher.start()
        self.addCleanup(patcher.stop)

    def _write_prev_state(self, tmp_dir, prs=None, tasks=None):
        """Write a minimal previous state.json for fallback tests."""
        state = {
            "generated_at": "2026-06-11T19:00:00Z",
            "repos": {
                "engine": {
                    "path": str(Path.home() / "src" / "IrredenEngine"),
                    "prs": prs if prs is not None else [_SAMPLE_PR],
                    "needs_plan": [],
                    "human_approved": [],
                    "closed_fleet_queued": [],
                    "recent_merged_prs": [],
                    "tasks": tasks if tasks is not None else _SAMPLE_TASK_QUEUE,
                    "epics": [],
                }
            },
        }
        state_file = Path(tmp_dir) / "state.json"
        state_file.write_text(json.dumps(state))
        return state_file

    def test_failed_pr_fetch_preserves_last_known_good(self):
        """fetch_prs returns None → last-known-good prs[] preserved + degraded marked."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=None), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.prs", state["degraded"])
        # Data preserved from previous snapshot
        self.assertEqual(state["repos"]["engine"]["prs"], [_SAMPLE_PR])

    def test_clean_empty_pr_fetch_not_degraded(self):
        """fetch_prs returns [] (genuine empty) → no degraded marker."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertNotIn("degraded", state)
        self.assertEqual(state["repos"]["engine"]["prs"], [])

    def test_failed_task_fetch_preserves_last_known_good(self):
        """fetch_task_queue returns None → last-known-good tasks preserved + degraded marked."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=None), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.tasks", state["degraded"])
        self.assertEqual(
            state["repos"]["engine"]["tasks"]["open"],
            _SAMPLE_TASK_QUEUE["open"],
        )

    def test_no_previous_state_failed_fetch_uses_empty_fallback(self):
        """First run + failed fetch → empty fallback used + degraded marked."""
        missing = Path("/tmp/__fleet_state_missing_9999.json")
        with patch.object(_mod, "STATE_FILE", missing), \
             patch.object(_mod, "fetch_prs", return_value=None), \
             patch.object(_mod, "fetch_needs_plan", return_value=[]), \
             patch.object(_mod, "fetch_human_approved", return_value=[]), \
             patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
             patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
             patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
             patch.object(_mod, "fetch_epics", return_value=[]), \
             patch.object(_mod, "GAME", missing.parent / "no-game"):
            state = collect_state()

        self.assertIn("degraded", state)
        self.assertIn("engine.prs", state["degraded"])
        # No previous data → empty fallback
        self.assertEqual(state["repos"]["engine"]["prs"], [])

    def test_multiple_failed_sections_all_listed(self):
        """Multiple failed sections all appear in degraded list."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=None), \
                 patch.object(_mod, "fetch_needs_plan", return_value=None), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertIn("engine.prs", state["degraded"])
        self.assertIn("engine.needs_plan", state["degraded"])

    def test_no_failures_no_degraded_key(self):
        """All fetches succeed → no 'degraded' key at all."""
        with tempfile.TemporaryDirectory() as tmp:
            prev_file = self._write_prev_state(tmp)
            with patch.object(_mod, "STATE_FILE", prev_file), \
                 patch.object(_mod, "fetch_prs", return_value=[_SAMPLE_PR]), \
                 patch.object(_mod, "fetch_needs_plan", return_value=[]), \
                 patch.object(_mod, "fetch_human_approved", return_value=[]), \
                 patch.object(_mod, "fetch_closed_fleet_queued", return_value=[]), \
                 patch.object(_mod, "fetch_recent_merged_prs", return_value=[]), \
                 patch.object(_mod, "fetch_task_queue", return_value=_SAMPLE_TASK_QUEUE), \
                 patch.object(_mod, "fetch_epics", return_value=[]), \
                 patch.object(_mod, "GAME", Path(tmp) / "no-game"):
                state = collect_state()

        self.assertNotIn("degraded", state)


_SAMPLE_EPIC = {"number": 7, "title": "umbrella", "labels": ["fleet:epic"],
                "checklist": [], "managed": True}
_SAMPLE_ISSUE = {"number": 8, "title": "issue", "labels": []}

# Every fetcher collect_state fans out, with the clean value each test
# starts from; a test overrides the one slice it drives.
_CLEAN_FETCHERS = {
    "fetch_prs": [_SAMPLE_PR],
    "fetch_needs_plan": [],
    "fetch_human_approved": [],
    "fetch_closed_fleet_queued": [],
    "fetch_recent_merged_prs": [],
    "fetch_task_queue": _SAMPLE_TASK_QUEUE,
    "fetch_epics": [],
}


class TestTransientEmptyHold(unittest.TestCase):
    """A `200 []` over a populated slice is held for one tick.

    `_rest_list` returns None only on a poll failure, so a label-filtered list
    that answers `200 []` while the last snapshot held rows was written through
    as a clean empty — purging the umbrellas' detail caches and firing the
    epic-steward edge on a snapshot that had not changed. The only signal that
    separates that transient from a genuinely emptied label set is the
    non-empty → empty transition itself, so collect_state holds last-known-good
    for that tick and accepts the empty when the next tick repeats it.
    """

    def setUp(self):
        patcher = patch.object(_mod, "fetch_plan_review", return_value=[])
        patcher.start()
        self.addCleanup(patcher.stop)

    def _prev_state(self, tmp, held_empty=None, **fields):
        repo = {
            "path": str(Path.home() / "src" / "IrredenEngine"),
            "prs": [_SAMPLE_PR],
            "needs_plan": [],
            "plan_review": [],
            "human_approved": [],
            "closed_fleet_queued": [],
            "recent_merged_prs": [],
            "tasks": _SAMPLE_TASK_QUEUE,
            "epics": [],
        }
        repo.update(fields)
        state = {"generated_at": "2026-09-16T14:00:00Z", "repos": {"engine": repo}}
        if held_empty:
            state["held_empty"] = list(held_empty)
        state_file = Path(tmp) / "state.json"
        state_file.write_text(json.dumps(state))
        return state_file

    def _collect(self, prev_file, logs=None, **overrides):
        fetchers = dict(_CLEAN_FETCHERS)
        fetchers.update(overrides)
        with ExitStack() as es:
            es.enter_context(patch.object(_mod, "STATE_FILE", prev_file))
            es.enter_context(
                patch.object(_mod, "GAME", prev_file.parent / "no-game"))
            for name, value in fetchers.items():
                es.enter_context(patch.object(_mod, name, return_value=value))
            if logs is not None:
                es.enter_context(patch.object(
                    _mod, "log", lambda msg, *a, **kw: logs.append(str(msg))))
            return collect_state()

    def test_slice_is_empty_shapes(self):
        empty_tasks = {"open": [], "in_progress": [], "done": [], "plan_gated": []}
        self.assertTrue(_mod._slice_is_empty("epics", []))
        self.assertFalse(_mod._slice_is_empty("epics", [_SAMPLE_EPIC]))
        self.assertTrue(_mod._slice_is_empty("tasks", empty_tasks))
        # `done` is filled after the fetch, so it never makes the slice non-empty.
        self.assertTrue(_mod._slice_is_empty(
            "tasks", dict(empty_tasks, done=[{"id": "#1"}])))
        self.assertFalse(_mod._slice_is_empty(
            "tasks", dict(empty_tasks, plan_gated=[99])))

    def test_empty_over_populated_epics_is_held_and_degraded(self):
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=[_SAMPLE_EPIC])
            logs = []
            state = self._collect(prev, logs=logs, fetch_epics=[])

        self.assertEqual(state["repos"]["engine"]["epics"], [_SAMPLE_EPIC])
        self.assertEqual(state["degraded"], ["engine.epics"])
        self.assertEqual(state["held_empty"], ["engine.epics"])
        self.assertTrue(any(
            ln.startswith("degraded: preserving last-known-good for engine.epics")
            for ln in logs), logs)

    def test_repeated_empty_on_next_tick_is_written_through(self):
        """The genuinely-empty arm: the confirmation tick writes []."""
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=[_SAMPLE_EPIC],
                                    held_empty=["engine.epics"])
            state = self._collect(prev, fetch_epics=[])

        self.assertEqual(state["repos"]["engine"]["epics"], [])
        self.assertNotIn("degraded", state)
        self.assertNotIn("held_empty", state)

    def test_recovered_list_on_next_tick_clears_the_hold(self):
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=[_SAMPLE_EPIC],
                                    held_empty=["engine.epics"])
            fresh = [dict(_SAMPLE_EPIC, title="renamed")]
            state = self._collect(prev, fetch_epics=fresh)

        self.assertEqual(state["repos"]["engine"]["epics"], fresh)
        self.assertNotIn("degraded", state)
        self.assertNotIn("held_empty", state)

    def test_held_slice_is_byte_identical_to_the_previous_snapshot(self):
        """No lane edge fires on the held tick: the projected input is the
        prior slice verbatim, so every stable_hash(projector(state)) repeats."""
        epics = [_SAMPLE_EPIC, dict(_SAMPLE_EPIC, number=9, title="second")]
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=epics)
            state = self._collect(prev, fetch_epics=[])

        self.assertEqual(_mod.stable_hash(state["repos"]["engine"]["epics"]),
                         _mod.stable_hash(epics))

    def test_every_label_list_field_takes_the_hold(self):
        for fetcher, field in (("fetch_needs_plan", "needs_plan"),
                               ("fetch_human_approved", "human_approved"),
                               ("fetch_epics", "epics")):
            with self.subTest(field=field), \
                 tempfile.TemporaryDirectory() as tmp:
                prev = self._prev_state(tmp, **{field: [_SAMPLE_ISSUE]})
                state = self._collect(prev, **{fetcher: []})
                self.assertEqual(state["repos"]["engine"][field], [_SAMPLE_ISSUE])
                self.assertEqual(state["degraded"], [f"engine.{field}"])

    def test_tasks_dict_empty_shape_is_held(self):
        """The tasks slice's empty shape is the four-bucket dict, not []."""
        empty_tasks = {"open": [], "in_progress": [], "done": [],
                       "plan_gated": []}
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp)
            state = self._collect(prev, fetch_task_queue=empty_tasks)

        self.assertEqual(state["repos"]["engine"]["tasks"]["open"],
                         _SAMPLE_TASK_QUEUE["open"])
        self.assertEqual(state["degraded"], ["engine.tasks"])
        self.assertEqual(state["held_empty"], ["engine.tasks"])

    def test_tasks_with_only_plan_gated_rows_is_not_empty(self):
        """A queue whose every row is plan-gated still fetched rows."""
        gated_only = {"open": [], "in_progress": [], "done": [],
                      "plan_gated": [99]}
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp)
            state = self._collect(prev, fetch_task_queue=gated_only)

        self.assertEqual(state["repos"]["engine"]["tasks"], gated_only)
        self.assertNotIn("degraded", state)

    def test_empty_over_empty_is_not_held(self):
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=[])
            state = self._collect(prev, fetch_epics=[])

        self.assertEqual(state["repos"]["engine"]["epics"], [])
        self.assertNotIn("degraded", state)
        self.assertNotIn("held_empty", state)

    def test_first_run_empty_is_written_through(self):
        missing = Path("/tmp/__fleet_state_missing_3459.json")
        state = self._collect(missing, fetch_epics=[])

        self.assertEqual(state["repos"]["engine"]["epics"], [])
        self.assertNotIn("degraded", state)

    def test_errored_fetch_after_a_hold_keeps_the_errored_arm(self):
        """The errored arm is untouched: None still preserves + degrades, and it
        does not count as the confirming empty."""
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp, epics=[_SAMPLE_EPIC],
                                    held_empty=["engine.epics"])
            state = self._collect(prev, fetch_epics=None)

        self.assertEqual(state["repos"]["engine"]["epics"], [_SAMPLE_EPIC])
        self.assertEqual(state["degraded"], ["engine.epics"])
        self.assertNotIn("held_empty", state)

    def test_prs_keep_the_single_tick_genuine_empty_contract(self):
        """The open-PR set is read through its own change-detector + GraphQL
        path and empties routinely; it is deliberately not held."""
        with tempfile.TemporaryDirectory() as tmp:
            prev = self._prev_state(tmp)
            state = self._collect(prev, fetch_prs=[])

        self.assertEqual(state["repos"]["engine"]["prs"], [])
        self.assertNotIn("degraded", state)


class _ScoutTickHarness:
    """One hermetic `tick_once()` driver shared by both edge-consumption suites.

    Not a TestCase — mixed into the two suites below so the harness exists
    once. `popen` and `logs` are optional because only the spawn-failure suite
    needs to make Popen raise and read back the emitted log lines.
    """

    def setUp(self):
        # The spawn-failure streak is a module global (the scout is a loop), so
        # it has to be reset or a streak leaks into the next test. getattr, not
        # a bare attribute: this mixin also drives the pre-existing #2965 cases,
        # and those must still pass against a pre-#2972 ref so the positive
        # control scores THIS change's tests rather than an import-time break.
        streak = getattr(_mod, "_spawn_fail_streak", None)
        if streak is not None:
            streak.clear()
            self.addCleanup(streak.clear)

    def _tick(self, tmp, projection, degraded, spawns, popen=None, logs=None,
              served=False, game_dir=None):
        """Run one tick_once() against a hermetic state dir.

        `spawns` accumulates each subprocess.Popen argv so a caller can count
        real spawns per lane. `popen` overrides the recorder (used to raise).
        `logs`, when given, accumulates the tick's log lines. `served` drives
        the follower arm (`authoritative = not served`), and `game_dir` stands
        in for a present game clone. Returns nothing — assertions read those
        lists and the seen-hash files under tmp.
        """
        state = {"generated_at": "2026-08-08T00:00:00Z", "repos": {}}
        if degraded:
            state["degraded"] = ["engine.tasks"]
        # write_atomic keeps its temp beside the target, so every directory it
        # writes into has to exist (production creates these at startup).
        for sub in ("seen-hashes", "triggers", "projections", "alerts"):
            (Path(tmp) / sub).mkdir(exist_ok=True)

        def _popen(argv, *a, **kw):
            spawns.append(argv)
            return None

        def _log(msg, *a, **kw):
            if logs is not None:
                logs.append(str(msg))

        # Both lanes are keyed on role name, so a two-entry PROJECTORS dict
        # exercises exactly the branches under test and nothing else.
        projectors = {
            "queue-manager": lambda _s: projection,
            "queue-manager-ingest": lambda _s: projection,
        }
        with ExitStack() as es:
            p = es.enter_context
            p(patch.object(_mod, "STATE_FILE", Path(tmp) / "state.json"))
            p(patch.object(_mod, "SEEN_DIR", Path(tmp) / "seen-hashes"))
            p(patch.object(_mod, "TRIGGERS_DIR", Path(tmp) / "triggers"))
            p(patch.object(_mod, "PROJECTIONS_DIR", Path(tmp) / "projections"))
            # Keep the alert file out of the real ~/.fleet/alerts.
            p(patch.object(_mod, "_alerts_dir", lambda: Path(tmp) / "alerts"))
            p(patch.object(_mod, "PROJECTORS", projectors))
            p(patch.object(_mod, "SLICERS", {}))
            p(patch.object(_mod, "GAME", game_dir or Path(tmp) / "no-game"))
            p(patch.object(_mod, "build_state", return_value=(state, served)))
            p(patch.object(_mod.subprocess, "Popen", popen or _popen))
            p(patch.object(_mod, "log", _log))
            for fn in ("_refresh_gh_token", "sample_github_rate_limit",
                       "refresh_all_details", "_populate_done_tasks",
                       "resolve_blocked_by",
                       "resolve_human_approved_blockers",
                       "resolve_needs_plan_blocked_by", "resolve_epic_children",
                       "enrich_inflight_pr_tasks", "enrich_stackable_blocker_prs"):
                p(patch.object(_mod, fn))
            _mod.tick_once()

    @staticmethod
    def _ingest_spawns(spawns):
        return [a for a in spawns if any("fleet-queue-ingest" in str(x) for x in a)]

    @staticmethod
    def _cleanup_spawns(spawns):
        return [a for a in spawns if any("cleanup" in str(x) for x in a)]

    def _seen(self, tmp, role):
        f = Path(tmp) / "seen-hashes" / role
        return f.read_text().strip() if f.exists() else None

    def _alert(self, tmp, lane):
        f = Path(tmp) / "alerts" / f"state-scout-spawn-{lane}"
        return f.read_text() if f.exists() else None


class TestDegradedSkipPreservesEdge(_ScoutTickHarness, unittest.TestCase):
    """#2965: the degraded skip must NOT consume the projection edge.

    `queue-manager` reconcile and `queue-manager-ingest` inline their own hash
    compare instead of routing through update_role_trigger. Recording the
    seen-hash before the degraded check therefore dropped the work permanently:
    the next tick compared equal and skipped. Periodic cleanup has the same
    contract for its deadline marker. Observed live as an agent-approved issue
    left unqueued for 8h14m after a single degraded tick.
    """

    def test_degraded_skip_spawns_nothing_and_leaves_hash_unwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns)

            self.assertEqual(self._ingest_spawns(spawns), [])
            self.assertEqual(self._cleanup_spawns(spawns), [])
            # The edge must still be pending — an unwritten hash is what makes
            # the next clean tick re-fire.
            self.assertIsNone(self._seen(tmp, "queue-manager-ingest"))
            self.assertIsNone(self._seen(tmp, "queue-manager"))

    def test_edge_survives_degraded_tick_and_fires_on_recovery(self):
        """The regression: SAME projection, degraded then clean → still fires."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns)
            self.assertEqual(self._ingest_spawns(spawns), [])

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)
            self.assertIsNotNone(self._seen(tmp, "queue-manager-ingest"))

    def test_clean_change_spawns_and_writes_hash(self):
        """Control: the non-degraded path is unchanged."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)

            self.assertEqual(len(self._ingest_spawns(spawns)), 1)
            self.assertIsNotNone(self._seen(tmp, "queue-manager-ingest"))
            # Unchanged projection on the next tick must NOT re-fire.
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)

    def test_persistent_degradation_yields_exactly_one_spawn_on_recovery(self):
        """N degraded ticks spawn nothing; recovery spawns once, not N times."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            for _ in range(3):
                self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns)
            self.assertEqual(self._ingest_spawns(spawns), [])

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)


class TestPeriodicClaimCleanup(_ScoutTickHarness, unittest.TestCase):
    """#2476: cleanup must not depend on a queue-manager projection edge."""

    def test_unchanged_projection_reaps_again_after_interval(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)

            marker = Path(tmp) / "seen-hashes" / _mod.CLAIM_CLEANUP_MARKER_NAME
            marker.write_text(
                f"{int(marker.read_text()) - _mod.CLAIM_CLEANUP_INTERVAL_SECONDS}\n"
            )
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)

            self.assertEqual(
                len(self._cleanup_spawns(spawns)), 2,
                "elapsed cleanup cadence must fire with an unchanged projection",
            )


def _raising_popen(attempts, exc=None):
    """A Popen that records the attempt, then fails the way a real host does.

    EAGAIN is the observed production shape (fork refused under many-pane
    load); FileNotFoundError during an install window is the other.
    """
    err = exc or OSError(35, "Resource temporarily unavailable")

    def _p(argv, *a, **kw):
        attempts.append(argv)
        raise err
    return _p


def _partial_popen(attempts, spawns, fail_when):
    """Succeed for some argvs and raise for others — the queue-manager lane
    fires several commands per firing, so partial failure is reachable."""
    def _p(argv, *a, **kw):
        attempts.append(argv)
        if any(fail_when in str(x) for x in argv):
            raise OSError(35, "Resource temporarily unavailable")
        spawns.append(argv)
        return None
    return _p


class TestSpawnFailurePreservesEdge(_ScoutTickHarness, unittest.TestCase):
    """#2972: a FAILED SPAWN must not consume the projection edge either.

    #2965 moved the seen-hash write below the `degraded` guard. It was still
    above `subprocess.Popen`, and both lanes swallow a spawn failure with a bare
    log — so a tick whose spawn raised recorded the hash and dropped the work
    permanently, exactly as the degraded tick used to. Reachable in production
    two ways, both observed classes: EAGAIN when fork is refused under many-pane
    load, and FileNotFoundError during an install/upgrade window where
    _fleet_script_argv's target is briefly absent.
    """

    def test_failed_spawn_leaves_hash_unwritten_both_lanes(self):
        """Neither lane may stamp its seen-hash when the spawn raised."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts = [], []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       popen=_raising_popen(attempts))

            # It genuinely tried — this is a failed spawn, not a skipped lane.
            self.assertTrue(attempts)
            # ...and nothing actually started.
            self.assertEqual(spawns, [])
            # The edge must still be pending for BOTH lanes.
            self.assertIsNone(self._seen(tmp, "queue-manager-ingest"))
            self.assertIsNone(self._seen(tmp, "queue-manager"))
            self.assertIsNone(
                self._seen(tmp, _mod.CLAIM_CLEANUP_MARKER_NAME))

    def test_edge_survives_failed_spawn_and_fires_once_on_recovery(self):
        """The regression: SAME projection, failing then healthy → still fires,
        exactly once per lane."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts = [], []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       popen=_raising_popen(attempts))
            self.assertEqual(spawns, [])

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)
            self.assertIsNotNone(self._seen(tmp, "queue-manager-ingest"))
            self.assertIsNotNone(self._seen(tmp, "queue-manager"))

            # And the recovered edge is properly consumed — no re-fire.
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)

    def test_persistent_spawn_failure_then_recovery_spawns_once(self):
        """N failing ticks spawn nothing; recovery spawns once, not N times."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts = [], []
            for _ in range(3):
                self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                           popen=_raising_popen(attempts))
            self.assertEqual(spawns, [])

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)

    def test_partial_sweep_failure_leaves_hash_unwritten_and_reruns_all(self):
        """queue-manager fires several commands; the stated rule is all-or-none.

        `cleanup --gh` spawning fine while `reconcile` fails must still leave the
        edge pending — the next tick re-runs the whole (idempotent) set.
        """
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts = [], []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       popen=_partial_popen(attempts, spawns, "reconcile"))

            # The non-failing command did start...
            self.assertTrue(spawns)
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)
            # ...but the failing one means both progress markers stay pending.
            self.assertIsNone(self._seen(tmp, "queue-manager"))
            self.assertIsNone(
                self._seen(tmp, _mod.CLAIM_CLEANUP_MARKER_NAME))

            spawns.clear()
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            # Whole set re-runs, reconcile included.
            self.assertEqual(len(self._cleanup_spawns(spawns)), 1)
            self.assertIsNotNone(self._seen(tmp, "queue-manager"))
            self.assertIsNotNone(
                self._seen(tmp, _mod.CLAIM_CLEANUP_MARKER_NAME))

    def test_persistent_failure_escalates_then_quiets(self):
        """The escalate-then-quiet contract: N ticks emit ONE loud line and one
        alert file, not N identical warnings — and a healthy tick clears both."""
        lane = "queue-manager-ingest"
        n = _mod.SPAWN_FAIL_ESCALATE_N
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts, logs = [], [], []
            for _ in range(n):
                self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                           popen=_raising_popen(attempts), logs=logs)

            escalations = [ln for ln in logs
                           if "ESCALATION" in ln and ln.startswith(lane)]
            self.assertEqual(len(escalations), 1, "exactly one loud line at N")
            alert = self._alert(tmp, lane)
            self.assertIsNotNone(alert, "alert file written at N")
            self.assertIn(f"consecutive_ticks={n}", alert)
            self.assertIn("lane=queue-manager-ingest", alert)

            # Past N: stderr goes quiet, but the alert keeps being refreshed —
            # a write-once alert would freeze count= at the escalation instant.
            logs.clear()
            for _ in range(3):
                self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                           popen=_raising_popen(attempts), logs=logs)
            self.assertEqual(
                [ln for ln in logs if ln.startswith(f"{lane}:")], [],
                "no further warns past the escalation")
            self.assertIn(f"consecutive_ticks={n + 3}", self._alert(tmp, lane))

            # A healthy pass clears both, so the next outage escalates afresh.
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(self._ingest_spawns(spawns)), 1)
            self.assertIsNone(self._alert(tmp, lane))

    def test_changed_failure_reason_restarts_the_streak(self):
        """A different failure mode is news again — it must not inherit the
        previous reason's streak and escalate early (or silently)."""
        lane = "queue-manager-ingest"
        n = _mod.SPAWN_FAIL_ESCALATE_N
        with tempfile.TemporaryDirectory() as tmp:
            spawns, attempts, logs = [], [], []
            for _ in range(n - 1):
                self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                           popen=_raising_popen(attempts), logs=logs)
            self.assertIsNone(self._alert(tmp, lane), "not escalated below N")

            # Switch OSError -> FileNotFoundError: the count restarts, so this
            # tick must NOT be the Nth.
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       popen=_raising_popen(attempts, FileNotFoundError("gone")),
                       logs=logs)
            self.assertIsNone(self._alert(tmp, lane))
            self.assertEqual(
                [ln for ln in logs if "ESCALATION" in ln], [],
                "changed reason restarts the count rather than escalating")


if __name__ == "__main__":
    unittest.main()
