"""Tests for state.json's size bound and the review-body retention it rests on (#2752).

Every fleet role's startup step reads `~/.fleet/state/state.json` with the Read
tool, which HARD ERRORS above 256 KB. The file reached 476.8 KB: `prs[].reviews`
was ~46% of it (a body retained on every review of every open PR, each capped at
2 KB but with an unbounded aggregate), and 89.4 KB was `indent=2` whitespace.

Two things are being guarded, and they need different kinds of test:

- **The size bound** is the acceptance gate. TestSizeBound drives the SHIPPED
  projection (`_fetch_prs_graphql`) and the SHIPPED emit (`emit_state`), never a
  re-implementation of either — a test that re-serialized the fixture itself
  would only be a change-detector on its own arithmetic. TestFixtureFidelity
  proves the fixture can express the bug: serialized the pre-fix way (every body
  retained, indent=2) the same input blows the cap, so a green TestSizeBound is
  the projection's doing and not a too-small fixture.

- **The two consumer predicates** must keep firing across the trim.
  TestConsumerPredicates models them from the role docs (they are gated files;
  this plan changes neither) and pins the input that can actually regress: the
  opus predicate's fixture carries NO `fleet:needs-opus-recheck` label, so the
  label disjunct cannot mask a broken phrase path — the PR #1473 regression the
  scout's own comment documents. Each predicate arm ships a positive control
  that flips the input and asserts the predicate stops firing.

Against the pre-fix `origin/master` the suite splits three ways, and the split is
the honest read of its worth: **3 arms FAIL behaviourally** (the two
latest-review-retention arms, which run against master's own
`_fetch_prs_graphql`, and the size bound, which falls back to the pre-fix emit
shape so it reports the real byte count instead of erroring), **8 ERROR** on
symbols this fix introduces (`emit_state`, `check_state_size`, the size
constants — the fix's own contract, not a control), and the rest pass as
non-regression assertions.

Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

_REPO = "jakildev/IrredenEngine"
_READ_CAP_BYTES = 256 * 1024

# The phrase the opus reviewer greps the LATEST review body for
# (.claude/commands/role-opus-reviewer.md). It lands as a trailing line, which is
# why only REVIEW_BODY_TAIL is load-bearing.
_PHRASE = "Opus recheck required"

# The plan's fixture scale: enough PRs and reviews that the pre-fix projection
# clears the cap on its own, close to the live tree (46 open PRs across repos).
_PR_COUNT = 45
_REVIEWS_PER_PR = 3
_FIRST_PR = 2000

# Live review bodies measured at the cap (median == max == 2063 B); oversize the
# fixture past head+tail so _truncate_review_body actually engages.
_BODY_FILLER = "x" * 4000


def _body(with_phrase: bool) -> str:
    tail = f"\n{_PHRASE}: the winner-election kernel needs a determinism pass.\n"
    return _BODY_FILLER + (tail if with_phrase else "\nLooks good to me.\n")


def _review(day: int, with_phrase: bool = False, author: str = "jakildev") -> dict:
    return {
        "author": {"login": author},
        "body": _body(with_phrase),
        "state": "COMMENTED",
        "submittedAt": f"2026-08-{day:02d}T00:00:00Z",
    }


def _pr(n: int, reviews=None, labels=None) -> dict:
    return {
        "number": n,
        "title": f"render: some change {n}",
        "headRefName": f"claude/{n - 500}-some-task",
        "headRefOid": f"{n:040d}",
        "baseRefName": "master",
        "author": {"login": "jakildev"},
        "labels": [{"name": name} for name in (labels if labels is not None
                                               else ["fleet:approved"])],
        "mergeable": "MERGEABLE",
        "isDraft": False,
        # Reviews arrive oldest-first from gh; the phrase rides the LATEST one.
        "reviews": reviews if reviews is not None else [
            _review(day=1 + i, with_phrase=(i == _REVIEWS_PER_PR - 1))
            for i in range(_REVIEWS_PER_PR)
        ],
        "updatedAt": "2026-08-08T00:00:00Z",
        "body": "",
    }


_FIXTURE = [_pr(n) for n in range(_FIRST_PR, _FIRST_PR + _PR_COUNT)]


def _fake_gh(argv, cwd=None):
    """Stand in for `gh pr list`, modelling its argument parsing (CLAUDE.md).

    Fails closed on any argv shape it does not model, so a rewrite of the call
    site cannot quietly slip past this suite.
    """
    if argv[:3] != ["gh", "pr", "list"]:
        raise AssertionError(f"unmodelled command: {argv!r}")
    for required in ("--repo", "--state", "--json"):
        if required not in argv:
            raise AssertionError(f"gh pr list requires {required}: {argv!r}")
    if "--limit" in argv:
        raw = argv[argv.index("--limit") + 1]
        if not raw.lstrip("-").isdigit() or int(raw) < 1:
            raise AssertionError(f"gh rejects --limit {raw!r}")
    return json.dumps(_FIXTURE)


def _project(prs):
    """Run the SHIPPED fetch projection over `prs`, returning the emitted records."""
    with patch.object(_mod, "run_capture", side_effect=lambda a, cwd=None: json.dumps(prs)):
        return _mod._fetch_prs_graphql(_REPO)


def _latest_review(pr):
    """The opus reviewer's own selection rule: sort reviews[] by submittedAt."""
    reviews = pr.get("reviews") or []
    if not reviews:
        return None
    return sorted(reviews, key=lambda r: r.get("submittedAt") or "")[-1]


