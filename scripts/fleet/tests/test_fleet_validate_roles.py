"""Tests for fleet_validate_roles (#1667).

Three acceptance-criteria groups:
  1. Passing tree — current engine tree (or a synthetic equivalent) exits 0.
  2. Deleted delta key — removing a key from a wrapper produces an error.
  3. Checklist drift — check_checklist warns on a drifted umbrella, passes on sync.

All I/O is via temporary directories; no network calls.
"""
import importlib.machinery
import importlib.util
import tempfile
import unittest
from pathlib import Path

# Load the module via importlib so the test works regardless of cwd.
_MODULE = Path(__file__).parent.parent / "fleet_validate_roles.py"
_loader = importlib.machinery.SourceFileLoader("fleet_validate_roles", str(_MODULE))
_spec = importlib.util.spec_from_loader("fleet_validate_roles", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

find_protocols = _mod.find_protocols
extract_protocol_keys = _mod.extract_protocol_keys
find_wrappers = _mod.find_wrappers
extract_wrapper_keys = _mod.extract_wrapper_keys
validate_wrapper = _mod.validate_wrapper
validate_roles = _mod.validate_roles
ERROR = _mod.ERROR
WARN = _mod.WARN

# Load check_checklist from fleet_validate_stack for group 3
_STACK_MODULE = Path(__file__).parent.parent / "fleet_validate_stack.py"
_sl = importlib.machinery.SourceFileLoader("fleet_validate_stack", str(_STACK_MODULE))
_ss = importlib.util.spec_from_loader("fleet_validate_stack", _sl)
_sm = importlib.util.module_from_spec(_ss)
_sl.exec_module(_sm)
check_checklist = _sm.check_checklist
parse_checklist = _sm.parse_checklist


# ---------------------------------------------------------------------------
# Helpers for building synthetic trees
# ---------------------------------------------------------------------------

def _write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _make_protocol(root, name, keys):
    """Write a synthetic *-protocol.md with the given delta keys."""
    rows = "\n".join("| **%s** | desc |" % k for k in keys)
    body = """\
# Protocol

## Repo deltas this flow needs

| Delta key | Description |
|---|---|
%s

## Other section

More text.
""" % rows
    _write(Path(root) / "docs" / "agents" / name, body)


def _make_wrapper(root, name, protocol_name, keys, extra_text=""):
    """Write a synthetic role-*.md wrapper with the given delta keys."""
    rows = "\n".join("| **%s** | value |" % k for k in keys)
    body = """\
---
name: {name}
description: test
---

See [`docs/agents/{proto}`](../../docs/agents/{proto}).

## Deltas (Test)

| Delta key | Value |
|---|---|
{rows}

## Responsibilities
{extra}
""".format(name=name, proto=protocol_name, rows=rows, extra=extra_text)
    _write(Path(root) / ".claude" / "commands" / name, body)


# ---------------------------------------------------------------------------
# Group 1: passing tree
# ---------------------------------------------------------------------------

class TestPassingTree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_no_protocols_is_empty_pass(self):
        # A repo with no *-protocol.md files → empty result, exits 0
        _write(Path(self.root) / "docs" / "agents" / "FLEET.md", "# Fleet\n")
        result = validate_roles([(self.root, "engine")])
        self.assertTrue(result["empty"])
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 0)

    def test_protocol_without_repo_deltas_section_is_exempt(self):
        # A *-protocol.md without the opt-in section is not linted
        _write(Path(self.root) / "docs" / "agents" / "reviewer-protocol.md",
               "# Reviewer Protocol\n\n## Rules\nsome rules\n")
        result = validate_roles([(self.root, "engine")])
        self.assertTrue(result["empty"])

    def test_single_protocol_with_matching_wrapper_passes(self):
        keys = ["repo-slug", "role-name", "feedback-file"]
        _make_protocol(self.root, "test-protocol.md", keys)
        _make_wrapper(self.root, "role-test.md", "test-protocol.md", keys)
        result = validate_roles([(self.root, "engine")])
        self.assertFalse(result["empty"])
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 0)

    def test_current_engine_tree_passes(self):
        # The live engine repo tree should pass without errors.
        # (Warnings are allowed — the game wrapper's inverted game-repo-slug
        # value is accepted since the key name is correct.)
        engine_root = str(Path(__file__).parent.parent.parent.parent)
        if not (Path(engine_root) / "docs" / "agents" / "architect-protocol.md").exists():
            self.skipTest("architect-protocol.md not found — not running in engine repo")
        result = validate_roles([(engine_root, "engine")])
        self.assertEqual(result["n_errors"], 0,
                         "engine tree should have 0 errors; got: %s" % result)

    def test_absent_game_root_skipped_gracefully(self):
        keys = ["repo-slug"]
        _make_protocol(self.root, "test-protocol.md", keys)
        _make_wrapper(self.root, "role-test.md", "test-protocol.md", keys)
        nonexistent = "/tmp/__nonexistent_game_root_%s" % id(self)
        result = validate_roles([(self.root, "engine"), (nonexistent, "game")])
        self.assertEqual(result["n_errors"], 0)


