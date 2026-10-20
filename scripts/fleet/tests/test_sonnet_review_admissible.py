"""Tests for the reviewer-lane admissibility predicates in fleet-state-scout.

The sonnet lane admits a PR with no verdict label OR a re-review trigger
(`RECHECK_LABELS`). `fleet:needs-opus-recheck` deliberately sets no verdict
label — the opus verdict swap is what does — so before this guard an
escalated PR was re-elected by the sonnet lane on every tick: a full pass on
the same head, the same `Opus recheck required:` ending, the same escalation
edge, and the projection unchanged. Both sonnet slots spent the afternoon
confirming one PR while the unreviewed queue waited.

The control that inverts is the escalated-with-no-trigger PR: the sonnet
lane must not admit it, the opus lane must, and an author push (a RECHECK
label) must bring the sonnet lane back. Asserting through both the
predicate and the slice keeps the dispatcher's pick list — what actually
launches a pane — in the assertion.

Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_scout = importlib.util.module_from_spec(_spec)
_loader.exec_module(_scout)

sonnet_review_admissible = _scout.sonnet_review_admissible
opus_review_admissible = _scout.opus_review_admissible
slice_sonnet_reviewer = _scout.slice_sonnet_reviewer
slice_opus_reviewer = _scout.slice_opus_reviewer
project_sonnet_reviewer = _scout.project_sonnet_reviewer
project_opus_reviewer = _scout.project_opus_reviewer
stable_hash = _scout.stable_hash

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import fleet_task_class  # noqa: E402


def _pr(number, labels, base="master"):
    return {"number": number, "title": f"pr {number}", "headRefName": f"claude/{number}-x",
            "baseRefName": base, "labels": list(labels), "mergeable": "MERGEABLE",
            "isDraft": False, "updatedAt": "2026-09-14T22:00:00Z"}


def _state(*prs):
    return {"repos": {"engine": {"prs": list(prs), "plan_review": []}}}


def _sonnet_numbers(*prs):
    return [p["number"] for p in slice_sonnet_reviewer(_state(*prs))["candidate_prs"]]


def _opus_numbers(*prs):
    return [p["number"] for p in slice_opus_reviewer(_state(*prs))["flagged_prs"]]


class EscalatedPrBelongsToTheOpusLane(unittest.TestCase):
    ESCALATED = ["fleet:needs-opus-recheck", "fleet:author-claude"]

    def test_escalated_pr_with_no_trigger_is_not_a_sonnet_candidate(self):
        pr = _pr(3202, self.ESCALATED)
        self.assertFalse(sonnet_review_admissible(pr, set(pr["labels"]), {}))
        self.assertEqual(_sonnet_numbers(pr), [])

    def test_escalated_pr_is_an_opus_candidate(self):
        pr = _pr(3202, self.ESCALATED)
        self.assertTrue(opus_review_admissible(pr, set(pr["labels"]), {}))
        self.assertEqual(_opus_numbers(pr), [3202])

    def test_has_nits_beside_the_escalation_is_not_a_verdict(self):
        # REVIEWER-PROTOCOL: has-nits rides with the escalation when sonnet
        # had nits; it is not a verdict and must not re-admit the PR.
        pr = _pr(3202, self.ESCALATED + ["fleet:has-nits"])
        self.assertFalse(sonnet_review_admissible(pr, set(pr["labels"]), {}))

    def test_author_push_brings_the_sonnet_lane_back(self):
        for trigger in ("fleet:changes-made", "human:re-review"):
            with self.subTest(trigger=trigger):
                pr = _pr(3202, self.ESCALATED + [trigger])
                self.assertTrue(sonnet_review_admissible(pr, set(pr["labels"]), {}))
                self.assertEqual(_sonnet_numbers(pr), [3202])
                # The opus lane still sees it; the two are not exclusive.
                self.assertEqual(_opus_numbers(pr), [3202])


class UnchangedSonnetAdmission(unittest.TestCase):
    """The pre-existing admission rules, pinned so the guard adds only the
    escalation case."""

    def test_unreviewed_pr_is_a_candidate(self):
        pr = _pr(3395, ["fleet:author-codex"])
        self.assertEqual(_sonnet_numbers(pr), [3395])

    def test_verdicted_pr_without_a_trigger_is_not(self):
        for verdict in ("fleet:approved", "fleet:needs-fix", "fleet:has-nits"):
            with self.subTest(verdict=verdict):
                pr = _pr(3362, [verdict])
                self.assertEqual(_sonnet_numbers(pr), [])

    def test_verdicted_pr_with_a_trigger_is(self):
        pr = _pr(3362, ["fleet:approved", "fleet:changes-made"])
        self.assertEqual(_sonnet_numbers(pr), [3362])

    def test_skip_labels_still_win_over_a_trigger(self):
        pr = _pr(3215, ["fleet:changes-made", "fleet:amending-mac-pool-4"])
        self.assertEqual(_sonnet_numbers(pr), [])
        pr = _pr(3215, ["fleet:needs-opus-recheck", "fleet:changes-made",
                        "fleet:semantic-conflict"])
        self.assertEqual(_sonnet_numbers(pr), [])
        self.assertEqual(_opus_numbers(pr), [])

    def test_slice_order_is_preserved(self):
        prs = [_pr(3390, []), _pr(3202, ["fleet:needs-opus-recheck"]), _pr(3395, [])]
        self.assertEqual(_sonnet_numbers(*prs), [3390, 3395])


class StackedChildGatedOnUnapprovedUpstream(unittest.TestCase):
    """REVIEWER-PROTOCOL.md §"Stack awareness" step 3: a stacked PR carrying
    fleet:awaiting-upstream-review with an unapproved upstream is skipped
    silently, no comment. Before this guard, _reviewable did not know the
    rule, so the child kept re-electing into both reviewer lanes every tick
    with nothing to show for it — the incident this suite pins."""

    def _upstream(self, labels):
        return _pr(3850, labels)

    def _child(self, extra_labels=()):
        return _pr(3857, ["fleet:awaiting-upstream-review", *extra_labels],
                   base="claude/3850-x")

    def test_gated_child_is_not_a_sonnet_candidate(self):
        upstream = self._upstream(["fleet:author-codex"])
        child = self._child()
        state = _state(upstream, child)
        self.assertNotIn(3857, _sonnet_numbers(upstream, child))
        self.assertNotIn(
            3857, [i["pr"] for i in project_sonnet_reviewer(state)]
        )

    def test_gated_escalated_child_is_not_an_opus_candidate(self):
        # The gate must hold for the opus lane too, not just sonnet's —
        # give the child the escalation flag it would otherwise be admitted
        # on so this test is not vacuously true.
        upstream = self._upstream(["fleet:author-codex"])
        child = self._child(["fleet:needs-opus-recheck"])
        state = _state(upstream, child)
        self.assertNotIn(3857, _opus_numbers(upstream, child))
        self.assertNotIn(
            3857, [i["pr"] for i in project_opus_reviewer(state)]
        )

    def test_upstream_approval_label_admits_the_child(self):
        for approval in ("fleet:approved", "human:approved"):
            with self.subTest(approval=approval):
                upstream = self._upstream([approval])
                child = self._child()
                self.assertIn(3857, _sonnet_numbers(upstream, child))

    def test_no_open_upstream_admits_the_child(self):
        # Upstream merged or closed: absent from the open-PR head map.
        child = self._child()
        self.assertIn(3857, _sonnet_numbers(child))

    def test_upstream_approval_changes_the_reviewer_projection_hash(self):
        unapproved = _state(self._upstream(["fleet:author-codex"]), self._child())
        approved = _state(self._upstream(["fleet:approved"]), self._child())
        self.assertNotEqual(
            stable_hash(project_sonnet_reviewer(unapproved)),
            stable_hash(project_sonnet_reviewer(approved)),
        )

    def test_master_based_pr_is_unaffected(self):
        # A non-stacked PR's baseRefName never resolves in head_labels, but
        # it also never carries the gate label — sanity-checks the None
        # upstream_labels arm doesn't accidentally admit a gated PR too.
        pr = _pr(3395, ["fleet:author-codex"])
        self.assertEqual(_sonnet_numbers(pr), [3395])


HEAD = "c" * 40
STAMP = "2026-10-17T20:03:38Z"
ESCALATION_BODY = (
    "## Review — re-bless four refs\n\n**Verdict:** approve pending Opus recheck\n\n"
    "Scoped to four PNGs.\n\n🤖 Reviewed by Codex (GPT-5)\n\n"
    "Opus recheck required: render-reference provenance.\n")


def _gh_review(body=ESCALATION_BODY, commit=HEAD, at="2026-10-17T20:02:57Z", author="jakildev"):
    return {"author": {"login": author}, "body": body, "commit": {"oid": commit},
            "state": "COMMENTED", "submittedAt": at}


def _gh_pr(number, labels, reviews):
    return {"number": number, "title": f"pr {number}", "headRefName": f"claude/{number}-x",
            "headRefOid": HEAD, "baseRefName": "master", "author": {"login": "jakildev"},
            "labels": [{"name": n} for n in labels], "mergeable": "MERGEABLE",
            "isDraft": False, "reviews": reviews, "updatedAt": STAMP, "body": ""}


class StrandedReviewIsReplayed(unittest.TestCase):
    """A fleet review on the current head with no verdict event after it —
    the body landed, the edge refused on a swept claim — is offered to the
    sonnet lane as `verdict-replay:`, never as a fresh review, and never
    hidden by the `review:` decline the refused iteration left behind.
    Driven from the gh list output through the fetch, the slice and the
    dispatcher's picker, so an empty projection cannot pass."""

    def setUp(self):
        self.events = {}
        self.event_reads = []
        self._orig = (_scout.run_capture, _scout._rest_list)
        _scout._rest_list = self._fake_rest_list
        self._tmp = tempfile.TemporaryDirectory()
        os.environ["FLEET_STATE_DIR"] = self._tmp.name
        declined = Path(self._tmp.name) / "declined"
        declined.mkdir()
        (declined / "review-engine-4483").write_text(
            f"{STAMP}\nfleet-review-verdict requires fleet:reviewing-mac-pool-2\nsonnet-reviewer\n")

    def tearDown(self):
        _scout.run_capture, _scout._rest_list = self._orig
        os.environ.pop("FLEET_STATE_DIR", None)
        self._tmp.cleanup()

    def _fake_rest_list(self, repo, path, params, per_page=100, max_pages=1):
        number = int(path.split("/")[1])
        self.event_reads.append(number)
        events = self.events.get(number)
        if events is None:
            raise AssertionError(f"unmodelled events read for #{number}")
        return events

    def _fetch(self, *gh_prs):
        _scout.run_capture = lambda cmd, cwd=None: json.dumps(list(gh_prs))
        return _scout._fetch_prs_graphql("jakildev/IrredenEngine")

    def _targets(self, prs):
        state = _state(*prs)
        sonnet = json.loads(json.dumps(slice_sonnet_reviewer(state)))
        opus = json.loads(json.dumps(slice_opus_reviewer(state)))
        return (fleet_task_class.pick_role(sonnet, "sonnet-reviewer"),
                fleet_task_class.pick_role(opus, "opus-reviewer"))

    def _verdict_event(self, at):
        return [{"event": "labeled", "label": {"name": "fleet:needs-fix"}, "created_at": at}]

    def test_the_fired_shape_emits_exactly_the_replay_target(self):
        self.events[4483] = []
        prs = self._fetch(_gh_pr(4483, ["fleet:author-claude"], [_gh_review()]))
        self.assertEqual(prs[0]["fleet_review_at"], "2026-10-17T20:02:57Z")
        self.assertEqual(prs[0]["verdict_event_at"], "")
        self.assertEqual(self._targets(prs), (["verdict-replay:engine:4483"], []))
        item = project_sonnet_reviewer(_state(*prs))[0]
        self.assertTrue(item["verdict_replay"])

    def test_a_review_newer_than_an_older_verdict_is_replayed(self):
        self.events[4483] = self._verdict_event("2026-10-16T00:00:00Z")
        prs = self._fetch(_gh_pr(4483, ["fleet:needs-fix", "fleet:changes-made"], [_gh_review()]))
        self.assertEqual(self._targets(prs)[0], ["verdict-replay:engine:4483"])

    def test_a_verdict_at_or_after_the_review_suppresses_it(self):
        for at in ("2026-10-17T20:02:57Z", "2026-10-17T20:03:12Z"):
            with self.subTest(at=at):
                self.events[4483] = self._verdict_event(at)
                prs = self._fetch(_gh_pr(4483, ["fleet:changes-made"], [_gh_review()]))
                record = slice_sonnet_reviewer(_state(*prs))["candidate_prs"][0]
                self.assertNotIn("verdict_replay", record)
                # Only the ordinary target is left, and the `review:` decline
                # still stands at this stamp.
                self.assertEqual(self._targets(prs), ([], []))

    def test_ineligible_reviews_read_no_events_and_emit_no_replay(self):
        cases = {
            "empty": _gh_review(body=""),
            "foreign": _gh_review(author="someone-else"),
            "unstructured": _gh_review(body="LGTM"),
            "old head": _gh_review(commit="b" * 40),
        }
        for name, review in cases.items():
            with self.subTest(name):
                self.event_reads.clear()
                prs = self._fetch(_gh_pr(4483, [], [review]))
                self.assertEqual(prs[0]["fleet_review_at"], "")
                self.assertIsNone(prs[0]["verdict_event_at"])
                self.assertEqual(self.event_reads, [])
                self.assertNotIn("verdict-replay:engine:4483", self._targets(prs)[0])

    def test_a_settled_verdict_label_reads_no_events(self):
        prs = self._fetch(_gh_pr(4483, ["fleet:approved"], [_gh_review()]))
        self.assertEqual(self.event_reads, [])
        self.assertIsNone(prs[0]["verdict_event_at"])
        self.assertEqual(self._targets(prs), ([], []))

    def test_a_failed_events_read_is_never_replay_work(self):
        _scout._rest_list = lambda *a, **k: None
        prs = self._fetch(_gh_pr(4483, [], [_gh_review()]))
        self.assertIsNone(prs[0]["verdict_event_at"])
        self.assertNotIn("verdict-replay:engine:4483", self._targets(prs)[0])

    def test_a_stranded_opus_review_is_replayed_not_rechecked_again(self):
        # The opus pass posted on an escalated head, then its edge failed:
        # fleet:needs-opus-recheck still stands. The opus lane must not spend
        # a second recheck; the sonnet lane replays the opus body.
        opus_body = ("## Final review — x\n\n**Verdict:** approve\n\n"
                     "🤖 Reviewed by Codex (review class: opus)\n\n"
                     "Opus recheck not required (reviewed at opus class).\n")
        self.events[4483] = [{"event": "labeled", "label": {"name": "fleet:needs-opus-recheck"},
                              "created_at": "2026-10-17T20:03:00Z"}]
        reviews = [_gh_review(), _gh_review(body=opus_body, at="2026-10-20T18:22:45Z")]
        prs = self._fetch(_gh_pr(4483, ["fleet:needs-opus-recheck"], reviews))
        self.assertEqual(self._targets(prs), (["verdict-replay:engine:4483"], []))
        self.assertEqual(project_opus_reviewer(_state(*prs)), [])
        # A verdict event after the opus review ends the replay; the
        # escalation label still standing is the opus lane's again.
        self.events[4483].append({"event": "unlabeled",
                                  "label": {"name": "fleet:needs-opus-recheck"},
                                  "created_at": "2026-10-21T00:00:00Z"})
        prs = self._fetch(_gh_pr(4483, ["fleet:needs-opus-recheck"], reviews))
        self.assertEqual(self._targets(prs)[1], ["review:engine:4483"])
        self.assertEqual(self._targets(prs)[0], [])

    def test_schema_bump_retires_records_without_the_fields(self):
        self.events[4483] = []
        prs = self._fetch(_gh_pr(4483, [], [_gh_review()]))
        self.assertEqual(prs[0]["schema"], _scout.PR_RECORD_SCHEMA)
        self.assertGreaterEqual(_scout.PR_RECORD_SCHEMA, 8)


if __name__ == "__main__":
    unittest.main()