def _opus_candidate(pr, fleet_login="jakildev"):
    """role-opus-reviewer.md step 5, the two label-independent-vs-label disjuncts.

    The phrase path must fire on its own — a PR carrying no
    `fleet:needs-opus-recheck` label is exactly the input that regresses when a
    body is trimmed away.
    """
    if "fleet:needs-opus-recheck" in pr.get("labels", []):
        return True
    latest = _latest_review(pr)
    return bool(latest) and _PHRASE in (latest.get("body") or "")


def _sonnet_candidate(pr, fleet_login="jakildev"):
    """role-sonnet-reviewer.md step 5: no fleet review yet."""
    return not any(r.get("author") == fleet_login for r in pr.get("reviews") or [])


class TestFixtureFidelity(unittest.TestCase):
    """Without this, a green size bound could just mean the fixture was small."""

    def test_fixture_blows_the_cap_under_the_pre_fix_serialization(self):
        # Pre-fix shape: a truncated body on EVERY review, pretty-printed.
        prefix = [
            dict(pr, reviews=[
                {
                    "author": r["author"]["login"],
                    "body": r["body"][:1024] + "\n…[truncated]…\n" + r["body"][-1024:],
                    "state": r["state"],
                    "submittedAt": r["submittedAt"],
                }
                for r in pr["reviews"]
            ])
            for pr in _FIXTURE
        ]
        state = {"repos": {"engine": {"prs": prefix}}}
        size = len(json.dumps(state, indent=2, sort_keys=True).encode("utf-8"))
        self.assertGreater(
            size, _READ_CAP_BYTES,
            f"fixture serializes to {size} B pre-fix — it must exceed the "
            f"{_READ_CAP_BYTES} B cap or TestSizeBound proves nothing")

    def test_stub_fails_closed_on_an_unmodelled_command(self):
        with self.assertRaises(AssertionError):
            _fake_gh(["gh", "issue", "list", "--repo", _REPO])


class TestSizeBound(unittest.TestCase):
    """The acceptance gate: shipped projection + shipped emit, under the cap."""

    def test_emitted_state_is_under_the_read_tool_cap(self):
        with patch.object(_mod, "run_capture", side_effect=_fake_gh):
            prs = _mod._fetch_prs_graphql(_REPO)
        state = {"generated_at": "2026-08-08T00:00:00Z",
                 "repos": {"engine": {"prs": prs}}}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "state.json"
            with patch.object(_mod, "STATE_FILE", path), \
                    patch.dict("os.environ", {"FLEET_ALERTS_DIR": str(Path(tmp) / "alerts")}), \
                    patch.object(_mod, "log", side_effect=lambda m: None):
                # emit_state is this fix's own surface. Falling back to the
                # pre-fix emit shape when it is absent keeps THIS arm — the
                # acceptance gate — a behavioural control: on the pre-fix tree it
                # goes red with the real byte count rather than erroring on a
                # missing attribute, so the number in the failure message is the
                # harm itself.
                emit = getattr(_mod, "emit_state", None)
                if emit is None:
                    payload = json.dumps(state, indent=2, sort_keys=True) + "\n"
                    _mod.write_atomic(path, payload)
                    size = len(payload.encode("utf-8"))
                else:
                    size = emit(state)
                on_disk = path.stat().st_size
        self.assertEqual(size, on_disk, "returned size must be the on-disk size")
        self.assertLess(
            size, _READ_CAP_BYTES,
            f"emitted state.json is {size} B — past the Read tool's "
            f"{_READ_CAP_BYTES} B cap, so every role's startup read hard-errors")

    def test_emit_is_compact_and_still_key_sorted(self):
        state = {"b": 1, "a": {"d": 2, "c": 3}}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "state.json"
            with patch.object(_mod, "STATE_FILE", path), \
                    patch.object(_mod, "log", side_effect=lambda m: None):
                _mod.emit_state(state)
            text = path.read_text()
        self.assertNotIn(": ", text, "compact emit must not pad separators")
        self.assertNotIn("\n  ", text, "compact emit must not indent")
        self.assertLess(text.index('"a"'), text.index('"b"'),
                        "sort_keys must stay on — the leader bundle diffs this file")
        self.assertEqual(json.loads(text), state, "must still round-trip")


