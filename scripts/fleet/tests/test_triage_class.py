"""fleet_triage_class: one positive and one refusal per graduated class,
direction stays recommend-only, the `## Triage` comment parser and decide(),
and the protocol and authority-table docs naming every class."""

import sys
import unittest
from pathlib import Path

SUBJECT = Path(__file__).resolve().parents[1] / "fleet_triage_class.py"
if not SUBJECT.is_file():
    print("SKIP: scripts/fleet/fleet_triage_class.py absent", file=sys.stderr)
    sys.exit(3)
REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SUBJECT.parent))
import fleet_triage_class as tc  # noqa: E402

FULL = {
    "rebless": {"measurement": "14/14 rows match, max delta 0, PSNR inf on macos-debug",
                "host": "macos", "confinement": "one framebuffer pixel (criterion 2)"},
    "docs-capture": {"ruling": "https://example.invalid/issues/1#issuecomment-2",
                     "grep": "rg 'ruling text' docs/ reports 0 hits on master"},
    "plan-residual": {"plan": "https://example.invalid/issues/3#issuecomment-4",
                      "decision": "D11: the radius bake is a residual"},
    "verification-run": {"criteria": "render-verify --target IRPerfGrid exits 0",
                         "host": "windows", "no_plan": "fleet:no-plan"},
    "objective-row": {"objective": "docs/design/objectives/zz-fixture-objective.md",
                      "row": "- [ ] The gui_scale space mismatch is reconciled",
                      "filer": "architect"},
}

COMMENT = """## Triage

**Verdict:** recommend-approve
**Class:** rebless
**Objective:** (none — defect-shaped)
**Suggested model:** sonnet
**Bar:**
- measurement: 14/14 rows match, max delta 0 on macos-debug
- host: macos
- confinement: one framebuffer pixel (criterion 2)
**Basis:** PR #4303 merged; the six rows moved by whole pixels.
Second basis line.
"""


class Bars(unittest.TestCase):
    def test_every_class_passes_with_its_full_bar(self):
        self.assertEqual(set(FULL), set(tc.GRADUATED))
        for name, bar in FULL.items():
            with self.subTest(cls=name):
                ok, reason = tc.check(name, bar)
                self.assertTrue(ok, reason)
                self.assertIn(name, reason)

    def test_every_class_refuses_with_one_bar_line_missing(self):
        for name, bar in FULL.items():
            for key in bar:
                with self.subTest(cls=name, missing=key):
                    short = {k: ("" if k == key else v) for k, v in bar.items()}
                    ok, reason = tc.check(name, short)
                    self.assertFalse(ok)
                    self.assertIn(f"missing {key}", reason)
                    del short[key]
                    self.assertFalse(tc.check(name, short)[0])

    def test_direction_unknown_and_shapeless_bars_refuse(self):
        ok, reason = tc.check(tc.DIRECTION, FULL["rebless"])
        self.assertFalse(ok)
        self.assertIn("recommend-only", reason)
        ok, reason = tc.check("perf", FULL["rebless"])
        self.assertFalse(ok)
        self.assertIn("unknown class", reason)
        for bar in (None, [], "text"):
            self.assertFalse(tc.check("rebless", bar)[0])

    def test_filer_and_no_plan_values_are_judged(self):
        bad_filer = dict(FULL["objective-row"], filer="worker")
        ok, reason = tc.check("objective-row", bad_filer)
        self.assertFalse(ok)
        self.assertIn("filer", reason)
        self.assertTrue(tc.check("objective-row", dict(bad_filer, filer="Steward"))[0])
        for value in ("no", "false", "0", "human:no-plan", "yes", "fleet:no-plan-ish",
                      "fleet:no-plan if the owner agrees"):
            with self.subTest(no_plan=value):
                ok, reason = tc.check("verification-run",
                                      dict(FULL["verification-run"], no_plan=value))
                self.assertFalse(ok)
                self.assertIn("fleet:no-plan", reason)
        for value in ("`fleet:no-plan`", " FLEET:NO-PLAN ", "[fleet:no-plan]"):
            with self.subTest(no_plan=value):
                self.assertTrue(tc.check("verification-run",
                                         dict(FULL["verification-run"], no_plan=value))[0])


class Labels(unittest.TestCase):
    def test_label_set_and_model_and_class_guards(self):
        self.assertEqual(tc.labels_for("sonnet", "rebless"),
                         ["fleet:agent-approved", "fleet:sonnet"])
        self.assertEqual(tc.labels_for("opus", "verification-run"),
                         ["fleet:agent-approved", "fleet:opus", "fleet:no-plan"])
        with self.assertRaises(ValueError):
            tc.labels_for("gpt", "rebless")
        for name in (tc.DIRECTION, "perf"):
            with self.assertRaises(ValueError):
                tc.labels_for("sonnet", name)

    def test_only_verification_run_writes_no_plan(self):
        for name in tc.GRADUATED:
            with self.subTest(cls=name):
                self.assertEqual("fleet:no-plan" in tc.labels_for("sonnet", name),
                                 name == "verification-run")

    def test_state_labels_are_every_fleet_and_human_label(self):
        names = ["bug", "human:owned", "area:render", "fleet:needs-human", "fleetish"]
        self.assertEqual(tc.state_labels(names), ["fleet:needs-human", "human:owned"])
        self.assertEqual(tc.state_labels(iter(["enhancement"])), [])


