"""Tests for fleet-stalled-sweep, the `fleet:stalled` producer.

The properties these cases lock:

  - the positive fire stamps AND comments, in that order — label first,
    because the reverse turns a persistently failing label add into one
    duplicate comment per sweep, forever;
  - the silent controls stay silent: under threshold, already stamped, and
    the two exempt labels whose exit is owned elsewhere;
  - the design parks and `fleet:design-unblocked` are NOT exempt — seven
    idle days there means the answer or resume lane failed;
  - the re-arm is stateless: label presence is the entire state, so a PR
    whose thread already holds a prior sweep comment fires again;
  - the exit half drops the label from a PR that is no longer wip;
  - a failed list fetch exits non-zero with zero mutations, so a broken
    fetch can never read as an empty fleet.

Hermetic: `gh` is a recording stub that models the real argument surface and
fails closed on anything the tool is not supposed to ask for. No live
GitHub, no live ~/.fleet, and the clock is injected from the fixture's own
timestamps via FLEET_STALLED_SWEEP_NOW.
"""
import calendar
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-stalled-sweep"
if not SUBJECT.is_file():
    print("SKIP: fleet-stalled-sweep subject absent", file=sys.stderr)
    sys.exit(3)

REPO = "jakildev/IrredenEngine"
DAY = 24 * 60 * 60
NOW = calendar.timegm(time.strptime("2026-09-20T12:00:00Z", "%Y-%m-%dT%H:%M:%SZ"))

# The stub `gh`. It transcribes the flag set the tool is allowed to use and
# exits 2 on anything else, so a tool change that reaches for an unmodelled
# endpoint fails here instead of silently passing. `--json …commits…` fails the
# way the live API fails it — that query exceeds GraphQL's node limit, which is
# why the tool derives idleness from updatedAt instead.
_STUB = r'''#!/usr/bin/env python3
import json, os, sys

argv = sys.argv[1:]
record = open(os.environ["STUB_RECORD"], "a")


def die(msg, code=2):
    print(f"stub gh: {msg}", file=sys.stderr)
    sys.exit(code)


def take(flag):
    if flag not in argv:
        die(f"missing {flag}")
    i = argv.index(flag)
    if i + 1 >= len(argv):
        die(f"{flag} needs a value")
    return argv[i + 1]


if argv[0] != "pr":
    die(f"unknown command {argv[0]!r}")

sub = argv[1] if len(argv) > 1 else ""
if sub == "list":
    take("--repo")
    if take("--state") != "open":
        die("stub models --state open only")
    fields = take("--json").split(",")
    if "commits" in fields:
        die("GraphQL: requesting up to 1,000,000 possible nodes which exceeds "
            "the maximum limit of 500,000", 1)
    unknown = set(fields) - {"number", "labels", "updatedAt"}
    if unknown:
        die(f"stub does not model --json field(s) {sorted(unknown)}")
    int(take("--limit"))
    record.write("list\n")
    if os.environ.get("STUB_FAIL_LIST"):
        die("could not resolve to a Repository", 1)
    prs = json.load(open(os.environ["STUB_FIXTURE"]))
    print(json.dumps([{k: pr[k] for k in fields if k in pr} for pr in prs]))
    sys.exit(0)

if sub == "edit":
    number = argv[2]
    take("--repo")
    if "--add-label" in argv:
        record.write(f"add {number} {take('--add-label')}\n")
    elif "--remove-label" in argv:
        record.write(f"remove {number} {take('--remove-label')}\n")
    else:
        die("stub models --add-label / --remove-label only")
    if os.environ.get("STUB_FAIL_EDIT"):
        die("label edit refused", 1)
    sys.exit(0)

if sub == "comment":
    number = argv[2]
    take("--repo")
    body = take("--body")
    record.write(f"comment {number} {len(body)}\n")
    if os.environ.get("STUB_FAIL_COMMENT"):
        die("comment refused", 1)
    sys.exit(0)

die(f"unknown subcommand {sub!r}")
'''


