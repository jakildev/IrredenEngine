"""Tests for fleet_checks_aggregate.py and .github/workflows/all-checks.yml.

  - Verdict: one fixture head per rule — green, a failure named, running, a
    path-filtered workflow that never started, an always-run workflow with no
    run, a superseded failure, a cancelled newest run, skipped and neutral, a
    workflow run with no check runs of its own, this workflow's own runs.
  - Paging: a failure on the second page of the check-run listing is found;
    a listing still full at the page cap is an error, never a verdict.
  - Posting, through a recording stub that fails closed on any request it was
    not given: no write when unchanged, `pending` first when starting from
    `success`, an API failure exits 1 with no verdict written.
  - Always-run derivation over fixture workflow files and the committed tree.
  - The workflow file, read as text: its trigger list names every other
    workflow (and only those), its permissions, its checkout, its concurrency
    group, and a status context no check already uses.

stdlib-only; no network, no live clone.
"""
import io
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

SUBJECT = Path(__file__).resolve().parent.parent / "fleet_checks_aggregate.py"
REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
WORKFLOW = REPO_ROOT / ".github/workflows/all-checks.yml"

if not SUBJECT.exists():
    print(f"SKIP: {SUBJECT} not found", file=sys.stderr)
    raise SystemExit(3)

sys.path.insert(0, str(SUBJECT.parent))
import fleet_checks_aggregate as agg  # noqa: E402

REPO = "example-owner/example-repo"
SHA = "a" * 40
ALWAYS = {".github/workflows/always.yml": "Always"}

ALWAYS_YML = "name: Always\non:\n  pull_request:\n  push:\n    branches: [master]\n"
FILTERED_YML = ("name: Filtered\non:\n  pull_request:\n    paths:\n      - 'engine/**'\n")


def check(id_, name, conclusion="success", status="completed", suite=1):
    return {"id": id_, "name": name, "status": status, "conclusion": conclusion,
            "check_suite": {"id": suite}}


def wrun(id_, name, conclusion="success", status="completed", suite=1, path=None):
    return {"id": id_, "name": name, "status": status, "conclusion": conclusion,
            "check_suite_id": suite,
            "path": path or f".github/workflows/{name.lower()}.yml"}


