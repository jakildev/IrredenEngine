"""Scout routing tests for the shared model-field parser (#2833)."""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest import mock

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout_model", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout_model", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class ScoutModelRouting(unittest.TestCase):
    def test_body_fallback_projects_one_or_zero_classes(self):
        cases = (
            (
                "qualified",
                "**Model:** sonnet (escalate to opus if needed)",
                "sonnet",
                {"sonnet"},
            ),
            ("unrecognized", "**Model:** TBD", None, set()),
        )
        for name, model_field, expected_model, expected_tags in cases:
            with self.subTest(name=name):
                issues = [{
                    "number": 2833,
                    "title": name,
                    "body": f"{model_field}\n**Blocked by:** (none)",
                    "labels": [{"name": "fleet:queued"}],
                }]
                with mock.patch.object(_mod, "_rest_list", return_value=issues):
                    task = _mod.fetch_task_queue(
                        "jakildev/IrredenEngine")["open"][0]

                self.assertEqual(task["model"], expected_model)
                self.assertEqual(_mod.model_tags(task), expected_tags)


if __name__ == "__main__":
    unittest.main()
