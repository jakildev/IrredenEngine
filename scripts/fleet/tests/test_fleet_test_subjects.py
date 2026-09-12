"""Unit + integration tests for fleet_test_subjects.py.

The module under test is the scan behind `OUT_OF_TREE_SUBJECTS`: an inclusion
list's complement is invisible to both a green run and a positive control
computed from the list, so completeness has to come from a population derived
independently of it.

What each group proves:

  ScanText          the F1 grammar — path boundaries, longest match, and the
                    two guards (no bare non-root basename, no substring inside
                    a longer filename). Each guard case is paired with a
                    reference scanner that LACKS the guard, so the case fails
                    if the guard is ever removed rather than passing vacuously.
  Coverage          the entry-covers-subject relation: exact, segment-bounded
                    `**`, and the refusal of a bare catch-all, which would
                    satisfy the ratchet by widening the filter to everything.
  WorkflowParsing   both-blocks predicate + `paths:` extraction, bounded to
                    the list rather than the whole `on:` sub-block.
  LiteralCompleteness  end-to-end over a synthetic indexed repo: double
                    omission fails, each registration layer alone still fails,
                    completing both passes; untracked / lookalike / in-tree
                    files stay out of the population.
  DerivedCompleteness  a fixture workflow declaring both blocks is discovered
                    with no registry edit; filters-only still fails.
  TreeWidth         `_SCAN_ROOT` vs the two filter blocks, per event, with an
                    ancestor glob accepted and a descendant glob refused.
  SetupFailures     missing metadata / unreadable input / empty discovery are
                    exit 2, never a pass — and the `--manifest` seam a
                    `git archive` stage (no .git) has to use.
  CommittedTree     the real checkout passes, and reports nonzero coverage
                    plus the subjects the registry is known to carry.

stdlib-only; every fixture lives under a TemporaryDirectory and the synthetic
trees are real `git init` repos so `tracked_paths` is exercised rather than
stubbed. Nothing here reads live fleet or GitHub state.
"""
import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import fleet_test_subjects as fts

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent

_LINTER_BODY = '#!/usr/bin/env python3\n_SCAN_ROOT = "scripts/"\n'


def _committed_tracked():
    """Tracked paths for the real-tree case, via the module's manifest seam.

    `fleet-positive-control` stages with `git archive`, so the stage has no
    .git and `git ls-files` cannot answer there. A control exports
    FLEET_TEST_SUBJECTS_MANIFEST pinned to the control commit; unpinned, this
    reads the checkout's own index. Never a silent fallback between the two —
    borrowing the caller's index would describe a different tree than the one
    staged, and the control would calibrate against the wrong thing.
    """
    manifest = os.environ.get(fts._MANIFEST_ENV)
    if manifest:
        return fts.read_manifest(manifest)
    return fts.tracked_paths(_REPO_ROOT)


def _run_main(argv_tail):
    """main() over a synthetic fixture, with output captured; (rc, stdout+stderr).

    The manifest env seam is cleared for the call. It exists for the real-tree
    case, and a positive control exports it for the whole process — inherited
    here it would answer a synthetic fixture's "is there an index?" question
    with a manifest pinned to an entirely different tree, turning the
    missing-metadata case into a silent pass.
    """
    out, err = io.StringIO(), io.StringIO()
    env = {k: v for k, v in os.environ.items() if k != fts._MANIFEST_ENV}
    with mock.patch.dict(os.environ, env, clear=True), \
            redirect_stdout(out), redirect_stderr(err):
        rc = fts.main(["fleet_test_subjects.py", *argv_tail])
    return rc, out.getvalue() + err.getvalue()


def _shortest_match_scan(text, tracked):
    """Reference scanner WITHOUT the longest-match guard.

    Used only to prove the guard cases are non-vacuous: every assertion that
    depends on longest-match is paired with one showing this variant gets the
    answer wrong. If the guard is dropped from the module, the paired
    assertions stop disagreeing and the test fails.
    """
    hits = set()
    for match in fts._PATH_RUN_RE.finditer(text):
        run = match.group(0)
        for start in [0] + [i + 1 for i, ch in enumerate(run) if ch == "/"]:
            piece = run[start:]
            if piece in tracked:
                hits.add(piece)
    return hits


