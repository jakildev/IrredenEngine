"""Tests for the stack-validation predicates in fleet_validate_stack.

Covers the drift modes the validator must catch at pre-approval time:
prose-only / missing `**Blocked by:**` (treated as an ambiguous *warning*
since it may be a legit root), `**Epic:**` header bullets in place of the
canonical `**Part of epic:**` line (which also breaks file-epic's own
discovery — a hard *error*), malformed `**Blocked by:**` lines that name no
`#N`, and a missing `**Model:**` line. Multiple blockers — multi-ref `#A, #B`
or several `**Blocked by:**` lines — are accepted (the gate unions every ref
and find-stackable-blockers live-resolves them).

The module is loaded via importlib (mirroring test_scope_shipped_reference.py)
so the test runs regardless of cwd.
"""
import importlib.machinery
import importlib.util
import io
import json
import subprocess
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

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

# The CLI imports the module by name; register the loaded copy so both share
# one `subprocess` binding for the gh stub.
sys.modules.setdefault("fleet_validate_stack", _mod)
_CLI = Path(__file__).parent.parent / "fleet-validate-stack"
_cli_loader = importlib.machinery.SourceFileLoader("fleet_validate_stack_cli",
                                                   str(_CLI))
_cli_spec = importlib.util.spec_from_loader("fleet_validate_stack_cli",
                                            _cli_loader)
_cli = importlib.util.module_from_spec(_cli_spec)
_cli_loader.exec_module(_cli)


def _msgs(findings):
    return [f["msg"] for f in findings]


# A fully template-compliant non-head child body.
COMPLIANT_CHILD = (
    "**Model:** opus\n"
    "**Part of epic:** #1307\n"
    "**Blocked by:** #1308 (T1 must land first)\n"
    "\n## Scope\nstuff\n"
)
# A condensed **Epic:** header bullet, with no Part-of-epic line.
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

    def test_decorated_and_alias_model_forms_pass(self):
        for model_line in (
            "**Model:** `sonnet`",
            "**Model:** [sonnet]",
            "- **Model:** sonnet",
            "**Suggested Model:** sonnet",
        ):
            with self.subTest(model_line=model_line):
                body = (f"{model_line}\n**Part of epic:** #1307\n"
                        "**Blocked by:** #1308\n")
                self.assertEqual(validate_child(body, 1307, is_head=False), [])

    def test_qualified_model_warns_and_uses_leading_token(self):
        body = (
            "**Model:** sonnet (escalate to opus if needed)\n"
            "**Part of epic:** #1307\n"
            "**Blocked by:** #1308\n"
        )
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.WARN)
        self.assertIn("leading token `sonnet`", findings[0]["msg"])

    def test_missing_blocked_by_on_non_head_is_warning(self):
        body = "**Model:** opus\n**Part of epic:** #1307\n"
        findings = validate_child(body, 1307, is_head=False)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["severity"], _mod.WARN)
        self.assertIn("Blocked by", findings[0]["msg"])

    def test_prose_blocked_on_header_is_invisible(self):
        # Prose "Blocked on T1" in the header bullet, no standalone Blocked-by
        # line. Flags the **Epic:** drift (error) and the missing Blocked-by
        # (warning).
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
        # Multiple refs on one line are supported — the gate unions them and
        # find-stackable-blockers live-resolves them.
        body = (
            "**Model:** opus\n**Part of epic:** #1307\n"
            "**Blocked by:** #1299, #1300\n"
        )
        self.assertEqual(validate_child(body, 1307, is_head=False), [])

    def test_multiple_separate_blocked_by_lines_are_accepted(self):
        # Several **Blocked by:** lines are unioned by the gate and
        # live-resolved by find-stackable-blockers.
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
        # A degraded form: non-bold inline `Blocked by: #N`. Must produce
        # exactly one WARN naming the canonical form.
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
        # An interior root (a parallel-track head) with no Blocked-by is a
        # warning, not an error — the stack stays ok unless there is a
        # genuine error elsewhere.
        children = [
            self._child(1067, "**Model:** sonnet\n**Part of epic:** #226\n"),
            self._child(1068, "**Model:** opus\n**Part of epic:** #226\n"),
        ]
        result = validate_stack(children, 226)
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 1)

    def test_real_world_epic_header_stack_fails(self):
        # **Epic:** bullet, no **Part of epic:**. Non-head children do carry
        # **Blocked by:**, so the only finding is the Part-of-epic drift
        # (error) — present on every child.
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


# A variant-spelled legacy child: no **Model:**, `- Parent epic:` membership.
LEGACY_CHILD = "## Scope\n- Parent epic: #7100 (Phase 5)\nstuff\n"


