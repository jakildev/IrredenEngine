"""Tests for project_smoke_worker() / slice_smoke_worker() in fleet-state-scout.

The smoke-worker pane is woken purely by the scout writing a trigger when this
projection's hash flips (update_role_trigger is transition-keyed on
stable_hash(projection)), and boot-dispatched by fleet-up's
smoke_worker_actionable, which reads the slice's smoke_pending_prs. Both read
_smoke_pr_eligible, whose pending-label set is _SMOKE_PENDING_LABELS.

The regression these tests lock in (#2804):

  fleet:needs-windows-smoke was missing from _SMOKE_PENDING_LABELS, on the
  stale rationale that Windows smoke is "cleared by platform-catchup (#1093),
  not by fleet agents". role-smoke-worker.md step 4 in fact defines a
  first-class native-Windows lane (host key `windows` polls that label) and
  names platform-catchup the *fallback* for when no Windows fleet is online.
  A projection that can never contain a Windows item never transitions on
  one, so the smoke-worker role was never woken for Windows-only pending work
  — on a fleet whose only smoke-pending PRs are Windows, the documented
  primary path was unreachable.

Host *routing* is deliberately not this projection's job: the role derives its
host key from `uname -s` at its step 4 and polls only that host's label, so a
Linux/macOS dispatch woken by a Windows-pending PR finds nothing and exits.
The trigger is transition-keyed, so that costs one wake per set change —
exactly what linux/macos already pay.

This harness pins:
  - a Windows-only pending PR appears in the projection and flips the hash.
  - the same PR reaches the slice (fleet-up's boot-dispatch predicate).
  - linux / macos remain pending labels (no regression).
  - the skip-label and approval gates apply to Windows exactly as to the
    other two hosts.
  - the projection stays engine-only.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_smoke_worker = _mod.project_smoke_worker
slice_smoke_worker = _mod.slice_smoke_worker
stable_hash = _mod.stable_hash

WINDOWS = "fleet:needs-windows-smoke"
LINUX = "fleet:needs-linux-smoke"
MACOS = "fleet:needs-macos-smoke"


def _state(prs, game_prs=None):
    return {"repos": {"engine": {"prs": prs},
                      "game": {"prs": game_prs or []}}}


def _pr(num, *, labels=None, head="claude/feat"):
    return {
        "number": num,
        "title": f"#{num}: render change",
        "headRefName": head,
        "baseRefName": "master",
        "labels": sorted(labels or []),
        "mergeable": "MERGEABLE",
        "author": "bot",
    }


def _approved(num, *smoke_labels, extra=()):
    return _pr(num, labels=["fleet:approved", *smoke_labels, *extra])


def _hash(state):
    return stable_hash(project_smoke_worker(state))


class WindowsSmokeWakesPane(unittest.TestCase):
    """The core fix: fleet:needs-windows-smoke must be a trigger signal."""

    def test_windows_only_pr_flips_hash(self):
        # The dead-dispatch-trigger case: the fleet's only smoke-pending work
        # is Windows, so nothing else will ever wake the pane for it.
        before = _state([])
        after = _state([_approved(101, WINDOWS)])
        self.assertNotEqual(
            _hash(before), _hash(after),
            "an approved Windows-smoke PR must wake the smoke-worker pane")

    def test_windows_only_pr_is_in_projection(self):
        items = project_smoke_worker(_state([_approved(101, WINDOWS)]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["pr"], 101)
        self.assertEqual(items[0]["smoke_labels"], [WINDOWS])

    def test_windows_reported_alongside_other_hosts(self):
        # A PR pending on two hosts reports both, so the item still changes
        # (and re-wakes the pane) when only one host's label clears.
        items = project_smoke_worker(_state([_approved(101, WINDOWS, LINUX)]))
        self.assertEqual(items[0]["smoke_labels"], sorted([LINUX, WINDOWS]))

    def test_clearing_windows_label_drops_pr(self):
        # The Windows smoke-worker verdicted: fleet:verified-windows replaces
        # the pending label, so the PR must leave the projection.
        items = project_smoke_worker(_state([
            _approved(101, extra=("fleet:verified-windows",)),
        ]))
        self.assertEqual(items, [])


class BootDispatchSeesWindows(unittest.TestCase):
    """fleet-up's smoke_worker_actionable reads slice smoke_pending_prs."""

    def test_slice_lists_windows_only_pr(self):
        out = slice_smoke_worker(_state([_approved(101, WINDOWS)]))
        self.assertEqual([p["number"] for p in out["smoke_pending_prs"]], [101])
        self.assertIn(WINDOWS, out["smoke_pending_prs"][0]["labels"])

    def test_slice_nonempty_so_boot_dispatch_fires(self):
        # bool(p["smoke_pending_prs"]) is fleet-up's predicate verbatim.
        out = slice_smoke_worker(_state([_approved(101, WINDOWS)]))
        self.assertTrue(bool(out["smoke_pending_prs"]))


class OtherHostsUnchanged(unittest.TestCase):
    """linux / macos must remain pending labels (no regression)."""

    def test_linux_still_pending(self):
        self.assertEqual(
            len(project_smoke_worker(_state([_approved(101, LINUX)]))), 1)

    def test_macos_still_pending(self):
        self.assertEqual(
            len(project_smoke_worker(_state([_approved(101, MACOS)]))), 1)

    def test_no_smoke_label_absent(self):
        self.assertEqual(project_smoke_worker(_state([_approved(101)])), [])


class GatesApplyToWindows(unittest.TestCase):
    """Approval and skip-label gates treat Windows like the other hosts."""

    def test_unapproved_windows_pr_absent(self):
        empty = _state([])
        unapproved = _state([_pr(101, labels=[WINDOWS])])
        self.assertEqual(_hash(empty), _hash(unapproved),
                         "smoke pickup requires fleet:approved")

    def test_skip_labels_drop_windows_pr(self):
        for skip in ("fleet:needs-fix", "fleet:blocker", "human:wip",
                     "fleet:wip", "fleet:merger-cooldown", "human:needs-fix"):
            with self.subTest(skip=skip):
                skipped = _state([_approved(101, WINDOWS, extra=(skip,))])
                self.assertEqual(
                    _hash(_state([])), _hash(skipped),
                    f"{skip} must hide a Windows-smoke PR from the pane")

    def test_reviewing_claim_drops_windows_pr(self):
        claimed = _state([
            _approved(101, WINDOWS, extra=("fleet:reviewing-mac-pool-1",)),
        ])
        self.assertEqual(_hash(_state([])), _hash(claimed))


class EngineOnly(unittest.TestCase):
    """Game PRs never carry cross-host smoke labels — projection is engine-only."""

    def test_game_windows_pr_absent(self):
        state = _state([], game_prs=[_approved(99, WINDOWS)])
        self.assertEqual(project_smoke_worker(state), [])
        self.assertEqual(slice_smoke_worker(state)["smoke_pending_prs"], [])


if __name__ == "__main__":
    unittest.main()
