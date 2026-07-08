"""Tests for the genuine-ship predicate in fleet_scope_shipped.

Covers the false-positive classes:

* #1304 — fleet-queue-ingest's scope-shipped pre-flight used to trust prs[0]
  from a `gh pr list --search '#N'` query, but GitHub matches the bare token N
  anywhere (titles, bodies, comments, line numbers, relevance). Incidental
  hits with no literal #N must be rejected.
* #1260 / #1269 — a bare word-boundary #N in a PR *body* is also not proof:
  PRs cite issues they explicitly do NOT fix ("downstream issues (#1260)",
  "pre-existing #1269", "filed as #1269", "Refs #N"). A body ref counts only
  when a closing-action verb sits directly before it; a title ref is trusted
  as-is (the `#N: <desc>` PR-naming convention).
* #1602 / #1612 (layer 3) — range endpoints in an epic-planning title
  ("file children #1602-#1612") enumerate, they don't ship.
* #1807 / #1802 / #1354 (layer 4) — a plan/design-doc title ("docs: plan …",
  "docs/design: …") names the issue it plans/designs, not one it ships, so its
  title ref is not trusted; only a body closing-verb ships it.
* #1640 ← #1700 (layer 6) — a doc-and-defer PR marks the issue deferred in a
  trusted (non-plan) title ("render: doc the invariant (#1640 deferred)"); a
  deferral-marked ref escalates the issue rather than shipping it.
* #2258 ← #2266 (layer 7) — a narrowed refactor marks the issue prep in a
  trusted (non-plan) title ("render: extract … helper (#2258 prep)", linked
  `Part of` not `Closes`); a prep-marked ref prepares the issue rather than
  shipping it.

The module is a real .py, but it is loaded via importlib (mirroring
test_enrich_stackable_blocker_prs.py) so the test runs regardless of cwd.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_MODULE = Path(__file__).parent.parent / "fleet_scope_shipped.py"

_loader = importlib.machinery.SourceFileLoader("fleet_scope_shipped", str(_MODULE))
_spec = importlib.util.spec_from_loader("fleet_scope_shipped", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
pr_references_issue = _mod.pr_references_issue
select_shipped_pr = _mod.select_shipped_pr


def _pr(number, title="", body=""):
    return {"number": number, "title": title, "body": body, "url": "", "mergedAt": ""}


class PrReferencesIssue(unittest.TestCase):
    def test_genuine_closes_in_body(self):
        self.assertTrue(pr_references_issue("some PR", "Closes #1300.", 1300))

    def test_genuine_closing_verb_in_body(self):
        self.assertTrue(pr_references_issue("title", "supersedes #1284 here", 1284))

    def test_genuine_ref_in_title(self):
        self.assertTrue(pr_references_issue("#1300: render fix", "", 1300))

    def test_bare_number_no_hash_is_not_a_reference(self):
        # The #1243 -> #1300 shape: "1300" appears as a line number, no '#'.
        self.assertFalse(
            pr_references_issue("delete orphaned queue scripts",
                                "touches lines 71, 176, 1300, 3619", 1300))

    def test_no_literal_number_at_all(self):
        # The #1160 -> #1284 shape: relevance-only hit, number absent entirely.
        self.assertFalse(
            pr_references_issue("codegen: emit per-component tick", "body text", 1284))

    def test_longer_number_does_not_match_prefix(self):
        # #13000 must not satisfy n=1300.
        self.assertFalse(pr_references_issue("", "see #13000", 1300))

    def test_longer_number_does_not_match_suffix(self):
        # #1300 query must not be satisfied by a leading-digit token.
        self.assertFalse(pr_references_issue("", "see #21300", 1300))

    def test_hash_ref_with_adjacent_punctuation(self):
        self.assertTrue(pr_references_issue("", "fixes (#1300) finally", 1300))

    def test_falsy_issue_number(self):
        self.assertFalse(pr_references_issue("#0 ref", "#0 ref", 0))

    def test_alphanumeric_before_hash_is_not_a_reference(self):
        # "abc#1300" — '#' immediately preceded by a word char must be rejected.
        self.assertFalse(pr_references_issue("", "abc#1300 fix", 1300))

    def test_non_numeric_truthy_n_returns_false(self):
        # int(n) raises ValueError for a truthy non-numeric string.
        self.assertFalse(pr_references_issue("", "Closes #1300", "abc"))

    # --- mentioned-but-not-shipped body refs (#1260 / #1269) ---

    def test_downstream_body_mention_rejected(self):
        # The #1260 <- #1282 shape: PR names #1260 as explicitly-NOT-fixed work.
        self.assertFalse(pr_references_issue(
            "#1271: demo: IRShapeDebug --spin-yaw",
            "bug-fixing is downstream issues (#1256, #1260, etc). This PR ships "
            "the regression scaffolding.", 1260))

    def test_pre_existing_body_mention_rejected(self):
        # The #1269 <- #1265 shape: PR cites #1269 as a pre-existing failure.
        self.assertFalse(pr_references_issue(
            "#1258: render: camera pitch/roll",
            "MatchesStd140Packing fails on origin/master independently of this "
            "change (filed as #1269). 997/998 pass; the one failure is the "
            "pre-existing #1269.", 1269))

    def test_refs_body_mention_rejected(self):
        # "Refs #N" deliberately does not auto-close — not ship evidence.
        self.assertFalse(pr_references_issue("title", "Using Refs #1271 here", 1271))

    def test_closing_verb_does_not_bind_across_sentence(self):
        # A verb earlier in the body must not bind to a later unrelated #N.
        self.assertFalse(pr_references_issue(
            "", "Fixes #1258. Also see downstream #1260.", 1260))

    def test_title_ref_counts_even_with_unrelated_body_mention(self):
        # The same PR that mentions #1269 in its body genuinely ships #1258.
        self.assertTrue(pr_references_issue(
            "#1258: render: camera pitch/roll", "pre-existing #1269 still fails", 1258))

    def test_closing_verb_variants_in_body(self):
        for body in ("fixed #1300", "resolves #1300", "Resolved: #1300",
                     "implements #1300", "shipped #1300", "supersedes #1300",
                     "closes #1300", "Fixes #1300"):
            self.assertTrue(pr_references_issue("", body, 1300), body)

    # --- range-endpoint refs (#1602 / #1612 <- #1614 epic-planning PR) ---

    _FILE_CHILDREN_TITLE = (
        "docs: re-plan entity-editor Phase 2 (#605) — file children #1602-#1612")

    def test_title_range_start_endpoint_rejected(self):
        # #1602 is the range START in "#1602-#1612": filed by #1614, not shipped.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1602))

    def test_title_range_end_endpoint_rejected(self):
        # #1612 is the range END (the dash sits directly before it).
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1612))

    def test_title_range_middle_child_never_matched(self):
        # #1607 is inside the span but not literally written with a '#'.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1607))

    def test_title_range_hashless_second_endpoint_still_rejects_start(self):
        # "#1602-1612" (second endpoint has no '#') still rejects the start.
        self.assertFalse(pr_references_issue("file children #1602-1612", "", 1602))

    def test_title_en_dash_range_rejected(self):
        self.assertFalse(pr_references_issue("file #1602–#1612", "", 1602))

    def test_title_em_dash_range_rejected(self):
        self.assertFalse(pr_references_issue("file #1602—#1612", "", 1612))

    def test_replan_doc_title_does_not_ship_the_epic(self):
        # Layer 4 supersedes the old "epic ref outside range still trusted" case:
        # _FILE_CHILDREN_TITLE is a "docs: re-plan …" plan-doc title, so even the
        # epic ref (#605) it re-plans is NOT trusted — a re-plan commits the plan,
        # not the implementation. Flipping is safe: a scope-shipped false negative
        # just leaves the issue queued.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 605))
        # A body closing-verb still ships it even from a plan PR.
        self.assertTrue(pr_references_issue(self._FILE_CHILDREN_TITLE, "Closes #605", 605))

    def test_plain_title_ref_unaffected_by_range_guard(self):
        # Regression: the normal "#N: <desc>" convention still counts.
        self.assertTrue(pr_references_issue("#1602: bind-pose on C_Skeleton", "", 1602))

    def test_em_dash_then_word_is_not_a_range(self):
        # "(#605) — phase 2": a dash followed by a WORD (no digit) is not a range.
        self.assertTrue(pr_references_issue("re-plan (#605) — phase 2", "", 605))

    def test_body_range_endpoint_rejected_even_with_verb(self):
        # Conservative: "Closes #1602-#1612" closes a span, not #1602's individual
        # scope. Reject — a scope-shipped false negative just leaves it queued.
        self.assertFalse(pr_references_issue("title", "Closes #1602-#1612", 1602))

    # --- plan/design-doc titles (#1807 / #1802 / #1354 — layer 4) ---

    def test_plan_doc_title_single_issue_rejected(self):
        # #1807 ← #1809: a "docs: plan …" PR plans the issue, doesn't ship it.
        self.assertFalse(pr_references_issue(
            "docs: plan rotation-profiling task (#1807)", "", 1807))

    def test_plan_doc_title_hash_subject_rejected(self):
        # "docs: plan #1052 …" names the planned epic in the subject.
        self.assertFalse(pr_references_issue(
            "docs: plan #1052 update-parallelization carve-offs", "", 1052))

    def test_plan_doc_title_slash_list_child_rejected(self):
        # #1802 ← #1805: filed children as a '/'-separated list — a shape the
        # layer-3 dash guard never covered; layer 4 rejects the whole plan title.
        title = "docs: plan #1052 update-parallelization carve-offs (#1802/#1803/#1804)"
        for child in (1802, 1803, 1804):
            self.assertFalse(pr_references_issue(title, "", child), child)

    def test_design_doc_scope_rejected(self):
        # #1354 ← #1411: "docs/design: …" designs the issue, doesn't ship it.
        self.assertFalse(pr_references_issue(
            "docs/design: world-space neighbour/spatial-query surface (#1354)", "", 1354))

    def test_design_subject_rejected(self):
        # "docs: design …" (design in the subject, not the scope) is also caught.
        self.assertFalse(pr_references_issue(
            "docs: design for the deterministic GUI/mouse harness (#1792)", "", 1792))

    def test_plan_doc_title_still_ships_via_body_closing_verb(self):
        # A doc PR whose deliverable genuinely IS the doc still ships via body.
        self.assertTrue(pr_references_issue(
            "docs: plan #1052 carve-offs", "Closes #1052", 1052))

    def test_non_plan_docs_pr_title_ref_still_trusted(self):
        # Regression: a normal docs PR (subject not plan/design) keeps title-trust.
        self.assertTrue(pr_references_issue(
            "docs: #1679 cmake preset -S flag", "", 1679))

    # --- closing verb inside a code span (#1824 ← #1854 — layer 5) ---

    def test_closing_verb_in_inline_code_span_rejected(self):
        # #1824 ← #1854: a plan PR's body quotes the `Closes #N` its FUTURE impl
        # PR will write. Layer 4 suppresses the plan-doc title; layer 5 must stop
        # the backtick-quoted closing verb in the body from shipping it.
        body = ("Adds `.fleet/plans/issue-1824.md`: the structured plan for "
                "#1824 (planning step output; impl PR will carry the code + "
                "`Closes #1824`).\n\nPlan doc for #1824 — does not close the "
                "issue (the implementation PR will).")
        self.assertFalse(pr_references_issue(
            "docs: plan #1824 — fleet-rebase fork-point inherited-prefix drop",
            body, 1824))

    def test_closing_verb_in_fenced_block_rejected(self):
        # A `Closes #N` shown inside a fenced example block is documentation.
        body = "Future impl PR body:\n```\nCloses #1300\n```\nThis PR only plans."
        self.assertFalse(pr_references_issue("title", body, 1300))

    def test_prose_close_with_adjacent_code_span_still_ships(self):
        # Regression: stripping code spans must not drop a genuine prose close
        # that merely sits near unrelated inline code.
        self.assertTrue(pr_references_issue(
            "title", "Closes #1300. Verified via `fleet-run IRShapeDebug`.", 1300))

    def test_code_span_strip_collapses_to_space_not_empty(self):
        # A code span between letters must collapse to a SPACE, never empty, so
        # it can't fuse into a spurious closing verb: "clo`x`ses #1300" must not
        # become "closes #1300".
        self.assertFalse(pr_references_issue("title", "clo`x`ses #1300", 1300))

    # --- deferral markers (#1640 <- #1700 — layer 6) ---

    _DEFER_TITLE_1700 = (
        "render: doc the Metal foreign-canvas R32I second-dispatch "
        "read-gap invariant (#1640 deferred)")

    def test_deferral_marked_title_ref_rejected(self):
        # #1640 <- #1700: a render:-scoped doc PR marks the issue deferred in its
        # title ("(#1640 deferred)") and only "Refs #1640" (no closing verb) in
        # the body. Layer 4 doesn't fire (title is render:, not docs:), so
        # title-trust would ship it without layer 6.
        self.assertFalse(pr_references_issue(
            self._DEFER_TITLE_1700, "Refs #1640.\n\n## Status: design-blocked", 1640))

    def test_deferral_leading_verb_rejected(self):
        # "defers #N": deferral word directly before the ref.
        self.assertFalse(pr_references_issue("render: defers #1640 to follow-up", "", 1640))

    def test_deferral_deferring_variant_rejected(self):
        self.assertFalse(pr_references_issue("render: doc gap, deferring #1640", "", 1640))

    def test_deferral_em_dash_marker_rejected(self):
        # "#N — deferred": em-dash gap between the ref and the deferral word.
        self.assertFalse(pr_references_issue("render: doc invariant #1640 — deferred", "", 1640))

    def test_deferral_of_other_issue_does_not_suppress_shipped(self):
        # "fix #1234 and defer #1235": #1234 genuinely ships (title-trust); the
        # far-away "defer" binds only to the adjacent #1235, not across to #1234.
        title = "render: fix #1234 and defer #1235"
        self.assertTrue(pr_references_issue(title, "", 1234))
        self.assertFalse(pr_references_issue(title, "", 1235))

    def test_deferral_title_still_ships_via_body_closing_verb(self):
        # A deferral title marker suppresses title-trust, but an explicit body
        # close still ships (consistent with layer 4: a genuine landing wins).
        self.assertTrue(pr_references_issue(self._DEFER_TITLE_1700, "Closes #1640", 1640))

    def test_plain_title_ref_unaffected_by_deferral_guard(self):
        # Regression: a normal "#N: <desc>" title with no deferral word still ships.
        self.assertTrue(pr_references_issue("#1640: render fix the R32I read", "", 1640))

    def test_defer_prefix_word_does_not_suppress(self):
        # \b-anchored: "deferential" carries "defer" as a prefix but is not a
        # deferral word, so a ref beside it still ships (the trailing \b fails to
        # close on "defer" inside "deferential").
        self.assertTrue(pr_references_issue("#1640 deferential render fix", "", 1640))

    # --- prep / partial markers (#2258 <- #2266 — layer 7) ---

    _PREP_TITLE_2266 = (
        "render: extract sunBakeFrustumUVBounds shared helper (#2258 prep)")

    def test_prep_marked_title_ref_rejected(self):
        # #2258 <- #2266: a render:-scoped narrowed refactor marks the issue prep
        # in its title ("(#2258 prep)") and only "Part of #2258" (no closing verb)
        # in the body. Layer 4 doesn't fire (title is render:, not docs:), so
        # title-trust would ship it without layer 7.
        self.assertFalse(pr_references_issue(
            self._PREP_TITLE_2266, "Part of #2258. Byte-identical extraction.", 2258))

    def test_prep_leading_word_rejected(self):
        # "prep for #N": prep word directly before the ref.
        self.assertFalse(pr_references_issue("render: prep for #2258 feeder cap", "", 2258))

    def test_prep_preparatory_variant_rejected(self):
        self.assertFalse(pr_references_issue("render: preparatory #2258 refactor", "", 2258))

    def test_prep_of_other_issue_does_not_suppress_shipped(self):
        # "land #1234 and prep #1235": #1234 genuinely ships (title-trust); the
        # "prep" binds only to the adjacent #1235 — the "and" breaks the
        # bounded-gap adjacency to #1234 (mirrors the layer-6 deferral case).
        title = "render: land #1234 and prep #1235"
        self.assertTrue(pr_references_issue(title, "", 1234))
        self.assertFalse(pr_references_issue(title, "", 1235))

    def test_prep_title_still_ships_via_body_closing_verb(self):
        # A prep title marker suppresses title-trust, but an explicit body close
        # still ships (consistent with layers 4/6: a genuine landing wins).
        self.assertTrue(pr_references_issue(self._PREP_TITLE_2266, "Closes #2258", 2258))

    def test_prep_prefix_word_does_not_suppress(self):
        # \b-anchored: "prepend" carries "prep" as a prefix but is not a prep
        # word, so a ref beside it still ships (the trailing \b fails to close on
        # "prep" inside "prepend").
        self.assertTrue(pr_references_issue("#2258 prepend the sun-bake header", "", 2258))


class SelectShippedPr(unittest.TestCase):
    def test_empty_candidates(self):
        self.assertIsNone(select_shipped_pr([], 1300))

    def test_no_genuine_reference_returns_none(self):
        prs = [
            _pr(1243, "delete orphaned queue scripts", "lines 71, 176, 1300"),
            _pr(1160, "codegen: emit per-component tick", "unrelated"),
        ]
        self.assertIsNone(select_shipped_pr(prs, 1300))

    def test_picks_first_genuine_reference(self):
        prs = [
            _pr(1243, "delete orphaned queue scripts", "lines 71, 176, 1300"),
            _pr(900, "real shipper", "Closes #1300"),
        ]
        best = select_shipped_pr(prs, 1300)
        self.assertIsNotNone(best)
        self.assertEqual(best["number"], 900)

    def test_missing_title_body_keys(self):
        # Defensive: a candidate dict without title/body must not raise.
        self.assertIsNone(select_shipped_pr([{"number": 5}], 1300))

    def test_real_world_1260_not_shipped_by_1282(self):
        # #1282 only mentions #1260 as downstream-not-fixed -> no ship.
        prs = [_pr(1282, "#1271: demo: IRShapeDebug --spin-yaw",
                   "bug-fixing is downstream issues (#1256, #1260, etc)")]
        self.assertIsNone(select_shipped_pr(prs, 1260))

    def test_real_world_1265_ships_1258_not_1269(self):
        # #1265 closes #1258 but only cites #1269 as a pre-existing failure.
        pr = _pr(1265, "#1258: render: camera pitch/roll",
                 "Closes #1258. The one failure is the pre-existing #1269.")
        self.assertEqual(select_shipped_pr([pr], 1258)["number"], 1265)
        self.assertIsNone(select_shipped_pr([pr], 1269))

    def test_real_world_1614_files_children_ships_none(self):
        # #1614 FILES the P2 children as a title range; it ships none of them —
        # and under layer 4 (a "docs: re-plan …" title) it doesn't ship the epic
        # #605 it re-plans either.
        pr = _pr(1614,
                 "docs: re-plan entity-editor Phase 2 (#605) — file children #1602-#1612",
                 "Files the P2 child tickets #1602-#1612 under epic #605.")
        for issue in (605, 1602, 1607, 1612):
            self.assertIsNone(select_shipped_pr([pr], issue), issue)

    def test_real_world_1807_planned_not_shipped_by_1809(self):
        # #1807 ← #1809: the plan-doc PR is the only #1807 search hit; it plans
        # the profiling task, it doesn't ship it.
        pr = _pr(1809, "docs: plan rotation-profiling task (#1807)",
                 "Plan doc committed for #1807.")
        self.assertIsNone(select_shipped_pr([pr], 1807))

    def test_real_world_1824_planned_not_shipped_by_1854(self):
        # #1824 ← #1854: plan-doc title (layer 4) AND a body that quotes the
        # `Closes #1824` the future impl PR will write (layer 5). Ships nothing.
        pr = _pr(1854,
                 "docs: plan #1824 — fleet-rebase fork-point inherited-prefix drop",
                 "Adds `.fleet/plans/issue-1824.md`: the structured plan for "
                 "#1824 (impl PR will carry the code + `Closes #1824`).")
        self.assertIsNone(select_shipped_pr([pr], 1824))

    def test_real_world_1640_deferred_by_1700(self):
        # #1640 <- #1700 (layer 6): a render:-scoped doc-and-defer PR marks the
        # issue deferred in its title and only "Refs #1640" (no closing verb) in
        # the body — it ships nothing, so ingest must not re-stamp scope-shipped.
        pr = _pr(1700,
                 "render: doc the Metal foreign-canvas R32I second-dispatch "
                 "read-gap invariant (#1640 deferred)",
                 "Refs #1640.\n\n## Status: design-blocked (see NEEDS-DESIGN comment)")
        self.assertIsNone(select_shipped_pr([pr], 1640))

    def test_real_world_2258_prepped_by_2266(self):
        # #2258 <- #2266 (layer 7): a render:-scoped narrowed refactor marks the
        # issue prep in its title and only "Part of #2258" (no closing verb) in
        # the body — it ships only a shared helper, not the issue's perf scope, so
        # ingest must not stamp scope-shipped and clobber the issue's re-queue.
        pr = _pr(2266,
                 "render: extract sunBakeFrustumUVBounds shared helper (#2258 prep)",
                 "Part of #2258. Keeps only the independently-correct refactor and "
                 "drops the dead plumbing (the `feederSubCap` derivation, the "
                 "stage-1 feeder early-return).")
        self.assertIsNone(select_shipped_pr([pr], 2258))


if __name__ == "__main__":
    unittest.main()
