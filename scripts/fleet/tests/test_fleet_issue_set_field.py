"""Hermetic tests for `fleet-issue set-field`, the validated routing-line write.

`gh` is a fail-closed PATH stub (plus a `gh.bat` twin) over one JSON state
file. It models exactly the four calls the verb issues — the live issue read,
the reference lookup, the record comment and the body edit — records every
write to a JSON-lines log, and exits 2 on anything else. HOME is a temp dir and
the real gh's credentials are poisoned, so an escaped call cannot reach GitHub.
"""

import importlib.machinery
import importlib.util
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBJECT = ROOT / "fleet-issue"
if not (ROOT / "fleet_issue_fields.py").is_file():
    print("SKIP: fleet_issue_fields.py subject absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(ROOT))
import fleet_issue_fields  # noqa: E402
from fleet_blocked_by import blocker_refs  # noqa: E402

SLUG = "jakildev/IrredenEngine"
GAME_SLUG = "jakildev/irreden"
ISSUE = 900001
BLOCKER_A = 900010
BLOCKER_B = 900011
MISSING = 900099

GH_STUB = r'''#!/usr/bin/env python3
import json, os, sys

def load():
    with open(os.environ["GH_STUB_STATE"], encoding="utf-8") as f:
        return json.load(f)

def save(state):
    with open(os.environ["GH_STUB_STATE"], "w", encoding="utf-8", newline="") as f:
        json.dump(state, f)

def die(msg, rc=2):
    sys.stderr.write("stub gh: " + msg + "\n")
    sys.exit(rc)

def flags(argv, allowed):
    out, i = {}, 0
    while i < len(argv):
        if argv[i] not in allowed:
            die("unmodeled argument " + argv[i])
        if i + 1 >= len(argv):
            die("flag needs a value: " + argv[i])
        out[argv[i]] = argv[i + 1]
        i += 2
    return out

def record(kind, number, text):
    with open(os.environ["GH_STUB_LOG"], "a", encoding="utf-8", newline="") as f:
        f.write(json.dumps({"kind": kind, "number": number, "body": text}) + "\n")

def main(argv):
    state = load()
    if argv[:2] == ["issue", "view"] and len(argv) >= 3:
        opts = flags(argv[3:], {"--repo", "--json"})
        rec = state["issues"].get(opts.get("--repo", "") + "#" + argv[2])
        if rec is None:
            die("Could not resolve to an issue", 1)
        fields = opts["--json"].split(",")
        shaped = dict(rec, number=int(argv[2]),
                      labels=[{"name": n} for n in rec["labels"]])
        print(json.dumps({k: shaped[k] for k in fields}))
        return
    if argv[:1] == ["api"] and len(argv) == 4 and argv[2:4] == ["--jq", ".number"]:
        parts = argv[1].split("/")
        if len(parts) != 5 or parts[0] != "repos" or parts[3] != "issues":
            die("unmodeled api path " + argv[1])
        if parts[1] + "/" + parts[2] + "#" + parts[4] not in state["exists"]:
            die("Not Found (HTTP 404)", 1)
        print(parts[4])
        return
    if argv[:2] in (["issue", "comment"], ["issue", "edit"]) and len(argv) >= 3:
        opts = flags(argv[3:], {"--repo", "--body-file"})
        key = opts["--repo"] + "#" + argv[2]
        if key not in state["issues"]:
            die("Could not resolve to an issue", 1)
        with open(opts["--body-file"], encoding="utf-8", newline="") as f:
            text = f.read()
        if argv[1] == "comment":
            if state.get("fail_comment"):
                die("comment refused", 1)
            record("comment", int(argv[2]), text)
        else:
            record("edit", int(argv[2]), text)
            state["issues"][key]["body"] = text
            save(state)
        return
    die("unmodeled command " + " ".join(argv))

main(sys.argv[1:])
'''

LF_BODY = (
    "**Area:** `scripts/fleet`\n"
    "**Model:** opus\n"
    "**Blocked by:** (none)\n"
    "\n"
    "## Scope\n"
    "\n"
    "Prose that mentions blockers in passing.\n"
)


def other_lines_equal(test, old, new, changed_old, changed_new):
    """`old` and `new` are byte-identical outside the given line indexes
    (newline-split, so a CRLF line's `\\r` is part of the compared line)."""
    old_lines, new_lines = old.split("\n"), new.split("\n")
    kept_old = [x for i, x in enumerate(old_lines) if i not in changed_old]
    kept_new = [x for i, x in enumerate(new_lines) if i not in changed_new]
    test.assertEqual(kept_old, kept_new)


class SetFieldCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.home = self.dir / "home"
        self.home.mkdir()
        self.bin = self.dir / "bin"
        self.bin.mkdir()
        stub = self.bin / "gh"
        stub.write_text(GH_STUB, encoding="utf-8")
        stub.chmod(stub.stat().st_mode | stat.S_IXUSR)
        (self.bin / "gh.bat").write_text('@echo off\npython3 "%~dp0gh" %*\n', encoding="utf-8")
        self.state_file = self.dir / "gh-state.json"
        self.log = self.dir / "gh-writes.jsonl"
        self.log.write_text("", encoding="utf-8")
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("FLEET_", "GH_", "GITHUB_"))}
        self.env.update({
            "HOME": str(self.home),
            "PATH": f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}",
            "GH_STUB_STATE": str(self.state_file), "GH_STUB_LOG": str(self.log),
            "GH_CONFIG_DIR": str(self.dir / "ghcfg"), "GH_TOKEN": "hermetic-test-invalid",
            "GH_HOST": "github.invalid",
            "FLEET_ROLE": "worker", "FLEET_ROLE_MODEL": "opus",
            "FLEET_TEST_HOST": "linux", "FLEET_ASSIGNED_WORKTREE": "/fixture/pool-9",
        })
        repo_map = self.dir / "repos.json"
        repo_map.write_text(json.dumps({"engine": SLUG, "game": GAME_SLUG,
                                        "other": "secret/FakeRepo"}),
                            encoding="utf-8")
        self.env["FLEET_PUBLIC_TEXT_REPO_MAP"] = str(repo_map)
        self.set_issue(LF_BODY)

    def tearDown(self):
        self.temp.cleanup()

    def set_issue(self, body, state="OPEN", labels=("fleet:task",), extra=None):
        data = {
            "issues": {f"{SLUG}#{ISSUE}": {"state": state, "labels": list(labels),
                                           "body": body}},
            "exists": [f"{SLUG}#{ISSUE}", f"{SLUG}#{BLOCKER_A}", f"{SLUG}#{BLOCKER_B}",
                       f"{GAME_SLUG}#{BLOCKER_A}"],
        }
        data.update(extra or {})
        self.state_file.write_text(json.dumps(data), encoding="utf-8")

    def body(self):
        state = json.loads(self.state_file.read_text(encoding="utf-8"))
        return state["issues"][f"{SLUG}#{ISSUE}"]["body"]

    def writes(self):
        return [json.loads(ln) for ln in self.log.read_text(encoding="utf-8").splitlines()]

    def run_tool(self, *args):
        return subprocess.run(
            [sys.executable, str(SUBJECT), "set-field", str(ISSUE), *args],
            capture_output=True, text=True, timeout=60, env=self.env, check=False)

    def assert_refused(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.writes(), [])