# ---------------------------------------------------------------------------
# Group 2: deleted delta key → error
# ---------------------------------------------------------------------------

class TestMissingDeltaKey(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_missing_key_is_error(self):
        proto_keys = ["repo-slug", "role-name", "feedback-file"]
        _make_protocol(self.root, "test-protocol.md", proto_keys)
        # Wrapper is missing "feedback-file"
        _make_wrapper(self.root, "role-test.md", "test-protocol.md",
                      ["repo-slug", "role-name"])
        result = validate_roles([(self.root, "engine")])
        self.assertFalse(result["ok"])
        self.assertEqual(result["n_errors"], 1)
        all_findings = [
            f for proto in result["protocols"]
            for repo in proto["repos"]
            for wr in repo["wrappers"]
            for f in wr["findings"]
        ]
        errors = [f for f in all_findings if f["severity"] == ERROR]
        self.assertEqual(len(errors), 1)
        self.assertIn("feedback-file", errors[0]["msg"])

    def test_all_missing_keys_reported(self):
        proto_keys = ["repo-slug", "role-name", "feedback-file", "repo-root"]
        _make_protocol(self.root, "test-protocol.md", proto_keys)
        # Wrapper has only one key
        _make_wrapper(self.root, "role-test.md", "test-protocol.md", ["repo-slug"])
        result = validate_roles([(self.root, "engine")])
        self.assertFalse(result["ok"])
        self.assertEqual(result["n_errors"], 3)

    def test_extra_key_in_wrapper_is_warn_not_error(self):
        proto_keys = ["repo-slug"]
        _make_protocol(self.root, "test-protocol.md", proto_keys)
        _make_wrapper(self.root, "role-test.md", "test-protocol.md",
                      ["repo-slug", "extra-key"])
        result = validate_roles([(self.root, "engine")])
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 1)

    def test_alias_satisfies_protocol_key(self):
        proto_keys = ["canonical-key"]
        _make_protocol(self.root, "test-protocol.md", proto_keys)
        # Wrapper uses legacy name
        _make_wrapper(self.root, "role-test.md", "test-protocol.md", ["legacy-key"])
        aliases = {"canonical-key": ["legacy-key"]}
        result = validate_roles([(self.root, "engine")], aliases=aliases)
        self.assertEqual(result["n_errors"], 0,
                         "alias should satisfy protocol key; got errors: %s" % result)

    def test_no_wrapper_in_present_repo_is_warn(self):
        proto_keys = ["repo-slug"]
        _make_protocol(self.root, "test-protocol.md", proto_keys)
        # No wrapper file at all
        result = validate_roles([(self.root, "engine")])
        self.assertTrue(result["ok"])
        self.assertEqual(result["n_errors"], 0)
        self.assertEqual(result["n_warnings"], 1)


# ---------------------------------------------------------------------------
# Group 3: checklist drift
# ---------------------------------------------------------------------------

class TestCheckChecklist(unittest.TestCase):
    def test_synced_checklist_has_no_drift(self):
        body = "## Children\n\n- [ ] #10\n- [ ] #11\n- [x] #12\n"
        drift = check_checklist(body, [10, 11, 12])
        self.assertEqual(drift, [])

    def test_missing_from_checklist(self):
        body = "## Children\n\n- [ ] #10\n"
        drift = check_checklist(body, [10, 11])
        missing = [d for d in drift if d["kind"] == "missing-from-checklist"]
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0]["number"], 11)

    def test_in_checklist_not_discovered(self):
        body = "## Children\n\n- [ ] #10\n- [ ] #99\n"
        drift = check_checklist(body, [10])
        orphan = [d for d in drift if d["kind"] == "in-checklist-not-discovered"]
        self.assertEqual(len(orphan), 1)
        self.assertEqual(orphan[0]["number"], 99)

    def test_empty_checklist_all_children_missing(self):
        drift = check_checklist("", [5, 6, 7])
        missing = [d for d in drift if d["kind"] == "missing-from-checklist"]
        self.assertEqual(sorted(d["number"] for d in missing), [5, 6, 7])

    def test_empty_children_all_checklist_orphans(self):
        body = "- [ ] #20\n- [x] #21\n"
        drift = check_checklist(body, [])
        orphans = [d for d in drift if d["kind"] == "in-checklist-not-discovered"]
        self.assertEqual(sorted(d["number"] for d in orphans), [20, 21])

    def test_parse_checklist_handles_crlf(self):
        body = "- [ ] #10\r\n- [X] #11\r\n"
        result = parse_checklist(body)
        self.assertEqual(result, {10: False, 11: True})

    def test_parse_checklist_case_insensitive_x(self):
        body = "- [X] #10\n- [x] #11\n"
        result = parse_checklist(body)
        self.assertTrue(result[10])
        self.assertTrue(result[11])

    def test_fully_synced_umbrella_no_warnings(self):
        body = "- [ ] #1\n- [ ] #2\n- [ ] #3\n"
        drift = check_checklist(body, [1, 2, 3])
        self.assertEqual(drift, [])


if __name__ == "__main__":
    unittest.main()