class Verdict(unittest.TestCase):
    def test_all_green_is_success(self):
        state, _ = agg.verdict([check(1, "lint"), check(2, "build")],
                               [wrun(10, "Always")], ALWAYS)
        self.assertEqual(state, "success")

    def test_one_failed_is_failure_and_names_it(self):
        state, description = agg.verdict(
            [check(1, "lint"), check(2, "linux-build", conclusion="failure")],
            [wrun(10, "Always", conclusion="failure")], ALWAYS)
        self.assertEqual(state, "failure")
        self.assertIn("linux-build", description)
        self.assertNotIn("lint,", description)

    def test_every_failed_class_is_failure(self):
        for conclusion in ("failure", "timed_out", "startup_failure", "action_required"):
            with self.subTest(conclusion=conclusion):
                state, _ = agg.verdict([check(1, "x", conclusion=conclusion)],
                                       [wrun(10, "Always")], ALWAYS)
                self.assertEqual(state, "failure")

    def test_failure_wins_over_running(self):
        state, _ = agg.verdict(
            [check(1, "a", status="in_progress", conclusion=None),
             check(2, "b", conclusion="failure")],
            [wrun(10, "Always", status="in_progress", conclusion=None)], ALWAYS)
        self.assertEqual(state, "failure")

    def test_one_running_is_pending(self):
        for status in ("queued", "in_progress", "waiting", "requested", "pending"):
            with self.subTest(status=status):
                state, description = agg.verdict(
                    [check(1, "lint"), check(2, "fleet-tests", status=status, conclusion=None)],
                    [wrun(10, "Always", status="in_progress", conclusion=None)], ALWAYS)
                self.assertEqual(state, "pending")
                self.assertIn("fleet-tests", description)

    def test_path_filtered_workflow_that_never_started_is_not_counted(self):
        with tempfile.TemporaryDirectory() as root:
            workflows = Path(root) / ".github/workflows"
            workflows.mkdir(parents=True)
            (workflows / "always.yml").write_text(ALWAYS_YML)
            (workflows / "filtered.yml").write_text(FILTERED_YML)
            always = agg.always_run_workflows(root)
        self.assertEqual(always, ALWAYS)
        state, _ = agg.verdict([check(1, "always-job")], [wrun(10, "Always")], always)
        self.assertEqual(state, "success")

    def test_always_run_workflow_with_no_run_is_pending(self):
        state, description = agg.verdict(
            [check(1, "filtered-job", suite=2)],
            [wrun(10, "Filtered", suite=2)], ALWAYS)
        self.assertEqual(state, "pending")
        self.assertIn("not reported: Always", description)

    def test_no_runs_at_all_is_pending(self):
        state, _ = agg.verdict([], [], ALWAYS)
        self.assertEqual(state, "pending")

    def test_superseded_failure_is_success(self):
        state, _ = agg.verdict(
            [check(5, "build"), check(3, "build", conclusion="failure")],
            [wrun(10, "Always")], ALWAYS)
        self.assertEqual(state, "success")

    def test_newer_failure_beats_older_success(self):
        state, _ = agg.verdict(
            [check(3, "build"), check(5, "build", conclusion="failure")],
            [wrun(10, "Always")], ALWAYS)
        self.assertEqual(state, "failure")

    def test_cancelled_or_stale_newest_run_is_pending(self):
        for conclusion in ("cancelled", "stale"):
            with self.subTest(conclusion=conclusion):
                state, description = agg.verdict(
                    [check(3, "build"), check(5, "build", conclusion=conclusion)],
                    [wrun(10, "Always")], ALWAYS)
                self.assertEqual(state, "pending")
                self.assertIn("build", description)

    def test_unknown_conclusion_is_pending_not_green(self):
        state, _ = agg.verdict([check(1, "build", conclusion="some_new_value")],
                               [wrun(10, "Always")], ALWAYS)
        self.assertEqual(state, "pending")

    def test_skipped_and_neutral_are_green(self):
        state, _ = agg.verdict(
            [check(1, "fleet-tests", conclusion="skipped"), check(2, "x", conclusion="neutral"),
             check(3, "gate")],
            [wrun(10, "Always")], ALWAYS)
        self.assertEqual(state, "success")

    def test_workflow_run_with_no_check_runs_is_judged_by_its_own_status(self):
        startup = wrun(11, "Broken", conclusion="startup_failure", suite=9)
        state, description = agg.verdict([check(1, "lint")], [wrun(10, "Always"), startup],
                                         ALWAYS)
        self.assertEqual(state, "failure")
        self.assertIn("Broken", description)

        queued = wrun(11, "Slow", status="queued", conclusion=None, suite=9)
        state, description = agg.verdict([check(1, "lint")], [wrun(10, "Always"), queued],
                                         ALWAYS)
        self.assertEqual(state, "pending")
        self.assertIn("Slow", description)

    def test_workflow_run_with_check_runs_is_represented_by_them(self):
        # The workflow run's own conclusion is the rollup of an older attempt;
        # the newest check runs in its suite are the reading.
        state, _ = agg.verdict([check(1, "lint", suite=1)],
                               [wrun(10, "Always", conclusion="failure", suite=1)], ALWAYS)
        self.assertEqual(state, "success")

    def test_own_workflow_runs_are_not_counted(self):
        own = wrun(20, "All Checks", status="in_progress", conclusion=None, suite=7,
                   path=agg.OWN_WORKFLOW)
        own_check = check(21, "aggregate", status="in_progress", conclusion=None, suite=7)
        state, _ = agg.verdict([check(1, "lint"), own_check], [wrun(10, "Always"), own],
                               ALWAYS)
        self.assertEqual(state, "success")

    def test_workflow_path_ref_suffix_is_ignored(self):
        run = wrun(10, "Always", path=".github/workflows/always.yml@refs/pull/1/merge")
        state, _ = agg.verdict([check(1, "lint")], [run], ALWAYS)
        self.assertEqual(state, "success")

    def test_description_fits_the_status_limit(self):
        failed = [check(i, f"a-rather-long-check-name-number-{i:03d}", conclusion="failure")
                  for i in range(1, 40)]
        state, description = agg.verdict(failed, [wrun(100, "Always")], ALWAYS)
        self.assertEqual(state, "failure")
        self.assertLessEqual(len(description), agg.DESCRIPTION_LIMIT)
        self.assertRegex(description, r"\+\d+ more$")


class StubApi:
    """Answers only the requests it was given; anything else raises, so a
    request the test did not model can never pass silently."""

    def __init__(self, gets, fail_get=None, fail_post=False):
        self.gets = dict(gets)
        self.fail_get = fail_get
        self.fail_post = fail_post
        self.requested = []
        self.posts = []

    def get(self, path):
        self.requested.append(path)
        if self.fail_get and path.startswith(self.fail_get):
            raise agg.ApiError(f"{path}: HTTP 502")
        if path not in self.gets:
            raise AssertionError(f"unmodelled GET {path}")
        return self.gets[path]

    def post(self, path, fields):
        if self.fail_post:
            raise agg.ApiError(f"{path}: HTTP 403")
        self.posts.append((path, dict(fields)))