class BlockedByWrites(SetFieldCase):
    def test_one_blocker_changes_that_line_only(self):
        result = self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "plan decision 3")
        self.assertEqual(result.returncode, 0, result.stderr)
        new = self.body()
        other_lines_equal(self, LF_BODY, new, {2}, {2})
        self.assertEqual(new.split("\n")[2], f"**Blocked by:** #{BLOCKER_A}")
        self.assertEqual(blocker_refs(new, SLUG), [(SLUG, str(BLOCKER_A))])
        comments = [w for w in self.writes() if w["kind"] == "comment"]
        self.assertEqual(len(comments), 1)
        first = comments[0]["body"].splitlines()[0]
        for part in ("**Blocked by**", "`(none)`", f"`#{BLOCKER_A}`",
                     "worker opus on linux pool-9"):
            self.assertIn(part, first)
        self.assertIn("Reason: plan decision 3", comments[0]["body"])

    def test_comment_is_written_before_the_body(self):
        self.run_tool("--blocked-by", str(BLOCKER_A), "--reason", "r")
        self.assertEqual([w["kind"] for w in self.writes()], ["comment", "edit"])

    def test_crlf_body_keeps_every_line_ending(self):
        crlf = LF_BODY.replace("\n", "\r\n")
        self.set_issue(crlf)
        result = self.run_tool("--blocked-by", f"#{BLOCKER_A}", f"#{BLOCKER_B}", "--reason", "r")
        self.assertEqual(result.returncode, 0, result.stderr)
        new = self.body()
        other_lines_equal(self, crlf, new, {2}, {2, 3})
        self.assertEqual(new.split("\n")[2:4], [f"**Blocked by:** #{BLOCKER_A}\r",
                                                f"**Blocked by:** #{BLOCKER_B}\r"])
        self.assertNotIn("\n", new.replace("\r\n", ""))

    def test_two_blockers_are_two_lines(self):
        result = self.run_tool("--blocked-by", f"#{BLOCKER_A}", f"#{BLOCKER_B}", "--reason", "r")
        self.assertEqual(result.returncode, 0, result.stderr)
        new = self.body()
        lines = [ln for ln in new.split("\n") if ln.startswith("**Blocked by:**")]
        self.assertEqual(lines, [f"**Blocked by:** #{BLOCKER_A}", f"**Blocked by:** #{BLOCKER_B}"])
        self.assertEqual(blocker_refs(new, SLUG),
                         [(SLUG, str(BLOCKER_A)), (SLUG, str(BLOCKER_B))])

    def test_none_collapses_existing_blockers_to_one_line(self):
        self.set_issue(LF_BODY.replace("**Blocked by:** (none)\n",
                                       f"**Blocked by:** #{BLOCKER_A}\n"
                                       f"**Blocked by:** #{BLOCKER_B}\n"))
        result = self.run_tool("--blocked-by", "none", "--reason", "r")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.body(), LF_BODY)

    def test_cross_repo_reference_is_refused_on_the_public_repo(self):
        result = self.run_tool("--blocked-by", f"irreden#{BLOCKER_A}", "--reason", "r")
        self.assert_refused(result)
        self.assertEqual(self.body(), LF_BODY)

    def test_absent_line_is_inserted_in_the_header(self):
        self.set_issue(LF_BODY.replace("**Blocked by:** (none)\n", ""))
        result = self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "r")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.body(), LF_BODY.replace("(none)", f"#{BLOCKER_A}"))