class VariantMembership(unittest.TestCase):
    def test_is_epic_child_accepts_every_variant(self):
        for body in ("- Parent epic: #7100\n", "**Part of:** epic #7100\n",
                     "- Epic: #7100\n", "**Part of epic #7100** — x\n",
                     "- Parent: Epic #7100\n", "- Epic umbrella: #7100\n"):
            with self.subTest(body=body):
                self.assertTrue(is_epic_child(body, 7100))

    def test_is_epic_child_rejects_another_epics_variant(self):
        self.assertFalse(is_epic_child("**Part of:** epic #7101\n", 7100))

    def test_open_variant_child_errors_quoting_its_line(self):
        body = "**Model:** opus\n**Part of:** epic #7100 (validation)\n"
        findings = validate_child(body, 7100, is_head=True)
        self.assertEqual([f["severity"] for f in findings], [_mod.ERROR])
        self.assertIn("`**Part of:** epic #7100 (validation)`",
                      findings[0]["msg"])
        self.assertIn("`**Part of epic:** #7100`", findings[0]["msg"])

    def test_closed_variant_child_only_warns(self):
        body = "**Model:** opus\n**Part of:** epic #7100\n"
        findings = validate_child(body, 7100, is_head=True, state="CLOSED")
        self.assertEqual([f["severity"] for f in findings], [_mod.WARN])
        result = validate_stack([{"number": 7101, "body": body,
                                  "state": "CLOSED"}], 7100)
        self.assertTrue(result["ok"])
        self.assertEqual(result["children"][0]["state"], "CLOSED")

    def test_all_closed_legacy_stack_passes_without_strict(self):
        children = [{"number": n, "body": LEGACY_CHILD, "state": "closed"}
                    for n in (7101, 7102)]
        result = validate_stack(children, 7100)
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        # Missing Model + variant membership (both), + missing Blocked-by on
        # the non-head child: all warnings.
        self.assertEqual(result["n_warnings"], 5)

    def test_same_legacy_stack_open_fails(self):
        children = [{"number": n, "body": LEGACY_CHILD, "state": "OPEN"}
                    for n in (7101, 7102)]
        self.assertFalse(validate_stack(children, 7100)["ok"])


class _GhIssueStub:
    """Models `gh issue list` / `gh issue view` argument parsing.

    The accepted flag sets are transcribed from `gh issue list --help` and
    `gh issue view --help`; an unknown flag, a missing value or an invalid
    `--state` fails with exit 1 the way gh does. `list` defaults `--state` to
    `open` and `--limit` to 30, exactly as gh does, and matches `--search
    "in:body #N"` by the bare number anywhere in the body.
    """

    _LIST_FLAGS = {"--app", "-a", "--assignee", "-A", "--author", "-q",
                   "--jq", "--json", "-l", "--label", "-L", "--limit",
                   "--mention", "-m", "--milestone", "-S", "--search", "-s",
                   "--state", "-t", "--template", "-R", "--repo"}
    _VIEW_FLAGS = {"-q", "--jq", "--json", "-t", "--template", "-R", "--repo"}

    def __init__(self, issues):
        self.issues = issues
        self.calls = []

    @staticmethod
    def _fail(argv, msg):
        return subprocess.CompletedProcess(argv, 1, stdout="", stderr=msg)

    def _parse(self, argv, rest, allowed):
        opts, positional = {}, []
        i = 0
        while i < len(rest):
            tok = rest[i]
            if tok.startswith("-"):
                name, eq, val = tok.partition("=")
                if name not in allowed:
                    return None, None, "unknown flag: %s" % name
                if not eq:
                    if i + 1 >= len(rest):
                        return None, None, "flag needs an argument: %s" % name
                    i += 1
                    val = rest[i]
                opts[name.lstrip("-")] = val
            else:
                positional.append(tok)
            i += 1
        return opts, positional, None

    def __call__(self, argv, **kwargs):
        self.calls.append(list(argv))
        if argv[:3] == ["gh", "issue", "list"]:
            opts, _pos, err = self._parse(argv, argv[3:], self._LIST_FLAGS)
            if err:
                return self._fail(argv, err)
            state = opts.get("state", opts.get("s", "open"))
            if state not in ("open", "closed", "all"):
                return self._fail(argv, "invalid argument %r for --state" % state)
            limit = int(opts.get("limit", opts.get("L", "30")))
            search = opts.get("search", opts.get("S", ""))
            number = search.replace("in:body", "").strip().lstrip("#")
            rows = [i for i in self.issues
                    if (state == "all" or i["state"].lower() == state)
                    and number in i["body"]]
            return subprocess.CompletedProcess(
                argv, 0, stdout=json.dumps(rows[:limit]), stderr="")
        if argv[:3] == ["gh", "issue", "view"]:
            opts, pos, err = self._parse(argv, argv[3:], self._VIEW_FLAGS)
            if err:
                return self._fail(argv, err)
            match = [i for i in self.issues if str(i["number"]) == pos[0]]
            if not match:
                return self._fail(argv, "Could not resolve to an issue")
            fields = opts["json"].split(",")
            row = {k: v for k, v in match[0].items() if k in fields}
            return subprocess.CompletedProcess(argv, 0, stdout=json.dumps(row),
                                               stderr="")
        raise AssertionError("unmodeled gh invocation: %r" % (argv,))