class TestLatestReviewBodyRetention(unittest.TestCase):
    """Phase 1: the aggregate bound. Bodies survive only on the latest review."""

    def test_only_the_latest_review_keeps_a_body(self):
        pr = _project([_pr(_FIRST_PR)])[0]
        bodies = [r["body"] for r in pr["reviews"]]
        self.assertTrue(bodies[-1], "the latest review must keep its body")
        self.assertEqual(bodies[:-1], [""] * (len(bodies) - 1),
                         "older reviews must emit an empty body")

    def test_latest_is_chosen_by_submitted_at_not_list_order(self):
        # gh's ordering is not a contract; the opus reviewer sorts by submittedAt,
        # so the projection must agree even when the newest arrives first.
        out_of_order = [_review(day=9, with_phrase=True), _review(day=2)]
        pr = _project([_pr(_FIRST_PR, reviews=out_of_order)])[0]
        self.assertIn(_PHRASE, pr["reviews"][0]["body"],
                      "the newest review by submittedAt keeps the body")
        self.assertEqual(pr["reviews"][1]["body"], "")

    def test_metadata_is_retained_on_every_review(self):
        pr = _project([_pr(_FIRST_PR)])[0]
        for review in pr["reviews"]:
            self.assertEqual(review["author"], "jakildev")
            self.assertTrue(review["submittedAt"])
            self.assertEqual(review["state"], "COMMENTED")

    def test_a_pr_with_no_reviews_still_projects_an_empty_list(self):
        pr = _project([_pr(_FIRST_PR, reviews=[])])[0]
        self.assertEqual(pr["reviews"], [])


class TestConsumerPredicates(unittest.TestCase):
    """Both role-doc predicates keep firing across the trim, each with a control."""

    def test_opus_phrase_predicate_fires_without_the_label(self):
        pr = _project([_pr(_FIRST_PR, labels=["fleet:approved"])])[0]
        self.assertNotIn("fleet:needs-opus-recheck", pr["labels"],
                         "fixture must be label-absent or the phrase path is masked")
        self.assertTrue(
            _opus_candidate(pr),
            "the opus recheck phrase must survive the projection on its own")

    def test_opus_phrase_predicate_control_stops_firing_when_absent(self):
        reviews = [_review(day=1 + i, with_phrase=False) for i in range(_REVIEWS_PER_PR)]
        pr = _project([_pr(_FIRST_PR, reviews=reviews, labels=["fleet:approved"])])[0]
        self.assertFalse(
            _opus_candidate(pr),
            "predicate fires on an input with no phrase — the arm above is vacuous")

    def test_opus_predicate_ignores_the_phrase_on_a_stale_review(self):
        # The role doc says LATEST. A phrase on a superseded review must not
        # re-arm the lane — and the trim is what makes that structural.
        reviews = [_review(day=1, with_phrase=True), _review(day=5, with_phrase=False)]
        pr = _project([_pr(_FIRST_PR, reviews=reviews, labels=["fleet:approved"])])[0]
        self.assertFalse(_opus_candidate(pr))

    def test_opus_label_disjunct_is_independent_of_any_body(self):
        pr = _project([_pr(_FIRST_PR, reviews=[],
                           labels=["fleet:needs-opus-recheck"])])[0]
        self.assertTrue(_opus_candidate(pr))

    def test_sonnet_unreviewed_predicate_fires(self):
        pr = _project([_pr(_FIRST_PR, reviews=[])])[0]
        self.assertTrue(_sonnet_candidate(pr),
                        "a PR with no reviews must still read as unreviewed")

    def test_sonnet_predicate_control_stops_firing_once_reviewed(self):
        pr = _project([_pr(_FIRST_PR)])[0]
        self.assertFalse(
            _sonnet_candidate(pr),
            "predicate fires on a reviewed PR — the arm above is vacuous. "
            "reviews[].author must survive the body trim")


class TestPhraseSurvivesTruncation(unittest.TestCase):
    """Phase 2/3: the constants are sized from the measured live corpus."""

    # Largest measured distance from the phrase line's start to end-of-body
    # across the 8 live bodies carrying it (2026-08-08).
    _MAX_LIVE_TAIL_DISTANCE = 539

    def test_tail_covers_the_measured_live_maximum(self):
        self.assertGreaterEqual(
            _mod.REVIEW_BODY_TAIL, self._MAX_LIVE_TAIL_DISTANCE,
            "REVIEW_BODY_TAIL must cover the longest live phrase tail or the "
            "opus recheck lane silently stops firing")

    def test_phrase_at_the_live_maximum_distance_survives(self):
        suffix = f"{_PHRASE}: " + "y" * (self._MAX_LIVE_TAIL_DISTANCE - len(_PHRASE) - 2)
        body = "z" * 8000 + "\n" + suffix
        self.assertIn(_PHRASE, _mod._truncate_review_body(body))

    def test_truncation_actually_engages_on_an_oversized_body(self):
        # Control: without this, the arm above would pass on a body short enough
        # to skip truncation entirely.
        body = "z" * 8000 + "\n" + _PHRASE
        self.assertIn("…[truncated]…", _mod._truncate_review_body(body))

    def test_a_short_body_is_returned_verbatim(self):
        self.assertEqual(_mod._truncate_review_body("short"), "short")


