"""Tests for _parse_blocked_by()'s no-blocker sentinel handling in
fleet-state-scout.

Regression guard for the game-queue freeze: a child whose `**Blocked by:**`
field said a bare `none — first unblocked.` (instead of the canonical
`(none)`) was parsed as an unresolved prose blocker, so the scout marked it
permanently blocked and it — plus the whole chain stacked behind it — never
got `fleet:queued`.

Covers:
  - bare `none`, `(none)`, `none — prose`, `(none) — prose`, `n/a`, `tbd`,
    lone dash, empty → no blocker (returns "")
  - real `#N` refs (canonical + inline-bold) → still surfaced
  - genuine prose blocker ("the auth redesign") → still gates
  - mixed `none, #N` → the #N still surfaces
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

_parse_blocked_by = _mod._parse_blocked_by
_is_no_blocker_value = _mod._is_no_blocker_value


def _body(blocked_by_line):
    return (
        "**Model:** opus\n"
        "**Part of epic:** #137\n"
        f"**Blocked by:** {blocked_by_line}\n"
        "\n## Scope\nstuff\n"
    )


class IsNoBlockerValue(unittest.TestCase):
    def test_sentinels(self):
        for v in ("(none)", "none", "None", "  none  ", "(none) — first unblocked.",
                  "none — first unblocked.", "n/a", "N/A", "na", "tbd", "TBD",
                  "—", "-", "–", "."):
            self.assertTrue(_is_no_blocker_value(v), f"{v!r} should be a no-blocker sentinel")

    def test_real_blockers(self):
        for v in ("#138", "#138, #139", "#138 (migration)", "the auth redesign",
                  "nonexistent #42 must land first"):
            self.assertFalse(_is_no_blocker_value(v), f"{v!r} should NOT be a sentinel")


class ParseBlockedBy(unittest.TestCase):
    def test_bare_none_variants_unblocked(self):
        # The exact form that froze the game queue, plus its siblings.
        for line in ("none — first unblocked.", "none", "(none)",
                     "(none) — first unblocked.", "n/a", "tbd", "—"):
            self.assertEqual(_parse_blocked_by(_body(line)), "",
                             f"{line!r} should parse as no blocker")

    def test_real_ref_surfaced(self):
        self.assertEqual(_parse_blocked_by(_body("#138")), "#138")
        self.assertEqual(_parse_blocked_by(_body("#138, #139")), "#138, #139")

    def test_inline_bold_ref_surfaced(self):
        body = "intro\n**Blocked by: #138 (migration)** trailing\n"
        self.assertIn("#138", _parse_blocked_by(body))

    def test_prose_blocker_still_gates(self):
        # No #N, but real prose → held (matches prior behavior; only the
        # none/n-a/tbd sentinels are exempted).
        self.assertEqual(_parse_blocked_by(_body("the auth redesign")),
                         "the auth redesign")

    def test_mixed_none_and_ref_keeps_ref(self):
        self.assertEqual(_parse_blocked_by(_body("none, #138")), "none, #138")


if __name__ == "__main__":
    unittest.main()
