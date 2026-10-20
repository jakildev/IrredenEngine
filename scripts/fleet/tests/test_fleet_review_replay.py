"""Tests for fleet_review_replay.py — the stranded-review predicate and the
body-to-edge mapping a `verdict-replay` dispatch applies.

The three PR bodies below keep the verdict line and ending of the three real
reviews one PR carried on one head: the sonnet escalation (`approve pending
Opus recheck` plus a final `Opus recheck required:` line), an opus needs-fix
with a trailing period, and an opus approve ending `Opus recheck not required
(reviewed at opus class).` — the body a substring match on "Opus recheck"
would wrongly escalate.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import fleet_review_replay as replay  # noqa: E402

_CLI = Path(__file__).resolve().parent.parent / "fleet_review_replay.py"
HEAD = "c" * 40
OLD = "b" * 40

SONNET_ESCALATION = """## Review — test(windows): re-bless four windows-debug refs

**Verdict:** approve pending Opus recheck

The PR is tightly scoped to the four allowed PNGs.

### Nits (follow-up)
- docs/x.md:3 — wording

<!-- fleet-review dispatch=0f1e2d3c agent=pool-2 -->
🤖 Reviewed by Codex (GPT-5)

Opus recheck required: Windows-specific render-reference provenance needs a final render review.
"""

OPUS_NEEDS_FIX = """## Final review — test(windows): re-bless four windows-debug refs

**Verdict:** needs-fix.

Sonnet flagged the provenance; on closer read I agree.

### Needs-fix
- docs/pr.md:10 — capture evidence missing — attach it

🤖 Reviewed by Codex (runtime: codex; model: GPT-5; review class: opus; review-pr skill)
"""

OPUS_APPROVE = """## Final review — test(windows): re-bless four windows-debug refs

**Verdict:** approve

The follow-up evidence resolves both prior needs-fix findings.

### Praise
- The mover attribution separates control-arm movers from post-fork movers.

🤖 Reviewed by Codex (runtime: codex; model: GPT-5; review class: opus; review-pr skill)

