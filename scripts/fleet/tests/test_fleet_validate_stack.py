"""Tests for the stack-validation predicates in fleet_validate_stack (#1312).

Covers the drift modes the validator must catch at pre-approval time:
prose-only / missing `**Blocked by:**` (the #1300, #1309-#1311 shape, treated
as an ambiguous *warning* since it may be a legit root), `**Epic:**` header
bullets in place of the canonical `**Part of epic:**` line (the #1308-#1311
shape, which also breaks file-epic's own discovery — a hard *error*),
malformed `**Blocked by:**` lines that name no `#N`, and a missing
`**Model:**` line. Multiple blockers — multi-ref `#A, #B` or several
`**Blocked by:**` lines — are accepted as of #1296 (the gate unions every ref
and find-stackable-blockers live-resolves them).

The module is loaded via importlib (mirroring test_scope_shipped_reference.py)
so the test runs regardless of cwd.
"""
import importlib.machinery
import importlib.util
import sys
import unittest
from pathlib import Path

# exec_module doesn't inherit the CLI wrapper's sys.path; add scripts/fleet/
# so fleet_blocked_by resolves when the loader runs.
sys.path.insert(0, str(Path(__file__).parent.parent))

_MODULE = Path(__file__).parent.parent / "fleet_validate_stack.py"
_loader = importlib.machinery.SourceFileLoader("fleet_validate_stack", str(_MODULE))
_spec = importlib.util.spec_from_loader("fleet_validate_stack", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
is_epic_child = _mod.is_epic_child
validate_child = _mod.validate_child
validate_stack = _mod.validate_stack


def _msgs(findings):
    return [f["msg"] for f in findings]


# A fully template-compliant non-head child body.
COMPLIANT_CHILD = (
    "**Model:** opus\n"
    "**Part of epic:** #1307\n"
    "**Blocked by:** #1308 (T1 must land first)\n"
    "\n## Scope\nstuff\n"
)
# The real #1308 head shape: condensed **Epic:** header bullet, no Part-of-epic.
EPIC_HEADER_HEAD = (
    "**Epic:** #1307 · **Design:** `d.md` (PR #1306) · **PR-1 of 4**\n"
    "**Model:** opus\n\n## Scope\n"
)


class IsEpicChild(unittest.TestCase):
    def test_matches_canonical_part_of_epic(self):
        self.assertTrue(is_epic_child("**Part of epic:** #1307", 1307))

    def test_matches_condensed_epic_header_bullet(self):
        self.assertTrue(is_epic_child(EPIC_HEADER_HEAD, 1307))

    def test_rejects_unrelated_body(self):
        self.assertFalse(is_epic_child("nothing epic here #1307 inline", 1307))

    def test_word_boundary_rejects_longer_number(self):
        self.assertFalse(is_epic_child("**Part of epic:** #13070", 1307))

    def test_handles_crlf(self):
        self.assertTrue(is_epic_child("x\r\n**Part of epic:** #1307\r\n", 1307))

    def test_non_numeric_umbrella_is_false(self):
        self.assertFalse(is_epic_child("**Part of epic:** #1307", "abc"))


class ValidateChildHead(unittest.TestCase):
    def test_compliant_head_passes(self):
        body = "**Model:** sonnet\n**Part of epic:** #1307\n## Scope\n"
        self.assertEqual(validate_child(body, 1307, is_head=True), [])

    def test_head_exempt_from_blocked_by(self):
        # No Blocked-by line, but head — must produce no finding at all.
        body = "**Model:** opus\n**Part of epic:** #1307\n"
        self.assertEqual(validate_child(body, 1307, is_head=True), [])

    def test_real_epic_header_head_flags_part_of_epic_as_error(self):
        findings = validate_child(EPIC_HEADER_HEAD, 1307, is_head=True)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.ERROR)
        self.assertIn("**Epic:**", findings[0]["msg"])
        self.assertIn("**Part of epic:**", findings[0]["msg"])


