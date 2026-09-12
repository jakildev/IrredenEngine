"""Unit + integration tests for lint_rules_registry.py (#2935).

`.claude/rules/README.md` states two obligations for every `cpp-*.md`:
`paths:` frontmatter (predicate A) and a row in the canonical-home map,
`docs/agents/CLAUDE-BASELINE.md`'s `## Canonical-home map` section
(predicate B). These cases lock the contract:

  - no frontmatter at all                                        -> flagged
  - frontmatter with no `paths:` key                              -> flagged
  - `paths:` key with an empty list                                -> flagged
  - empty `paths:`, list belongs to a later top-level key          -> flagged
  - `paths:` whose value is a mapping, a scalar, or `[]`           -> flagged
  - `paths:` key with a non-empty list                             -> clean
  - block sequence at the key's own indentation (valid YAML)       -> clean
  - flow sequence (`paths: [item]`)                                -> clean
  - a non-empty `paths:` followed by a sibling key                 -> clean
  - a comment line between `paths:` and its items                  -> clean
  - CRLF line endings, same content                                -> clean
  - unregistered rule (file present, no map row)                   -> flagged
  - registered rule (map row present)                              -> clean
  - a mention outside the map section does not register            -> flagged
  - a frontmatter-less non-`cpp-*.md` file (README.md) is ignored   -> clean
  - the reconstructed pre-fix (`12b8a49c8`) population              -> exactly
    the issue's 3 findings
  - the committed tree is green

stdlib-only; every fixture is written under a TemporaryDirectory (no network,
no repo mutation, no live git — the fixtures are never `git init`'d, so
`collect_rule_files` exercises its non-git glob fallback throughout). Only
CommittedTree exercises `main()` end-to-end against this repo's real tree.
"""
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import lint_rules_registry as lint

_FLEET_ROOT = Path(__file__).resolve().parent.parent.parent.parent

_SIBLING_FRONTMATTER = (
    "---\n"
    "paths:\n"
    "  - \"engine/**/*.{hpp,cpp,h,cc}\"\n"
    "  - \"creations/**/*.{hpp,cpp,h,cc}\"\n"
    "---\n"
)

_MAP_HEADER = (
    "## Canonical-home map\n"
    "\n"
    "| Topic | Canonical home |\n"
    "|---|---|\n"
)


def _run_main(repo_root):
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return lint.main(["lint_rules_registry.py", str(repo_root)])


class TmpTreeTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path

    def write_baseline(self, map_body=""):
        return self.write(
            "docs/agents/CLAUDE-BASELINE.md",
            "# CLAUDE-BASELINE\n\n" + _MAP_HEADER + map_body + "\n## Next section\n"
        )


class FrontmatterPredicate(TmpTreeTest):
    def test_no_frontmatter_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md", "# Some rule\n\nbody\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_frontmatter_without_paths_key_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\nfoo: 1\n---\n\n# Some rule\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_paths_key_with_empty_list_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths:\n---\n\n# Some rule\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_empty_paths_does_not_borrow_a_later_keys_list(self):
        # `paths` is null; the list is `other`'s value. Delimiting the
        # `paths:` block by indentation instead of by the next top-level key
        # counts these items as its own and reports a false green.
        for sibling_list in ("  - \"engine/**/*.hpp\"", "- \"engine/**/*.hpp\""):
            with self.subTest(sibling_list=sibling_list):
                path = self.write(
                    ".claude/rules/cpp-example.md",
                    f"---\npaths:\nother:\n{sibling_list}\n---\n\n# Some rule\n")
                self.assertFalse(lint.has_paths_frontmatter(path))

    def test_paths_key_with_a_nested_mapping_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths:\n  inner: 1\n---\n\n# Some rule\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_paths_key_with_a_scalar_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths: \"engine/**/*.hpp\"\n---\n\n# Some rule\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_empty_flow_sequence_is_flagged(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths: []\n---\n\n# Some rule\n")
        self.assertFalse(lint.has_paths_frontmatter(path))

    def test_paths_key_with_items_is_clean(self):
        path = self.write(".claude/rules/cpp-example.md",
                          _SIBLING_FRONTMATTER + "\n# Some rule\n")
        self.assertTrue(lint.has_paths_frontmatter(path))

    def test_zero_indent_block_sequence_is_clean(self):
        # A block sequence at its parent key's own indentation is valid YAML,
        # so the `paths:` block is delimited by the next top-level key rather
        # than by indentation.
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths:\n- \"engine/**/*.hpp\"\n- \"a:b\"\n---\n")
        self.assertTrue(lint.has_paths_frontmatter(path))

    def test_flow_sequence_is_clean(self):
        path = self.write(".claude/rules/cpp-example.md",
                          "---\npaths: [\"engine/**/*.hpp\"]\n---\n\n# Some rule\n")
        self.assertTrue(lint.has_paths_frontmatter(path))

    def test_items_followed_by_a_sibling_key_are_clean(self):
        path = self.write(
            ".claude/rules/cpp-example.md",
            "---\npaths:\n  - \"engine/**/*.hpp\"\nglob: \"*.hpp\"\n---\n")
        self.assertTrue(lint.has_paths_frontmatter(path))

    def test_comment_between_key_and_items_is_clean(self):
        path = self.write(
            ".claude/rules/cpp-example.md",
            "---\npaths:\n  # the injection scope\n  - \"engine/**/*.hpp\"\n---\n")
        self.assertTrue(lint.has_paths_frontmatter(path))

    def test_crlf_file_with_paths_is_clean(self):
        crlf_body = _SIBLING_FRONTMATTER.replace("\n", "\r\n") + "\r\n# Some rule\r\n"
        path = self.write(".claude/rules/cpp-example.md", crlf_body)
        self.assertTrue(lint.has_paths_frontmatter(path))