def page(path, n):
    sep = "&" if "?" in path else "?"
    return f"{path}{sep}per_page={agg.PER_PAGE}&page={n}"


STATUS_PATH = f"repos/{REPO}/commits/{SHA}/status"
CHECKS_PATH = f"repos/{REPO}/commits/{SHA}/check-runs?filter=latest"
RUNS_PATH = f"repos/{REPO}/actions/runs?head_sha={SHA}"
POST_PATH = f"repos/{REPO}/statuses/{SHA}"


def head(current=None, checks=None, runs=None):
    statuses = [] if current is None else [
        {"context": "lint", "state": "success", "description": "other context"},
        {"context": agg.CONTEXT, "state": current[0], "description": current[1]}]
    return {
        page(STATUS_PATH, 1): {"state": "pending", "statuses": statuses},
        page(CHECKS_PATH, 1): {"total_count": 0,
                               "check_runs": checks if checks is not None else [check(1, "lint")]},
        page(RUNS_PATH, 1): {"workflow_runs": runs if runs is not None else [wrun(10, "Always")]},
    }


class Posting(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        workflows = Path(self.root) / ".github/workflows"
        workflows.mkdir(parents=True)
        (workflows / "always.yml").write_text(ALWAYS_YML)
        (workflows / "filtered.yml").write_text(FILTERED_YML)

    def tearDown(self):
        self._tmp.cleanup()

    def run_main(self, api, *extra):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = agg.main(["--repo", REPO, "--sha", SHA, "--root", self.root, *extra],
                            api=api)
        return code, out.getvalue(), err.getvalue()

    def states(self, api):
        return [fields["state"] for _, fields in api.posts]

    def test_no_status_yet_writes_the_verdict_once(self):
        api = StubApi(head(current=None))
        code, out, _ = self.run_main(api, "--target-url", "https://example.invalid/run/1")
        self.assertEqual(code, 0)
        self.assertEqual(self.states(api), ["success"])
        path, fields = api.posts[0]
        self.assertEqual(path, POST_PATH)
        self.assertEqual(fields["context"], "all-checks")
        self.assertEqual(fields["target_url"], "https://example.invalid/run/1")
        self.assertIn("success", out)

    def test_no_write_when_unchanged(self):
        api = StubApi(head(current=("failure", "failed: linux-build"),
                           checks=[check(1, "linux-build", conclusion="failure")]))
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 0)
        self.assertEqual(api.posts, [])

    def test_same_state_new_description_is_written(self):
        api = StubApi(head(current=("failure", "failed: lint"),
                           checks=[check(1, "linux-build", conclusion="failure")]))
        self.run_main(api)
        self.assertEqual([f["description"] for _, f in api.posts], ["failed: linux-build"])

    def test_pending_is_written_first_when_starting_from_success(self):
        api = StubApi(head(current=("success", "2 checks passed"),
                           checks=[check(1, "lint"), check(2, "build")]))
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 0)
        self.assertEqual(self.states(api), ["pending", "success"])
        # The pending write precedes every check read.
        self.assertEqual(api.requested[0], page(STATUS_PATH, 1))

    def test_pending_first_then_failure(self):
        api = StubApi(head(current=("success", "1 check passed"),
                           checks=[check(1, "lint", conclusion="failure")]))
        self.run_main(api)
        self.assertEqual(self.states(api), ["pending", "failure"])

    def test_api_failure_exits_nonzero_and_writes_no_verdict(self):
        for fail in (CHECKS_PATH, RUNS_PATH, STATUS_PATH):
            with self.subTest(fail=fail):
                api = StubApi(head(current=None), fail_get=fail)
                code, _, err = self.run_main(api)
                self.assertEqual(code, 1)
                self.assertEqual(api.posts, [])
                self.assertIn("no verdict written", err)

    def test_api_failure_after_the_pending_write_leaves_only_pending(self):
        api = StubApi(head(current=("success", "1 check passed")), fail_get=CHECKS_PATH)
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 1)
        self.assertEqual(self.states(api), ["pending"])

    def test_write_failure_exits_nonzero(self):
        api = StubApi(head(current=None), fail_post=True)
        code, _, err = self.run_main(api)
        self.assertEqual(code, 1)
        self.assertIn("HTTP 403", err)

    def test_failure_on_second_page_is_found(self):
        gets = head(current=None)
        first = [check(i, f"check-{i:03d}") for i in range(1, agg.PER_PAGE + 1)]
        gets[page(CHECKS_PATH, 1)] = {"check_runs": first}
        gets[page(CHECKS_PATH, 2)] = {"check_runs": [check(500, "late-check",
                                                            conclusion="failure")]}
        api = StubApi(gets)
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 0)
        self.assertEqual(self.states(api), ["failure"])
        self.assertIn("late-check", api.posts[0][1]["description"])
        self.assertIn(page(CHECKS_PATH, 2), api.requested)

    def test_listing_full_at_the_page_cap_is_an_error(self):
        gets = head(current=None)
        full = [check(i, f"check-{i}") for i in range(agg.PER_PAGE)]
        for n in range(1, agg.MAX_PAGES + 1):
            gets[page(CHECKS_PATH, n)] = {"check_runs": full}
        api = StubApi(gets)
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 1)
        self.assertEqual(api.posts, [])

    def test_malformed_listing_is_an_error(self):
        gets = head(current=None)
        gets[page(CHECKS_PATH, 1)] = {"message": "Not Found"}
        api = StubApi(gets)
        code, _, _ = self.run_main(api)
        self.assertEqual(code, 1)
        self.assertEqual(api.posts, [])

    def test_bad_arguments_are_refused_before_any_request(self):
        api = StubApi({})
        for argv in (["--repo", REPO, "--sha", "HEAD", "--root", self.root],
                     ["--repo", "not a repo", "--sha", SHA, "--root", self.root],
                     ["--repo", REPO, "--sha", SHA, "--root", str(Path(self.root) / "nope")]):
            with self.subTest(argv=argv), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    agg.main(argv, api=api)
                self.assertEqual(raised.exception.code, 2)
        self.assertEqual(api.requested, [])