class HostWrites(SetFieldCase):
    def test_host_is_inserted_above_the_first_heading(self):
        result = self.run_tool("--host", "macos", "--reason", "Metal-only capture")
        self.assertEqual(result.returncode, 0, result.stderr)
        new = self.body()
        new_lines = new.split("\n")
        at = new_lines.index("**Host:** macos")
        self.assertLess(at, new_lines.index("## Scope"))
        other_lines_equal(self, LF_BODY, new, set(), {at})
        self.assertEqual(fleet_issue_fields.body_required_host(new), "mac")
        self.assertEqual(len([w for w in self.writes() if w["kind"] == "comment"]), 1)

    def test_existing_host_line_is_replaced_in_place(self):
        self.set_issue(LF_BODY.replace("**Model:** opus\n", "**Model:** opus\n**Host:** linux\n"))
        result = self.run_tool("--host", "windows", "--reason", "r")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("**Host:** windows\n", self.body())
        self.assertNotIn("linux", self.body())
        self.assertIn("`linux` → `windows`", self.writes()[0]["body"])


class Refusals(SetFieldCase):
    def test_private_reason_writes_nothing(self):
        result = self.run_tool("--host", "macos", "--reason", "see secret/FakeRepo")
        self.assert_refused(result)
        self.assertIn("foreign-repository line 3", result.stderr)

    def test_unresolved_reference(self):
        self.assert_refused(self.run_tool("--blocked-by", f"#{MISSING}", "--reason", "r"))

    def test_self_reference(self):
        self.assert_refused(self.run_tool("--blocked-by", f"#{ISSUE}", "--reason", "r"))

    def test_unrouted_repo_qualifier(self):
        self.assert_refused(self.run_tool("--blocked-by", f"elsewhere#{BLOCKER_A}",
                                          "--reason", "r"))

    def test_not_a_reference(self):
        self.assert_refused(self.run_tool("--blocked-by", "irreden45", "--reason", "r"))

    def test_none_with_a_reference(self):
        self.assert_refused(self.run_tool("--blocked-by", "none", f"#{BLOCKER_A}",
                                          "--reason", "r"))

    def test_host_words_outside_the_three(self):
        for word in ("mac", "opengl", "linux-or-windows"):
            with self.subTest(word=word):
                self.assert_refused(self.run_tool("--host", word, "--reason", "r"))

    def test_missing_reason(self):
        self.assert_refused(self.run_tool("--host", "macos"))

    def test_blank_reason(self):
        self.assert_refused(self.run_tool("--host", "macos", "--reason", "   "))

    def test_closed_issue(self):
        self.set_issue(LF_BODY, state="CLOSED")
        self.assert_refused(self.run_tool("--host", "macos", "--reason", "r"))

    def test_human_owned_issue(self):
        self.set_issue(LF_BODY, labels=("fleet:task", "human:owned"))
        self.assert_refused(self.run_tool("--host", "macos", "--reason", "r"))

    def test_quoted_existing_line_only(self):
        self.set_issue(LF_BODY.replace("**Blocked by:** (none)", "> **Blocked by:** (none)"))
        self.assert_refused(self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "r"))

    def test_fenced_existing_line_only(self):
        self.set_issue(LF_BODY.replace("**Blocked by:** (none)\n",
                                       "```\n**Blocked by:** (none)\n```\n"))
        self.assert_refused(self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "r"))

    def test_quoted_existing_line_only_for_none(self):
        self.set_issue(LF_BODY.replace("**Blocked by:** (none)", "> **Blocked by:** (none)"))
        self.assert_refused(self.run_tool("--blocked-by", "none", "--reason", "r"))

    def test_another_blocker_declaration_remains(self):
        self.set_issue(LF_BODY.replace("Prose that", f"Blocked by: #{BLOCKER_B} prose that"))
        self.assert_refused(self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "r"))

    def test_host_mentioned_in_prose_ahead_of_the_header(self):
        self.set_issue("Copy the `**Host:** linux` line.\n\n" + LF_BODY)
        self.assert_refused(self.run_tool("--host", "macos", "--reason", "r"))

    def test_failed_comment_leaves_the_body_alone(self):
        self.set_issue(LF_BODY, extra={"fail_comment": True})
        result = self.run_tool("--host", "macos", "--reason", "r")
        self.assert_refused(result)
        self.assertEqual(self.body(), LF_BODY)


