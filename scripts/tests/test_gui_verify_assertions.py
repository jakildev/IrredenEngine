"""Regression tests for GUI assertion coverage gates."""

from __future__ import annotations

import importlib.util
import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

import verify_common  # noqa: E402

_GUI_VERIFY_SPEC = importlib.util.spec_from_file_location(
    "gui_verify", SCRIPTS_DIR / "gui-verify.py"
)
if _GUI_VERIFY_SPEC is None or _GUI_VERIFY_SPEC.loader is None:
    raise RuntimeError("could not load gui-verify.py")
gui_verify = importlib.util.module_from_spec(_GUI_VERIFY_SPEC)
_GUI_VERIFY_SPEC.loader.exec_module(gui_verify)


def _assert_line(shot: int) -> str:
    return (
        f"GUI-ASSERT shot={shot} label=shot-{shot} kind=visible target=1 "
        f"name=probe-{shot} result=PASS actual=true\n"
    )


def _coverage_line(shot: int, assertion_count: int) -> str:
    return (
        f"GUI-ASSERT-COVERAGE shot={shot} label=shot-{shot} "
        f"assertions={assertion_count}\n"
    )


class ReportGuiAssertsTest(unittest.TestCase):
    def test_reports_announced_shot_with_no_assertions(self) -> None:
        output = (
            "GuiTest 1/3: first (zoom=4, cam=(0,0), yaw=0, inputs=0)\n"
            + _coverage_line(0, 1)
            + _assert_line(0)
            + "GuiTest 2/3: second (zoom=4, cam=(0,0), yaw=0, inputs=0)\n"
            + _coverage_line(1, 1)
            + _assert_line(1)
        )

        with redirect_stdout(io.StringIO()) as captured:
            assertions, hung, failures, missing = verify_common.report_gui_asserts(
                output, "[gui-verify] "
            )

        self.assertEqual(len(assertions), 2)
        self.assertFalse(hung)
        self.assertEqual(failures, [])
        self.assertEqual(missing, [2])
        self.assertIn("shots with zero GUI-ASSERT lines: 2", captured.getvalue())

    def test_complete_table_has_no_coverage_gap(self) -> None:
        output = "".join(
            f"GuiTest {shot + 1}/15: shot-{shot}\n"
            f"{_coverage_line(shot, 2)}{_assert_line(shot)}{_assert_line(shot)}"
            for shot in range(15)
        )

        with redirect_stdout(io.StringIO()) as captured:
            assertions, _, failures, missing = verify_common.report_gui_asserts(output)

        self.assertEqual(len(assertions), 30)
        self.assertEqual(failures, [])
        self.assertEqual(missing, [])
        self.assertIn("30/30 assertions passed", captured.getvalue())

    def test_assertion_free_editor_shots_are_covered(self) -> None:
        assertion_counts = {
            shot: 2 if shot < 7 else 1
            for shot in range(4, 15)
        }
        output = "".join(
            f"GuiTest {shot + 1}/17: editor-{shot}\n"
            + "".join(_assert_line(shot)
                      for _ in range(assertion_counts.get(shot, 0)))
            + _coverage_line(shot, assertion_counts.get(shot, 0))
            for shot in range(17)
        )

        assertions = verify_common.parse_gui_asserts(output)

        self.assertEqual(len(assertions), 14)
        self.assertEqual(
            verify_common.missing_gui_assert_shots(output, assertions), []
        )

    def test_truncated_table_reports_zero_based_missing_shot(self) -> None:
        output = "GuiTest 1/2: first\n" + _assert_line(0)

        assertions = verify_common.parse_gui_asserts(output)

        self.assertEqual(
            verify_common.missing_gui_assert_shots(output, assertions), [1]
        )


class GuiVerifyEmptyPolicyTest(unittest.TestCase):
    def run_main(self, *args: str, output: str = "") -> tuple[str, int | None]:
        argv = ["gui-verify.py", "IRShapeDebug", "--no-build", *args]
        with patch.object(sys, "argv", argv), \
                patch.object(verify_common, "run_capture", return_value=(0, output)), \
                redirect_stdout(io.StringIO()) as captured:
            try:
                gui_verify.main()
            except SystemExit as exc:
                return captured.getvalue(), exc.code
        return captured.getvalue(), None

    def test_zero_assertions_fail_by_default(self) -> None:
        output, exit_code = self.run_main()

        self.assertEqual(exit_code, 1)
        self.assertIn("no GUI-ASSERT lines found", output)
        self.assertIn("use --allow-no-assertions", output)

    def test_zero_assertions_can_be_explicitly_allowed(self) -> None:
        output, exit_code = self.run_main("--allow-no-assertions")

        self.assertIsNone(exit_code)
        self.assertIn("allowed by --allow-no-assertions", output)

    def test_opt_out_does_not_hide_an_announced_shot_gap(self) -> None:
        output, exit_code = self.run_main(
            "--allow-no-assertions",
            output="GuiTest 1/2: exits-before-assert\n",
        )

        self.assertEqual(exit_code, 1)
        self.assertIn("shots with zero GUI-ASSERT lines: 0, 1", output)


if __name__ == "__main__":
    unittest.main()
