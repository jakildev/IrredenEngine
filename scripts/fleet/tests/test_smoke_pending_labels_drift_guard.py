"""Drift guard for `_SMOKE_PENDING_LABELS`'s two hand-listed consumers (#2957).

`fleet-state-scout`'s `_SMOKE_PENDING_LABELS` is the producer set for the
cross-host smoke labels. Two consumers must agree on what a pending smoke
means for merger work:

  - `_merger_action_signal` (`fleet-state-scout`): a smoke-pending PR is
    never `merge-ready`, and a smoke-pending conflict is always
    `needs-resolve`. The human merges without waiting for smoke, so a
    pending smoke never holds conflict work.
  - `SKIP_LABEL_RE` (`fleet-rebase`): a bash regex in a different language
    and process, so derivation isn't practical and this file guards it. It
    must match NO smoke label, or tier-0 parks every approved conflict
    until a host of that tier smokes a head that cannot merge.

The producer has orphaned its consumers before (#2804 widened it from two
labels to three; #2888 / PR #2953 fixed that instance). Every assertion below
reads the label set from `_SMOKE_PENDING_LABELS` at runtime, so widening the
producer is exercised against both consumers with no test edit.
"""
import importlib.machinery
import importlib.util
import re
import unittest
from pathlib import Path

_SCOUT_PATH = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCOUT_PATH))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_scout = importlib.util.module_from_spec(_spec)
_loader.exec_module(_scout)
_SMOKE_PENDING_LABELS = _scout._SMOKE_PENDING_LABELS
_merger_action_signal = _scout._merger_action_signal

_REBASE_PATH = Path(__file__).parent.parent / "fleet-rebase"


def _fleet_rebase_skip_label_re():
    """Lift `SKIP_LABEL_RE`'s value out of `fleet-rebase` without sourcing
    the script — it runs its full bootstrap/main path when invoked, so
    execution is not a safe way to read one constant out of it."""
    for line in _REBASE_PATH.read_text().splitlines():
        m = re.match(r"^SKIP_LABEL_RE='(.*)'$", line)
        if m:
            return m.group(1)
    raise AssertionError(f"SKIP_LABEL_RE= assignment not found in {_REBASE_PATH}")


class MergerKeepsEverySmokeLabelOffMergeReady(unittest.TestCase):
    """`_merger_action_signal` must return None, not `merge-ready`, for an
    approved, MERGEABLE PR carrying any producer label."""

    def test_every_smoke_label_is_not_merge_ready(self):
        self.assertTrue(_SMOKE_PENDING_LABELS, "producer set must be non-empty")
        for label in sorted(_SMOKE_PENDING_LABELS):
            with self.subTest(label=label):
                signal = _merger_action_signal(
                    {"fleet:approved", label}, "MERGEABLE", "master"
                )
                self.assertIsNone(
                    signal,
                    f"{label} reads {signal!r}: a PR still owing that smoke "
                    "must not be reported merge-ready",
                )


class MergerTreatsEverySmokeConflictAsWork(unittest.TestCase):
    """`_merger_action_signal` must report `needs-resolve` for an approved,
    conflicting PR carrying any producer label."""

    def test_every_smoke_label_conflict_needs_resolve(self):
        for label in sorted(_SMOKE_PENDING_LABELS):
            with self.subTest(label=label):
                self.assertEqual(
                    _merger_action_signal({"fleet:approved", label},
                                          "CONFLICTING", "master"),
                    "needs-resolve",
                    f"{label} hides a conflict from the merger",
                )


class FleetRebaseSkipRegexMatchesNoSmokeLabel(unittest.TestCase):
    """`fleet-rebase`'s `SKIP_LABEL_RE` must match no producer label: the
    same substring test `preflight_pr` runs against a joined label list
    (`grep -qE "$SKIP_LABEL_RE"`)."""

    def test_no_smoke_label_matches_skip_regex(self):
        skip_re = _fleet_rebase_skip_label_re()
        self.assertTrue(_SMOKE_PENDING_LABELS, "producer set must be non-empty")
        for label in sorted(_SMOKE_PENDING_LABELS):
            with self.subTest(label=label):
                self.assertIsNone(
                    re.search(skip_re, label),
                    f"{label} matches fleet-rebase's SKIP_LABEL_RE — tier-0 "
                    "would leave an approved conflict parked on a pending smoke",
                )


if __name__ == "__main__":
    unittest.main()
