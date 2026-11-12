"""The validator index names every validator its area pages hold.

docs/agents/VALIDATION.md keeps the cross-cutting rows and an area table whose
lines list, verbatim, the first-column names of the rows on each
VALIDATION-<AREA>.md page beside it. Issues and plans cite validators by that
name, so a name on a page and not in the area table is unfindable from the
index, and a name in the area table with no row is a dead citation.
"""

import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
DOCS = REPO / "docs/agents"
INDEX = DOCS / "VALIDATION.md"
INDEX_MAX_BYTES = 12_000
CITED_HEADINGS = (
    "## Harness dependency contract",
    "## Default-off features need a positive enabled-path test",
)
if not INDEX.is_file():
    print("SKIP: docs/agents/VALIDATION.md absent", file=sys.stderr)
    sys.exit(3)

AREA_LINK = re.compile(r"^\[([^\]]+)\]\(([^)#]+)\)$")


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def tables(text):
    """Each markdown table as (header cells, body rows of cells)."""
    out, current = [], None
    for line in text.splitlines():
        if not line.startswith("|"):
            current = None
            continue
        if current is None:
            current = (cells(line), [])
            out.append(current)
        elif not re.fullmatch(r"\|(\s*:?-+:?\s*\|)+", line.strip()):
            current[1].append(cells(line))
    return out


def validator_names(text):
    return [row[0] for header, rows in tables(text)
            if header[:1] == ["Validator"] for row in rows]


def area_table(text):
    """{page file name: [validator names]} from the index's area table."""
    found = [rows for header, rows in tables(text) if header[:1] == ["Page"]]
    if len(found) != 1:
        raise AssertionError(f"expected one area table (header `| Page |`), found {len(found)}")
    areas = {}
    for row in found[0]:
        match = AREA_LINK.match(row[0])
        if not match or match.group(1) != match.group(2):
            raise AssertionError(f"area row's first cell is not a [page](page) link: {row[0]!r}")
        areas[match.group(2)] = [n.strip() for n in row[-1].split(";") if n.strip()]
    return areas


class ValidationIndex(unittest.TestCase):
    def setUp(self):
        self.text = INDEX.read_text()

    def test_index_fits_its_byte_budget(self):
        size = len(self.text.encode())
        self.assertLessEqual(size, INDEX_MAX_BYTES,
                             f"VALIDATION.md is {size} bytes, budget {INDEX_MAX_BYTES}")

    def test_cited_section_headings_stay_in_the_index(self):
        for heading in CITED_HEADINGS:
            with self.subTest(heading=heading):
                self.assertEqual(re.findall(f"^{re.escape(heading)}$", self.text, re.M),
                                 [heading])

    def test_every_area_page_is_listed(self):
        pages = sorted(p.name for p in DOCS.glob("VALIDATION-*.md"))
        self.assertEqual(sorted(area_table(self.text)), pages)

    def test_area_table_names_exactly_each_pages_validators(self):
        for page, listed in area_table(self.text).items():
            path = DOCS / page
            with self.subTest(page=page):
                self.assertTrue(path.is_file(), f"area table links a missing page {page}")
                on_page = validator_names(path.read_text())
                self.assertTrue(on_page, f"{page} has no validator table")
                for name in on_page:
                    self.assertIn(name, listed,
                                  f"{page} row {name!r} is missing from the index's area table")
                for name in listed:
                    self.assertIn(name, on_page,
                                  f"area table names {name!r}, which {page} has no row for")

    def test_no_validator_name_appears_twice(self):
        names = validator_names(self.text)
        for page in area_table(self.text):
            path = DOCS / page
            if path.is_file():
                names += validator_names(path.read_text())
        duplicates = sorted({n for n in names if names.count(n) > 1})
        self.assertEqual(duplicates, [])
        listed = [n for group in area_table(self.text).values() for n in group]
        self.assertEqual(sorted({n for n in listed if listed.count(n) > 1}), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
