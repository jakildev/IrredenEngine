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

Host *routing* is deliberately not this PROJECTION's job — it stays the
host-agnostic cross-host record of outstanding smoke debt, which is what
`platform-catchup` reads. But routing is no longer doc-only either: #2839 added
the code-level host gate one layer downstream, at dispatch, where
`_host_incompatible` already gates the task (#1998) and feedback-PR (#2695)
lanes. `fleet_task_class.HOST_SMOKE_LABELS` / `smoke_pr_for_host` is the
canonical map; `fleet-up`'s bootstrap heredoc carries an inlined copy (it cannot
import — see below) and `fleet-dispatcher`'s `smoke_worker_should_fire` reaches
it through the `--smoke-check` CLI arm. Before that gate, a standing
Windows-pending set kept every non-Windows host boot-dispatching and re-arming
smoke panes for work it could never do.

This harness pins:
  - a Windows-only pending PR appears in the projection and flips the hash.
  - the same PR reaches the slice (fleet-up's boot-dispatch predicate).
  - linux / macos remain pending labels (no regression).
  - the skip-label and approval gates apply to Windows exactly as to the
    other two hosts.
  - the projection stays engine-only.
  - the dispatch-side host gate routes each host key to its own label, fails
    closed on an unknown host, and leaves the projection host-agnostic.
  - fleet-up's inlined copy of that map agrees with the canonical one — a
    duplicate with no drift guard is the whole risk of inlining it.
"""
import importlib.machinery
import importlib.util
import json
import os
import platform
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_smoke_worker = _mod.project_smoke_worker
slice_smoke_worker = _mod.slice_smoke_worker
stable_hash = _mod.stable_hash

_TASK_CLASS = Path(__file__).parent.parent / "fleet_task_class.py"
sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_task_class import (  # noqa: E402
    HOST_SMOKE_LABELS,
    smoke_pr_for_host,
    smoke_prs_for_host,
)

WINDOWS = "fleet:needs-windows-smoke"
LINUX = "fleet:needs-linux-smoke"
MACOS = "fleet:needs-macos-smoke"


def _fleet_up_boot_predicate():
    """fleet-up's INLINED smoke_worker_actionable, lifted out of its heredoc.

    The heredoc is a standalone `python3 - <<'PY'` with no path to
    FLEET_LIB_DIR, running under `|| true` — an ImportError there would be
    swallowed and would kill the bootstrap trigger for every role at once, so
    the map is deliberately duplicated rather than imported
    (`fleet_task_class.smoke_pr_for_host` documents why). A duplicate needs its
    own coverage, hence this lift; `TwoCopiesAgree` below is the drift guard.

    Only the function definitions are sliced out and exec'd: the heredoc's
    module-level statements mkdir under $HOME, which a hermetic test must not
    touch (scripts/fleet/CLAUDE.md). Bash-function equivalent of the same trick:
    test_fleet_up_resume_boot.sh's `extract_fn`.
    """
    src = (Path(__file__).parent.parent / "fleet-up").read_text().splitlines()
    start = next(i for i, ln in enumerate(src)
                 if ln.startswith("def _current_host():"))
    end = next(i for i, ln in enumerate(src)
               if ln.startswith("for role, predicate in ["))
    ns = {"os": os, "platform": platform}
    exec("\n".join(src[start:end]), ns)  # noqa: S102 — defs only, sliced above
    return ns


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

    def test_slice_nonempty_so_boot_dispatch_fires_on_windows(self):
        # The slice reaching fleet-up's predicate non-empty is necessary but no
        # longer sufficient: #2839 host-filters it, so the boot dispatch fires
        # on a Windows host and NOT on the other two. Asserted against the real
        # predicate rather than a hand-copy of `bool(p["smoke_pending_prs"])` —
        # the hand-copy this replaces would have gone on passing while encoding
        # the pre-fix behaviour.
        out = slice_smoke_worker(_state([_approved(101, WINDOWS)]))
        self.assertTrue(bool(out["smoke_pending_prs"]))
        actionable = _fleet_up_boot_predicate()["smoke_worker_actionable"]
        with mock.patch.dict(os.environ, {"FLEET_TEST_HOST": "windows"}):
            self.assertTrue(actionable(out))
        for host in ("mac", "linux"):
            with self.subTest(host=host), \
                    mock.patch.dict(os.environ, {"FLEET_TEST_HOST": host}):
                self.assertFalse(
                    actionable(out),
                    f"{host} must not boot-dispatch on Windows-only smoke work")


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


class HostGateRoutesEachHostKey(unittest.TestCase):
    """The canonical dispatch-side gate: fleet_task_class.smoke_pr_for_host.

    Every host key is pinned explicitly. The `mac` host key vs the `macos`
    label is the trap (#1383 reconciled the host detectors, not the spelling):
    a mismatch matches no label, the host is never actionable, and the smoke
    lane silently dies — the #2804 failure mode from the other direction.
    """

    def test_each_host_matches_only_its_own_label(self):
        for host, own in (("linux", LINUX), ("mac", MACOS),
                          ("windows", WINDOWS)):
            for label in (LINUX, MACOS, WINDOWS):
                with self.subTest(host=host, label=label):
                    self.assertEqual(smoke_pr_for_host([label], host),
                                     label == own)

    def test_mac_host_key_maps_to_the_macos_label(self):
        # Pinned on its own: this is the one pair whose two spellings differ.
        self.assertEqual(HOST_SMOKE_LABELS["mac"], MACOS)
        self.assertTrue(smoke_pr_for_host([MACOS], "mac"))
        self.assertFalse(smoke_pr_for_host([MACOS], "macos"))

    def test_unknown_host_is_fail_closed(self):
        for host in ("unknown", "", None, "macos"):
            with self.subTest(host=host):
                self.assertFalse(smoke_pr_for_host([LINUX, MACOS, WINDOWS],
                                                   host))

    def test_multi_host_pr_is_servable_by_each_named_host(self):
        # A PR pending on two hosts is real work for BOTH; the gate must not
        # collapse it to the first label.
        self.assertTrue(smoke_pr_for_host([WINDOWS, LINUX], "linux"))
        self.assertTrue(smoke_pr_for_host([WINDOWS, LINUX], "windows"))
        self.assertFalse(smoke_pr_for_host([WINDOWS, LINUX], "mac"))

    def test_no_labels_never_matches(self):
        for labels in ([], None, ["fleet:approved"]):
            with self.subTest(labels=labels):
                self.assertFalse(smoke_pr_for_host(labels, "linux"))

    def test_filter_keeps_only_this_hosts_prs(self):
        prs = slice_smoke_worker(_state([
            _approved(101, WINDOWS), _approved(102, MACOS),
            _approved(103, WINDOWS, LINUX),
        ]))["smoke_pending_prs"]
        got = {h: [p["number"] for p in smoke_prs_for_host(prs, h)]
               for h in ("windows", "mac", "linux", "unknown")}
        self.assertEqual(got, {"windows": [101, 103], "mac": [102],
                               "linux": [103], "unknown": []})

    def test_projection_itself_stays_host_agnostic(self):
        # Approach (A)'s load-bearing property: the scout's output is identical
        # on every host, so state.json stays the cross-host record of smoke debt
        # that platform-catchup reads. Only the dispatch-side filter differs.
        state = _state([_approved(101, WINDOWS), _approved(102, MACOS)])
        per_host = []
        for host in ("windows", "mac", "linux"):
            with mock.patch.dict(os.environ, {"FLEET_TEST_HOST": host}):
                per_host.append(stable_hash(project_smoke_worker(state)))
        self.assertEqual(len(set(per_host)), 1)


class TwoCopiesAgree(unittest.TestCase):
    """fleet-up's inlined map/host-key must not drift from the canonical one.

    The inlining is deliberate (`fleet_task_class.smoke_pr_for_host` says why),
    which makes drift the standing risk it buys — so it gets a guard rather
    than a comment.
    """

    def setUp(self):
        self.ns = _fleet_up_boot_predicate()

    def test_label_maps_are_identical(self):
        self.assertEqual(self.ns["HOST_SMOKE_LABELS"], HOST_SMOKE_LABELS)

    def test_host_detection_agrees_per_uname(self):
        # Both copies map uname -s the same way, including the Windows family
        # and the fail-closed default.
        cases = {"Darwin": "mac", "Linux": "linux", "Windows_NT": "windows",
                 "MINGW64_NT-10.0": "windows", "MSYS_NT-10.0": "windows",
                 "CYGWIN_NT-10.0": "windows", "Plan9": "unknown"}
        for sysname, expected in cases.items():
            with self.subTest(sysname=sysname), \
                    mock.patch.dict(os.environ, {}, clear=False):
                os.environ.pop("FLEET_TEST_HOST", None)
                with mock.patch.object(platform, "system",
                                       return_value=sysname):
                    self.assertEqual(self.ns["_current_host"](), expected)

    def test_fleet_test_host_override_honored(self):
        with mock.patch.dict(os.environ, {"FLEET_TEST_HOST": "windows"}):
            self.assertEqual(self.ns["_current_host"](), "windows")


class SmokeCheckCli(unittest.TestCase):
    """`fleet_task_class.py --smoke-check` — the seam fleet-dispatcher uses.

    It shells out rather than imports, so the CLI contract (host-filtered PR
    numbers on stdout, empty = stand down) is what the dispatcher's gate
    actually depends on.
    """

    def _run(self, slice_data, host):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "smoke-worker.json"
            path.write_text(json.dumps(slice_data))
            env = {**os.environ, "FLEET_TEST_HOST": host}
            return subprocess.run(
                [sys.executable, str(_TASK_CLASS), "--smoke-check", str(path)],
                capture_output=True, text=True, env=env, check=False)

    def _slice(self):
        return slice_smoke_worker(_state([
            _approved(101, WINDOWS), _approved(102, MACOS),
        ]))

    def test_prints_only_this_hosts_prs(self):
        for host, expected in (("windows", "101"), ("mac", "102"),
                               ("linux", ""), ("unknown", "")):
            with self.subTest(host=host):
                res = self._run(self._slice(), host)
                self.assertEqual(res.returncode, 0, res.stderr)
                self.assertEqual(res.stdout.strip(), expected)

    def test_missing_slice_is_quiet_not_an_error(self):
        # The dispatcher runs this every tick a trigger stands; a missing or
        # unreadable slice must mean "don't fire", never a crash it would
        # swallow into the same verdict by accident.
        with tempfile.TemporaryDirectory() as tmp:
            res = subprocess.run(
                [sys.executable, str(_TASK_CLASS), "--smoke-check",
                 str(Path(tmp) / "absent.json")],
                capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout.strip(), "")

    def test_bad_arity_is_a_usage_error(self):
        res = subprocess.run(
            [sys.executable, str(_TASK_CLASS), "--smoke-check"],
            capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 2)
        self.assertIn("usage:", res.stderr)


if __name__ == "__main__":
    unittest.main()
