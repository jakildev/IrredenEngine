"""Unit + integration tests for lint_rules_commands.py (#2823).

The lint scans fenced code blocks in `.claude/rules/*.md` / `docs/agents/*.md`
for command-position `fleet-*` tokens (first whitespace-delimited token per
line, optional `$ ` prompt stripped) and requires each to resolve against a
git-tracked file's basename/stem or PATH. These cases lock the contract:

  - positive: unresolved fleet-* token in a fenced block       -> flagged
  - resolves via tracked basename (`fleet-common`)              -> clean
  - resolves via tracked stem (`fleet-common.sh`)                -> clean
  - resolves via PATH                                            -> clean
  - prose outside a fence, not command position                  -> clean
  - mid-line (not first-token) mention inside a fence             -> clean
  - `rules-cmd-ok` marker immediately above the fence              -> clean
  - marker separated from the fence by a blank line               -> still flagged
  - `$ fleet-x` prompt-prefixed command position                  -> flagged like bare
  - `docs/agents/*.md` is scanned, not just `.claude/rules/*.md`  -> covered
  - the committed `.claude/rules/*.md` + `docs/agents/*.md` tree is green

stdlib-only; every fixture is written under a TemporaryDirectory (no network,
no repo mutation). Tracked-name sets are passed in directly for the unit
tests below (no live `git ls-files` call) — only the CommittedTree class
exercises `main()` end-to-end against this repo's real working tree. Host
PATH is pinned empty for every case (see PathPinnedTest) so resolution is
decided by the fixtures, never by what happens to be installed.
"""
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import lint_rules_commands as lint

_FLEET_ROOT = Path(__file__).resolve().parent.parent.parent.parent


def _run_main(repo_root):
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return lint.main(["lint_rules_commands.py", str(repo_root)])


class PathPinnedTest(unittest.TestCase):
    """Base class: `shutil.which` resolves nothing unless a case says otherwise.

    `resolve()` falls back to a live PATH lookup, so any case whose
    expectation depends on whether a token resolves is otherwise only as
    stable as the host's `~/bin`. The fixtures here cite real fleet tool
    names, and `install.sh` symlinks those onto every fleet host — so when
    `fleet-rules-sweep` landed (#2744) it inverted five cases at once: the
    four "should be flagged" assertions failed, and the marker-suppression
    case began passing vacuously (#2823 review). Pinning the lookup empty
    keeps each case deciding on the tracked-name sets it passes in, and
    matches what CI sees: a bare checkout with nothing installed.

    Cases that mean to exercise the PATH arm call `set_path_resolver`.
    """

    def setUp(self):
        self.set_path_resolver(lambda name: None)

    def set_path_resolver(self, resolver):
        real = lint.shutil.which
        lint.shutil.which = resolver
        self.addCleanup(setattr, lint.shutil, "which", real)


class TmpTreeTest(PathPinnedTest):
    def setUp(self):
        super().setUp()
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path


