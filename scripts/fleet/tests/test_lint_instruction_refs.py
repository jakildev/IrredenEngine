"""lint_instruction_refs.py: the path, label, component and command arms, the
placeholder and suppression skips, the shrink-only token baseline, and the
`--against` split into introduced and inherited findings.

Hermetic: every case builds a throwaway git repository holding its own label
catalog, state machine, label reference and sources, and points the lint at
it with `--root`; nothing reads the real repository.

A missing subject fails every case rather than exiting 3: the subject is a
new lint, and `fleet-positive-control` against the tree before it must read
as failing, not as a suite that ran nothing."""
import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

SUBJECT = Path(__file__).resolve().parents[2] / "lint_instruction_refs.py"
lint = None
if SUBJECT.is_file():
    sys.path.insert(0, str(SUBJECT.parent))
    _spec = importlib.util.spec_from_file_location("lint_instruction_refs", SUBJECT)
    lint = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(lint)

BASELINE_REL = lint.BASELINE_REL if lint else None

CATALOG = """\
#!/usr/bin/env bash
LABELS=(
    # fleet:commented-out is prose in the block, not an entry
    "fleet:queued|c5def5|Queued"
    "fleet:wip|ffffff|Work in progress"
)
"""
STATE_MACHINE = {"labels": [{"name": "human:approved"}, {"name": "fleet:needs-fix"}]}
REFERENCE = """\
# Fleet label reference

## Claims

| `fleet:claim-<host>-<agent>` | issue |

`fleet:nit-of-pr` is a reviewer convention outside the catalog. Every
`fleet:*` label is namespaced.

## Retired

`fleet:stacked` and `fleet:awaiting-base`. A straggler is inert.

## Reconcile

Nothing here.
"""


class Repo(unittest.TestCase):
    """A git repository with the label sources and a few tracked files."""

    def setUp(self):
        if lint is None:
            self.fail(f"subject absent: {SUBJECT}")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.com")
        self.git("config", "user.name", "t")
        self.write("scripts/fleet/fleet-labels", CATALOG)
        self.write("docs/agents/fleet-state-machine.json", json.dumps(STATE_MACHINE))
        self.write("docs/agents/fleet-labels-reference.md", REFERENCE)
        self.write("engine/real/real.hpp", "struct C_RealThing {};\n")
        self.write("engine/real/CMakeLists.txt", "add_library(real)\n")
        self.write("scripts/fleet/fleet-real-tool", "#!/bin/sh\n")
        self.write(".gitignore", "build/\n")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(text) if isinstance(text, str) else text)
        self.git("add", "-f", rel)

    def baseline(self, entries):
        self.write(BASELINE_REL, json.dumps(entries))

    def read_baseline(self):
        return json.loads((self.root / BASELINE_REL).read_text())

    def commit(self, msg="c"):
        self.git("add", "-A")
        self.git("commit", "-qm", msg)
        return self.git("rev-parse", "HEAD")

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = lint.main(["--root", str(self.root), *argv])
        return rc, out.getvalue(), err.getvalue()

    def finding_lines(self, out):
        return [line for line in out.splitlines() if "does not resolve" in line]


