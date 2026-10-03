"""Keep fleet-claim's live amend gate aligned with scout admission."""

from __future__ import annotations

import importlib.machinery
import importlib.util
import re
import unittest
from pathlib import Path

FLEET_DIR = Path(__file__).parent.parent
CLAIM_PATH = FLEET_DIR / "fleet-claim"
SCOUT_PATH = FLEET_DIR / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("amend_gate_scout", str(SCOUT_PATH))
_spec = importlib.util.spec_from_loader(_loader.name, _loader)
scout = importlib.util.module_from_spec(_spec)
_loader.exec_module(scout)


def _shell_label_set(name: str) -> set[str]:
    source = CLAIM_PATH.read_text()
    match = re.search(rf'^{name}="([^"]*)"$', source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing {name} in fleet-claim")
    return set(match.group(1).split())


class AmendClaimLabelSets(unittest.TestCase):
    def test_feedback_tiers_match_scout(self):
        human = _shell_label_set("AMEND_HUMAN_FEEDBACK_LABELS")
        fleet = _shell_label_set("AMEND_FLEET_FEEDBACK_LABELS")

        self.assertEqual(fleet, set(scout._WORKER_SUPPRESSIBLE_LABELS))
        self.assertEqual(
            human,
            (set(scout.FEEDBACK_LABELS) - set(scout._WORKER_SUPPRESSIBLE_LABELS))
            | {"fleet:human-amending"},
        )

    def test_suppressible_parks_match_scout(self):
        self.assertEqual(
            _shell_label_set("AMEND_SUPPRESSIBLE_PARK_LABELS"),
            set(scout.WORKER_SKIP_LABELS),
        )

    def test_absolute_parks_match_project_worker(self):
        source = SCOUT_PATH.read_text()
        self.assertIn('if "human:wip" in labels:', source)
        self.assertIn('if "fleet:gated" in labels:', source)
        self.assertEqual(
            _shell_label_set("AMEND_ABSOLUTE_PARK_LABELS"),
            {"human:wip", "fleet:gated"},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
