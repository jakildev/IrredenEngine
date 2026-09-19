"""Tests for the genuine-ship predicate in fleet_scope_shipped.

Covers the false-positive classes:

* layer 1 — a `gh pr list --search '#N'` query can return a PR that never
  literally cites #N: GitHub matches the bare token N anywhere (titles,
  bodies, comments, line numbers, relevance). Incidental hits with no
  literal #N must be rejected.
* layer 2 — a bare word-boundary #N in a PR *body* is also not proof: PRs
  cite issues they explicitly do NOT fix ("downstream issues (#N)",
  "pre-existing #N", "filed as #N", "Refs #N"). A body ref counts only when
  a closing-action verb sits directly before it; a title ref is trusted
  as-is (the `#N: <desc>` PR-naming convention).
* layer 3 — range endpoints in an epic-planning title ("file children
  #N-#M") enumerate, they don't ship.
* layer 4 — a plan/design-doc title ("docs: plan …", "docs/design: …")
  names the issue it plans/designs, not one it ships, so its title ref is
  not trusted; only a body closing-verb ships it.
* layer 6 — a doc-and-defer PR marks the issue deferred in a trusted
  (non-plan) title ("render: doc the invariant (#N deferred)"); a
  deferral-marked ref escalates the issue rather than shipping it.
* layer 7 — a narrowed refactor marks the issue prep in a trusted (non-plan)
  title ("render: extract … helper (#N prep)", linked `Part of` not
  `Closes`); a prep-marked ref prepares the issue rather than shipping it.
* layer 8 — a bookkeeping PR can name an issue in a trusted (non-plan)
  title while its diff is entirely `.fleet/` files; an all-`.fleet/` diff
  ships no code scope, so it falls through to the body closing-verb check.
* layer 9 — a render-scoped verification PR can deliberately leave the
  issue open for a later platform phase while changing only `docs/`; the
  title ref falls through to the body closing-verb check. The same applies
  to a mixed `.fleet/` + `docs/` diff.

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


def _pr(number, title="", body="", files=None):
    pr = {"number": number, "title": title, "body": body, "url": "", "mergedAt": ""}
    if files is not None:
        # Mirror the `gh pr list --json files` shape: a list of {'path': ...} dicts.
        pr["files"] = [{"path": p} for p in files]
    return pr


class PrReferencesIssue(unittest.TestCase):
    def test_genuine_closes_in_body(self):
        self.assertTrue(pr_references_issue("some PR", "Closes #1300.", 1300))

    def test_genuine_closing_verb_in_body(self):
        self.assertTrue(pr_references_issue("title", "supersedes #1284 here", 1284))

    def test_genuine_ref_in_title(self):
        self.assertTrue(pr_references_issue("#1300: render fix", "", 1300))

    def test_bare_number_no_hash_is_not_a_reference(self):
        # A number appearing as a line number, with no '#', is not a reference.
        self.assertFalse(
            pr_references_issue("delete orphaned queue scripts",
                                "touches lines 71, 176, 1300, 3619", 1300))

    def test_no_literal_number_at_all(self):
        # A relevance-only search hit, with the number absent entirely, is
        # not a reference.
        self.assertFalse(
            pr_references_issue("codegen: emit per-component tick", "body text", 1284))

    def test_longer_number_does_not_match_prefix(self):
        # A longer number must not satisfy a shorter query by prefix.
        self.assertFalse(pr_references_issue("", "see #13000", 1300))

    def test_longer_number_does_not_match_suffix(self):
        # A query must not be satisfied by a longer token that merely ends
        # with it.
        self.assertFalse(pr_references_issue("", "see #21300", 1300))

    def test_hash_ref_with_adjacent_punctuation(self):
        self.assertTrue(pr_references_issue("", "fixes (#1300) finally", 1300))

    def test_falsy_issue_number(self):
        self.assertFalse(pr_references_issue("#0 ref", "#0 ref", 0))

    def test_alphanumeric_before_hash_is_not_a_reference(self):
        # A '#' immediately preceded by a word character (no boundary) must
        # be rejected as a reference.
        self.assertFalse(pr_references_issue("", "abc#1300 fix", 1300))

    def test_non_numeric_truthy_n_returns_false(self):
        # int(n) raises ValueError for a truthy non-numeric string.
        self.assertFalse(pr_references_issue("", "Closes #1300", "abc"))

    # --- mentioned-but-not-shipped body refs ---

    def test_downstream_body_mention_rejected(self):
        # A PR can name an issue as explicitly-NOT-fixed work in its body.
        self.assertFalse(pr_references_issue(
            "#1271: demo: IRShapeDebug --spin-yaw",
            "bug-fixing is downstream issues (#1256, #1260, etc). This PR ships "
            "the regression scaffolding.", 1260))

    def test_pre_existing_body_mention_rejected(self):
        # A PR can cite an issue as a pre-existing failure rather than one it
        # fixes.
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
        # A PR that mentions one issue in its body can still genuinely ship
        # a different issue via its title.
        self.assertTrue(pr_references_issue(
            "#1258: render: camera pitch/roll", "pre-existing #1269 still fails", 1258))

    def test_closing_verb_variants_in_body(self):
        for body in ("fixed #1300", "resolves #1300", "Resolved: #1300",
                     "implements #1300", "shipped #1300", "supersedes #1300",
                     "closes #1300", "Fixes #1300"):
            self.assertTrue(pr_references_issue("", body, 1300), body)

    # --- range-endpoint refs (epic-planning PR) ---

    _FILE_CHILDREN_TITLE = (
        "docs: re-plan entity-editor Phase 2 (#605) — file children #1602-#1612")

    def test_title_range_start_endpoint_rejected(self):
        # The range START endpoint in a title range is filed, not shipped.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1602))

    def test_title_range_end_endpoint_rejected(self):
        # The range END endpoint is rejected too (the dash sits directly
        # before it).
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1612))

    def test_title_range_middle_child_never_matched(self):
        # A child inside the span but not literally written with a '#'
        # never matches.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 1607))

    def test_title_range_hashless_second_endpoint_still_rejects_start(self):
        # A range whose second endpoint has no '#' still rejects the start.
        self.assertFalse(pr_references_issue("file children #1602-1612", "", 1602))

    def test_title_en_dash_range_rejected(self):
        self.assertFalse(pr_references_issue("file #1602–#1612", "", 1602))

    def test_title_em_dash_range_rejected(self):
        self.assertFalse(pr_references_issue("file #1602—#1612", "", 1612))

    def test_replan_doc_title_does_not_ship_the_epic(self):
        # _FILE_CHILDREN_TITLE is a "docs: re-plan …" plan-doc title, so even
        # the epic ref it re-plans is NOT trusted — a re-plan commits the
        # plan, not the implementation. A false negative here just leaves the
        # issue queued.
        self.assertFalse(pr_references_issue(self._FILE_CHILDREN_TITLE, "", 605))
        # A body closing-verb still ships it even from a plan PR.
        self.assertTrue(pr_references_issue(self._FILE_CHILDREN_TITLE, "Closes #605", 605))

    def test_plain_title_ref_unaffected_by_range_guard(self):
        # Regression: the normal "#N: <desc>" convention still counts.
        self.assertTrue(pr_references_issue("#1602: bind-pose on C_Skeleton", "", 1602))

    def test_em_dash_then_word_is_not_a_range(self):
        # A dash followed by a WORD (no digit) is not a range.
        self.assertTrue(pr_references_issue("re-plan (#605) — phase 2", "", 605))

    def test_body_range_endpoint_rejected_even_with_verb(self):
        # Conservative: a closing verb over a range closes the span, not an
        # individual issue's scope within it. Reject — a false negative just
        # leaves it queued.
        self.assertFalse(pr_references_issue("title", "Closes #1602-#1612", 1602))

    # --- plan/design-doc titles (layer 4) ---

    def test_plan_doc_title_single_issue_rejected(self):
        # A "docs: plan …" PR plans the issue, it doesn't ship it.
        self.assertFalse(pr_references_issue(
            "docs: plan rotation-profiling task (#1807)", "", 1807))

    def test_plan_doc_title_hash_subject_rejected(self):
        # A "docs: plan #N …" title names the planned epic in the subject.
        self.assertFalse(pr_references_issue(
            "docs: plan #1052 update-parallelization carve-offs", "", 1052))

    def test_plan_doc_title_slash_list_child_rejected(self):
        # Filed children as a '/'-separated list — a shape the layer-3 dash
        # guard never covers; layer 4 rejects the whole plan title.
        title = "docs: plan #1052 update-parallelization carve-offs (#1802/#1803/#1804)"
        for child in (1802, 1803, 1804):
            self.assertFalse(pr_references_issue(title, "", child), child)

    def test_design_doc_scope_rejected(self):
        # A "docs/design: …" title designs the issue, it doesn't ship it.
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

    # --- closing verb inside a code span (layer 5) ---

    def test_closing_verb_in_inline_code_span_rejected(self):
        # A plan PR's body can quote the `Closes #N` its FUTURE impl PR will
        # write. Layer 4 suppresses the plan-doc title; layer 5 must stop the
        # backtick-quoted closing verb in the body from shipping it.
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
        # A code span between letters must collapse to a SPACE, never empty,
        # so it can't fuse into a spurious closing verb where none was written.
        self.assertFalse(pr_references_issue("title", "clo`x`ses #1300", 1300))

    # --- deferral markers (layer 6) ---

    _DEFER_TITLE_1700 = (
        "render: doc the Metal foreign-canvas R32I second-dispatch "
        "read-gap invariant (#1640 deferred)")

    def test_deferral_marked_title_ref_rejected(self):
        # A render:-scoped doc PR can mark the issue deferred in its title
        # ("(#N deferred)") and only "Refs #N" (no closing verb) in the body.
        # Layer 4 doesn't fire (title is render:, not docs:), so title-trust
        # would ship it without layer 6.
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
        # A far-away deferral word binds only to the adjacent ref, not across
        # to an earlier ref that genuinely ships (title-trust).
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

    # --- prep / partial markers (layer 7) ---

    _PREP_TITLE_2266 = (
        "render: extract sunBakeFrustumUVBounds shared helper (#2258 prep)")

    def test_prep_marked_title_ref_rejected(self):
        # A render:-scoped narrowed refactor can mark the issue prep in its
        # title ("(#N prep)") and only "Part of #N" (no closing verb) in the
        # body. Layer 4 doesn't fire (title is render:, not docs:), so
        # title-trust would ship it without layer 7.
        self.assertFalse(pr_references_issue(
            self._PREP_TITLE_2266, "Part of #2258. Byte-identical extraction.", 2258))

    def test_prep_leading_word_rejected(self):
        # "prep for #N": prep word directly before the ref.
        self.assertFalse(pr_references_issue("render: prep for #2258 feeder cap", "", 2258))

    def test_prep_preparatory_variant_rejected(self):
        self.assertFalse(pr_references_issue("render: preparatory #2258 refactor", "", 2258))

    def test_prep_of_other_issue_does_not_suppress_shipped(self):
        # A "prep" word binds only to the adjacent ref — the "and" breaks the
        # bounded-gap adjacency to an earlier ref that genuinely ships
        # (title-trust), mirroring the layer-6 deferral case.
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

    # --- bookkeeping diff — all-.fleet/ PRs (layer 8) ---

    _STEWARD_TITLE_2392 = (
        "docs/fleet: epic-steward — #2317 rollup + #2385 adoption (#2314)")
    _FLEET_FILES = [{"path": ".fleet/status/epic-2314.md"},
                    {"path": ".fleet/status/epic-2385.md"}]

    def test_bookkeeping_diff_title_ref_rejected(self):
        # A steward rollup PR can name an issue in a trusted (non-plan) title
        # while its diff is only .fleet/ docs — it maintains fleet state, it
        # does not ship the render fix. Its subject is "epic-steward" (neither
        # plan nor design), so layers 4/6/7 do not fire; the all-.fleet/ diff does.
        self.assertFalse(pr_references_issue(
            self._STEWARD_TITLE_2392, "", 2385, self._FLEET_FILES))
        # An epic ref it rolls up is likewise not shipped.
        self.assertFalse(pr_references_issue(
            self._STEWARD_TITLE_2392, "", 2314, self._FLEET_FILES))

    def test_bookkeeping_diff_string_paths_accepted(self):
        # Defensive: a plain list of path strings (not {'path': ...} dicts) works.
        self.assertFalse(pr_references_issue(
            self._STEWARD_TITLE_2392, "", 2385,
            [".fleet/status/epic-2314.md", ".fleet/status/epic-2385.md"]))

    def test_bookkeeping_diff_still_ships_via_body_closing_verb(self):
        # A .fleet/-only PR whose deliverable genuinely IS the fleet change still
        # ships via a prose body close (consistent with layers 4/6/7).
        self.assertTrue(pr_references_issue(
            self._STEWARD_TITLE_2392, "Closes #2385", 2385, self._FLEET_FILES))

    def test_mixed_diff_keeps_title_trust(self):
        # A PR that touches a .fleet/ note AND code is NOT all-.fleet/, so
        # title-trust is retained.
        files = [{"path": ".fleet/status/epic-2385.md"},
                 {"path": "engine/render/fog.cpp"}]
        self.assertTrue(pr_references_issue("#2385: render: fog fix", "", 2385, files))

    def test_no_files_keeps_pre_layer8_title_trust(self):
        # files omitted (default None) — layer 8 is inert, so the steward
        # title's bare ref is still trusted; layer 8 only fires once the
        # caller supplies the diff.
        self.assertTrue(pr_references_issue(self._STEWARD_TITLE_2392, "", 2385))

    def test_empty_files_list_keeps_title_trust(self):
        # An empty diff list carries no signal — treat as no-info, keep title-trust.
        self.assertTrue(pr_references_issue(self._STEWARD_TITLE_2392, "", 2385, []))

    # --- documentation diff — all non-shipping paths (layer 9) ---

    _VERIFY_TITLE_3020 = (
        "render: GL Phase-0 verification of world-placed detached "
        "re-voxelize cast (#2091)")

    def test_documentation_diff_title_ref_rejected(self):
        self.assertFalse(pr_references_issue(
            self._VERIFY_TITLE_3020, "No partial close.", 2091,
            ["docs/design/detached-revoxelize-world-light.md"]))

    def test_nonshipping_union_diff_title_ref_rejected(self):
        files = [".fleet/plans/issue-2298.md",
                 "docs/design/voxel-occlusion-culling.md"]
        self.assertFalse(pr_references_issue(
            "docs/render: close out #2298 — trace-invariant record + plan "
            "file (impl shipped in #2475)", "", 2475, files))

    def test_documentation_diff_still_ships_via_body_closing_verb(self):
        self.assertTrue(pr_references_issue(
            self._VERIFY_TITLE_3020, "Closes #2091", 2091,
            ["docs/design/detached-revoxelize-world-light.md"]))

    def test_root_markdown_keeps_title_trust(self):
        self.assertTrue(pr_references_issue(
            "docs: update README for #2091", "", 2091, ["README.md"]))


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
        # A PR that only mentions an issue as downstream-not-fixed does not
        # ship it.
        prs = [_pr(1282, "#1271: demo: IRShapeDebug --spin-yaw",
                   "bug-fixing is downstream issues (#1256, #1260, etc)")]
        self.assertIsNone(select_shipped_pr(prs, 1260))

    def test_real_world_1265_ships_1258_not_1269(self):
        # A PR can close one issue while only citing another as a
        # pre-existing failure — it ships the first, not the second.
        pr = _pr(1265, "#1258: render: camera pitch/roll",
                 "Closes #1258. The one failure is the pre-existing #1269.")
        self.assertEqual(select_shipped_pr([pr], 1258)["number"], 1265)
        self.assertIsNone(select_shipped_pr([pr], 1269))

    def test_real_world_1614_files_children_ships_none(self):
        # A PR that FILES children as a title range ships none of them — and
        # under layer 4 (a "docs: re-plan …" title) it doesn't ship the epic
        # it re-plans either.
        pr = _pr(1614,
                 "docs: re-plan entity-editor Phase 2 (#605) — file children #1602-#1612",
                 "Files the P2 child tickets #1602-#1612 under epic #605.")
        for issue in (605, 1602, 1607, 1612):
            self.assertIsNone(select_shipped_pr([pr], issue), issue)

    def test_real_world_1807_planned_not_shipped_by_1809(self):
        # A plan-doc PR can be the only search hit for an issue; it plans
        # the task, it doesn't ship it.
        pr = _pr(1809, "docs: plan rotation-profiling task (#1807)",
                 "Plan doc committed for #1807.")
        self.assertIsNone(select_shipped_pr([pr], 1807))

    def test_real_world_1824_planned_not_shipped_by_1854(self):
        # A plan-doc title (layer 4) combined with a body that quotes the
        # `Closes #N` a future impl PR will write (layer 5) ships nothing.
        pr = _pr(1854,
                 "docs: plan #1824 — fleet-rebase fork-point inherited-prefix drop",
                 "Adds `.fleet/plans/issue-1824.md`: the structured plan for "
                 "#1824 (impl PR will carry the code + `Closes #1824`).")
        self.assertIsNone(select_shipped_pr([pr], 1824))

    def test_real_world_1640_deferred_by_1700(self):
        # (layer 6): a render:-scoped doc-and-defer PR can mark the issue
        # deferred in its title and only "Refs #N" (no closing verb) in the
        # body — it ships nothing, so ingest must not re-stamp scope-shipped.
        pr = _pr(1700,
                 "render: doc the Metal foreign-canvas R32I second-dispatch "
                 "read-gap invariant (#1640 deferred)",
                 "Refs #1640.\n\n## Status: design-blocked (see NEEDS-DESIGN comment)")
        self.assertIsNone(select_shipped_pr([pr], 1640))

    def test_real_world_2258_prepped_by_2266(self):
        # (layer 7): a render:-scoped narrowed refactor can mark the issue
        # prep in its title and only "Part of #N" (no closing verb) in the
        # body — it ships only a shared helper, not the issue's perf scope,
        # so ingest must not stamp scope-shipped and clobber the issue's
        # re-queue.
        pr = _pr(2266,
                 "render: extract sunBakeFrustumUVBounds shared helper (#2258 prep)",
                 "Part of #2258. Keeps only the independently-correct refactor and "
                 "drops the dead plumbing (the `feederSubCap` derivation, the "
                 "stage-1 feeder early-return).")
        self.assertIsNone(select_shipped_pr([pr], 2258))

    def test_real_world_2385_bookkept_by_2392(self):
        # (layer 8): an epic-steward rollup PR can be a search hit for an
        # issue whose diff is entirely .fleet/plans/ docs — it adopts the
        # plan, it ships no render fix, so ingest must not stamp
        # scope-shipped and bounce a queue-ready issue back to needs-plan.
        pr = _pr(2392,
                 "docs/fleet: epic-steward — #2317 rollup + #2385 adoption (#2314)",
                 "Rolls up #2317; adopts the #2385 plan into the ledger.",
                 files=[".fleet/status/epic-2314.md", ".fleet/status/epic-2385.md"])
        self.assertIsNone(select_shipped_pr([pr], 2385))
        # True-positive retained: a genuine impl PR delivering the fix still stamps.
        impl = _pr(2500, "#2385: render: fog vision fix", "Closes #2385",
                   files=["engine/render/fog.cpp"])
        self.assertEqual(select_shipped_pr([impl], 2385)["number"], 2500)

    def test_real_world_2091_documented_by_3020(self):
        pr = _pr(
            3020,
            "render: GL Phase-0 verification of world-placed detached "
            "re-voxelize cast (#2091)",
            "No `Closes #2091` — deliberate. Phase 1 remains outstanding.",
            files=["docs/design/detached-revoxelize-world-light.md"],
        )
        self.assertIsNone(select_shipped_pr([pr], 2091))
        fleet_control = _pr(
            3020, pr["title"], pr["body"],
            files=[".fleet/plans/issue-2091.md"],
        )
        self.assertIsNone(select_shipped_pr([fleet_control], 2091))
        impl_control = _pr(
            3020, pr["title"], pr["body"],
            files=["engine/render/fog.cpp"],
        )
        self.assertEqual(select_shipped_pr([impl_control], 2091)["number"], 3020)


if __name__ == "__main__":
    unittest.main()