class Fires(Repo):
    def test_missing_path_label_and_component_each_yield_one_line(self):
        self.write("docs/agents/doc.md", """\
            # Doc

            See [the gone file](../../engine/gone/gone.hpp) for details.
            Apply `fleet:no-such-label` when done.
            The `C_MissingThing` component carries it.
            """)
        rc, out, err = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out), [
            "docs/agents/doc.md:3: path '../../engine/gone/gone.hpp' does not resolve",
            "docs/agents/doc.md:4: label 'fleet:no-such-label' does not resolve",
            "docs/agents/doc.md:5: component 'C_MissingThing' does not resolve",
        ])
        self.assertIn(BASELINE_REL, err)
        self.assertIn("ref-ok", err)

    def test_inline_code_path_under_a_top_level_dir(self):
        self.write("CLAUDE.md", "Read `engine/real/missing.hpp` and `engine/real/real.hpp`.\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out),
                         ["CLAUDE.md:1: path 'engine/real/missing.hpp' does not resolve"])

    def test_label_inside_a_fence_is_checked(self):
        self.write("docs/agents/doc.md", """\
            ```
            gh pr edit 1 --add-label "fleet:not-a-label"
            ```
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out),
                         ["docs/agents/doc.md:2: label 'fleet:not-a-label' does not resolve"])

    def test_retired_label_outside_its_section(self):
        self.write("docs/agents/doc.md", "Remove `fleet:stacked` first.\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out),
                         ["docs/agents/doc.md:1: label 'fleet:stacked' does not resolve"])

    def test_namespace_wide_mention_is_not_a_template(self):
        self.write("docs/agents/doc.md", "Apply `fleet:unknown-thing`.\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("label 'fleet:unknown-thing'", out)

    def test_component_named_only_in_markdown_or_the_baseline_does_not_resolve(self):
        self.write("docs/design/notes.md", "`C_OnlyInDocs` was removed.\n")
        self.baseline({"CLAUDE.md": ["C_OnlyInDocs"]})
        self.write("CLAUDE.md", "`C_OnlyInDocs` and `C_RealThing`.\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)
        self.write(BASELINE_REL, "{}")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out),
                         ["CLAUDE.md:1: component 'C_OnlyInDocs' does not resolve"])

    def test_fenced_command_in_a_nested_claude_md(self):
        self.write("engine/real/CLAUDE.md", """\
            ```
            fleet-real-tool --check
            fleet-zz-synthetic-absent-tool --go
            ```
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out), [
            "engine/real/CLAUDE.md:3: command 'fleet-zz-synthetic-absent-tool' does not resolve",
        ])

    def test_command_arm_skips_files_the_command_lint_reads(self):
        self.write("docs/agents/CLAUDE.md", """\
            ```
            fleet-zz-synthetic-absent-tool --go
            ```
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)


class Silent(Repo):
    def test_resolving_references(self):
        self.write("docs/agents/sibling.md", "x\n")
        self.write("docs/agents/doc.md", """\
            [sibling](sibling.md#anchor), [root-relative](engine/real/real.hpp),
            [dir](../../engine/real/), [web](https://example.com/x), [anchor](#top).

            `engine/real/real.hpp:12`, `engine/real/real.hpp::C_RealThing`,
            `engine/real/`, `real/real.hpp` is under engine.
            `fleet:queued`, `fleet:wip`, `human:approved`, `fleet:needs-fix`,
            `fleet:nit-of-pr`, `fleet:claim-mac-pool-1`, the `fleet:claim-` prefix.
            `C_RealThing`, `getComponent<C_RealThing>()`.
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)
        self.assertIn("ok:", out)

    def test_placeholders_and_templates(self):
        self.write("docs/agents/doc.md", """\
            `engine/<module>/CLAUDE.md`, `engine/*/CLAUDE.md`, `engine/{a,b}.hpp`,
            `engine/…/x.hpp`, `engine/.../x.hpp`, `engine/$DIR/x`, `engine/voxel:` scope.
            `fleet:claim-<host>-<agent>`, `fleet:needs-<host>-smoke`, `fleet:amending-*`.
            `C_A&`, `C_B`, `C_Foo`, `C_FooBar`, `C_Foo_declared_twice`.
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)

    def test_paths_inside_a_fence_are_not_checked(self):
        self.write("docs/agents/doc.md", """\
            ```
            cat engine/gone/gone.hpp
            ```
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)

    def test_suppression_marker(self):
        self.write("docs/agents/doc.md", """\
            <!-- lint: ref-ok engine/gone/gone.hpp -- cited as the removed file -->
            `engine/gone/gone.hpp` was removed.

            <!-- lint: ref-ok fleet:future-label -- lands with the next tool -->

            ```
            gh pr edit 1 --add-label fleet:future-label
            ```
            <!-- lint: ref-ok C_Gone -- the retired name -->
            `C_Gone`.
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)

    def test_marker_covers_only_the_next_line(self):
        self.write("docs/agents/doc.md", """\
            <!-- lint: ref-ok C_Gone -- once -->
            `C_Gone` here.
            `C_Gone` again.
            """)
        rc, out, _ = self.run_main()
        self.assertEqual(self.finding_lines(out),
                         ["docs/agents/doc.md:3: component 'C_Gone' does not resolve"])

    def test_retired_label_inside_the_retired_section(self):
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)

    def test_gitignored_path_resolves(self):
        self.write("CLAUDE.md", "Outputs land in `build/linux-debug/creations`.\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)

    def test_non_instruction_markdown_is_not_scanned(self):
        self.write("docs/design/notes.md", "`fleet:no-such-label` `engine/gone.hpp`\n")
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)


class Baseline(Repo):
    def test_listed_dead_token_passes(self):
        self.write("CLAUDE.md", "`engine/gone/gone.hpp` and `fleet:retired-x`.\n")
        self.baseline({"CLAUDE.md": ["engine/gone/gone.hpp", "fleet:retired-x"]})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 0, out)
        self.assertIn("2 accepted", out)

    def test_entry_that_now_resolves_is_stale(self):
        self.write("CLAUDE.md", "`engine/real/real.hpp`\n")
        self.baseline({"CLAUDE.md": ["engine/real/real.hpp"]})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("CLAUDE.md: baseline entry 'engine/real/real.hpp' is stale (now resolves)",
                      out)

    def test_entry_no_longer_cited_is_stale(self):
        self.write("CLAUDE.md", "nothing\n")
        self.baseline({"CLAUDE.md": ["C_Gone"], "gone/CLAUDE.md": ["C_Gone"]})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("CLAUDE.md: baseline entry 'C_Gone' is stale (no longer cited)", out)
        self.assertIn("gone/CLAUDE.md: baseline entry 'C_Gone' is stale "
                      "(file is no longer in the population)", out)

    def test_entry_for_one_token_does_not_cover_another(self):
        self.write("CLAUDE.md", "`C_Gone` `C_AlsoGone`\n")
        self.baseline({"CLAUDE.md": ["C_Gone"]})
        rc, out, _ = self.run_main()
        self.assertEqual(rc, 1)
        self.assertEqual(self.finding_lines(out),
                         ["CLAUDE.md:1: component 'C_AlsoGone' does not resolve"])

    def test_update_baseline_only_removes(self):
        self.write("CLAUDE.md", "`C_Gone` `C_New` `engine/real/real.hpp`\n")
        self.baseline({"CLAUDE.md": ["C_Gone", "engine/real/real.hpp", "C_Uncited"]})
        rc, out, _ = self.run_main("--update-baseline")
        self.assertEqual(rc, 1)
        self.assertEqual(self.read_baseline(), {"CLAUDE.md": ["C_Gone"]})
        self.assertIn("component 'C_New' does not resolve", out)

    def test_update_baseline_refuses_against(self):
        with self.assertRaises(SystemExit):
            with redirect_stderr(io.StringIO()):
                self.run_main("--update-baseline", "--against", "HEAD")


class Against(Repo):
    def test_inherited_and_introduced(self):
        self.write("CLAUDE.md", "`C_OldGone`\n")
        base = self.commit("base")
        self.write("CLAUDE.md", "`C_OldGone` `C_NewGone`\n")
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        introduced, inherited = out.split("inherited:")
        self.assertIn("component 'C_NewGone' does not resolve", introduced)
        self.assertNotIn("C_OldGone", introduced)
        self.assertIn("component 'C_OldGone' does not resolve", inherited)
        self.assertIn(base, err)

    def test_only_inherited_passes(self):
        self.write("CLAUDE.md", "`C_OldGone`\n")
        base = self.commit("base")
        self.write("CLAUDE.md", "`C_OldGone`\n\nmore text\n")
        rc, out, err = self.run_main("--against", base)
        self.assertEqual(rc, 0, out)
        self.assertIn("inherited:", out)
        self.assertIn("C_OldGone", out.split("inherited:")[1])

    def test_replacing_one_dead_reference_with_another_is_introduced(self):
        self.write("CLAUDE.md", "`C_OldGone`\n")
        base = self.commit("base")
        self.write("CLAUDE.md", "`C_NewGone`\n")
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        self.assertIn("C_NewGone", out.split("inherited:")[0])

    def test_rename_breaks_the_citation(self):
        self.write("CLAUDE.md", "`engine/real/real.hpp`\n")
        base = self.commit("base")
        self.git("mv", "engine/real/real.hpp", "engine/real/renamed.hpp")
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 1)
        self.assertIn("path 'engine/real/real.hpp' does not resolve",
                      out.split("inherited:")[0])

    def test_base_baseline_is_read_at_the_ref(self):
        self.write("CLAUDE.md", "`C_OldGone`\n")
        self.baseline({"CLAUDE.md": ["C_OldGone"]})
        base = self.commit("base")
        rc, out, _ = self.run_main("--against", base)
        self.assertEqual(rc, 0, out)
        self.assertNotIn("inherited:", out)

    def test_unreadable_ref(self):
        self.commit("base")
        rc, _, err = self.run_main("--against", "no-such-ref")
        self.assertEqual(rc, 2)
        self.assertIn("cannot measure no-such-ref", err)


if __name__ == "__main__":
    unittest.main()
