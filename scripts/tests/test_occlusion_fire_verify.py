"""Tests for occlusion-fire-verify.py — the per-voxel occlusion cull's
positive-fire gate.

The identity gate (render-verify on perf_grid) passes trivially for a cull
that never fires, so this script must fail when the chunk-only and
chunk+per-voxel arms report the same average visible count. These tests feed
synthetic ``profile_report.txt`` texts through the REAL parser
(``scripts/perf/compare_perf_runs.py::parse_report``) and pin:

  * the marginal is chunk-only minus chunk+per-voxel, read from the ``Visible``
    avg column of the ``--- Voxel cull stats ---`` block;
  * a marginal at or above ``--min-marginal`` PASSES (exit 0), and one below —
    including the equal-avg case, marginal 0 — FAILS (exit 1);
  * a report with no cull block (samples 0) is rejected, never read as a
    real zero;
  * each arm's run command carries the fixture, the arm's cull toggles, and
    any ``--demo-arg`` extras, in that order.

No build, no demo launch: ``main`` is driven with ``_run_arm`` stubbed to
write the synthetic reports. Import the dashed-name subject via importlib,
matching test_render_verify.py.

Positive control: change ``marginal < min_marginal`` in ``verdict`` to
``<=`` and ``test_marginal_at_threshold_passes`` fails; drop the
``samples == 0`` guard in ``marginal_reduction`` and
``test_report_without_cull_block_is_rejected`` fails.
"""
import importlib.machinery
import importlib.util
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_ofv = _load("occlusion_fire_verify", "occlusion-fire-verify.py")


def _report(avg_visible: float, samples: int = 59) -> str:
    """A profile_report.txt fragment in the engine's cull-block layout."""
    return (
        "=== PROFILE REPORT ===\n"
        "Frame time: avg=3.10ms p50=3.00ms p95=3.50ms p99=4.00ms min=2.90ms max=5.00ms\n"
        "\n"
        "--- Voxel cull stats ---\n"
        "                        Avg            Max    Samples\n"
        f"Visible             {avg_visible:>7.1f}          23816         {samples}\n"
        f"Total              262144.0         262144         {samples}\n"
        f"AxisEntries             0.0              0         {samples}\n"
        f"Feeder                  0.0              0         {samples}\n"
        "Ratio:               0.0488 (unique retained candidates / pool slots)\n"
        "=== END REPORT ===\n"
    )


NO_CULL_REPORT = (
    "=== PROFILE REPORT ===\n"
    "Frame time: avg=3.10ms p50=3.00ms p95=3.50ms p99=4.00ms min=2.90ms max=5.00ms\n"
    "=== END REPORT ===\n"
)


class MarginalReduction(unittest.TestCase):
    def _measure(self, chunk_only_text: str, chunk_pv_text: str):
        with tempfile.TemporaryDirectory() as td:
            a = Path(td) / "chunk_only.txt"
            b = Path(td) / "chunk_pv.txt"
            a.write_text(chunk_only_text)
            b.write_text(chunk_pv_text)
            return _ofv.marginal_reduction(a, b)

    def test_marginal_is_chunk_only_minus_chunk_per_voxel(self):
        chunk_only, chunk_pv, marginal = self._measure(
            _report(23816.0), _report(12781.3))
        self.assertEqual(chunk_only, 23816.0)
        self.assertEqual(chunk_pv, 12781.3)
        self.assertAlmostEqual(marginal, 11034.7)

    def test_equal_arms_read_zero_marginal(self):
        _, _, marginal = self._measure(_report(23816.0), _report(23816.0))
        self.assertEqual(marginal, 0.0)

    def test_report_without_cull_block_is_rejected(self):
        with self.assertRaises(SystemExit):
            self._measure(NO_CULL_REPORT, _report(12781.3))
        with self.assertRaises(SystemExit):
            self._measure(_report(23816.0), NO_CULL_REPORT)


class Verdict(unittest.TestCase):
    def _verdict(self, chunk_only, chunk_pv, min_marginal=1.0) -> bool:
        with redirect_stdout(io.StringIO()):
            return _ofv.verdict(chunk_only, chunk_pv, chunk_only - chunk_pv,
                                min_marginal)

    def test_positive_marginal_passes(self):
        self.assertTrue(self._verdict(23816.0, 12781.3))

    def test_zero_marginal_fails(self):
        self.assertFalse(self._verdict(23816.0, 23816.0))

    def test_negative_marginal_fails(self):
        self.assertFalse(self._verdict(12781.3, 23816.0))

    def test_marginal_at_threshold_passes(self):
        self.assertTrue(self._verdict(1001.0, 1.0, min_marginal=1000.0))

    def test_marginal_below_threshold_fails(self):
        self.assertFalse(self._verdict(1000.0, 1.0, min_marginal=1000.0))


class ArmArgs(unittest.TestCase):
    def test_chunk_only_arm_adds_both_toggles(self):
        self.assertEqual(
            _ofv.arm_args(_ofv.ARM_CHUNK_ONLY, []),
            _ofv.FIXTURE_ARGS + ["--occlusion-cull", "--no-per-voxel-occlusion"])

    def test_chunk_per_voxel_arm_adds_cull_only(self):
        self.assertEqual(
            _ofv.arm_args(_ofv.ARM_CHUNK_PER_VOXEL, []),
            _ofv.FIXTURE_ARGS + ["--occlusion-cull"])

    def test_extras_append_last_on_both_arms(self):
        for arm in (_ofv.ARM_CHUNK_ONLY, _ofv.ARM_CHUNK_PER_VOXEL):
            with self.subTest(arm=arm):
                self.assertEqual(
                    _ofv.arm_args(arm, ["--no-per-voxel-occlusion"])[-1],
                    "--no-per-voxel-occlusion")


class MainExitCode(unittest.TestCase):
    """End-to-end through ``main`` with the demo runs replaced by canned reports."""

    def _run_main(self, reports: dict[str, str], argv: list[str]) -> int:
        def fake_run_arm(*, arm, worktree, report, dest, frames, timeout, extra):
            dest.write_text(reports[arm])

        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "build" / "creations" / "demos" / "perf_grid" / "IRPerfGrid"
            exe.parent.mkdir(parents=True)
            exe.touch()
            with patch.object(_ofv, "_run_arm", side_effect=fake_run_arm), \
                    patch.object(_ofv.verify_common, "detect_worktree_root",
                                 return_value=Path(td)), \
                    patch.object(_ofv.verify_common, "find_exe", return_value=exe), \
                    redirect_stdout(io.StringIO()):
                return _ofv.main(["--no-build"] + argv)

    def test_firing_cull_exits_zero(self):
        rc = self._run_main({_ofv.ARM_CHUNK_ONLY: _report(23816.0),
                             _ofv.ARM_CHUNK_PER_VOXEL: _report(12781.3)}, [])
        self.assertEqual(rc, 0)

    def test_silent_cull_exits_one(self):
        rc = self._run_main({_ofv.ARM_CHUNK_ONLY: _report(23816.0),
                             _ofv.ARM_CHUNK_PER_VOXEL: _report(23816.0)}, [])
        self.assertEqual(rc, 1)

    def test_min_marginal_raises_the_bar(self):
        reports = {_ofv.ARM_CHUNK_ONLY: _report(23816.0),
                   _ofv.ARM_CHUNK_PER_VOXEL: _report(12781.3)}
        self.assertEqual(self._run_main(reports, ["--min-marginal", "11000"]), 0)
        self.assertEqual(self._run_main(reports, ["--min-marginal", "12000"]), 1)


if __name__ == "__main__":
    unittest.main()