def _pr(number, labels, idle_days, updated=None):
    if updated is None:
        updated = time.strftime("%Y-%m-%dT%H:%M:%SZ",
                                time.gmtime(NOW - idle_days * DAY))
    return {"number": number, "labels": [{"name": n} for n in labels],
            "updatedAt": updated}


class _SweepCase(unittest.TestCase):
    """Runs the real tool against a fixture PR list and a recording stub gh."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.stub = self.tmp / "gh"
        self.stub.write_text(_STUB)
        self.stub.chmod(0o755)
        self.record = self.tmp / "record"
        self.fixture = self.tmp / "prs.json"

    def run_sweep(self, prs, *extra_args, now=NOW, fail=None, env_extra=None):
        """Returns (CompletedProcess, [recorded stub calls])."""
        self.fixture.write_text(json.dumps(prs))
        self.record.write_text("")
        env = dict(os.environ)
        env.update({
            "FLEET_STALLED_SWEEP_GH_BIN": str(self.stub),
            "FLEET_STALLED_SWEEP_NOW": str(now),
            "STUB_FIXTURE": str(self.fixture),
            "STUB_RECORD": str(self.record),
            # The stub is on PATH too, so a bare `gh` would still be the stub
            # rather than the host's real client if the seam above regressed.
            "PATH": f"{self.tmp}{os.pathsep}{env.get('PATH', '')}",
        })
        for key in ("FLEET_WIP_STALL_SECS",):
            env.pop(key, None)
        if fail:
            env[f"STUB_FAIL_{fail.upper()}"] = "1"
        if env_extra:
            env.update(env_extra)
        proc = subprocess.run(
            [sys.executable, str(SUBJECT), "--repo", REPO, *extra_args],
            capture_output=True, text=True, timeout=60, env=env,
        )
        calls = [ln for ln in self.record.read_text().splitlines() if ln]
        return proc, calls

    @staticmethod
    def mutating(calls):
        return [c for c in calls if not c.startswith("list")]


class PositiveFire(_SweepCase):

    def test_idle_wip_pr_is_labeled_then_commented(self):
        proc, calls = self.run_sweep([_pr(101, ["fleet:wip"], idle_days=8)])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        mut = self.mutating(calls)
        self.assertEqual(len(mut), 2,
                         f"expected exactly one label add + one comment: {calls}")
        self.assertEqual(mut[0], "add 101 fleet:stalled")
        # The stub records the body length, so a non-empty body is proof the
        # comment carried one.
        kind, number, body_len = mut[1].split(" ")
        self.assertEqual((kind, number), ("comment", "101"))
        self.assertGreater(int(body_len), 0)

    def test_label_add_precedes_the_comment(self):
        _, calls = self.run_sweep([_pr(101, ["fleet:wip"], idle_days=8)])
        mut = self.mutating(calls)
        self.assertEqual(len(mut), 2, mut)
        self.assertTrue(mut[0].startswith("add 101 fleet:stalled"), mut)
        self.assertTrue(mut[1].startswith("comment 101 "), mut)

    def test_the_comment_names_the_idle_duration_and_signs_itself(self):
        # The body is built once in the tool; assert on the tool's own source
        # rather than re-deriving it, so a reworded comment still has to keep
        # the three human moves and the signature.
        body = SUBJECT.read_text()
        for needle in ("Remove `fleet:wip`", "Close the PR",
                       "Remove `fleet:stalled`", "— fleet-stalled-sweep"):
            self.assertIn(needle, body)

    def test_exactly_one_list_call_per_repo(self):
        _, calls = self.run_sweep([_pr(101, ["fleet:wip"], idle_days=8)])
        self.assertEqual([c for c in calls if c == "list"], ["list"])


class SilentControls(_SweepCase):

    def test_under_threshold_is_untouched(self):
        proc, calls = self.run_sweep([_pr(102, ["fleet:wip"], idle_days=6)])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.mutating(calls), [])

    def test_already_stalled_is_never_re_commented(self):
        proc, calls = self.run_sweep(
            [_pr(103, ["fleet:wip", "fleet:stalled"], idle_days=30)])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.mutating(calls), [])

    def test_exempt_labels_are_untouched(self):
        for exempt in ("human:wip", "fleet:awaiting-infra"):
            with self.subTest(label=exempt):
                _, calls = self.run_sweep(
                    [_pr(104, ["fleet:wip", exempt], idle_days=30)])
                self.assertEqual(self.mutating(calls), [])

    def test_non_wip_pr_without_the_label_is_untouched(self):
        _, calls = self.run_sweep([_pr(105, ["fleet:approved"], idle_days=30)])
        self.assertEqual(self.mutating(calls), [])

    def test_boundary_day_fires_and_one_second_short_does_not(self):
        pr = _pr(106, ["fleet:wip"], idle_days=7)
        _, calls = self.run_sweep([pr])
        self.assertEqual(len(self.mutating(calls)), 2, "exactly-7d must fire")
        _, calls = self.run_sweep([pr], now=NOW - 1)
        self.assertEqual(self.mutating(calls), [], "7d minus 1s must not fire")


class NotExempt(_SweepCase):
    """The design parks are deliberately swept: their exit is an agent
    answering, not an automated transition, so an idle park is evidence that
    lane did not act. `fleet:stalled` has no consumer, so the stamp costs one
    human ping and blocks nothing."""

    def test_design_labels_are_stamped(self):
        for label in ("fleet:design-blocked", "fleet:design-proposed",
                      "fleet:design-unblocked"):
            with self.subTest(label=label):
                _, calls = self.run_sweep(
                    [_pr(107, ["fleet:wip", label], idle_days=8)])
                self.assertEqual(len(self.mutating(calls)), 2,
                                 f"{label} must not be exempt: {calls}")


class StatelessReArm(_SweepCase):
    """Removing the label re-arms the timer. The tool keys on label presence
    alone — it never reads comment history — so a PR that was swept before and
    hand-cleared is stamped again once it goes idle another window."""

    def test_previously_swept_pr_fires_again_once_idle(self):
        # The fixture models the post-removal state: the human's removal bumped
        # updatedAt, then the PR sat another 8 days.
        _, calls = self.run_sweep([_pr(108, ["fleet:wip"], idle_days=8)])
        self.assertEqual(len(self.mutating(calls)), 2)

    def test_the_tool_never_reads_the_comment_thread(self):
        _, calls = self.run_sweep([_pr(108, ["fleet:wip"], idle_days=8)])
        reads = [c for c in calls if c.startswith("list")]
        self.assertEqual(len(reads), 1,
                         f"the list call is the tool's only read: {calls}")


class ExitHalf(_SweepCase):

    def test_stalled_without_wip_is_cleared_without_a_comment(self):
        proc, calls = self.run_sweep(
            [_pr(109, ["fleet:stalled", "fleet:approved"], idle_days=30)])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.mutating(calls), ["remove 109 fleet:stalled"])

    def test_stalled_with_wip_is_left_alone(self):
        _, calls = self.run_sweep(
            [_pr(110, ["fleet:stalled", "fleet:wip"], idle_days=30)])
        self.assertEqual(self.mutating(calls), [])


class DryRun(_SweepCase):

    def test_dry_run_prints_the_candidate_and_mutates_nothing(self):
        proc, calls = self.run_sweep(
            [_pr(111, ["fleet:wip"], idle_days=8)], "--dry-run")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.mutating(calls), [])
        self.assertIn("111", proc.stdout)
        self.assertIn("dry-run", proc.stdout)

    def test_dry_run_prints_the_wip_census(self):
        proc, _ = self.run_sweep(
            [_pr(112, ["fleet:wip"], idle_days=1),
             _pr(113, ["fleet:approved"], idle_days=1)], "--dry-run")
        self.assertIn("1 with fleet:wip", proc.stdout)


class Failures(_SweepCase):

    def test_failed_list_exits_non_zero_with_zero_mutations(self):
        proc, calls = self.run_sweep(
            [_pr(114, ["fleet:wip"], idle_days=8)], fail="list")
        self.assertNotEqual(proc.returncode, 0,
                            "a failed fetch must never read as an empty fleet")
        self.assertEqual(self.mutating(calls), [])
        self.assertIn("gh pr list", proc.stderr)

    def test_failed_label_add_reports_and_skips_the_comment(self):
        proc, calls = self.run_sweep(
            [_pr(115, ["fleet:wip"], idle_days=8)], fail="edit")
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(self.mutating(calls), ["add 115 fleet:stalled"],
                         "no comment may follow a failed label add")

    def test_failed_comment_is_reported_and_never_retried(self):
        proc, calls = self.run_sweep(
            [_pr(116, ["fleet:wip"], idle_days=8)], fail="comment")
        self.assertEqual(len(self.mutating(calls)), 2)
        self.assertIn("not retried", proc.stderr)
        # Second run: the label landed, so the PR is out of the candidate set
        # and the lost comment is never re-attempted.
        _, calls2 = self.run_sweep(
            [_pr(116, ["fleet:wip", "fleet:stalled"], idle_days=8)])
        self.assertEqual(self.mutating(calls2), [])

    def test_unparseable_timestamp_is_skipped_not_stamped(self):
        _, calls = self.run_sweep(
            [_pr(117, ["fleet:wip"], idle_days=0, updated="not-a-date")])
        self.assertEqual(self.mutating(calls), [])

    def test_missing_repo_argument_is_a_usage_error(self):
        proc = subprocess.run([sys.executable, str(SUBJECT)],
                              capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 2, proc.stderr)


class Threshold(_SweepCase):

    def test_env_override_narrows_the_window(self):
        _, calls = self.run_sweep(
            [_pr(118, ["fleet:wip"], idle_days=1)],
            env_extra={"FLEET_WIP_STALL_SECS": str(DAY // 2)})
        self.assertEqual(len(self.mutating(calls)), 2)

    def test_non_positive_override_falls_back_to_the_default(self):
        # A zero window would stamp every open fleet:wip PR on the fleet.
        _, calls = self.run_sweep(
            [_pr(119, ["fleet:wip"], idle_days=1)],
            env_extra={"FLEET_WIP_STALL_SECS": "0"})
        self.assertEqual(self.mutating(calls), [])

    def test_the_shipped_default_is_the_documented_seven_days(self):
        src = SUBJECT.read_text()
        self.assertIn("DEFAULT_STALL_SECONDS = 7 * 24 * 60 * 60", src)


class StubFidelity(_SweepCase):
    """The stub is only evidence if it fails the way GitHub fails."""

    def test_stub_rejects_the_commits_field_the_live_api_refuses(self):
        self.fixture.write_text("[]")
        self.record.write_text("")
        env = dict(os.environ)
        env.update({"STUB_FIXTURE": str(self.fixture),
                    "STUB_RECORD": str(self.record)})
        proc = subprocess.run(
            [str(self.stub), "pr", "list", "--repo", REPO, "--state", "open",
             "--json", "number,commits", "--limit", "300"],
            capture_output=True, text=True, timeout=30, env=env)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("exceeds the maximum limit", proc.stderr)

    def test_stub_rejects_an_unmodelled_subcommand(self):
        env = dict(os.environ)
        env.update({"STUB_FIXTURE": str(self.fixture),
                    "STUB_RECORD": str(self.record)})
        self.fixture.write_text("[]")
        self.record.write_text("")
        proc = subprocess.run([str(self.stub), "pr", "merge", "1"],
                              capture_output=True, text=True, timeout=30, env=env)
        self.assertEqual(proc.returncode, 2, proc.stderr)


if __name__ == "__main__":
    unittest.main()
