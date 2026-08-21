"""Drift guard for `_SMOKE_PENDING_LABELS`'s two hand-listed consumers (#2957).

`fleet-state-scout`'s `_SMOKE_PENDING_LABELS` is the producer set for the
cross-host smoke labels. Two consumers need every member of that set to also
treat the label as "not my job right now":

  - `_merger_action_signal`'s skip set (`fleet-state-scout`) — same module,
    same language, so it's derived from `_SMOKE_PENDING_LABELS` directly
    (a union, not a second hand-written list) rather than merely guarded.
  - `SKIP_LABEL_RE` (`fleet-rebase`) — a bash regex string in a different
    language and process, so derivation isn't practical; this file is the
    guard that keeps it honest instead.

This has orphaned both consumers twice already (#2804 widened the producer
two->three and left both at two; #2888 / PR #2953 fixed the instance but
added no guard against a repeat). Both assertions below read their expected
label set from `_SMOKE_PENDING_LABELS` at runtime — neither re-lists the
labels — so a future widening of the producer is exercised against both
consumers automatically, with no test edit required.
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


class MergerSkipSetCoversEverySmokeLabel(unittest.TestCase):
    """`_merger_action_signal` must skip (return None for) every producer
    label — an approved, otherwise-mergeable PR carrying only that label."""

    def test_every_smoke_label_is_skipped(self):
        self.assertTrue(_SMOKE_PENDING_LABELS, "producer set must be non-empty")
        for label in sorted(_SMOKE_PENDING_LABELS):
            with self.subTest(label=label):
                signal = _merger_action_signal(
                    {"fleet:approved", label}, "MERGEABLE", "master"
                )
                self.assertIsNone(
                    signal,
                    f"{label} is missing from _merger_action_signal's skip "
                    f"set — the merger would treat it as {signal!r} instead "
                    "of leaving it for the smoke-runner",
                )


class FleetRebaseSkipRegexCoversEverySmokeLabel(unittest.TestCase):
    """`fleet-rebase`'s `SKIP_LABEL_RE` must match every producer label —
    the same substring test `preflight_pr` runs against a joined label list
    (`grep -qE "$SKIP_LABEL_RE"`)."""

    def test_every_smoke_label_matches_skip_regex(self):
        skip_re = _fleet_rebase_skip_label_re()
        self.assertTrue(_SMOKE_PENDING_LABELS, "producer set must be non-empty")
        for label in sorted(_SMOKE_PENDING_LABELS):
            with self.subTest(label=label):
                self.assertIsNotNone(
                    re.search(skip_re, label),
                    f"{label} does not match fleet-rebase's SKIP_LABEL_RE — "
                    "tier-0 would rebase and force-push a PR still pending "
                    "that host's smoke run",
                )


if __name__ == "__main__":
    unittest.main()