class ScanText(unittest.TestCase):
    def test_bare_repo_relative_path(self):
        tracked = {"cmake/run_clang_format_changed.cmake"}
        self.assertEqual(
            fts.scan_text("covers cmake/run_clang_format_changed.cmake here", tracked),
            tracked,
        )

    def test_shell_variable_prefix_is_stripped(self):
        tracked = {"cmake/ir_quality_tools.cmake"}
        self.assertEqual(
            fts.scan_text('QUALITY_CMAKE="$REPO_ROOT/cmake/ir_quality_tools.cmake"', tracked),
            tracked,
        )

    def test_root_dotfile_after_a_variable_prefix(self):
        tracked = {".clang-format"}
        self.assertEqual(
            fts.scan_text('STYLE_FILE="$SCRIPT_DIR/.clang-format"', tracked), tracked,
        )

    def test_dot_dot_prefixed_spelling(self):
        tracked = {"cmake/run_header_checks_standalone.cmake"}
        self.assertEqual(
            fts.scan_text('"$(dirname "$0")/../../../cmake/run_header_checks_standalone.cmake"',
                          tracked),
            tracked,
        )

    def test_path_ending_a_prose_sentence(self):
        tracked = {"docs/design/skill-sharing.md"}
        self.assertEqual(
            fts.scan_text("# see docs/design/skill-sharing.md.\n", tracked), tracked,
        )

    def test_arbitrary_directory_names_and_extensions(self):
        tracked = {"weird-dir/sub_dir/thing.xyz", "engine/tools/bin/ir-run"}
        self.assertEqual(
            fts.scan_text("weird-dir/sub_dir/thing.xyz engine/tools/bin/ir-run", tracked),
            tracked,
        )

    def test_non_root_basename_alone_is_not_a_match(self):
        # The guard: a bare basename is not a complete repo-relative path.
        tracked = {"cmake/ir_quality_tools.cmake"}
        self.assertEqual(fts.scan_text("parses ir_quality_tools.cmake", tracked), set())

    def test_substring_inside_a_longer_filename_is_not_a_match(self):
        tracked = {"ruff.toml", "cmake/x.cmake"}
        self.assertEqual(fts.scan_text("myruff.toml and cmake/x.cmake.in", tracked), set())

    def test_longest_match_wins_over_a_root_basename(self):
        # `scripts/fleet/CLAUDE.md` must not be read as the repo-root CLAUDE.md.
        tracked = {"CLAUDE.md", "scripts/fleet/CLAUDE.md"}
        text = "# dual-spelling rule (scripts/fleet/CLAUDE.md)\n"
        self.assertEqual(fts.scan_text(text, tracked), {"scripts/fleet/CLAUDE.md"})
        # Non-vacuity: a scanner without the guard reports the wrong subject.
        self.assertIn("CLAUDE.md", _shortest_match_scan(text, tracked))

    def test_longest_match_wins_over_a_shorter_tracked_prefix_path(self):
        tracked = {"creations/CLAUDE.md", "CLAUDE.md"}
        text = 'cat > "$REPO/creations/CLAUDE.md" <<EOF\n'
        self.assertEqual(fts.scan_text(text, tracked), {"creations/CLAUDE.md"})
        self.assertEqual(
            _shortest_match_scan(text, tracked), {"creations/CLAUDE.md", "CLAUDE.md"},
        )

    def test_untracked_lookalike_is_ignored(self):
        self.assertEqual(fts.scan_text("cmake/not_tracked.cmake", {"ruff.toml"}), set())