class TestSizeGuard(unittest.TestCase):
    """Phase 5: the drift that produced this issue must be operator-visible."""

    def setUp(self):
        _mod._state_size_over_streak = 0

    def _run(self, size_bytes, alerts_dir):
        logged = []
        with patch.dict("os.environ", {"FLEET_ALERTS_DIR": str(alerts_dir)}), \
                patch.object(_mod, "log", side_effect=logged.append):
            _mod.check_state_size(size_bytes)
        return logged

    def test_silent_below_the_warn_threshold(self):
        with tempfile.TemporaryDirectory() as tmp:
            alerts = Path(tmp) / "alerts"
            logged = self._run(_mod.STATE_SIZE_WARN_BYTES - 1, alerts)
        self.assertEqual(logged, [], f"must not warn under threshold: {logged!r}")

    def test_warns_and_writes_an_alert_on_the_crossing_tick(self):
        with tempfile.TemporaryDirectory() as tmp:
            alerts = Path(tmp) / "alerts"
            logged = self._run(_mod.STATE_SIZE_WARN_BYTES + 1, alerts)
            alert = (alerts / "state-scout-state-size").read_text()
        self.assertTrue(any("state.json is" in m for m in logged), repr(logged))
        self.assertIn("consecutive_ticks=1", alert)
        self.assertIn("past_read_cap=no", alert)

    def test_quiets_after_the_crossing_but_keeps_refreshing_the_alert(self):
        with tempfile.TemporaryDirectory() as tmp:
            alerts = Path(tmp) / "alerts"
            first = self._run(_mod.STATE_SIZE_WARN_BYTES + 1, alerts)
            second = self._run(_mod.STATE_SIZE_WARN_BYTES + 1, alerts)
            alert = (alerts / "state-scout-state-size").read_text()
        self.assertTrue(first, "the crossing tick must be loud")
        self.assertEqual(second, [], f"an every-tick warn is spam: {second!r}")
        self.assertIn("consecutive_ticks=2", alert,
                      "a write-once alert would freeze its count while the "
                      "condition is still live")

    def test_names_the_read_cap_once_past_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            alerts = Path(tmp) / "alerts"
            logged = self._run(_mod.STATE_SIZE_READ_CAP_BYTES + 1, alerts)
            alert = (alerts / "state-scout-state-size").read_text()
        self.assertTrue(any("PAST the 256 KB Read-tool cap" in m for m in logged),
                        repr(logged))
        self.assertIn("past_read_cap=yes", alert)

    def test_all_clear_logs_once_and_removes_the_alert(self):
        with tempfile.TemporaryDirectory() as tmp:
            alerts = Path(tmp) / "alerts"
            self._run(_mod.STATE_SIZE_WARN_BYTES + 1, alerts)
            cleared = self._run(_mod.STATE_SIZE_WARN_BYTES - 1, alerts)
            self.assertTrue(any("back under" in m for m in cleared), repr(cleared))
            self.assertFalse((alerts / "state-scout-state-size").exists())
            quiet = self._run(_mod.STATE_SIZE_WARN_BYTES - 1, alerts)
        self.assertEqual(quiet, [], f"all-clear must fire once: {quiet!r}")

    def test_warn_threshold_leaves_headroom_under_the_read_cap(self):
        self.assertLess(_mod.STATE_SIZE_WARN_BYTES, _mod.STATE_SIZE_READ_CAP_BYTES)
        self.assertEqual(_mod.STATE_SIZE_READ_CAP_BYTES, _READ_CAP_BYTES)

    def test_warn_threshold_sits_above_the_measured_post_fix_size(self):
        # A threshold under the file's own steady-state size fires on every tick
        # from day one — a guard that is always on reports nothing. Measured
        # 2026-08-08 by re-projecting the live cache (46 open PRs) through this
        # change: 522,206 B -> 210,642 B.
        measured_post_fix_bytes = 210_642
        self.assertGreater(
            _mod.STATE_SIZE_WARN_BYTES, measured_post_fix_bytes,
            "warn threshold must sit above the measured post-fix size or the "
            "guard is permanently tripped")


if __name__ == "__main__":
    unittest.main()