class AlwaysRun(unittest.TestCase):
    CASES = {
        "on: pull_request\n": True,
        "on: [push, pull_request]\n": True,
        "on:\n  - push\n  - pull_request\n": True,
        "on:\n  pull_request:\n": True,
        "on:\n  pull_request: {}\n": True,
        "on:\n  pull_request:\n    types: [opened, reopened, synchronize, edited]\n": True,
        "on:\n  pull_request:\n    types: [synchronize]\n": False,
        "on:\n  pull_request:\n    paths:\n      - 'engine/**'\n": False,
        "on:\n  pull_request:\n    paths-ignore: ['docs/**']\n": False,
        "on:\n  pull_request:\n    branches: [master]\n": False,
        "on:\n  pull_request: {branches: [master]}\n": False,
        "on:\n  push:\n    branches: [master]\n": False,
        "on:\n  workflow_dispatch:\n": False,
        "name: x\n": False,
        "on:\n  # pull_request:\n  push:\n": False,
    }

    def test_fixture_triggers(self):
        for text, expected in self.CASES.items():
            with self.subTest(text=text):
                self.assertEqual(agg.runs_on_every_pr(text), expected)

    def test_committed_tree(self):
        always = agg.always_run_workflows(REPO_ROOT)
        for path in ("comment-refs.yml", "fleet-tests.yml", "instruction-size.yml",
                     "no-plan-files.yml", "pr-closes-lint.yml", "whitespace.yml"):
            self.assertIn(f".github/workflows/{path}", always)
        for path in ("engine-tests.yml", "perf-gate.yml", "python-lint.yml",
                     "auto-rereview.yml", "all-checks.yml"):
            self.assertNotIn(f".github/workflows/{path}", always)
        self.assertEqual(always[".github/workflows/fleet-tests.yml"], "Fleet Tests")