class StripSubjectArray(unittest.TestCase):
    def test_declaration_is_removed_and_the_rest_kept(self):
        text = (
            "# header mentions ruff.toml\n"
            "OUT_OF_TREE_SUBJECTS=(\n"
            "    'cmake/only_here.cmake'\n"
            ")\n"
            "echo docs/agents/FLEET.md\n"
        )
        stripped = fts.strip_subject_array(text)
        self.assertNotIn("cmake/only_here.cmake", stripped)
        self.assertIn("ruff.toml", stripped)
        self.assertIn("docs/agents/FLEET.md", stripped)

    def test_absent_declaration_leaves_text_unchanged(self):
        self.assertEqual(fts.strip_subject_array("echo hi\n"), "echo hi\n")


class Coverage(unittest.TestCase):
    def test_exact_entry_covers(self):
        self.assertTrue(fts.covers("ruff.toml", "ruff.toml"))

    def test_recursive_glob_covers_descendants_and_itself(self):
        self.assertTrue(fts.covers("docs/agents/**", "docs/agents/FLEET.md"))
        self.assertTrue(fts.covers("docs/agents/**", "docs/agents/**"))

    def test_recursive_glob_is_segment_bounded(self):
        self.assertFalse(fts.covers("docs/agents/**", "docs/agentsX/y.md"))

    def test_sibling_subtree_glob_does_not_cover(self):
        self.assertFalse(fts.covers("scripts/fleet/**", "scripts/dev/x.sh"))

    def test_catch_all_entry_covers_nothing(self):
        for entry in ("**", "*", "**/*"):
            self.assertFalse(fts.covers(entry, "ruff.toml"), entry)

    def test_a_longer_entry_containing_the_subject_is_not_coverage(self):
        # Coverage is path-structural, not textual: a substring test would
        # read 'creations/CLAUDE.md' as coverage for 'CLAUDE.md'.
        self.assertFalse(fts.covers("creations/CLAUDE.md", "CLAUDE.md"))
        self.assertEqual(fts.uncovered(["CLAUDE.md"], ["creations/CLAUDE.md"]), ["CLAUDE.md"])

    def test_uncovered_reports_only_the_gaps(self):
        self.assertEqual(
            fts.uncovered(["a.md", "docs/agents/x.md", "b.md"], ["docs/agents/**", "a.md"]),
            ["b.md"],
        )

    def test_scan_root_accepts_its_own_and_an_ancestor_glob(self):
        self.assertTrue(fts.covers_scan_root(["scripts/**"], "scripts/"))
        self.assertTrue(fts.covers_scan_root(["scripts/**"], "scripts/dev/"))

    def test_scan_root_refuses_a_descendant_glob_or_a_file_list(self):
        self.assertFalse(fts.covers_scan_root(["scripts/fleet/**"], "scripts/"))
        self.assertFalse(fts.covers_scan_root(["scripts/fleet/foo.py"], "scripts/"))

    def test_scan_root_refuses_a_catch_all(self):
        self.assertFalse(fts.covers_scan_root(["**"], "scripts/"))


class WorkflowParsing(unittest.TestCase):
    YAML = (
        "on:\n"
        "  push:\n"
        "    branches: [master]\n"
        "    paths:\n"
        "      - 'scripts/**'\n"
        "      - 'ruff.toml'\n"
        "  pull_request:\n"
        "    paths:\n"
        "      - 'scripts/**'\n"
        "  workflow_dispatch:\n"
        "\n"
        "permissions:\n"
        "  contents: read\n"
    )

    def test_paths_list_per_block(self):
        self.assertEqual(fts.paths_list(self.YAML, "push"), ["scripts/**", "ruff.toml"])
        self.assertEqual(fts.paths_list(self.YAML, "pull_request"), ["scripts/**"])

    def test_permissions_subkeys_are_not_read_as_workflow_content(self):
        # perf-gate.yml's shape: pull_request: runs straight into a top-level
        # key whose own sub-keys are two-space indented.
        yaml = (
            "on:\n"
            "  pull_request:\n"
            "    paths:\n"
            "      - 'engine/**'\n"
            "\n"
            "permissions:\n"
            "  contents: write\n"
        )
        self.assertEqual(fts.paths_list(yaml, "pull_request"), ["engine/**"])
        self.assertEqual(fts.paths_list(yaml, "push"), [])

    def test_both_blocks_predicate(self):
        self.assertTrue(fts.declares_both_blocks(self.YAML))
        self.assertFalse(fts.declares_both_blocks("on:\n  push:\n    branches: [master]\n"))