Opus recheck not required (reviewed at opus class).
"""


def _gql(body, commit=HEAD, at="2026-10-17T20:02:57Z", author="jakildev", state="COMMENTED"):
    return {"author": {"login": author}, "body": body, "commit": {"oid": commit},
            "state": state, "submittedAt": at}


def _rest(body, commit=HEAD, at="2026-10-17T20:02:57Z", author="jakildev", state="COMMENTED"):
    return {"user": {"login": author}, "body": body, "commit_id": commit,
            "state": state, "submitted_at": at}


class VerdictEdge(unittest.TestCase):
    def test_the_three_real_bodies(self):
        self.assertEqual(replay.verdict_edge(SONNET_ESCALATION)[0], replay.EDGE_OPUS_RECHECK)
        self.assertEqual(replay.verdict_edge(OPUS_NEEDS_FIX)[0], replay.EDGE_NEEDS_FIX)
        self.assertEqual(replay.verdict_edge(OPUS_APPROVE)[0], replay.EDGE_APPROVE)

    def test_opus_not_required_ending_with_nits_is_approve_nits(self):
        body = OPUS_APPROVE.replace(
            "### Praise", "### Nits\n- a.cpp:3 — missing assert\n\n### Praise")
        self.assertEqual(replay.verdict_edge(body)[0], replay.EDGE_APPROVE_NITS)

    def test_follow_up_nits_and_empty_nits_are_not_nits(self):
        follow_up = OPUS_APPROVE.replace(
            "### Praise", "### Nits (follow-up)\n- a.md:1 — word\n\n### Praise")
        self.assertEqual(replay.verdict_edge(follow_up)[0], replay.EDGE_APPROVE)
        empty = OPUS_APPROVE.replace("### Praise", "### Nits\n- none\n\n### Praise")
        self.assertEqual(replay.verdict_edge(empty)[0], replay.EDGE_APPROVE)

    def test_plain_approve_with_a_required_line_escalates(self):
        body = OPUS_APPROVE.replace(
            "Opus recheck not required (reviewed at opus class).",
            "Opus recheck required: touches engine/render/.")
        self.assertEqual(replay.verdict_edge(body)[0], replay.EDGE_OPUS_RECHECK)

    def test_ambiguous_and_unknown_bodies_fail_closed(self):
        two = OPUS_APPROVE.replace(
            "**Verdict:** approve", "**Verdict:** approve\n\n**Verdict:** needs-fix")
        for body in (two, OPUS_APPROVE.replace("approve\n", "lgtm\n", 1), "", "no verdict here"):
            with self.subTest(body=body[:40]):
                edge, reason = replay.verdict_edge(body)
                self.assertIsNone(edge)
                self.assertTrue(reason)


class FleetReviewEligibility(unittest.TestCase):
    def test_structured_actor_review_is_a_fleet_review(self):
        self.assertTrue(replay.is_fleet_review(_gql(OPUS_APPROVE)))
        self.assertTrue(replay.is_fleet_review(_rest(OPUS_APPROVE)))

    def test_empty_foreign_and_unstructured_reviews_are_not(self):
        cases = {
            "empty": _gql(""),
            "foreign": _gql(OPUS_APPROVE, author="someone-else"),
            "unstructured": _gql("LGTM, thanks!"),
            "no footer": _gql("**Verdict:** approve\n\nlooks fine\n"),
        }
        for name, review in cases.items():
            with self.subTest(name):
                self.assertFalse(replay.is_fleet_review(review))

    def test_actor_set_is_configurable(self):
        review = _gql(OPUS_APPROVE, author="fleet-bot")
        self.assertFalse(replay.is_fleet_review(review))
        actors = replay.review_actors({"FLEET_REVIEW_ACTORS": "a, fleet-bot"})
        self.assertTrue(replay.is_fleet_review(review, actors))

    def test_head_review_is_the_newest_submitted_on_the_head(self):
        reviews = [_gql(SONNET_ESCALATION, at="2026-10-17T20:02:57Z"),
                   _gql(OPUS_NEEDS_FIX, at="2026-10-20T17:57:08Z"),
                   _gql(OPUS_APPROVE, at="2026-10-20T18:22:45Z"),
                   _gql("draft", at="2026-10-20T19:00:00Z", state="PENDING"),
                   _gql(OPUS_APPROVE, commit=OLD, at="2026-10-21T00:00:00Z")]
        self.assertIs(replay.head_review(reviews, HEAD), reviews[2])
        self.assertEqual(replay.fleet_review_at(reviews, HEAD), "2026-10-20T18:22:45Z")

    def test_old_head_and_a_later_foreign_head_review_yield_nothing(self):
        self.assertEqual(replay.fleet_review_at([_gql(OPUS_APPROVE, commit=OLD)], HEAD), "")
        later_human = [_gql(OPUS_APPROVE), _gql("thanks", at="2026-10-18T00:00:00Z")]
        self.assertEqual(replay.fleet_review_at(later_human, HEAD), "")
        self.assertEqual(replay.fleet_review_at([], HEAD), "")
        self.assertEqual(replay.fleet_review_at([_gql(OPUS_APPROVE)], ""), "")


class ReplayPending(unittest.TestCase):
    EVENTS = [
        {"event": "labeled", "label": {"name": "fleet:reviewing-mac-pool-2"},
         "created_at": "2026-10-17T01:51:00Z"},
        {"event": "labeled", "label": {"name": "fleet:needs-fix"},
         "created_at": "2026-10-16T00:00:00Z"},
        {"event": "unlabeled", "label": {"name": "fleet:needs-fix"},
         "created_at": "2026-10-16T05:00:00Z"},
        {"event": "commented", "created_at": "2026-10-19T00:00:00Z"},
    ]

    def test_newest_verdict_event_reads_both_event_types_on_verdict_labels_only(self):
        self.assertEqual(replay.newest_verdict_event(self.EVENTS), "2026-10-16T05:00:00Z")
        self.assertEqual(replay.newest_verdict_event([]), "")

    def test_pending_shapes(self):
        review = "2026-10-17T20:02:57Z"
        cases = [
            ("no verdict event ever", "", True),
            ("older verdict", "2026-10-16T05:00:00Z", True),
            ("verdict the same second", review, False),
            ("verdict after the review", "2026-10-20T18:22:54Z", False),
            ("events not read", None, False),
        ]
        for name, event, expected in cases:
            with self.subTest(name):
                pr = {"fleet_review_at": review, "verdict_event_at": event}
                self.assertEqual(replay.replay_pending(pr), expected)
        self.assertFalse(replay.replay_pending({"fleet_review_at": "", "verdict_event_at": ""}))
        self.assertFalse(replay.replay_pending({}))

    def test_provenance(self):
        self.assertEqual(replay.provenance(SONNET_ESCALATION),
                         {"dispatch": "0f1e2d3c", "agent": "pool-2"})
        self.assertIsNone(replay.provenance(OPUS_APPROVE))


class InspectCli(unittest.TestCase):
    def _inspect(self, reviews, head=HEAD):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "reviews.json")
            Path(path).write_text(json.dumps(reviews))
            argv = [sys.executable, str(_CLI), "inspect", "--reviews", path, "--head", head]
            return subprocess.run(argv,
                                  capture_output=True, text=True, timeout=30)

    def test_fleet_review_line(self):
        proc = self._inspect([_rest(SONNET_ESCALATION)])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        fields = proc.stdout.rstrip("\n").split("\t")
        self.assertEqual(fields[:3], ["jakildev", "1", replay.EDGE_OPUS_RECHECK])
        self.assertEqual(fields[4:], ["0f1e2d3c", "pool-2"])

    def test_nested_pages_and_a_foreign_review(self):
        later = _rest("hm", author="x", at="2026-10-18T00:00:00Z")
        proc = self._inspect([[_rest(OPUS_APPROVE)], [later]])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.split("\t")[:3], ["x", "0", "-"])

    def test_no_head_review_exits_1(self):
        self.assertEqual(self._inspect([_rest(OPUS_APPROVE, commit=OLD)]).returncode, 1)


if __name__ == "__main__":
    unittest.main()
