"""Tests for update_role_trigger's empty-projection wake suppression.

A projection-hash change whose NEW projection is empty is a transition to
nothing-to-do (a verdict label-swap, an amend claim, a merge emptied the
set). Waking the role then is a guaranteed no-op iteration — observed
2026-07-01 as the opus-reviewer dispatching 4x in 5 minutes, every
iteration "no actionable candidates". The invariants:

  - non-empty -> non-empty change: hash recorded, trigger touched;
  - non-empty -> EMPTY change: hash recorded, trigger NOT touched (the
    trailing wake is swallowed);
  - empty -> non-empty change: fires normally (the recorded empty hash
    guarantees the flip back is seen as a change);
  - unchanged projection: no hash write, no trigger (pre-existing).

An `<role>.empty-suppressed` marker in SEEN_DIR records whether the most
recent hash write was such a suppression, so `fleet-debug triggers` (#2185)
can report it — the on-disk state can't otherwise tell "suppressed" from
"dispatched then consumed". The marker invariants are exercised below too.

Three classes, because #2700 split the rule by role:

  - `UpdateRoleTriggerEmpty` — the whole-projection rule above, run under
    role "r". "r" is deliberately NOT in PER_KIND_TRIGGER_ROLES, so this
    class is the unmodified byte-for-byte pin on the legacy path.
  - `UpdateRoleTriggerPerKind` — the per-sub-lane membership rule for
    allowlisted roles (today: "worker"). The whole-projection test is
    structurally unreachable for a union lane, so suppression there keys on
    "did some kind GAIN an item" instead, and the seen file becomes the
    fmt-2 per-kind payload.
  - `UpdateRoleTriggerLegacyPathPreserved` — feeds an opus-reviewer-shaped
    projection (kind-less PR items mixed with {kind: "plan_review"} issue
    items) through a non-allowlisted role, pinning that kind-carrying items
    alone do NOT opt a role into the per-kind path.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class UpdateRoleTriggerEmpty(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self._orig_seen = _mod.SEEN_DIR
        self._orig_triggers = _mod.TRIGGERS_DIR
        _mod.SEEN_DIR = base / "seen-hashes"
        _mod.TRIGGERS_DIR = base / "triggers"
        _mod.SEEN_DIR.mkdir(parents=True)
        _mod.TRIGGERS_DIR.mkdir(parents=True)

    def tearDown(self):
        _mod.SEEN_DIR = self._orig_seen
        _mod.TRIGGERS_DIR = self._orig_triggers
        self._tmp.cleanup()

    def _trigger_exists(self, role):
        return (_mod.TRIGGERS_DIR / role).exists()

    def _seen(self, role):
        return (_mod.SEEN_DIR / role).read_text().strip()

    def _suppressed_marker(self, role):
        return _mod.SEEN_DIR / f"{role}.empty-suppressed"

    def test_non_empty_change_fires(self):
        self.assertTrue(_mod.update_role_trigger("r", [{"pr": 1}]))
        self.assertTrue(self._trigger_exists("r"))

    def test_transition_to_empty_records_hash_without_wake(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()  # dispatcher consumed it
        self.assertFalse(_mod.update_role_trigger("r", []))
        self.assertFalse(self._trigger_exists("r"))
        self.assertEqual(self._seen("r"), _mod.stable_hash([]))

    def test_empty_to_non_empty_fires(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        _mod.update_role_trigger("r", [])
        self.assertTrue(_mod.update_role_trigger("r", [{"pr": 2}]))
        self.assertTrue(self._trigger_exists("r"))

    def test_transition_to_empty_leaves_pending_trigger_pending(self):
        # A trigger armed by earlier non-empty work and not yet consumed
        # must survive the empty transition — suppression only skips the
        # touch, it never clears.
        _mod.update_role_trigger("r", [{"pr": 1}])
        self.assertTrue(self._trigger_exists("r"))
        _mod.update_role_trigger("r", [])
        self.assertTrue(self._trigger_exists("r"))

    def test_unchanged_projection_is_inert(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        self.assertFalse(_mod.update_role_trigger("r", [{"pr": 1}]))
        self.assertFalse(self._trigger_exists("r"))

    # --- empty-suppression marker (fleet-debug triggers reads it) ------------
    # Invariant: the marker is present iff the most recent hash *write* for the
    # role was an empty-projection suppression.

    def test_non_empty_fire_leaves_no_marker(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        self.assertFalse(self._suppressed_marker("r").exists())

    def test_transition_to_empty_writes_marker(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()  # dispatcher consumed it
        _mod.update_role_trigger("r", [])
        marker = self._suppressed_marker("r")
        self.assertTrue(marker.exists())
        self.assertEqual(marker.read_text().strip(), _mod.stable_hash([]))

    def test_subsequent_non_empty_fire_clears_marker(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        _mod.update_role_trigger("r", [])
        self.assertTrue(self._suppressed_marker("r").exists())
        _mod.update_role_trigger("r", [{"pr": 2}])
        self.assertFalse(self._suppressed_marker("r").exists())

    def test_unchanged_projection_leaves_marker_intact(self):
        # The hash-unchanged early return writes no hash, so it must not touch
        # the marker either — "last hash write" semantics would otherwise drift.
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        _mod.update_role_trigger("r", [])
        self.assertFalse(_mod.update_role_trigger("r", []))  # unchanged empty
        self.assertTrue(self._suppressed_marker("r").exists())


def _task(n, blocked_by="(none)"):
    return {"kind": "task", "repo": "engine", "id": f"#{n}",
            "blocked_by": blocked_by}


def _pr(n, labels=("fleet:needs-fix",)):
    return {"kind": "pr", "repo": "engine", "pr": n, "labels": sorted(labels)}


def _needs_plan(n):
    return {"kind": "needs_plan", "repo": "engine", "issue": n}


class UpdateRoleTriggerPerKind(unittest.TestCase):
    """Per-sub-lane suppression for union projections (#2700).

    The whole-projection `if not projection:` test is unreachable for the
    worker lane: it unions four kinds and `task` is structurally never empty,
    so every hash change fires — including pure REMOVALS (a task claimed, a
    feedback label cleared, a merge closing an issue), which provably reduce
    available work yet fan out every idle pane.

    Role "worker" is in PER_KIND_TRIGGER_ROLES; role "r" (used by the suite
    above, unmodified) is not, which makes that suite the legacy-path pin.
    """

    ROLE = "worker"

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self._orig_seen = _mod.SEEN_DIR
        self._orig_triggers = _mod.TRIGGERS_DIR
        _mod.SEEN_DIR = base / "seen-hashes"
        _mod.TRIGGERS_DIR = base / "triggers"
        _mod.SEEN_DIR.mkdir(parents=True)
        _mod.TRIGGERS_DIR.mkdir(parents=True)

    def tearDown(self):
        _mod.SEEN_DIR = self._orig_seen
        _mod.TRIGGERS_DIR = self._orig_triggers
        self._tmp.cleanup()

    def _fire(self, projection):
        return _mod.update_role_trigger(self.ROLE, projection)

    def _trigger_exists(self):
        return (_mod.TRIGGERS_DIR / self.ROLE).exists()

    def _consume(self):
        (_mod.TRIGGERS_DIR / self.ROLE).unlink()

    def _marker(self):
        return _mod.SEEN_DIR / f"{self.ROLE}.empty-suppressed"

    def _seen_kinds(self):
        raw = (_mod.SEEN_DIR / self.ROLE).read_text()
        return json.loads(raw)["kinds"]

    def test_worker_is_allowlisted_and_others_are_not(self):
        self.assertIn("worker", _mod.PER_KIND_TRIGGER_ROLES)
        for role in ("opus-reviewer", "sonnet-reviewer", "merger",
                     "smoke-worker", "epic-steward"):
            self.assertNotIn(role, _mod.PER_KIND_TRIGGER_ROLES)

    # --- AC 1: pure removal from a still-non-empty lane suppresses ----------

    def test_pure_removal_from_non_empty_lane_suppresses(self):
        # The criterion the whole-projection rule cannot express, and the
        # dominant wake class: a task got claimed, the lane still holds 1.
        self._fire([_task(1), _task(2), _pr(9)])
        self._consume()
        self.assertFalse(self._fire([_task(1), _pr(9)]))
        self.assertFalse(self._trigger_exists())
        # The hash still advanced, so the flip back is seen as a change.
        self.assertEqual(len(self._seen_kinds()["task"]), 1)
        self.assertTrue(self._marker().exists())

    def test_pr_lane_drain_with_tasks_unchanged_suppresses(self):
        # AC 2: sub-lane -> zero while another lane stays non-empty. The
        # feedback PR was claimed; `task` items keep the projection non-empty.
        self._fire([_task(1), _task(2), _pr(9)])
        self._consume()
        self.assertFalse(self._fire([_task(1), _task(2)]))
        self.assertFalse(self._trigger_exists())
        self.assertNotIn("pr", self._seen_kinds())

    def test_semantic_conflict_claim_drop_suppresses(self):
        conflict = {"kind": "semantic_conflict", "repo": "engine", "pr": 7}
        self._fire([_task(1), conflict])
        self._consume()
        self.assertFalse(self._fire([_task(1)]))
        self.assertFalse(self._trigger_exists())

    # --- AC 3: positive controls (the fix cannot pass by never firing) ------

    def test_new_item_in_empty_lane_fires(self):
        self._fire([_task(1)])
        self._consume()
        self.assertTrue(self._fire([_task(1), _pr(9)]))
        self.assertTrue(self._trigger_exists())

    def test_item_modification_fires(self):
        # Same id, blocked_by discharged: re-hashes as remove+add, so the new
        # item is absent from the old set. Unblocking IS new work.
        self._fire([_task(1, blocked_by="#2385"), _task(2)])
        self._consume()
        self.assertTrue(self._fire([_task(1, blocked_by="(none)"), _task(2)]))
        self.assertTrue(self._trigger_exists())

    def test_pr_label_swap_fires(self):
        self._fire([_task(1), _pr(9, labels=["fleet:needs-fix"])])
        self._consume()
        self.assertTrue(self._fire([_task(1), _pr(9, labels=["fleet:has-nits"])]))
        self.assertTrue(self._trigger_exists())

    def test_simultaneous_remove_and_add_across_kinds_fires(self):
        self._fire([_task(1), _task(2), _pr(9)])
        self._consume()
        # task 2 leaves, needs_plan 5 arrives.
        self.assertTrue(self._fire([_task(1), _pr(9), _needs_plan(5)]))
        self.assertTrue(self._trigger_exists())

    def test_new_item_added_within_same_kind_fires(self):
        self._fire([_task(1)])
        self._consume()
        self.assertTrue(self._fire([_task(1), _task(2)]))
        self.assertTrue(self._trigger_exists())

    # --- AC 4: the whole-projection-empty case is subsumed ------------------

    def test_transition_to_empty_suppresses(self):
        self._fire([_task(1)])
        self._consume()
        self.assertFalse(self._fire([]))
        self.assertFalse(self._trigger_exists())
        self.assertEqual(self._seen_kinds(), {})
        self.assertTrue(self._marker().exists())

    def test_empty_to_non_empty_fires(self):
        self._fire([_task(1)])
        self._consume()
        self._fire([])
        self.assertTrue(self._fire([_task(2)]))
        self.assertTrue(self._trigger_exists())

    def test_suppression_leaves_pending_trigger_pending(self):
        # Suppression skips the touch; it never clears an armed trigger.
        self._fire([_task(1), _task(2)])
        self.assertTrue(self._trigger_exists())
        self._fire([_task(1)])
        self.assertTrue(self._trigger_exists())

    # --- no-write / marker semantics ---------------------------------------

    def test_unchanged_projection_is_inert(self):
        self._fire([_task(1), _pr(9)])
        self._consume()
        self.assertFalse(self._fire([_task(1), _pr(9)]))
        self.assertFalse(self._trigger_exists())

    def test_unchanged_projection_does_not_rewrite_seen_file(self):
        # The compare is on the serialized fmt-2 payload, so a stable lane
        # must take the early return — otherwise the upgrade tick's format
        # change re-fires forever.
        self._fire([_task(1)])
        seen = _mod.SEEN_DIR / self.ROLE
        before = seen.stat().st_mtime_ns
        self.assertFalse(self._fire([_task(1)]))
        self.assertEqual(seen.stat().st_mtime_ns, before)

    def test_unchanged_projection_leaves_marker_intact(self):
        self._fire([_task(1), _task(2)])
        self._consume()
        self._fire([_task(1)])
        self.assertTrue(self._marker().exists())
        self.assertFalse(self._fire([_task(1)]))
        self.assertTrue(self._marker().exists())

    def test_fire_clears_marker(self):
        self._fire([_task(1), _task(2)])
        self._consume()
        self._fire([_task(1)])
        self.assertTrue(self._marker().exists())
        self._fire([_task(1), _task(3)])
        self.assertFalse(self._marker().exists())

    def test_item_order_does_not_matter(self):
        self._fire([_task(1), _task(2)])
        self._consume()
        self.assertFalse(self._fire([_task(2), _task(1)]))
        self.assertFalse(self._trigger_exists())

    # --- AC 6: upgrade from the legacy bare-hex seen file -------------------

    def test_legacy_seen_file_fires_once_then_settles(self):
        seen = _mod.SEEN_DIR / self.ROLE
        seen.write_text(_mod.stable_hash([_task(1)]) + "\n")
        projection = [_task(1)]
        # Unparseable as fmt-2 => every kind reads as all-new => one fire.
        self.assertTrue(self._fire(projection))
        self.assertTrue(self._trigger_exists())
        self.assertEqual(json.loads(seen.read_text())["fmt"], 2)
        self._consume()
        # Quiet thereafter on the same projection.
        self.assertFalse(self._fire(projection))
        self.assertFalse(self._trigger_exists())

    def test_legacy_seen_file_with_empty_projection_suppresses(self):
        seen = _mod.SEEN_DIR / self.ROLE
        seen.write_text(_mod.stable_hash([_task(1)]) + "\n")
        self.assertFalse(self._fire([]))
        self.assertFalse(self._trigger_exists())

    def test_corrupt_seen_file_fires_once_never_idles(self):
        # The safe direction: unknown state must not read as "nothing new"
        # (that is #561's permanently-idle role).
        (_mod.SEEN_DIR / self.ROLE).write_text("{not json\n")
        self.assertTrue(self._fire([_task(1)]))
        self.assertTrue(self._trigger_exists())

    def test_kindless_items_degrade_to_one_bucket(self):
        # Defensive default bucket: a projector that grows a kind-less item
        # must not raise, and behaves whole-lane for that bucket.
        self._fire([{"repo": "engine", "pr": 1}])
        self._consume()
        self.assertFalse(self._fire([]))
        self.assertFalse(self._trigger_exists())


class UpdateRoleTriggerLegacyPathPreserved(unittest.TestCase):
    """AC 5: kind-carrying items on a NON-allowlisted role keep old semantics.

    project_opus_reviewer emits kind-less PR items AND {kind: "plan_review"}
    issue items, so a shape-keyed implementation ("bucket by item shape; a
    single-lane role reduces to one bucket") would have silently changed this
    role's suppression. The allowlist is what prevents that, and this pins it.
    """

    ROLE = "opus-reviewer"

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self._orig_seen = _mod.SEEN_DIR
        self._orig_triggers = _mod.TRIGGERS_DIR
        _mod.SEEN_DIR = base / "seen-hashes"
        _mod.TRIGGERS_DIR = base / "triggers"
        _mod.SEEN_DIR.mkdir(parents=True)
        _mod.TRIGGERS_DIR.mkdir(parents=True)

    def tearDown(self):
        _mod.SEEN_DIR = self._orig_seen
        _mod.TRIGGERS_DIR = self._orig_triggers
        self._tmp.cleanup()

    def _projection(self, prs, plan_reviews):
        items = [{"repo": "engine", "pr": n} for n in prs]
        items += [{"kind": "plan_review", "repo": "engine", "issue": n}
                  for n in plan_reviews]
        return items

    def test_pure_removal_while_non_empty_still_fires(self):
        # Today's semantics for a non-allowlisted role — preserved, not fixed.
        _mod.update_role_trigger(self.ROLE, self._projection([1, 2], [7]))
        (_mod.TRIGGERS_DIR / self.ROLE).unlink()
        self.assertTrue(
            _mod.update_role_trigger(self.ROLE, self._projection([1], [7])))
        self.assertTrue((_mod.TRIGGERS_DIR / self.ROLE).exists())

    def test_seen_file_stays_bare_hash(self):
        projection = self._projection([1], [7])
        _mod.update_role_trigger(self.ROLE, projection)
        self.assertEqual((_mod.SEEN_DIR / self.ROLE).read_text().strip(),
                         _mod.stable_hash(projection))

    def test_transition_to_empty_still_suppresses(self):
        _mod.update_role_trigger(self.ROLE, self._projection([1], [7]))
        (_mod.TRIGGERS_DIR / self.ROLE).unlink()
        self.assertFalse(_mod.update_role_trigger(self.ROLE, []))
        self.assertFalse((_mod.TRIGGERS_DIR / self.ROLE).exists())


if __name__ == "__main__":
    unittest.main()