class RegistrationPredicate(TmpTreeTest):
    def test_unregistered_rule_is_flagged(self):
        baseline = self.write_baseline("| Some topic | `docs/agents/BUILD.md` |\n")
        _, rows = lint.canonical_home_map_lines(baseline)
        self.assertFalse(lint.is_registered("cpp-example.md", rows))

    def test_registered_rule_is_clean(self):
        baseline = self.write_baseline(
            "| Some topic | `.claude/rules/cpp-example.md` |\n")
        _, rows = lint.canonical_home_map_lines(baseline)
        self.assertTrue(lint.is_registered("cpp-example.md", rows))

    def test_mention_outside_map_section_does_not_register(self):
        # The row-shaped mention lives in "## Next section", past the map's
        # own heading boundary — canonical_home_map_lines must not see it.
        baseline = self.write(
            "docs/agents/CLAUDE-BASELINE.md",
            "# CLAUDE-BASELINE\n\n" + _MAP_HEADER +
            "| Some topic | `docs/agents/BUILD.md` |\n"
            "\n## Next section\n"
            "\nSee `.claude/rules/cpp-example.md` for details.\n"
        )
        _, rows = lint.canonical_home_map_lines(baseline)
        self.assertFalse(lint.is_registered("cpp-example.md", rows))


class NonCppRuleIgnored(TmpTreeTest):
    def test_frontmatterless_readme_is_not_a_subject(self):
        self.write(".claude/rules/README.md", "# rules directory index\n")
        self.write_baseline()
        self.assertEqual(_run_main(self.root), 0)


class PreFixPopulation(TmpTreeTest):
    """Reconstructs the `12b8a49c8` population the issue measured: six
    `cpp-*.md` files, `cpp-globals.md` frontmatter-less, map rows for exactly
    `cpp-ecs-smells`, `cpp-math`, `cpp-systems`, `cpp-globals`, `README`."""

    def test_pre_fix_population_has_exactly_three_findings(self):
        for name in ("cpp-ecs.md", "cpp-ecs-smells.md", "cpp-lua-enums.md",
                      "cpp-math.md", "cpp-systems.md"):
            self.write(f".claude/rules/{name}", _SIBLING_FRONTMATTER + "\n# rule\n")
        # cpp-globals.md never carried paths: at 12b8a49c8.
        self.write(".claude/rules/cpp-globals.md", "# Global state\n\nbody\n")
        self.write_baseline(
            "| ECS smells | `.claude/rules/cpp-ecs-smells.md` |\n"
            "| Math | `.claude/rules/cpp-math.md` |\n"
            "| Systems | `.claude/rules/cpp-systems.md` |\n"
            "| Globals | `.claude/rules/cpp-globals.md` |\n"
            "| Sweep trap | `.claude/rules/README.md` |\n"
        )

        buf = io.StringIO()
        with redirect_stdout(buf), redirect_stderr(io.StringIO()):
            rc = lint.main(["lint_rules_registry.py", str(self.root)])
        self.assertEqual(rc, 1)

        findings = buf.getvalue().splitlines()
        self.assertEqual(len(findings), 3)
        joined = "\n".join(findings)
        self.assertIn("cpp-globals.md:1:", joined)
        self.assertIn(".claude/rules/cpp-ecs.md has no canonical-home row", joined)
        self.assertIn(".claude/rules/cpp-lua-enums.md has no canonical-home row", joined)


class CommittedTree(unittest.TestCase):
    def test_committed_rules_registry_is_green(self):
        self.assertEqual(_run_main(_FLEET_ROOT), 0)


if __name__ == "__main__":
    unittest.main()