def _issue(number, body, state="CLOSED"):
    return {"number": number, "title": "child %d" % number, "body": body,
            "state": state, "labels": []}


# The all-closed shape: four canonically-declared children, every one closed.
ALL_CLOSED = [
    _issue(7200, "## Children\n- [x] #7201\n- [x] #7202\n- [x] #7203\n"
                 "- [x] #7204\n", state="OPEN"),
    _issue(7201, "**Model:** opus\n**Part of epic:** #7200\n"),
    _issue(7202, "**Model:** opus\n**Part of epic:** #7200\n**Blocked by:** #7201\n"),
    _issue(7203, "**Model:** opus\n**Part of epic:** #7200\n**Blocked by:** #7202\n"),
    _issue(7204, "**Model:** opus\n**Part of epic:** #7200\n**Blocked by:** #7203\n"),
]


class Cli(unittest.TestCase):
    def _run(self, issues, *argv):
        stub = _GhIssueStub(issues)
        out, err = io.StringIO(), io.StringIO()
        with patch.object(subprocess, "run", stub), \
                patch.object(_cli, "repo_slug", return_value="o/r"), \
                patch.object(sys, "argv", ["fleet-validate-stack", *argv]), \
                redirect_stdout(out), redirect_stderr(err):
            code = _cli.main()
        return code, out.getvalue(), stub.calls

    def test_stub_fidelity(self):
        stub = _GhIssueStub([])
        self.assertEqual(stub(["gh", "issue", "list", "--bogus", "x"]).returncode, 1)
        self.assertEqual(stub(["gh", "issue", "list", "--state", "done"]).returncode, 1)
        self.assertEqual(stub(["gh", "issue", "list", "--state"]).returncode, 1)
        self.assertEqual(stub(["gh", "issue", "view", "1", "--state", "all"]).returncode, 1)

    def test_default_invocation_passes_state_all(self):
        code, out, calls = self._run(ALL_CLOSED, "7200")
        lists = [c for c in calls if c[:3] == ["gh", "issue", "list"]]
        self.assertEqual(len(lists), 1)
        self.assertIn("--state", lists[0])
        self.assertEqual(lists[0][lists[0].index("--state") + 1], "all")

    def test_all_closed_epic_lists_its_children(self):
        code, out, _calls = self._run(ALL_CLOSED, "7200")
        self.assertEqual(code, 0, out)
        self.assertIn("4 child issue(s) (0 open, 4 closed)", out)
        for n in (7201, 7202, 7203, 7204):
            self.assertIn("#%d" % n, out)

    def test_open_filter_on_all_closed_epic_names_the_filter(self):
        code, out, _calls = self._run(ALL_CLOSED, "7200", "--state", "open")
        self.assertEqual(code, 0)
        self.assertIn("--state open", out)
        self.assertIn("4 with --state all", out)
        self.assertIn("re-run with --state all", out)
        self.assertNotIn("has no children yet", out)

    def test_children_path_fetches_state(self):
        code, out, calls = self._run(ALL_CLOSED, "7200", "--children",
                                     "7201", "7202")
        self.assertEqual(code, 0, out)
        self.assertIn("2 child issue(s) (0 open, 2 closed)", out)
        views = [c for c in calls if c[:3] == ["gh", "issue", "view"]]
        self.assertTrue(all("state" in c[c.index("--json") + 1].split(",")
                            for c in views))

    def test_open_variant_child_fails_the_stack(self):
        issues = ALL_CLOSED + [_issue(7205, "**Model:** opus\n"
                                            "- Parent epic: #7200\n",
                                      state="OPEN")]
        code, out, _calls = self._run(issues, "7200")
        self.assertEqual(code, 1)
        self.assertIn("(1 open, 4 closed)", out)
        self.assertIn("`- Parent epic: #7200`", out)


if __name__ == "__main__":
    unittest.main()
