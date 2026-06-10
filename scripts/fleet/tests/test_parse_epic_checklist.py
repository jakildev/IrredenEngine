"""Tests for _parse_epic_checklist() in fleet-state-scout (#1664).

The umbrella body's `## Children` checklist is the epic-steward's
membership source of truth. The parser must read every `- [ ] #N` /
`- [x] #N` row (first ref per row wins, bold-wrapped refs included)
and ignore everything else — a stray `#N` in prose adopted as a child
would corrupt rollup/closeout for the whole epic.
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
_parse_epic_checklist = _mod._parse_epic_checklist


class BasicRows(unittest.TestCase):
    def test_unchecked_row(self):
        self.assertEqual(_parse_epic_checklist("- [ ] #1662 — P1: docs"),
                         [{"number": 1662, "checked": False}])

    def test_checked_lower_x(self):
        self.assertEqual(_parse_epic_checklist("- [x] #1663 — P2"),
                         [{"number": 1663, "checked": True}])

    def test_checked_upper_x(self):
        self.assertEqual(_parse_epic_checklist("- [X] #1664 — P3"),
                         [{"number": 1664, "checked": True}])

    def test_document_order_preserved(self):
        body = "- [x] #3 done\n- [ ] #1 first\n- [ ] #2 second"
        self.assertEqual([e["number"] for e in _parse_epic_checklist(body)],
                         [3, 1, 2])


class RefExtraction(unittest.TestCase):
    def test_bold_wrapped_ref(self):
        self.assertEqual(_parse_epic_checklist("- [x] **#1527 — mechanism**"),
                         [{"number": 1527, "checked": True}])

    def test_first_ref_per_row_wins(self):
        self.assertEqual(_parse_epic_checklist("- [ ] #10 supersedes #11 and #12"),
                         [{"number": 10, "checked": False}])

    def test_indented_sub_bullet_counts(self):
        self.assertEqual(_parse_epic_checklist("  - [ ] #99 nested child"),
                         [{"number": 99, "checked": False}])


class NonRowsIgnored(unittest.TestCase):
    def test_prose_ref_ignored(self):
        self.assertEqual(_parse_epic_checklist("Blocked by #123, see also #124."),
                         [])

    def test_plain_bullet_ignored(self):
        self.assertEqual(_parse_epic_checklist("- #124 plain bullet, no checkbox"),
                         [])

    def test_refless_checkbox_does_not_swallow_next_line(self):
        # `[ \t]` after the box (not `\s`) keeps the match on one line — a
        # ref-less checkbox row must not pair with a `#N` from the next line.
        self.assertEqual(_parse_epic_checklist("- [ ] refless row\n#55 next line"),
                         [])

    def test_empty_and_none_bodies(self):
        self.assertEqual(_parse_epic_checklist(""), [])
        self.assertEqual(_parse_epic_checklist(None), [])


class RealUmbrellaShape(unittest.TestCase):
    def test_epic_1661_children_section_shape(self):
        body = (
            "## Scope\n\nA new autonomous role, see #1600 for context.\n\n"
            "## Children\n\n"
            "- [ ] #1662 — P1: canonical protocol, role-sharing doc\n"
            "- [ ] #1663 — P2: steward claim class\n"
            "- [x] #1664 — P3: scout epic fetch, projection + slice\n\n"
            "## Closing criteria\n\n- All seven children merged.\n"
        )
        self.assertEqual(_parse_epic_checklist(body), [
            {"number": 1662, "checked": False},
            {"number": 1663, "checked": False},
            {"number": 1664, "checked": True},
        ])


if __name__ == "__main__":
    unittest.main()