def listed_trigger_workflows(text):
    """The names under `on.workflow_run.workflows` of a workflow file."""
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if re.match(r"^\s+workflows:\s*$", line))
    indent = len(lines[start]) - len(lines[start].lstrip())
    names = []
    for line in lines[start + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        match = re.match(r"^\s*-\s+(.+?)\s*$", line)
        if match:
            names.append(match.group(1).strip("'\""))
    return names


def other_workflow_names(root):
    names = {}
    for path in agg.workflow_files(root):
        if f"{agg.WORKFLOW_DIR}/{path.name}" != agg.OWN_WORKFLOW:
            names[path.name] = agg.workflow_name(path.read_text(encoding="utf-8"))
    return names


def trigger_gaps(text, names):
    """`(missing, unknown)`: workflow names the trigger list omits, and listed
    names no workflow file declares."""
    listed = set(listed_trigger_workflows(text))
    declared = set(names.values())
    return sorted(declared - listed), sorted(listed - declared)


class TriggerList(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text(encoding="utf-8")
        self.names = other_workflow_names(REPO_ROOT)

    def test_every_other_workflow_has_a_name(self):
        unnamed = [file for file, name in self.names.items() if not name]
        self.assertEqual(unnamed, [], "workflow_run matches by name; these have none")
        self.assertGreater(len(self.names), 1)

    def test_trigger_list_names_every_other_workflow(self):
        self.assertEqual(trigger_gaps(self.text, self.names), ([], []))

    def test_removing_a_name_is_reported(self):
        victim = self.names["fleet-tests.yml"]
        mutated = re.sub(rf"^\s+- {re.escape(victim)}\n", "", self.text, flags=re.M)
        self.assertNotEqual(mutated, self.text)
        self.assertEqual(trigger_gaps(mutated, self.names), ([victim], []))

    def test_a_ghost_name_is_reported(self):
        mutated = self.text.replace("      - Whitespace\n",
                                    "      - Whitespace\n      - Retired Workflow\n")
        self.assertEqual(trigger_gaps(mutated, self.names), ([], ["Retired Workflow"]))

    def test_trigger_is_completion_only(self):
        self.assertRegex(self.text, r"(?m)^    types: \[completed\]$")


def top_level_block(text, key):
    lines = text.splitlines()
    start = lines.index(f"{key}:")
    block = []
    for line in lines[start + 1:]:
        if line and not line.startswith((" ", "#")):
            break
        if line.strip() and not line.lstrip().startswith("#"):
            block.append(line.strip())
    return block


def step_blocks(text):
    """Each `- name:` / `- uses:` step of the file, as its lines."""
    steps, current = [], None
    for line in text.splitlines():
        if re.match(r"^      - ", line):
            current = [line]
            steps.append(current)
        elif current is not None and (line.startswith("        ") or not line.strip()):
            current.append(line)
        else:
            current = None
    return steps


class WorkflowShape(unittest.TestCase):
    def setUp(self):
        self.text = WORKFLOW.read_text(encoding="utf-8")

    def test_permissions_are_exactly_the_needed_ones(self):
        self.assertEqual(sorted(top_level_block(self.text, "permissions")),
                         ["actions: read", "checks: read", "contents: read",
                          "statuses: write"])
        self.assertEqual(len(re.findall(r"(?m)^\s*permissions:", self.text)), 1,
                         "a job-level permissions block would widen or replace these")

    def test_checkout_names_no_pull_request_ref(self):
        checkouts = [s for s in step_blocks(self.text)
                     if any("uses: actions/checkout" in line for line in s)]
        self.assertEqual(len(checkouts), 1)
        body = "\n".join(checkouts[0])
        refs = re.findall(r"(?m)^\s+ref:\s*(.+)$", body)
        self.assertEqual(refs, ["${{ github.event.repository.default_branch }}"])
        for forbidden in ("head_sha", "head_branch", "head_ref", "pull_request", "refs/pull",
                          "inputs.sha", "HEAD_SHA"):
            self.assertNotIn(forbidden, body)
        self.assertIn("persist-credentials: false", body)

    def test_no_step_fetches_or_checks_out_other_code(self):
        for forbidden in ("git fetch", "git checkout", "gh pr checkout", "git switch"):
            self.assertNotIn(forbidden, self.text)

    def test_concurrency_is_keyed_by_the_head_commit(self):
        block = top_level_block(self.text, "concurrency")
        group = next(line for line in block if line.startswith("group:"))
        self.assertIn("github.event.workflow_run.head_sha", group)
        self.assertIn("inputs.sha", group)
        self.assertIn("cancel-in-progress: true", block)

    def test_runs_the_helper_on_pull_request_heads(self):
        self.assertIn("python3 scripts/fleet/fleet_checks_aggregate.py", self.text)
        self.assertIn("github.event.workflow_run.event == 'pull_request'", self.text)

    def test_context_name_is_not_an_existing_check_name(self):
        taken = set()
        for path in agg.workflow_files(REPO_ROOT):
            text = path.read_text(encoding="utf-8")
            jobs = text.split("\njobs:\n", 1)[-1]
            taken.update(re.findall(r"(?m)^  ([A-Za-z0-9_-]+):\s*$", jobs))
            taken.update(m.strip("'\"") for m in re.findall(r"(?m)^    name:\s*(.+)$", jobs))
        self.assertIn("gate", taken)
        self.assertIn("closes-lint", taken)
        self.assertNotIn(agg.CONTEXT, taken)


if __name__ == "__main__":
    unittest.main()