class Comments(unittest.TestCase):
    def test_parse_reads_every_field_and_the_bar_block(self):
        fields = tc.parse_triage_comment(COMMENT)
        self.assertEqual(fields["verdict"], "recommend-approve")
        self.assertEqual(fields["class"], "rebless")
        self.assertEqual(fields["model"], "sonnet")
        self.assertEqual(fields["objective"], "(none — defect-shaped)")
        self.assertEqual(fields["basis"], "PR #4303 merged; the six rows moved by whole pixels.")
        self.assertEqual(fields["bar"], {
            "measurement": "14/14 rows match, max delta 0 on macos-debug",
            "host": "macos",
            "confinement": "one framebuffer pixel (criterion 2)"})

    def test_triage_heading_is_a_heading_line_not_a_mention(self):
        for text in (COMMENT, "Repro.\n\n## Triage\n", "   ## triage\n", "##\tTriage"):
            with self.subTest(text=text):
                self.assertTrue(tc.has_triage_heading(text))
        for text in ("posts a `## Triage` comment", "### Triage", "## Triaged",
                     "    ## Triage", "", None):
            with self.subTest(text=text):
                self.assertFalse(tc.has_triage_heading(text))

    def test_decide_stamps_only_a_complete_graduated_approve(self):
        labels, reason = tc.decide(tc.parse_triage_comment(COMMENT))
        self.assertEqual(labels, ["fleet:agent-approved", "fleet:sonnet"])
        self.assertIn("rebless", reason)
        verification = COMMENT.replace("**Class:** rebless", "**Class:** Verification-Run").replace(
            "- measurement: 14/14 rows match, max delta 0 on macos-debug\n- host: macos\n"
            "- confinement: one framebuffer pixel (criterion 2)\n",
            "- criteria: render-verify --target IRPerfGrid exits 0\n- host: windows\n"
            "- no_plan: fleet:no-plan\n")
        labels, reason = tc.decide(tc.parse_triage_comment(verification))
        self.assertEqual(labels, ["fleet:agent-approved", "fleet:sonnet", "fleet:no-plan"])
        self.assertIn("verification-run", reason)
        labels, reason = tc.decide(tc.parse_triage_comment(
            verification.replace("- no_plan: fleet:no-plan", "- no_plan: human:no-plan")))
        self.assertEqual(labels, ["fleet:triage-recommend"])
        self.assertIn("human:no-plan", reason)
        # The shared field grammar: a decorated name or the plain **Model:** key.
        for text in (COMMENT.replace("**Suggested model:** sonnet", "**Suggested model:** `Opus`"),
                     COMMENT.replace("**Suggested model:** sonnet", "**Model:** [fable]")):
            labels, _ = tc.decide(tc.parse_triage_comment(text))
            self.assertEqual(labels[0], "fleet:agent-approved", text)
        labels, _ = tc.decide(tc.parse_triage_comment(
            COMMENT.replace("**Verdict:** recommend-approve", "**Verdict:** `Recommend-Approve`")))
        self.assertEqual(labels[0], "fleet:agent-approved")
        self.assertEqual(tc.model_of("`sonnet`"), "sonnet")
        self.assertIsNone(tc.model_of("sonnet (n/a)"))
        cases = (
            (COMMENT.replace("**Class:** rebless", "**Class:** direction"), "recommend-only"),
            (COMMENT.replace("- host: macos\n", ""), "missing host"),
            (COMMENT.replace("**Verdict:** recommend-approve", "**Verdict:** park"),
             "not exactly recommend-approve"),
            (COMMENT.replace("**Verdict:** recommend-approve",
                             "**Verdict:** recommend-approve-extra"),
             "not exactly recommend-approve"),
            (COMMENT.replace("**Verdict:** recommend-approve", "**Verdict:** recommend-approved"),
             "not exactly recommend-approve"),
            (COMMENT.replace("**Suggested model:** sonnet", "**Suggested model:** (n/a)"),
             "suggested model"),
            (COMMENT.replace("**Class:** rebless\n", ""), "recommend-only"),
            (COMMENT.replace("**Suggested model:** sonnet", "**Suggested model:** sonnet, maybe"),
             "suggested model"),
            (COMMENT.replace("**Suggested model:** sonnet", "**Suggested model:** opus | sonnet"),
             "suggested model"),
        )
        for text, expected in cases:
            with self.subTest(expected=expected):
                labels, reason = tc.decide(tc.parse_triage_comment(text))
                self.assertEqual(labels, ["fleet:triage-recommend"])
                self.assertIn(expected, reason)


class Docs(unittest.TestCase):
    def test_protocol_and_authority_table_name_every_class(self):
        protocol = (REPO / "docs/agents/triage-protocol.md").read_text(encoding="utf-8")
        graduation = protocol.split("## Graduation", 1)[1]
        unblock = (REPO / "docs/agents/skills/fleet-unblock.md").read_text(encoding="utf-8")
        for name in tc.GRADUATED:
            with self.subTest(cls=name):
                self.assertIn(f"`{name}`", graduation)
                self.assertIn(f"`{name}`", unblock)
        self.assertNotIn("(not in effect)", graduation.splitlines()[0])

    def test_role_wrapper_carries_every_class_row(self):
        wrapper = (REPO / ".claude/commands/role-triage.md").read_text(encoding="utf-8")
        table = wrapper.split("## Graduated classes", 1)[1]
        rows = {line.split("|")[1].strip(): line for line in table.splitlines()
                if line.startswith("| `")}
        self.assertEqual(set(rows), {f"`{name}`" for name in tc.GRADUATED})
        for name in tc.GRADUATED:
            with self.subTest(cls=name):
                row = rows[f"`{name}`"]
                for key in tc.CLASSES[name][0]:
                    self.assertIn(f"`{key}`", row)
                for label in tc.CLASS_LABELS.get(name, ()):
                    self.assertIn(f"`{label}`", row)
                self.assertEqual(row.count(tc.NO_PLAN), len(tc.CLASS_LABELS.get(name, ())))


if __name__ == "__main__":
    unittest.main(verbosity=2)