class DryRunAndNoOp(SetFieldCase):
    def test_dry_run_prints_and_writes_nothing(self):
        result = self.run_tool("--blocked-by", f"#{BLOCKER_A}", "--reason", "plan decision 3",
                               "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.writes(), [])
        self.assertIn("- **Blocked by:** (none)", result.stdout)
        self.assertIn(f"+ **Blocked by:** #{BLOCKER_A}", result.stdout)
        self.assertIn("fleet-issue set-field: **Blocked by**", result.stdout)
        self.assertIn("Reason: plan decision 3", result.stdout)

    def test_rerun_of_a_held_change_is_a_silent_no_op(self):
        for args in (("--blocked-by", f"#{BLOCKER_A}"), ("--host", "macos")):
            with self.subTest(args=args):
                self.set_issue(LF_BODY)
                self.log.write_text("", encoding="utf-8")
                self.assertEqual(self.run_tool(*args, "--reason", "r").returncode, 0)
                after_first = self.writes()
                rerun = self.run_tool(*args, "--reason", "r")
                self.assertEqual(rerun.returncode, 0, rerun.stderr)
                self.assertEqual(self.writes(), after_first)


class OneParser(unittest.TestCase):
    def setUp(self):
        script = ROOT / "fleet-state-scout"
        loader = importlib.machinery.SourceFileLoader("fleet_state_scout_set_field", str(script))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        self.scout = importlib.util.module_from_spec(spec)
        loader.exec_module(self.scout)
        self.source = script.read_text(encoding="utf-8")

    def test_scout_defines_no_copy_of_the_host_reader(self):
        for name in ("_parse_issue_field", "_host_requirement_re", "_host_field_words",
                     "_host_field_keys", "_body_required_host"):
            with self.subTest(name=name):
                self.assertIsNone(re.search(rf"^def {name}\(", self.source, re.MULTILINE))
        self.assertIsNone(re.search(r"^_HOST_(?:REQUIRED_RE|KEYS)\s*=", self.source,
                                    re.MULTILINE))

    def test_scout_binds_the_shared_functions(self):
        self.assertIs(self.scout._body_required_host, fleet_issue_fields.body_required_host)
        self.assertIs(self.scout._host_field_keys, fleet_issue_fields.host_field_keys)
        self.assertIs(self.scout._parse_issue_field, fleet_issue_fields.parse_issue_field)


if __name__ == "__main__":
    unittest.main()