class ValidateChildNonHead(unittest.TestCase):
    def test_compliant_non_head_passes(self):
        self.assertEqual(validate_child(COMPLIANT_CHILD, 1307, is_head=False), [])

    def test_missing_model_line_is_error(self):
        body = "**Part of epic:** #1307\n**Blocked by:** #1308\n"
        findings = validate_child(body, 1307, is_head=False)
        model = [f for f in findings if "**Model:**" in f["msg"]]
        self.assertEqual(len(model), 1)
        self.assertEqual(model[0]["severity"], _mod.ERROR)

    def test_missing_blocked_by_on_non_head_is_warning(self):
        body = "**Model:** opus\n**Part of epic:** #1307\n"
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.WARN)
        self.assertIn("Blocked by", findings[0]["msg"])

    def test_prose_blocked_on_header_is_invisible(self):
        # The #1309 pre-fix shape: prose "Blocked on T1" in the header bullet,
        # no standalone Blocked-by line. Flags the **Epic:** drift (error) and
        # the missing Blocked-by (warning).
        body = (
            "**Epic:** #1307 · **Blocked on T1 + docs PR #1306**\n"
            "**Model:** opus\n## Scope\n"
        )
        findings = validate_child(body, 1307, is_head=False)
        epic = [f for f in findings if "**Epic:**" in f["msg"]]
        blocked = [f for f in findings if "Blocked by" in f["msg"]]
        self.assertEqual(epic[0]["severity"], _mod.ERROR)
        self.assertEqual(blocked[0]["severity"], _mod.WARN)

    def test_multi_ref_blocked_by_line_is_accepted(self):
        # #1296: multiple refs on one line are supported — the gate unions them
        # and find-stackable-blockers live-resolves them. No longer an error.
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** #1299, #1300\n"
        )
        self.assertEqual(validate_child(body, 1307, is_head=False), [])

    def test_multiple_separate_blocked_by_lines_are_accepted(self):
        # #1296: several **Blocked by:** lines are unioned by the gate and
        # live-resolved by find-stackable-blockers. No longer an error.
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** #1308\n"
            "**Blocked by:** #1309\n"
        )
        self.assertEqual(validate_child(body, 1307, is_head=False), [])

    def test_blocked_by_line_without_ref_is_error(self):
        # A **Blocked by:** line that names no #N is still malformed.
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** the upstream redesign\n"
        )
        findings = validate_child(body, 1307, is_head=False)
        bad = [f for f in findings if "malformed" in f["msg"]]
        self.assertEqual(len(bad), 1)
        self.assertEqual(bad[0]["severity"], _mod.ERROR)

    def test_single_blocker_with_rationale_passes(self):
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** #1308 (foundation must land)\n"
        )
        self.assertEqual(validate_child(body, 1307, is_head=False), [])

    def test_plain_only_blocked_by_warns_with_specific_message(self):
        # The #174-children degraded form: non-bold inline `Blocked by: #N`.
        # Must produce exactly one WARN naming the canonical form (#1786).
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "Part of epic #1307 (Phase D). [opus] Blocked by: #1308, #1309.\n"
        )
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.WARN)
        self.assertIn("degraded plain", findings[0]["msg"])
        self.assertIn("#1786", findings[0]["msg"])

    def test_plain_only_warn_stack_still_ok(self):
        # A plain-only child is a warning, not an error — the stack stays ok.
        body = (
            "**Model:** sonnet\n**Part of epic:** #1307\n"
            "Blocked by: #1308\n"
        )
        result = validate_stack(
            [
                {"number": 1308, "body": "**Model:** sonnet\n**Part of epic:** #1307\n"},
                {"number": 1309, "body": body},
            ],
            1307,
        )
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 1)

    def test_bold_canonical_blocked_by_not_flagged_as_plain(self):
        # Canonical bold form must NOT trigger the plain-only warning.
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** #1308\n"
        )
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(findings, [])

    def test_no_blocker_at_all_gets_generic_warning(self):
        # A child with NO blocker declaration at all (not even plain) still
        # gets the existing generic warning, not the plain-only message.
        body = "**Model:** opus\n**Part of epic:** #1307\n"
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.WARN)
        self.assertNotIn("degraded plain", findings[0]["msg"])


class ValidateStack(unittest.TestCase):
    def _child(self, number, body, title=""):
        return {"number": number, "body": body, "title": title}

    def test_empty_stack(self):
        result = validate_stack([], 1307)
        self.assertTrue(result["empty"])
        self.assertTrue(result["ok"])
        self.assertEqual(result["children"], [])

    def test_lowest_number_is_head(self):
        # Pass children out of order; head (no Blocked-by) must be the min.
        head = self._child(1308, "**Model:** opus\n**Part of epic:** #1307\n")
        second = self._child(1309, COMPLIANT_CHILD)
        result = validate_stack([second, head], 1307)
        by_num = {c["number"]: c for c in result["children"]}
        self.assertTrue(by_num[1308]["is_head"])
        self.assertFalse(by_num[1309]["is_head"])
        self.assertTrue(result["ok"])

    def test_compliant_stack_passes(self):
        children = [
            self._child(1308, "**Model:** opus\n**Part of epic:** #1307\n"),
            self._child(1309, "**Model:** opus\n**Part of epic:** #1307\n"
                              "**Blocked by:** #1308\n"),
            self._child(1310, "**Model:** opus\n**Part of epic:** #1307\n"
                              "**Blocked by:** #1309\n"),
        ]
        result = validate_stack(children, 1307)
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 0)

    def test_multi_root_epic_missing_blocked_by_warns_but_stays_ok(self):
        # The #226 shape: an interior root (#1068, a parallel-track head) with
        # no Blocked-by is a warning, not an error — the stack stays ok unless
        # there is a genuine error elsewhere.
        children = [
            self._child(1067, "**Model:** sonnet\n**Part of epic:** #226\n"),
            self._child(1068, "**Model:** opus\n**Part of epic:** #226\n"),
        ]
        result = validate_stack(children, 226)
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 1)

    def test_real_world_epic_header_stack_fails(self):
        # The actual #1308-#1311 shapes: **Epic:** bullet, no **Part of epic:**.
        # Non-head children do carry **Blocked by:**, so the only finding is
        # the Part-of-epic drift (error) — present on every child.
        children = [
            self._child(1308, EPIC_HEADER_HEAD),
            self._child(1309, "**Epic:** #1307 · **PR-2 of 4**\n"
                              "**Blocked by:** #1308 (T1 first)\n**Model:** opus\n"),
        ]
        result = validate_stack(children, 1307)
        self.assertFalse(result["ok"])
        self.assertTrue(result["n_errors"] >= 2)
        for c in result["children"]:
            self.assertTrue(any("**Part of epic:**" in m for m in _msgs(c["findings"])))


if __name__ == "__main__":
    unittest.main()
