"""Tests for the shared epic-membership grammar in fleet_epic_membership.

Table-driven: one positive row per field-shaped spelling found in the engine
issue population, one negative row per look-alike that is not membership.
Fixture lines are transcribed from real bodies, renumbered to synthetic
umbrella/child numbers where the row does not depend on a specific value.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from fleet_epic_membership import (  # noqa: E402
    canonical_epic_refs,
    epic_refs,
    membership_lines,
)

# (name, body, expected epic_refs)
POSITIVES = [
    ("canonical", "**Part of epic:** #9001\n", [9001]),
    ("canonical-with-rationale",
     "**Model:** opus\n**Part of epic:** #9001 (phase 2)\n", [9001]),
    ("parent-epic-bullet", "- Parent epic: #9002\n", [9002]),
    ("parent-epic-parenthetical",
     "- Parent epic: #9002 (Phase 1 — Static voxel authoring, F-1.5)\n",
     [9002]),
    ("part-of-plus-epic-word",
     "**Part of:** epic #9003 (tooling prerequisite for child #9099)\n",
     [9003]),
    ("epic-header-bold", "**Epic:** #9004 · **Design:** `d.md`\n", [9004]),
    ("epic-bullet", "- Epic: #9005\n", [9005]),
    ("colonless-inline-bold",
     "**Part of epic #9006** — rotation groundwork.\n", [9006]),
    ("parent-colon-epic-word",
     "- Parent: Epic #9007 (Rotation architecture)\n", [9007]),
    ("epic-umbrella", "- Epic umbrella: #9008\n", [9008]),
    ("indented-star-bullet", "  * **Part of epic:** #9009\n", [9009]),
    ("case-insensitive", "- parent EPIC: #9010\n", [9010]),
    ("colon-outside-bold", "**Part of epic**: #9011\n", [9011]),
    ("crlf", "**Model:** opus\r\n- Parent epic: #9012\r\n", [9012]),
]

# (name, body, umbrella that must NOT be in epic_refs)
NEGATIVES = [
    ("part-of-other-epic", "**Part of:** epic #9021\n", 9020),
    ("epic-doc-bullet", "- Epic doc: #9020\n", 9020),
    ("related-epic", "**Related:** epic #9020\n", 9020),
    ("not-part-of-epic-prose",
     "Refs #9020 — Not `Part of epic:` #9020; a sibling concern.\n", 9020),
    ("prose-epic-mention", "Epic #9020's sole open child is closed.\n", 9020),
    ("part-of-without-epic-word", "**Part of:** #9020\n", 9020),
    ("parent-without-epic-word", "- Parent: #9020\n", 9020),
    ("ref-inside-parenthetical",
     "**Part of epic:** #9022 (via sub-epic #9020)\n", 9020),
    ("middle-dot-stops-list", "- Parent epic: #9023 · Umbrella: #9020\n", 9020),
    ("em-dash-tail",
     "**Part of:** epic #9024 (thread) — sibling of #9020\n", 9020),
    ("longer-number", "**Part of epic:** #90201\n", 9020),
    ("mid-line-field", "See also **Part of epic:** #9020 above.\n", 9020),
]

MULTI = [
    ("comma-pair", "- Parent epic: #9031, #9032\n", [9031, 9032]),
    ("slash-pair-parenthetical",
     "- Parent epic: #9032 / #9033 (editor)\n", [9032, 9033]),
    ("parenthetical-each",
     "- Parent epic: #9032 (entity creation mode), #9034 (Phase 0)\n",
     [9032, 9034]),
    ("and-joined", "**Part of epic:** #9035 and #9036\n", [9035, 9036]),
    ("ampersand-joined", "- Epic: #9035 & #9037\n", [9035, 9037]),
]


class Positives(unittest.TestCase):
    def test_every_census_spelling_is_membership(self):
        for name, body, expected in POSITIVES:
            with self.subTest(name):
                self.assertEqual(epic_refs(body), expected)


class Negatives(unittest.TestCase):
    def test_look_alikes_are_not_membership(self):
        for name, body, umbrella in NEGATIVES:
            with self.subTest(name):
                self.assertNotIn(umbrella, epic_refs(body))

    def test_part_of_other_epic_still_names_its_own(self):
        # The same line is a positive for the umbrella it names.
        self.assertEqual(epic_refs("**Part of:** epic #9021\n"), [9021])


class MultiUmbrella(unittest.TestCase):
    def test_every_list_item_is_a_member(self):
        for name, body, expected in MULTI:
            with self.subTest(name):
                self.assertEqual(epic_refs(body), expected)

    def test_canonical_and_variant_lines_in_one_body(self):
        # One body that is a canonical positive, an `Epic umbrella` positive,
        # and a `·` stop-token negative at once.
        body = ("**Model:** sonnet\n**Part of epic:** #9040\n\n## Links\n"
                "- Epic umbrella: #9040 · core: #9041\n- Epic doc: #9042\n")
        self.assertEqual(epic_refs(body), [9040])
        self.assertEqual(canonical_epic_refs(body), [9040])


class Canonical(unittest.TestCase):
    def test_only_the_standalone_bold_field_is_canonical(self):
        self.assertEqual(canonical_epic_refs("**Part of epic:** #9050\n"), [9050])
        for name, body, _expected in POSITIVES:
            if name.startswith("canonical"):
                continue
            with self.subTest(name):
                self.assertEqual(canonical_epic_refs(body), [])

    def test_membership_lines_quotes_the_source_line(self):
        lines = membership_lines("intro\n- Parent epic: #9051 (phase)\nrest\n")
        self.assertEqual(lines, [("- Parent epic: #9051 (phase)", [9051])])

    def test_empty_and_none_bodies(self):
        self.assertEqual(epic_refs(None), [])
        self.assertEqual(epic_refs(""), [])
        self.assertEqual(canonical_epic_refs(None), [])


if __name__ == "__main__":
    unittest.main()
