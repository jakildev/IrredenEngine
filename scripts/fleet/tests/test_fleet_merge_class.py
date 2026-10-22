"""Hermetic tests for fleet_merge_class.py: the auto-merge lane's path classes,
its canonical label predicate (and its drift guard against the merger ladder's
park sets), the enablement parser, and the historical 100-PR profile fixture."""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if not (ROOT / "fleet_merge_class.py").is_file():
    print("SKIP: fleet_merge_class.py subject absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(ROOT))
import fleet_merge_class as mc  # noqa: E402

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "merge_class_profile.json"
SCOUT = ROOT / "fleet-state-scout"
REBASE = ROOT / "fleet-rebase"

# The eight-row profile of the fixture's 100 PRs: the 100 most recently
# CREATED engine PRs that had merged by 2026-10-13T18:17Z (gh pr list's
# default order), created 2026-10-06..10-13 and merged on five days, the
# window the design issue describes. Its table read 8/3/2/1/12/8/1/65; the
# rebless, docs, docs-agents, claude and objective rows agree exactly, and
# the table's precedence for a test-tree CMake file and two tooling PRs
# that also touch engine/tools or a docs/agents JSON is not recorded, so
# those three rows are pinned to what this fixture and these rules read.
EXPECTED_PROFILE = {"rebless": 8, "docs": 3, "docs-agents": 2, "test": 2,
                    "fleet-tooling": 10, "claude": 8, "objective": 1, "code": 66}
REBLESS_PRS = [4373, 4346, 4326, 4302, 4268, 4236, 4212, 4203]

# Synthetic paths: shaped like the real trees, naming no tracked file.
REBLESS_SET = ["creations/demos/fixture_demo/test/references/macos-debug/zoom4_origin.png",
               "creations/demos/fixture_demo/test/references/manifest.json",
               "docs/pr-screenshots/fixture/after.png"]
DOCS_SET = ["docs/design/fixture-topic.md", "engine/fixture/README.md", "README.md"]


def load_fixture():
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


class ProfileFixture(unittest.TestCase):
    def test_fixture_shape(self):
        fx = load_fixture()
        self.assertEqual(fx["repo"], "jakildev/IrredenEngine")
        self.assertEqual(len(fx["prs"]), 100)
        self.assertTrue(all(pr["files"] for pr in fx["prs"]))
        self.assertEqual(len({pr["number"] for pr in fx["prs"]}), 100)

    def test_profile_counts(self):
        counts = {}
        members = {}
        for pr in load_fixture()["prs"]:
            row = mc.profile_class(pr["files"])
            counts[row] = counts.get(row, 0) + 1
            members.setdefault(row, []).append(pr["number"])
        self.assertEqual(counts, EXPECTED_PROFILE)
        self.assertEqual(members["rebless"], REBLESS_PRS)

    def test_only_rebless_rows_are_auto_today(self):
        kinds = {}
        for pr in load_fixture()["prs"]:
            kind = mc.classify_paths(pr["files"]).kind
            kinds[kind] = kinds.get(kind, 0) + 1
        self.assertEqual(kinds["auto:rebless"], EXPECTED_PROFILE["rebless"])
        self.assertEqual(kinds["auto:docs"], 2)
        self.assertNotIn("auto:mixed", kinds)
        self.assertEqual(kinds["human"], 90)


class ClassifyPaths(unittest.TestCase):
    def test_rebless_set(self):
        v = mc.classify_paths(REBLESS_SET)
        self.assertEqual(v.kind, "auto:rebless")
        self.assertEqual(v.classes, frozenset({"rebless"}))

    def test_docs_set(self):
        self.assertEqual(mc.classify_paths(DOCS_SET).kind, "auto:docs")

    def test_mixed_set(self):
        v = mc.classify_paths(REBLESS_SET + DOCS_SET)
        self.assertEqual(v.kind, "auto:mixed")
        self.assertEqual(v.classes, frozenset({"rebless", "docs"}))

    def test_empty_set_is_human(self):
        v = mc.classify_paths([])
        self.assertEqual(v.kind, "human")
        self.assertIn("no changed files", v.reason)

    def test_one_code_file_drops_a_rebless_set_to_human(self):
        v = mc.classify_paths(REBLESS_SET + ["engine/render/src/trixel.cpp"])
        self.assertEqual(v.kind, "human")
        self.assertIn("engine/render/src/trixel.cpp", v.reason)

    def test_docs_exclusions(self):
        for path in (".claude/commands/role-fixture.md", ".claude/skills/fixture/SKILL.md",
                     "docs/design/objectives/fixture-objective.md",
                     "docs/agents/fixture-protocol.md", "docs/agents/skills/fixture.md",
                     "engine/fixture/CLAUDE.md", "CLAUDE.md",
                     "docs/design/diagram.png", "docs/design/notes.txt"):
            with self.subTest(path=path):
                self.assertFalse(mc.is_docs_path(path))
                self.assertEqual(mc.classify_paths(DOCS_SET + [path]).kind, "human")

    def test_rebless_boundaries(self):
        for path in ("creations/demos/x/test/references/a/b.png", "docs/pr-screenshots/x/y.png",
                     "creations/editors/x/test/references/manifest.json",
                     "creations/x/test/references/macos-debug/a.png",
                     "tools/x/test/references/a.png", "test/references/a.png"):
            with self.subTest(path=path):
                self.assertTrue(mc.is_rebless_path(path))
        for path in ("creations/demos/x/test/gui_sessions/a.json", "creations/demos/x/src/a.cpp",
                     "creations/demos/x/main.cpp", "test/render/references/a.png",
                     "docs/pr-screenshots.md", "creations/x/test/references_tool.cpp",
                     "creations/x/test/fixture_root/test/references/a.png",
                     "test/catalog/fixtures/root/test/references/a.png",
                     "scripts/x/tests/fixture/test/references/a.png"):
            with self.subTest(path=path):
                self.assertFalse(mc.is_rebless_path(path))


class Grant(unittest.TestCase):
    def test_disabled_lane_grants_nothing(self):
        ok, reason = mc.grant(mc.classify_paths(REBLESS_SET), ())
        self.assertFalse(ok)
        self.assertIn("lane disabled", reason)

    def test_enabled_class_grants(self):
        ok, reason = mc.grant(mc.classify_paths(REBLESS_SET), {"rebless"})
        self.assertTrue(ok)
        self.assertEqual(reason, "auto:rebless")

    def test_docs_needs_its_own_class(self):
        ok, reason = mc.grant(mc.classify_paths(DOCS_SET), {"rebless"})
        self.assertFalse(ok)
        self.assertIn("class docs not enabled", reason)

    def test_mixed_needs_both(self):
        mixed = mc.classify_paths(REBLESS_SET + DOCS_SET)
        self.assertFalse(mc.grant(mixed, {"rebless"})[0])
        self.assertFalse(mc.grant(mixed, {"docs"})[0])
        self.assertTrue(mc.grant(mixed, {"rebless", "docs"})[0])

    def test_human_never_grants(self):
        ok, _ = mc.grant(mc.classify_paths(["engine/a.cpp"]), {"rebless", "docs"})
        self.assertFalse(ok)


NON_BLOCKING = ["fleet:human-deferred", "fleet:needs-macos-smoke", "fleet:needs-windows-smoke",
                "fleet:verified-windows", "fleet:authored-on-macos", "fleet:author-claude",
                "fleet:author-codex", "fleet:runtime-claude"]


class LabelPredicate(unittest.TestCase):
    def test_approved_alone_admits(self):
        self.assertIsNone(mc.label_block_reason(["fleet:approved"]))

    def test_non_blocking_debt_admits(self):
        self.assertIsNone(mc.label_block_reason(["fleet:approved", *NON_BLOCKING]))

    def test_missing_approval_refuses(self):
        self.assertEqual(mc.label_block_reason(NON_BLOCKING), "missing fleet:approved")
        self.assertEqual(mc.label_block_reason([]), "missing fleet:approved")

    def test_every_exact_blocker_refuses(self):
        for label in sorted(mc.BLOCKING_EXACT):
            with self.subTest(label=label):
                self.assertEqual(mc.label_block_reason(["fleet:approved", label]),
                                 f"blocked by label {label}")

    def test_every_prefix_blocker_refuses(self):
        for prefix in mc.BLOCKING_PREFIXES:
            label = prefix + "mac-pool-3"
            with self.subTest(label=label):
                self.assertEqual(mc.label_block_reason(["fleet:approved", label]),
                                 f"blocked by label {label}")

    def test_human_hold_and_human_approved_refuse(self):
        for label in ("human:hold", "human:approved", "human:owned"):
            with self.subTest(label=label):
                self.assertIsNotNone(mc.label_block_reason(["fleet:approved", label]))

    def test_auto_merged_refuses_a_second_pass(self):
        self.assertIsNotNone(mc.label_block_reason(["fleet:approved", "fleet:auto-merged"]))

    def test_covers_is_falsifiable(self):
        self.assertFalse(mc.covers("fleet:no-such-label"))
        self.assertFalse(mc.covers("fleet:no-such-prefix-"))


def scout_frozenset(name):
    text = SCOUT.read_text(encoding="utf-8")
    m = re.search(rf"^{name} = frozenset\(\{{(.*?)\}}\)", text, re.S | re.M)
    if not m:
        raise AssertionError(f"{name} not found in {SCOUT}")
    return re.findall(r'"([^"]+)"', m.group(1))


def scout_tuple(name):
    text = SCOUT.read_text(encoding="utf-8")
    m = re.search(rf"^{name} = \((.*?)\)", text, re.S | re.M)
    if not m:
        raise AssertionError(f"{name} not found in {SCOUT}")
    return re.findall(r'"([^"]+)"', m.group(1))


def rebase_skip_tokens():
    text = REBASE.read_text(encoding="utf-8")
    m = re.search(r"^SKIP_LABEL_RE='([^']+)'", text, re.M)
    if not m:
        raise AssertionError(f"SKIP_LABEL_RE not found in {REBASE}")
    return m.group(1).split("|")


class ParkSetDrift(unittest.TestCase):
    """The lane's predicate stays a superset of both existing merger park sets."""

    def test_scout_projection_skip_labels_are_covered(self):
        labels = scout_frozenset("_MERGER_PROJECTION_SKIP_LABELS")
        self.assertGreaterEqual(len(labels), 10)
        for label in labels:
            with self.subTest(label=label):
                self.assertTrue(mc.covers(label), f"{label} would not refuse an auto-merge")

    def test_scout_llm_skip_set_is_covered(self):
        for label in scout_frozenset("_MERGER_LLM_SKIP_LABELS") + scout_tuple(
                "_MERGER_LLM_SKIP_PREFIXES"):
            with self.subTest(label=label):
                self.assertTrue(mc.covers(label))

    def test_rebase_skip_re_is_covered(self):
        tokens = rebase_skip_tokens()
        self.assertGreaterEqual(len(tokens), 10)
        for token in tokens:
            with self.subTest(token=token):
                self.assertTrue(mc.covers(token), f"{token} would not refuse an auto-merge")

    def test_drift_guard_detects_a_new_park_label(self):
        """Mutation control: a label the ladder could gain tomorrow is not
        covered, so the three tests above are not tautologies."""
        self.assertFalse(mc.covers("fleet:parked-for-a-reason-not-yet-invented"))


class Enablement(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.conf = Path(self.temp.name) / "fleet-up.conf"
        self.saved = os.environ.pop("FLEET_AUTOMERGE_CLASSES", None)

    def tearDown(self):
        if self.saved is not None:
            os.environ["FLEET_AUTOMERGE_CLASSES"] = self.saved
        self.temp.cleanup()

    def read(self, text):
        self.conf.write_text(text, encoding="utf-8")
        return mc.read_enablement(self.conf)

    def test_missing_conf_enables_nothing(self):
        en = mc.read_enablement(self.conf)
        self.assertFalse(en.enabled)
        self.assertEqual(en.hold_secs, 0)
        self.assertTrue(en.problems)

    def test_unset_knob_enables_nothing(self):
        en = self.read("FLEET_RUNTIMES=claude\n")
        self.assertFalse(en.enabled)
        self.assertEqual(en.problems, ())

    def test_first_wave(self):
        en = self.read("FLEET_AUTOMERGE_CLASSES=rebless\n")
        self.assertEqual(en.classes, frozenset({"rebless"}))
        self.assertEqual(en.hold_secs, 0)

    def test_quoted_two_classes_and_hold(self):
        en = self.read('export FLEET_AUTOMERGE_CLASSES="rebless, docs"  # both waves\n'
                       "FLEET_AUTOMERGE_HOLD_SECS=1800\n")
        self.assertEqual(en.classes, frozenset({"rebless", "docs"}))
        self.assertEqual(en.hold_secs, 1800)

    def test_unknown_class_enables_nothing(self):
        en = self.read("FLEET_AUTOMERGE_CLASSES=rebless,code\n")
        self.assertFalse(en.enabled)
        self.assertIn("unknown class 'code'", en.problems[0])

    def test_malformed_hold_enables_nothing(self):
        en = self.read("FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_HOLD_SECS=soon\n")
        self.assertFalse(en.enabled)
        self.assertTrue(any("HOLD_SECS" in p for p in en.problems))

    def test_malformed_value_enables_nothing(self):
        for text in ('FLEET_AUTOMERGE_CLASSES="rebless\n',
                     "FLEET_AUTOMERGE_CLASSES='rebless\n",
                     'FLEET_AUTOMERGE_CLASSES="rebless"junk\n',
                     "FLEET_AUTOMERGE_CLASSES='rebless'junk\n",
                     'FLEET_AUTOMERGE_CLASSES="rebless"#x\n',
                     'FLEET_AUTOMERGE_CLASSES="rebless""docs"\n',
                     'FLEET_AUTOMERGE_CLASSES="rebless" junk\n',
                     "FLEET_AUTOMERGE_CLASSES=rebless rebless\n",
                     'FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_HOLD_SECS="60\n',
                     'FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_HOLD_SECS="60"0\n'):
            with self.subTest(text=text):
                en = self.read(text)
                self.assertFalse(en.enabled)
                self.assertEqual(en.classes, frozenset())
                self.assertTrue(any("malformed" in p for p in en.problems))

    def test_malformed_value_parses_to_none(self):
        self.assertIsNone(mc.parse_conf_text('FLEET_AUTOMERGE_CLASSES="rebless\n')
                          ["FLEET_AUTOMERGE_CLASSES"])
        self.assertIsNone(mc.parse_conf_text('FLEET_AUTOMERGE_CLASSES="rebless"junk\n')
                          ["FLEET_AUTOMERGE_CLASSES"])
        self.assertEqual(mc.parse_conf_text('FLEET_AUTOMERGE_CLASSES=""\n')
                         ["FLEET_AUTOMERGE_CLASSES"], "")

    @unittest.skipUnless(shutil.which("bash"), "bash not on PATH")
    def test_accepted_values_match_what_bash_sources(self):
        # The parser's admitted values are the ones bash itself would assign;
        # a glued-on suffix is where the two diverge, so it must be refused.
        for line in ('FLEET_AUTOMERGE_CLASSES=rebless',
                     'FLEET_AUTOMERGE_CLASSES="rebless, docs"  # both waves',
                     "FLEET_AUTOMERGE_CLASSES='rebless'",
                     'export FLEET_AUTOMERGE_CLASSES=rebless,docs # c',
                     'FLEET_AUTOMERGE_CLASSES=rebless#x',
                     'FLEET_AUTOMERGE_CLASSES= # empty',
                     'FLEET_AUTOMERGE_CLASSES=',
                     'FLEET_AUTOMERGE_CLASSES="rebless"junk',
                     'FLEET_AUTOMERGE_CLASSES="rebless""docs"'):
            with self.subTest(line=line):
                self.conf.write_text(line + "\n", encoding="utf-8")
                sourced = subprocess.run(
                    [shutil.which("bash") or "bash", "-c",
                     'source "$1"; printf %s "$FLEET_AUTOMERGE_CLASSES"', "_", str(self.conf)],
                    capture_output=True, text=True, check=True).stdout
                parsed = mc.parse_conf_text(line)["FLEET_AUTOMERGE_CLASSES"]
                if parsed is not None:
                    self.assertEqual(parsed, sourced)
                else:
                    self.assertNotIn(sourced, ("rebless", "rebless, docs"))

    def test_comment_and_last_assignment(self):
        en = self.read("# FLEET_AUTOMERGE_CLASSES=docs\nFLEET_AUTOMERGE_CLASSES=docs\n"
                       "FLEET_AUTOMERGE_CLASSES=rebless\n")
        self.assertEqual(en.classes, frozenset({"rebless"}))

    def test_environment_never_enables(self):
        os.environ["FLEET_AUTOMERGE_CLASSES"] = "rebless"
        self.assertFalse(mc.read_enablement(self.conf).enabled)
        self.assertFalse(self.read("FLEET_RUNTIMES=claude\n").enabled)

    def test_repos_default_to_engine(self):
        for text in ("FLEET_AUTOMERGE_CLASSES=rebless\n",
                     'FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_REPOS=""\n'):
            with self.subTest(text=text):
                self.assertEqual(self.read(text).repos, frozenset({"engine"}))

    def test_repos_opt_in(self):
        en = self.read('FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_REPOS="engine game"\n')
        self.assertEqual(en.repos, frozenset({"engine", "game"}))
        en = self.read("FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_REPOS=game\n")
        self.assertEqual(en.repos, frozenset({"game"}))

    def test_malformed_repos_enable_nothing(self):
        for text, problem in (("FLEET_AUTOMERGE_REPOS=engine,other\n", "unknown repo 'other'"),
                              ("FLEET_AUTOMERGE_REPOS=jakildev/IrredenEngine\n", "unknown repo"),
                              ('FLEET_AUTOMERGE_REPOS="engine game\n', "malformed"),
                              ("FLEET_AUTOMERGE_REPOS=engine game\n", "malformed")):
            with self.subTest(text=text):
                en = self.read("FLEET_AUTOMERGE_CLASSES=rebless\n" + text)
                self.assertFalse(en.enabled)
                self.assertEqual(en.repos, frozenset())
                self.assertTrue(any(problem in p for p in en.problems))


if __name__ == "__main__":
    unittest.main()
