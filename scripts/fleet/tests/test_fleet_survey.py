"""Tests for fleet-survey, the deterministic half of the fleet-survey skill.

Hermetic: the scout cache, the issue and PR listings, the usage samples and
the dispatcher log are all fixtures; no gh, no git. The cases pin the
partitions a human acts on:

  - a queued task shadowed by a merged PR with no open PR is a ghost, not
    claimable; the same task with an open PR is in flight
  - a host pin on another OS (or a GL pin on a Metal host) is pinned away
  - a park whose named issues all closed reads as lifted; one naming a ghost
    reads as parked on a delivered issue; one naming a needs-human issue
    waits on the human; an unparsable park is malformed
  - a wip PR with no owner label is stranded; a claim label vouches for it;
    design-unblocked is a pending resume
  - a fleet:task issue with no approval label is the approval gap, epic
    children first; approved, parked and epic issues stay out
  - an epic whose only unchecked children are ghosts or closed is close-out
    ready; one with an open child is not
  - the untriaged predicate is "no fleet:/human: label"
  - idle ticks count only after the last dispatch line
  - `--repo` takes engine, game or all and nothing else; a repo named
    outright that the cache does not carry is an error, while `all` skips it

Import the script via importlib because it has no .py extension.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import json
import re
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock

_SCRIPT = Path(__file__).parent.parent / "fleet-survey"
_loader = importlib.machinery.SourceFileLoader("fleet_survey", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_survey", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

NOW = datetime(2026, 10, 7, 4, 50, tzinfo=timezone.utc)


def labels(*names):
    return [{"name": n} for n in names]


def task(num, **kw):
    row = {"id": f"#{num}", "issue": f"#{num}", "title": f"task {num}", "model": "opus",
           "owner": "free", "blocked": False, "blocked_by": "(none)", "needs_host": None,
           "needs_gl_host": False}
    row.update(kw)
    return row


STATE = {
    "generated_at": "2026-10-07T04:49:00Z",
    "repos": {"engine": {"tasks": {
        "open": [
            task(100),
            task(101, shadow_merged_pr={"number": 900, "mergedAt": "2026-10-06T00:00:00Z"}),
            task(102, shadow_merged_pr={"number": 901}, inflight_pr={"number": 950}),
            task(103, needs_host="windows"),
            task(104, needs_gl_host=True),
            task(105, needs_host="mac"),
            task(109, needs_gl_host=True, backend_symmetric=True),
            task(106, blocked=True, blocked_by="#101"),
            task(108, blocked=False, blocked_by="#102, jakildev/other#5"),
        ],
        "in_progress": [task(107, owner="pool-3")],
    }}},
}

ISSUES = [
    {"number": 100, "title": "claimable", "labels": labels("fleet:queued", "human:approved")},
    {"number": 101, "title": "ghost", "labels": labels("fleet:queued", "fleet:agent-approved")},
    {"number": 102, "title": "in flight", "labels": labels("fleet:queued")},
    {"number": 103, "title": "windows", "labels": labels("fleet:queued")},
    {"number": 104, "title": "gl", "labels": labels("fleet:queued")},
    {"number": 105, "title": "mac", "labels": labels("fleet:queued")},
    {"number": 106, "title": "blocked", "labels": labels("fleet:queued", "fleet:blocked")},
    {"number": 107, "title": "in progress", "labels": labels("fleet:queued", "fleet:in-progress")},
    {"number": 108, "title": "stack-blocked", "labels": labels("fleet:queued")},
    {"number": 109, "title": "symmetric gl", "labels": labels("fleet:queued")},
    {"number": 200, "title": "needs human", "labels": labels("fleet:task", "fleet:needs-human")},
    {"number": 201, "title": "unapproved standalone", "labels": labels("fleet:task"),
     "body": "**Model:** opus\n"},
    {"number": 202, "title": "unapproved child", "labels": labels("fleet:task"),
     "body": "**Model:** opus\n**Part of epic:** #300\n"},
    {"number": 203, "title": "approved", "labels": labels("fleet:task", "human:approved")},
    {"number": 300, "title": "epic open child", "labels": labels("fleet:epic"),
     "body": "## Children\n- [x] #299\n- [ ] #202\n"},
    {"number": 301, "title": "epic ghost remainder", "labels": labels("fleet:epic"),
     "body": "## Children\r\n- [X] #299\r\n- [ ] #101\r\n- [ ] #998\r\n"},
    {"number": 302, "title": "epic in-flight child", "labels": labels("fleet:epic"),
     "body": "## Children\n- [ ] #102\n"},
    {"number": 204, "title": "child linked only by a closing ref", "labels": labels("fleet:task"),
     "body": "**Part of epic:** #303\n"},
    {"number": 303, "title": "epic child in flight on a hand-named branch",
     "labels": labels("fleet:epic"), "body": "## Children\n- [ ] #204\n"},
    {"number": 205, "title": "child closed by a qualified ref", "labels": labels("fleet:task"),
     "body": "**Part of epic:** #304\n"},
    {"number": 206, "title": "child named by a cross-repo ref", "labels": labels("fleet:task"),
     "body": "**Part of epic:** #304\n"},
    {"number": 304, "title": "epic with qualified and cross-repo refs",
     "labels": labels("fleet:epic"), "body": "## Children\n- [ ] #205\n- [ ] #206\n"},
    {"number": 400, "title": "untriaged idea", "labels": []},
]

PRS = [
    {"number": 950, "title": "in flight for 102", "headRefName": "claude/102-thing",
     "labels": labels("fleet:wip", "fleet:claim-mac-pool-1"), "mergeable": "MERGEABLE",
     "body": "Closes #102"},
    {"number": 951, "title": "lifted park", "headRefName": "claude/110-x",
     "labels": labels("fleet:wip", "fleet:awaiting-infra"), "mergeable": "CONFLICTING",
     "body": "Closes #110\nParked-until: #998, #999\n"},
    {"number": 952, "title": "park on ghost", "headRefName": "claude/111-x",
     "labels": labels("fleet:wip", "fleet:awaiting-infra"), "mergeable": "MERGEABLE",
     "body": "Parked-until: #101\n"},
    {"number": 953, "title": "park on needs-human", "headRefName": "claude/112-x",
     "labels": labels("fleet:wip", "fleet:awaiting-infra"), "mergeable": "MERGEABLE",
     "body": "Parked-until: #200\n"},
    {"number": 954, "title": "park legit", "headRefName": "claude/113-x",
     "labels": labels("fleet:wip", "fleet:awaiting-infra"), "mergeable": "MERGEABLE",
     "body": "old line\nParked-until: #997\nparked-until: #100 (the real one)\n"},
    {"number": 955, "title": "park malformed", "headRefName": "claude/114-x",
     "labels": labels("fleet:wip", "fleet:awaiting-infra"), "mergeable": "MERGEABLE",
     "body": "parked behind #100 in prose\n"},
    {"number": 956, "title": "stranded", "headRefName": "claude/115-x",
     "labels": labels("fleet:wip", "fleet:author-codex"), "mergeable": "CONFLICTING",
     "body": ""},
    {"number": 957, "title": "resume pending", "headRefName": "claude/116-x",
     "labels": labels("fleet:wip", "fleet:design-unblocked"), "mergeable": "MERGEABLE",
     "body": ""},
    {"number": 958, "title": "design blocked", "headRefName": "claude/117-x",
     "labels": labels("fleet:wip", "fleet:design-blocked"), "mergeable": "MERGEABLE",
     "body": ""},
    {"number": 959, "title": "steward parked", "headRefName": "claude/118-x",
     "labels": labels("fleet:wip", "fleet:design-proposed"), "mergeable": "MERGEABLE",
     "body": ""},
    {"number": 960, "title": "hand-named branch", "headRefName": "feature/no-number-here",
     "labels": labels("fleet:claim-mac-pool-3"), "mergeable": "MERGEABLE",
     "body": "Some prose, then\n\nCloses #204\n"},
    {"number": 961, "title": "qualified same-repo ref", "headRefName": "feature/also-no-number",
     "labels": labels("fleet:claim-mac-pool-4"), "mergeable": "MERGEABLE",
     "body": "Closes jakildev/IrredenEngine#205\n"},
    {"number": 962, "title": "cross-repo ref only", "headRefName": "feature/other-repo",
     "labels": labels("fleet:claim-mac-pool-5"), "mergeable": "MERGEABLE",
     "body": "Closes jakildev/irreden#206\n"},
]

LOG = """\
[2026-10-06T22:00:00Z dispatcher] worker: no candidate could be claimed (class=sonnet)
[2026-10-06T22:40:31Z dispatcher] dispatching opus-reviewer -> %0 [target=review:game:487]
[2026-10-06T22:50:00Z dispatcher] worker: class=opus has no claimable candidate left
[2026-10-06T22:55:00Z dispatcher] worker: class=opus has no claimable candidate left
[2026-10-06T23:00:00Z dispatcher] usage gate closed:github_graphql[user] missing or stale
"""


class FleetSurveyFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        usage = Path(cls.tmp.name) / "usage"
        usage.mkdir()
        (usage / "github-app-graphql.json").write_text(json.dumps(
            {"utilization": 0.31, "used": 1558, "remaining": 3442,
             "interval_points_per_minute": 45.2, "identity": "app", "observed_at": 200}))
        (usage / "github-graphql.json").write_text(json.dumps(
            {"utilization": 0.01, "used": 79, "identity": "app", "observed_at": 100}))
        cls.log = Path(cls.tmp.name) / "dispatcher.log"
        cls.log.write_text(LOG)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_survey(self, host="mac"):
        return _mod.survey(STATE, {"engine": ISSUES}, {"engine": PRS}, host,
                           Path(self.tmp.name) / "usage", self.log, now=NOW)


class Dispatch(FleetSurveyFixture):
    def test_cache_age_reads_generated_at_and_fails_closed(self):
        self.assertEqual(_mod.cache_age_seconds(STATE, NOW), 60)
        self.assertGreater(_mod.cache_age_seconds({}, NOW), _mod.STALE_CACHE_S)
        self.assertGreater(_mod.cache_age_seconds({"generated_at": "garbage"}, NOW),
                           _mod.STALE_CACHE_S)

    def test_idle_ticks_count_after_the_last_dispatch(self):
        d = self.run_survey()["dispatch"]
        self.assertEqual(d["last_dispatch"], "2026-10-06T22:40:31Z")
        self.assertEqual(d["idle_ticks"], 2)
        self.assertIn("usage gate closed", d["usage_gate"])
        self.assertEqual(d["cache_age_s"], 60)
        self.assertEqual(d["pools"]["github-app-graphql"]["used"], 1558)


class Queue(FleetSurveyFixture):
    def ids(self, bucket, host="mac"):
        return [r["id"] for r in self.run_survey(host)["repos"]["engine"]["queue"][bucket]]

    def test_partition_on_a_metal_host(self):
        self.assertEqual(self.ids("claimable_here"), ["#100", "#105", "#109"])
        self.assertEqual(self.ids("ghost"), ["#101"])
        self.assertEqual(self.ids("in_flight"), ["#102"])
        self.assertEqual(self.ids("pinned_away"), ["#103", "#104"])
        self.assertEqual(self.ids("blocked"), ["#106", "#108"])

    def test_an_open_blocker_overrides_the_scouts_stackable_reading(self):
        rows = self.run_survey()["repos"]["engine"]["queue"]["blocked"]
        stacked = next(r for r in rows if r["id"] == "#108")
        self.assertIn("not claimable", stacked["note"])
        self.assertNotIn("note", next(r for r in rows if r["id"] == "#106"))
        self.assertEqual(self.ids("in_progress"), ["#107"])

    def test_a_gl_host_claims_the_gl_pin_and_loses_the_mac_pin(self):
        self.assertEqual(self.ids("claimable_here", "linux"), ["#100", "#104", "#109"])
        self.assertEqual(self.ids("pinned_away", "linux"), ["#103", "#105"])

    def test_ghost_row_names_the_merged_pr(self):
        ghost = self.run_survey()["repos"]["engine"]["queue"]["ghost"][0]
        self.assertEqual(ghost["shadow_pr"], 900)


class Parks(FleetSurveyFixture):
    def verdicts(self):
        return {r["pr"]: r["verdict"] for r in self.run_survey()["repos"]["engine"]["parks"]}

    def test_each_park_shape_gets_its_verdict(self):
        v = self.verdicts()
        self.assertTrue(v[951].startswith("lifted"))
        self.assertTrue(v[952].startswith("parked on a delivered issue"))
        self.assertTrue(v[953].startswith("waits on a human"))
        self.assertTrue(v[954].startswith("legit"))
        self.assertTrue(v[955].startswith("malformed"))
        self.assertNotIn(950, v)

    def test_last_parked_until_line_wins(self):
        row = next(r for r in self.run_survey()["repos"]["engine"]["parks"] if r["pr"] == 954)
        self.assertEqual(row["parked_until"], [100])
        self.assertEqual(row["issues"], {100: "open"})

    def test_ghost_park_names_the_shadowing_pr(self):
        row = next(r for r in self.run_survey()["repos"]["engine"]["parks"] if r["pr"] == 952)
        self.assertEqual(row["issues"][101], "ghost (shadowed by PR #900)")


class Stranded(FleetSurveyFixture):
    def test_only_unvouched_or_resume_pending_wip_is_listed(self):
        rows = {r["pr"]: r["state"] for r in self.run_survey()["repos"]["engine"]["stranded"]}
        self.assertEqual(set(rows), {956, 957})
        self.assertTrue(rows[956].startswith("stranded"))
        self.assertTrue(rows[957].startswith("resume pending"))


class ApprovalGap(FleetSurveyFixture):
    def test_unapproved_tasks_with_epic_children_first(self):
        rows = self.run_survey()["repos"]["engine"]["approval_gap"]
        self.assertEqual([(r["issue"], r["epic"]) for r in rows],
                         [(202, 300), (204, 303), (205, 304), (206, 304), (201, None)])


class Epics(FleetSurveyFixture):
    def test_close_out_ready_only_when_nothing_open_remains(self):
        rows = {r["epic"]: r for r in self.run_survey()["repos"]["engine"]["epics"]}
        self.assertFalse(rows[300]["close_out_ready"])
        self.assertEqual(rows[300]["left"], {202: "open"})
        self.assertTrue(rows[301]["close_out_ready"])
        self.assertEqual(rows[301]["left"],
                         {101: "ghost (shadowed by PR #900)", 998: "closed, box unchecked"})
        self.assertFalse(rows[302]["close_out_ready"])
        self.assertEqual(rows[302]["left"], {102: "in flight: PR #950"})

    def test_a_body_closing_ref_links_a_pr_on_a_hand_named_branch(self):
        rows = {r["epic"]: r for r in self.run_survey()["repos"]["engine"]["epics"]}
        self.assertEqual(rows[303]["left"], {204: "in flight: PR #960"})
        self.assertFalse(rows[303]["close_out_ready"])

    def test_a_qualified_own_repo_ref_links_and_a_cross_repo_ref_does_not(self):
        rows = {r["epic"]: r for r in self.run_survey()["repos"]["engine"]["epics"]}
        self.assertEqual(rows[304]["left"], {205: "in flight: PR #961", 206: "open"})


class Untriaged(FleetSurveyFixture):
    def test_predicate_is_no_fleet_or_human_label(self):
        self.assertEqual(self.run_survey()["repos"]["engine"]["untriaged"], [400])


class ParkRegexDriftGuard(unittest.TestCase):
    def test_pattern_matches_reconcile_r8_byte_for_byte(self):
        source = (Path(__file__).parent.parent / "fleet-claim").read_text(encoding="utf-8")
        self.assertIn('r"' + _mod.PARKED_UNTIL_RE.pattern + '"', source)
        self.assertIn("re.IGNORECASE | re.MULTILINE", source)
        self.assertTrue(_mod.PARKED_UNTIL_RE.flags & re.IGNORECASE)
        self.assertTrue(_mod.PARKED_UNTIL_RE.flags & re.MULTILINE)


class Render(FleetSurveyFixture):
    def test_render_mentions_every_section(self):
        text = _mod.render(self.run_survey())
        for needle in ("## Dispatch", "## engine: queue", "ghost (1)", "## engine: parks",
                       "## engine: stranded wip", "## engine: approval gap", "## engine: epics",
                       "close-out ready", "## engine: untriaged (1)", "fleet-triage-sweep"):
            self.assertIn(needle, text)


class Cli(FleetSurveyFixture):
    """main() on the live-fetch path, with gh replaced by a seam that fails closed."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.state_path = Path(cls.tmp.name) / "state.json"
        cls.state_path.write_text(json.dumps(STATE))

    def fake_gh(self, args):
        self.gh_calls.append(args)
        if args[:2] == ["issue", "list"]:
            return ISSUES
        if args[:2] == ["pr", "list"]:
            return PRS
        raise AssertionError(f"unmodelled gh call: {args}")

    def run_main(self, *argv):
        self.gh_calls = []
        out, err = io.StringIO(), io.StringIO()
        full = [*argv, "--allow-stale", "--state", str(self.state_path), "--host", "mac",
                "--usage-dir", str(Path(self.tmp.name) / "usage"), "--log", str(self.log)]
        with mock.patch.object(_mod, "gh_json", side_effect=self.fake_gh), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                code = _mod.main(full)
            except SystemExit as exc:
                code = exc.code
        return code, out.getvalue(), err.getvalue()

    def fetched_slugs(self):
        return sorted({args[args.index("--repo") + 1] for args in self.gh_calls})

    def test_an_unknown_repo_is_refused_before_any_report(self):
        code, out, err = self.run_main("--repo", "bogus")
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertIn("--repo", err)
        self.assertIn("bogus", err)
        self.assertEqual(self.gh_calls, [])

    def test_a_named_repo_fetches_only_that_repo(self):
        code, out, _ = self.run_main("--repo", "engine")
        self.assertEqual(code, 0)
        self.assertIn("## engine: queue", out)
        self.assertEqual(self.fetched_slugs(), ["jakildev/IrredenEngine"])

    def test_all_skips_a_repo_the_cache_does_not_carry(self):
        code, out, _ = self.run_main("--repo", "all")
        self.assertEqual(code, 0)
        self.assertIn("## engine: queue", out)
        self.assertNotIn("## game:", out)
        self.assertEqual(self.fetched_slugs(), ["jakildev/IrredenEngine"])

    def test_a_named_repo_missing_from_the_cache_is_an_error(self):
        code, out, err = self.run_main("--repo", "game")
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertIn("no game repo", err)
        self.assertEqual(self.gh_calls, [])


if __name__ == "__main__":
    unittest.main()