# ----------------------------------------------------------------------
# Synthetic-tree integration
# ----------------------------------------------------------------------
def _workflow_yaml(push_entries, pr_entries):
    lines = ["on:", "  push:", "    branches: [master]", "    paths:"]
    lines += [f"      - '{e}'" for e in push_entries]
    lines += ["  pull_request:", "    paths:"]
    lines += [f"      - '{e}'" for e in pr_entries]
    lines += ["  workflow_dispatch:", ""]
    return "\n".join(lines)


def _registry_suite(subjects, body=""):
    return (
        "#!/usr/bin/env bash\n"
        "# fixture registry suite\n"
        "OUT_OF_TREE_SUBJECTS=(\n"
        + "".join(f"    '{s}'\n" for s in subjects)
        + ")\n"
        + body
    )


class _TreeCase(unittest.TestCase):
    """A synthetic indexed repo carrying the four inputs the module reads."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.write("scripts/fleet/lint_python_registry.py", _LINTER_BODY)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path

    def index(self):
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.email", "t@example.com"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.name", "t"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)

    def register(self, subjects, push=None, pr=None):
        """Write the registry suite and fleet-tests.yml with the given lists."""
        self.write("scripts/fleet/tests/test_fleet_tests_workflow_paths.sh",
                   _registry_suite(subjects))
        base = ["scripts/**"]
        self.write(".github/workflows/fleet-tests.yml",
                   _workflow_yaml(base + (subjects if push is None else push),
                                  base + (subjects if pr is None else pr)))

    def findings(self):
        return fts.check(self.root, fts.tracked_paths(self.root))[0]

    def assertFindings(self, expected_prefixes):
        got = self.findings()
        self.assertEqual(len(got), len(expected_prefixes), got)
        for finding, prefix in zip(sorted(got), sorted(expected_prefixes)):
            self.assertTrue(finding.startswith(prefix), f"{finding!r} !~ {prefix!r}")


class LiteralCompleteness(_TreeCase):
    SUBJECT = "engine/tools/lib/concurrency_helpers.sh"

    def setUp(self):
        super().setUp()
        self.write(self.SUBJECT, "# helper\n")
        self.write(".clang-format", "ColumnLimit: 100\n")
        self.write("CMakePresets.json", "{}\n")
        self.write("ruff.toml", "target-version = \"py310\"\n")
        # Two sources, one of each extension, using two reference spellings.
        self.write("scripts/fleet/tests/test_build_dir.sh",
                   f'HELPER="$REPO_ROOT/{self.SUBJECT}"\nSTYLE="$SCRIPT_DIR/.clang-format"\n')
        self.write("scripts/fleet/tests/test_presets.py",
                   '"""reads CMakePresets.json and ruff.toml."""\n')
        # fleet-tests.yml is the derived population's only member here;
        # registering it keeps F2 quiet so each case isolates its own check.
        self.base = [".clang-format", "CMakePresets.json", "ruff.toml",
                     ".github/workflows/fleet-tests.yml"]

    def test_double_omission_is_reported_for_the_literal(self):
        self.register(self.base)
        self.index()
        self.assertFindings([f"F1 {self.SUBJECT}"])

    def test_registering_only_the_filter_still_fails_completeness(self):
        # The subject is in both paths: blocks but not in the array.
        self.register(self.base,
                      push=[*self.base, self.SUBJECT],
                      pr=[*self.base, self.SUBJECT])
        self.index()
        self.assertFindings([f"F1 {self.SUBJECT}"])

    def test_registering_only_the_array_fails_the_filter_check(self):
        self.register([*self.base, self.SUBJECT],
                      push=self.base, pr=self.base)
        self.index()
        self.assertFindings([f"F3 push {self.SUBJECT}", f"F3 pull_request {self.SUBJECT}"])

    def test_completing_both_layers_passes(self):
        self.register([*self.base, self.SUBJECT])
        self.index()
        self.assertEqual(self.findings(), [])

    def test_a_recursive_glob_entry_covers_the_subject(self):
        self.register([*self.base, "engine/**"])
        self.index()
        self.assertEqual(self.findings(), [])

    def test_provenance_names_the_referencing_suite(self):
        self.register(self.base)
        self.index()
        self.assertIn("test_build_dir.sh", self.findings()[0])

    def test_untracked_file_is_outside_the_population(self):
        self.register([*self.base, self.SUBJECT])
        self.index()
        self.write("engine/tools/lib/scratch.sh", "# scratch\n")
        self.write("scripts/fleet/tests/test_scratch.sh",
                   'S="$REPO_ROOT/engine/tools/lib/scratch.sh"\n')
        self.assertEqual(self.findings(), [])

    def test_in_tree_file_is_outside_the_population(self):
        self.write("scripts/fleet/fleet-thing", "#!/usr/bin/env bash\n")
        self.write("scripts/fleet/tests/test_thing.sh",
                   'T="$REPO_ROOT/scripts/fleet/fleet-thing"\n')
        self.register([*self.base, self.SUBJECT])
        self.index()
        self.assertEqual(self.findings(), [])

    def test_a_scripts_file_outside_scripts_fleet_is_in_the_population(self):
        # Width: scripts/dev/ is out-of-tree for this scan even though the
        # filter's scripts/** glob covers it, so the array must carry it too.
        self.write("scripts/dev/gen.sh", "# gen\n")
        self.write("scripts/fleet/tests/test_gen.sh", 'G="$REPO_ROOT/scripts/dev/gen.sh"\n')
        self.register([*self.base, self.SUBJECT])
        self.index()
        self.assertFindings(["F1 scripts/dev/gen.sh"])
        self.register([*self.base, self.SUBJECT,
                       "scripts/dev/gen.sh"])
        self.index()
        self.assertEqual(self.findings(), [])

    def test_the_array_declaration_is_not_evidence_for_its_own_members(self):
        # A subject registered but referenced NOWHERE else is not "discovered"
        # — otherwise every registry row would discover itself and F1 could
        # never fire.
        self.register([*self.base, self.SUBJECT,
                       "docs/design/ghost.md"])
        self.write("docs/design/ghost.md", "# ghost\n")
        self.index()
        tracked = fts.tracked_paths(self.root)
        self.assertNotIn("docs/design/ghost.md", fts.discover_literal_subjects(self.root, tracked))

    def test_other_references_in_the_registry_suite_are_still_scanned(self):
        self.write("docs/design/cited.md", "# cited\n")
        self.write("scripts/fleet/tests/test_fleet_tests_workflow_paths.sh",
                   _registry_suite([".clang-format", "CMakePresets.json", "ruff.toml",
                                    self.SUBJECT],
                                   body="# see docs/design/cited.md for why\n"))
        self.write(".github/workflows/fleet-tests.yml",
                   _workflow_yaml(["scripts/**"], ["scripts/**"]))
        self.index()
        tracked = fts.tracked_paths(self.root)
        self.assertIn("docs/design/cited.md", fts.discover_literal_subjects(self.root, tracked))


class DerivedCompleteness(_TreeCase):
    def setUp(self):
        super().setUp()
        self.write("ruff.toml", "target-version = \"py310\"\n")
        self.write("scripts/fleet/tests/test_reg.py", '"""reads ruff.toml."""\n')

    def add_fixture_workflow(self):
        self.write(".github/workflows/new-gate.yml",
                   _workflow_yaml(["engine/**"], ["engine/**"]))

    def test_a_new_both_blocks_workflow_is_discovered_with_no_registry_edit(self):
        self.register(["ruff.toml", ".github/workflows/fleet-tests.yml"])
        self.add_fixture_workflow()
        self.index()
        self.assertIn(".github/workflows/new-gate.yml", fts.covered_workflows(self.root))

    def test_filters_only_registration_still_fails_registry_completeness(self):
        subjects = ["ruff.toml", ".github/workflows/fleet-tests.yml"]
        self.register(subjects,
                      push=subjects + [".github/workflows/new-gate.yml"],
                      pr=subjects + [".github/workflows/new-gate.yml"])
        self.add_fixture_workflow()
        self.index()
        self.assertFindings(["F2 .github/workflows/new-gate.yml"])

    def test_completing_both_layers_passes(self):
        self.register(["ruff.toml", ".github/workflows/fleet-tests.yml",
                       ".github/workflows/new-gate.yml"])
        self.add_fixture_workflow()
        self.index()
        self.assertEqual(self.findings(), [])

    def test_removing_a_workflow_from_both_layers_fails(self):
        # fleet-tests.yml itself dropped from the array AND (via register) from
        # both paths: blocks. No suite source names it here, so F1 is silent by
        # construction — F2 is the only thing that can see a derived subject.
        self.register(["ruff.toml"])
        self.index()
        self.assertFindings(["F2 .github/workflows/fleet-tests.yml"])

    def test_a_single_block_workflow_is_out_of_the_derived_population(self):
        self.register(["ruff.toml", ".github/workflows/fleet-tests.yml"])
        self.write(".github/workflows/half.yml",
                   "on:\n  push:\n    paths:\n      - 'engine/**'\n")
        self.index()
        self.assertNotIn(".github/workflows/half.yml", fts.covered_workflows(self.root))


class TreeWidth(_TreeCase):
    def setUp(self):
        super().setUp()
        self.write("ruff.toml", "target-version = \"py310\"\n")
        self.write("scripts/fleet/tests/test_reg.py", '"""reads ruff.toml."""\n')
        self.subjects = ["ruff.toml", ".github/workflows/fleet-tests.yml"]

    def narrow(self, push_root, pr_root):
        self.write("scripts/fleet/tests/test_fleet_tests_workflow_paths.sh",
                   _registry_suite(self.subjects))
        self.write(".github/workflows/fleet-tests.yml",
                   _workflow_yaml([push_root] + self.subjects, [pr_root] + self.subjects))

    def test_root_glob_in_both_blocks_passes(self):
        self.narrow("scripts/**", "scripts/**")
        self.index()
        self.assertEqual(self.findings(), [])

    def test_narrowing_push_only_is_reported_for_push_only(self):
        self.narrow("scripts/fleet/**", "scripts/**")
        self.index()
        self.assertFindings(["F4 push"])

    def test_narrowing_pull_request_only_is_reported_for_that_block_only(self):
        self.narrow("scripts/**", "scripts/fleet/**")
        self.index()
        self.assertFindings(["F4 pull_request"])

    def test_narrowing_both_is_reported_for_both(self):
        self.narrow("scripts/fleet/**", "scripts/fleet/**")
        self.index()
        self.assertFindings(["F4 push", "F4 pull_request"])

    def test_an_ancestor_glob_covers_the_scan_root(self):
        self.write("scripts/fleet/lint_python_registry.py",
                   '#!/usr/bin/env python3\n_SCAN_ROOT = "scripts/dev/"\n')
        self.narrow("scripts/**", "scripts/**")
        self.index()
        self.assertEqual(self.findings(), [])


class SetupFailures(_TreeCase):
    def setUp(self):
        super().setUp()
        self.write("ruff.toml", "target-version = \"py310\"\n")
        self.write("scripts/fleet/tests/test_reg.py", '"""reads ruff.toml."""\n')
        self.register(["ruff.toml", ".github/workflows/fleet-tests.yml"])

    def test_missing_git_metadata_is_exit_2_not_a_pass(self):
        rc, out = _run_main([str(self.root)])
        self.assertEqual(rc, 2)
        self.assertIn("--manifest", out)

    def test_manifest_seam_stands_in_for_the_index(self):
        manifest = self.root / "manifest.txt"
        rels = sorted(
            str(p.relative_to(self.root))
            for p in self.root.rglob("*") if p.is_file() and p.name != "manifest.txt"
        )
        manifest.write_text("\n".join(rels) + "\n", encoding="utf-8")
        rc, out = _run_main([str(self.root), "--manifest", str(manifest)])
        self.assertEqual(rc, 0, out)

    def test_empty_manifest_is_a_setup_error(self):
        manifest = self.root / "manifest.txt"
        manifest.write_text("\n", encoding="utf-8")
        rc, _ = _run_main([str(self.root), "--manifest", str(manifest)])
        self.assertEqual(rc, 2)

    def test_missing_manifest_file_is_a_setup_error(self):
        rc, _ = _run_main([str(self.root), "--manifest", str(self.root / "nope.txt")])
        self.assertEqual(rc, 2)

    def test_unreadable_required_input_is_a_setup_error(self):
        (self.root / ".github/workflows/fleet-tests.yml").unlink()
        self.index()
        with self.assertRaises(fts.SetupError):
            fts.check(self.root, fts.tracked_paths(self.root))

    def test_empty_discovery_is_a_setup_error(self):
        # A tree whose suites reference nothing outside scripts/fleet/ is a
        # broken scan, not a clean pass.
        for stray in ("ruff.toml", "scripts/fleet/tests/test_reg.py"):
            (self.root / stray).unlink()
        self.write("scripts/fleet/tests/test_nothing.sh", "echo hi\n")
        self.index()
        with self.assertRaises(fts.SetupError):
            fts.check(self.root, fts.tracked_paths(self.root))

    def test_missing_subject_array_is_a_setup_error(self):
        self.write("scripts/fleet/tests/test_fleet_tests_workflow_paths.sh", "echo hi\n")
        self.index()
        with self.assertRaises(fts.SetupError):
            fts.check(self.root, fts.tracked_paths(self.root))


class CommittedTree(unittest.TestCase):
    """End-to-end acceptance against the real checkout."""

    @classmethod
    def setUpClass(cls):
        cls.tracked = _committed_tracked()
        cls.literals = fts.discover_literal_subjects(_REPO_ROOT, cls.tracked)
        cls.findings, cls.stats = fts.check(_REPO_ROOT, cls.tracked)

    def test_the_committed_registry_is_complete(self):
        self.assertEqual(self.findings, [])

    def test_coverage_is_nonzero(self):
        # A zero-finding run over zero sources is not evidence of anything; the
        # counts are what make a green result reportable.
        for key in ("sources", "shell", "python", "tracked", "literals", "workflows"):
            self.assertGreater(self.stats[key], 0, key)
        self.assertEqual(self.stats["shell"] + self.stats["python"], self.stats["sources"])

    def test_the_three_reported_gaps_are_discovered(self):
        # One subject per discovery path: a shell suite's fixed literal, a
        # python suite's fixed literal, and a member of the derived workflow
        # set.
        for path in ("engine/tools/lib/concurrency_helpers.sh",
                     "ruff.toml",
                     ".github/workflows/fleet-tests.yml"):
            self.assertIn(path, self.literals, path)

    def test_the_three_unreported_gaps_are_discovered(self):
        # Subjects no hand-maintained list carries; only the scan reaches them.
        for path in (".clang-format",
                     "cmake/ir_quality_tools.cmake",
                     "cmake/run_header_checks_standalone.cmake"):
            self.assertIn(path, self.literals, path)

    def test_the_derived_workflow_population_includes_fleet_tests_itself(self):
        self.assertIn(".github/workflows/fleet-tests.yml", fts.covered_workflows(_REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
