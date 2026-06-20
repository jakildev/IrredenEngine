"""Corpus + unit tests for the shared blocker parser fleet_blocked_by.py.

This module is the single source of truth imported by fleet-state-scout,
fleet-queue-ingest, and fleet-claim (#1749). Cross-parser agreement is
structural once all three import it; this corpus documents and locks the
contract, with one case per declaration form:

  - canonical standalone `**Blocked by:** #N`
  - inline-bold `**Blocked by: #N (label)**` (#1423)
  - PLAIN mid-line `Blocked by: #N` (the #174-children mechanism, #1749)
  - multi-line + multi-ref union
  - `Blocked on #N` header fallback (#1326)
  - no-blocker sentinels: `(none)`, bare `none`, `n/a`, `tbd`, lone dash
  - cross-repo `[owner/]Repo#N` qualifier routing (#1522)
  - false-positive guard: "not blocked by anything" → unblocked
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
import fleet_blocked_by as fbb


class IsNoBlockerValue(unittest.TestCase):
    def test_sentinels(self):
        for v in ("(none)", "none", "None", "  none  ", "(none) — first unblocked.",
                  "none — first unblocked.", "n/a", "N/A", "na", "tbd", "TBD",
                  "—", "-", "–", ".", ""):
            self.assertTrue(fbb.is_no_blocker_value(v), f"{v!r} should be a sentinel")

    def test_real_blockers(self):
        for v in ("#138", "#138, #139", "#138 (migration)", "the auth redesign",
                  "none, #138", "nonexistent #42 must land first"):
            self.assertFalse(fbb.is_no_blocker_value(v), f"{v!r} should NOT be a sentinel")


class SeeAlsoParallelIdiom(unittest.TestCase):
    """#1910: a `#N` introduced by a see-also / parallel qualifier inside a
    leading-`none` value is a sibling cross-reference, not a blocker — while
    the anti-evasion guard for `none, blocked by #5` stays intact."""

    # --- excused: leading `none` + every ref is a see-also / parallel sibling ---
    def test_parallel_idiom_excused(self):
        for v in (
            "(none) — independent tooling; can start immediately, in parallel with #1883",
            "_(none — independent tooling; in parallel with #1883)_",  # #1910's italic field form
            "*none* — in parallel with #1883",                          # bold form
            "(none — runs in parallel with #1883)",
            "none — see also #5",
            "none; sibling of #7",
            "(none) — related to #9, alongside #10",
            "n/a — cf. #12",
        ):
            self.assertTrue(fbb.is_no_blocker_value(v),
                            f"{v!r} should be excused as a see-also sibling")

    def test_parallel_idiom_parses_unblocked(self):
        # The live #1910 field shape (the bug this hardening fixes at the source).
        body = ("**Blocked by:** (none) — independent tooling; "
                "in parallel with #1883\n")
        self.assertEqual(fbb.parse_blocked_by(body), "")
        self.assertEqual(fbb.blocker_refs(body, "jakildev/IrredenEngine"), [])

    # --- anti-evasion: a blocker verb (or no qualifier at all) still gates ---
    def test_evasion_blocker_verb_still_gates(self):
        for v in (
            "none, actually blocked by #5",        # the canonical evasion
            "(none) — depends on #5",
            "none — requires #6 first",
            "none, #138",                          # no qualifier at all
            "none — see also #5, blocked by #6",   # mixed: the blocker ref gates the whole value
        ):
            self.assertFalse(fbb.is_no_blocker_value(v), f"{v!r} must still gate")

    def test_evasion_surfaces_real_ref(self):
        body = "**Blocked by:** none, actually blocked by #5\n"
        self.assertEqual(fbb.parse_blocked_by(body), "none, actually blocked by #5")
        self.assertEqual(fbb.blocker_refs(body, "jakildev/IrredenEngine"),
                         [("jakildev/IrredenEngine", "5")])

    # --- a #N WITHOUT a leading `none` sentinel is never excused ---
    def test_ref_without_leading_none_gates(self):
        for v in ("#1883", "in parallel with #1883", "see also #5"):
            self.assertFalse(fbb.is_no_blocker_value(v),
                             f"{v!r} has no leading `none` → must gate")


class ParseBlockedBy(unittest.TestCase):
    def _body(self, line):
        return ("**Model:** opus\n**Part of epic:** #137\n"
                f"**Blocked by:** {line}\n\n## Scope\nstuff\n")

    # --- canonical standalone form ---
    def test_canonical_single(self):
        self.assertEqual(fbb.parse_blocked_by(self._body("#138")), "#138")

    def test_canonical_multi_ref(self):
        self.assertEqual(fbb.parse_blocked_by(self._body("#138, #139")), "#138, #139")

    def test_canonical_bullet_prefix(self):
        self.assertEqual(fbb.parse_blocked_by("- **Blocked by:** #50\n"), "#50")

    def test_suggested_prefix(self):
        self.assertEqual(fbb.parse_blocked_by("**Suggested Blocked by:** #51\n"), "#51")

    # --- inline-bold form (#1423) ---
    def test_inline_bold(self):
        body = "**Part of epic:** #104 · **Blocked by: #138 (Phase 2)** trailing\n"
        self.assertIn("#138", fbb.parse_blocked_by(body))

    # --- NEW plain form (#1749 / #174 children) ---
    def test_plain_single_midline(self):
        body = "Part of epic #174 (Phase E). [sonnet] Blocked by: #178.\n"
        self.assertEqual(fbb.parse_blocked_by(body), "#178.")

    def test_plain_multi_ref_midline(self):
        # The verified #178 body shape.
        body = "Part of epic #174 (Phase D). [opus] Blocked by: #175, #176, #177.\n"
        self.assertEqual(
            sorted(fbb.blocker_refs(body, "jakildev/IrredenEngine")),
            [("jakildev/IrredenEngine", "175"),
             ("jakildev/IrredenEngine", "176"),
             ("jakildev/IrredenEngine", "177")],
        )

    def test_plain_does_not_match_bold(self):
        # A canonical line must be captured by the canonical form, not
        # double-counted by the plain form's `(?<!\*)` lookbehind.
        self.assertEqual(fbb.parse_blocked_by("**Blocked by:** #5\n"), "#5")

    # --- multi-line union ---
    def test_multiline_union(self):
        body = "**Blocked by:** #100\n**Blocked by:** #101\n"
        self.assertEqual(
            sorted(fbb.blocker_refs(body, "jakildev/IrredenEngine")),
            [("jakildev/IrredenEngine", "100"),
             ("jakildev/IrredenEngine", "101")],
        )

    # --- header fallback (#1326) ---
    def test_header_fallback(self):
        self.assertEqual(fbb.parse_blocked_by("## Blocked on #1300\n"), "#1300")

    def test_header_only_when_no_field(self):
        # An explicit `(none)` field is authoritative over a stray header.
        body = "**Blocked by:** (none — independent)\n\n## Blocked on #101\n"
        self.assertEqual(fbb.parse_blocked_by(body), "")

    def test_canonical_wins_over_header(self):
        body = "**Blocked by:** #50\n\n## Blocked on #99\n"
        self.assertEqual(fbb.parse_blocked_by(body), "#50")

    def test_header_refless_not_a_blocker(self):
        self.assertEqual(fbb.parse_blocked_by("## Blocked on the redesign\n"), "")

    # --- sentinels ---
    def test_sentinel_variants_unblocked(self):
        for line in ("none — first unblocked.", "none", "(none)",
                     "(none) — first unblocked.", "n/a", "tbd", "—"):
            self.assertEqual(fbb.parse_blocked_by(self._body(line)), "",
                             f"{line!r} should parse as unblocked")

    def test_mixed_sentinel_and_ref_keeps_ref(self):
        self.assertEqual(fbb.parse_blocked_by(self._body("none, #138")), "none, #138")

    def test_prose_blocker_still_gates(self):
        self.assertEqual(fbb.parse_blocked_by(self._body("the auth redesign")),
                         "the auth redesign")

    # --- false-positive guard ---
    def test_false_positive_guard(self):
        for body in ("This task is not blocked by anything yet.\n",
                     "Was blocked by: nothing in particular.\n",
                     "unblocked by: #5 is a non-field substring\n"):
            self.assertEqual(fbb.parse_blocked_by(body), "",
                             f"{body!r} must not be read as a blocker")

    def test_empty_body(self):
        self.assertEqual(fbb.parse_blocked_by(""), "")
        self.assertEqual(fbb.parse_blocked_by(None), "")


class BlockerRefs(unittest.TestCase):
    def test_bare_ref_defaults_to_repo(self):
        self.assertEqual(
            fbb.blocker_refs("**Blocked by:** #1214\n", "jakildev/IrredenEngine"),
            [("jakildev/IrredenEngine", "1214")],
        )

    def test_cross_repo_owner_qualified(self):
        self.assertEqual(
            fbb.blocker_refs("**Blocked by:** jakildev/irreden#125\n", "jakildev/IrredenEngine"),
            [("jakildev/irreden", "125")],
        )

    def test_cross_repo_bare_qualifier(self):
        self.assertEqual(
            fbb.blocker_refs("**Blocked by:** irreden#126\n", "jakildev/IrredenEngine"),
            [("jakildev/irreden", "126")],
        )

    def test_unrecognized_qualifier_defaults(self):
        self.assertEqual(
            fbb.blocker_refs("**Blocked by:** foo#7\n", "jakildev/IrredenEngine"),
            [("jakildev/IrredenEngine", "7")],
        )

    def test_plain_form_refs(self):
        # The #179 replay: plain single ref surfaces.
        self.assertEqual(
            fbb.blocker_refs("Part of epic #174. [sonnet] Blocked by: #178.\n",
                             "jakildev/IrredenEngine"),
            [("jakildev/IrredenEngine", "178")],
        )

    def test_sentinel_no_refs(self):
        self.assertEqual(fbb.blocker_refs("**Blocked by:** (none)\n", "jakildev/IrredenEngine"), [])


class HasBlockedByField(unittest.TestCase):
    def test_present_forms(self):
        for body in ("**Blocked by:** #5\n", "**Blocked by:** (none)\n",
                     "**Blocked by: #5 (x)**\n", "Blocked by: #5\n",
                     "## Blocked on #5\n"):
            self.assertTrue(fbb.has_blocked_by_field(body), f"{body!r} should count as present")

    def test_absent(self):
        for body in ("", "## Scope\nIndependent task.\n",
                     "not blocked by anything yet\n", "## Blocked on the redesign\n"):
            self.assertFalse(fbb.has_blocked_by_field(body), f"{body!r} should count as absent")


if __name__ == "__main__":
    unittest.main()