class ScanFile(TmpTreeTest):
    def test_unresolved_command_position_token_is_flagged(self):
        path = self.write(".claude/rules/example.md",
                          "## Detection\n\n"
                          "```\n"
                          "fleet-rules-sweep --glob 'x'\n"
                          "```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [(4, "fleet-rules-sweep")])

    def test_resolves_via_tracked_basename(self):
        path = self.write(".claude/rules/example.md",
                          "```\nfleet-common\n```\n")
        self.assertEqual(lint.scan_file(path, {"fleet-common"}, set()), [])

    def test_resolves_via_tracked_stem(self):
        # fleet-common.sh is tracked; the citation names the bare stem.
        path = self.write(".claude/rules/example.md",
                          "```\nfleet-common\n```\n")
        self.assertEqual(lint.scan_file(path, set(), {"fleet-common"}), [])

    def test_resolves_via_path(self):
        path = self.write(".claude/rules/example.md",
                          "```\nfleet-definitely-on-path\n```\n")
        self.set_path_resolver(
            lambda name: "/usr/bin/x" if name == "fleet-definitely-on-path" else None)
        self.assertEqual(lint.scan_file(path, set(), set()), [])

    def test_prose_outside_fence_is_not_scanned(self):
        path = self.write(".claude/rules/example.md",
                          "Run `fleet-nonexistent-tool` to check this.\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [])

    def test_mid_line_mention_inside_fence_is_not_command_position(self):
        # Only the FIRST token of a fenced line counts — an argument or
        # trailing mention of a fleet-* name is not itself a command.
        path = self.write(".claude/rules/example.md",
                          "```\necho 'see fleet-nonexistent-tool for details'\n```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [])

    def test_marker_immediately_above_fence_suppresses(self):
        path = self.write(".claude/rules/example.md",
                          "<!-- lint: rules-cmd-ok fleet-rules-sweep -- not yet landed -->\n"
                          "```\nfleet-rules-sweep --glob 'x'\n```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [])

    def test_marker_separated_by_blank_line_does_not_suppress(self):
        # The marker must be the LAST non-blank line before the fence,
        # mirroring the trailing/line-above adjacency of the Python opt-out.
        path = self.write(".claude/rules/example.md",
                          "<!-- lint: rules-cmd-ok fleet-rules-sweep -- not yet landed -->\n"
                          "\n"
                          "```\nfleet-rules-sweep --glob 'x'\n```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [(4, "fleet-rules-sweep")])

    def test_dollar_prompt_prefixed_command_is_flagged_like_bare(self):
        path = self.write(".claude/rules/example.md",
                          "```\n$ fleet-rules-sweep --glob 'x'\n```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [(2, "fleet-rules-sweep")])

    def test_non_fleet_token_is_never_flagged(self):
        path = self.write(".claude/rules/example.md",
                          "```\nrg -n 'pattern' .\n```\n")
        self.assertEqual(lint.scan_file(path, set(), set()), [])


class IterDocsAndMain(TmpTreeTest):
    def test_docs_agents_tree_is_scanned(self):
        self.write("docs/agents/FLEET.md",
                  "```\nfleet-nonexistent-tool\n```\n")
        self.assertEqual(_run_main(self.root), 1)

    def test_clean_tree_exits_zero(self):
        self.write(".claude/rules/example.md", "prose only, no fences\n")
        self.assertEqual(_run_main(self.root), 0)

    def test_repo_wide_tracked_file_resolves_citation(self):
        # collect_tracked_names walks the whole repo (git ls-files), not just
        # the doc directories — a tool tracked anywhere resolves.
        self.write("scripts/fleet/fleet-real-tool", "#!/usr/bin/env bash\n")
        self.write(".claude/rules/example.md",
                  "```\nfleet-real-tool\n```\n")
        import subprocess
        subprocess.run(["git", "-C", str(self.root), "init", "-q"], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "-A"], check=True)
        self.assertEqual(_run_main(self.root), 0)


class CommittedTree(PathPinnedTest):
    def test_committed_rules_and_agent_docs_are_green(self):
        # Acceptance: every fleet-* citation in the committed rules/protocol
        # docs resolves against a tracked file. PATH is pinned empty by the
        # base class, so this is the ratchet CI runs — a citation that only
        # resolves because the reader happens to have the tool installed is
        # a finding here, not a pass.
        self.assertEqual(_run_main(_FLEET_ROOT), 0)

    def test_cpp_math_citation_is_flagged_when_nothing_resolves(self):
        # Positive control (#2823 AC2): the #2823 offender, cpp-math.md:87,
        # scanned with no tracked names and no PATH. It must still be
        # flagged — that proves the green above is resolution finding the
        # tracked `scripts/fleet/fleet-rules-sweep` (landed in #2744), not
        # the predicate going blind on this file.
        real = (_FLEET_ROOT / ".claude" / "rules" / "cpp-math.md").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / ".claude" / "rules" / "cpp-math.md"
            path.parent.mkdir(parents=True)
            path.write_text(real, encoding="utf-8")
            findings = lint.scan_file(path, set(), set())
        self.assertIn("fleet-rules-sweep", [token for _, token in findings])


if __name__ == "__main__":
    unittest.main()
