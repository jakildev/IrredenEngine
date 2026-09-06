"""Tests for pivot-verify.py's per-pass gating — every pass can fail (#2851).

#2648 removed the `focus-ctr` SDF twin's only gate rather than bounding it to
the destination-grid floor its evidence measures, leaving the twin the single
pass in the harness that could not fail: a 200px silhouette drift on it exited
0. These tests lock the replacement — a floor-aware bound in game px
(`SDF_BOUND_GAME_PX`) — plus the loud-classification guard that closes the same
hole for any future unclassified block.

Both boundaries are stubbed so the code under test is the shipped ``main()``,
not a re-implementation: ``verify_common.run_pass`` stands in for the
``fleet-run`` capture, ``_score_pass`` for the ``jitter_probe`` comparison, and
``_output_scale_factor`` for the captured PNG's HiDPI ratio. That last stub is
what lets one suite exercise BOTH host classes (2x macOS, 1x Windows/Linux) —
the bound is stated in game px precisely so the two agree.

No engine, no GL/Metal build, no captures.
"""
import importlib.machinery
import importlib.util
import io
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

_SCRIPTS = Path(__file__).resolve().parent.parent
# pivot-verify.py does a bare `import verify_common` (#2461), which resolves
# only if scripts/ is on sys.path. Without this the suite dies at import when
# run on its own, and passes only when an alphabetically-earlier sibling in
# this directory happens to insert the path first (#2825).
sys.path.insert(0, str(_SCRIPTS))

import verify_common  # noqa: E402


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


pv = _load("pivot_verify", "pivot-verify.py")

# One `[pivot-focus-assert]` line per shot, all at the same latched derive so
# _score_focus_asserts' moved-value check is not what decides the verdict.
_ASSERT_LINE = ("[pivot-focus-assert] probe derived=(4.0,4.0,2.5) "
                "analytic=(4.0,4.0,2.5) view_held=1 result={result}")


class _Harness:
    """Stubbed capture + score boundaries for one ``main()`` invocation.

    ``readings`` maps ``(block, sdf)`` to the deviation px ``_score_pass``
    should report; ``default_reading`` covers everything else.
    """

    def __init__(self, readings=None, default_reading=0.5, scale=2.0,
                 focus_result="PASS"):
        self.readings = readings or {}
        self.default_reading = default_reading
        self.scale = scale
        self.focus_result = focus_result
        self.captures = []          # (block, sdf, zoom) per run_pass call
        self.scores = []            # (block, sdf, zoom, max_deviation, dev)
        self._current = None

    def run_pass(self, cmd, cwd=None, shots_dir=None, timeout=None):
        block = cmd[cmd.index("--pivot-verify") + 1]
        sdf = "--pivot-verify-sdf" in cmd
        zoom = float(cmd[cmd.index("--zoom") + 1])
        self._current = (block, sdf, zoom)
        self.captures.append(self._current)
        output = "\n".join(_ASSERT_LINE.format(result=self.focus_result)
                           for _ in range(9))
        frames = [Path(f"shot_{i:03d}.png") for i in range(9)]
        return 0, output, frames

    def score_pass(self, probe_exe, frames, max_deviation):
        block, sdf, zoom = self._current
        dev = self.readings.get((block, sdf), self.default_reading)
        self.scores.append((block, sdf, zoom, max_deviation, dev))
        verdict = "PINNED" if dev <= max_deviation else "DRIFT"
        return verdict, dev, dev, ""

    def output_scale_factor(self, frame, config):
        return self.scale

    def bound_for(self, block, sdf):
        """The max_deviation ``main()`` actually handed _score_pass."""
        for b, s, _, max_dev, _dev in self.scores:
            if (b, s) == (block, sdf):
                return max_dev
        raise AssertionError(f"no scored pass for {(block, sdf)}")


def _run(harness, argv):
    """Drive ``main(argv)`` against ``harness``; return (rc, verdicts)."""
    def _no_build(*a, **k):
        raise AssertionError("verify_common.run called despite --no-build")

    out = io.StringIO()
    with patch.object(verify_common, "run_pass", harness.run_pass), \
            patch.object(verify_common, "run", _no_build), \
            patch.object(verify_common, "find_exe",
                         lambda *a, **k: Path("jitter_probe")), \
            patch.object(verify_common, "detect_worktree_root",
                         lambda *a, **k: Path(".")), \
            patch.object(pv, "_score_pass", harness.score_pass), \
            patch.object(pv, "_output_scale_factor",
                         harness.output_scale_factor), \
            redirect_stdout(out), redirect_stderr(io.StringIO()):
        rc = pv.main(argv + ["--no-build"])
    return rc, _verdicts(out.getvalue())


def _verdicts(stdout):
    """Parse the results table into ``{pass-label: verdict}``."""
    verdicts = {}
    for line in stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and "@z" in fields[0]:
            verdicts[fields[0]] = fields[1]
    return verdicts


class SdfTwinGate(unittest.TestCase):
    """The twin is gated, at a bound that spares its measured floor."""

    def test_t1_twin_drift_fails_the_run(self):
        # The issue's FIRING arm: identical inputs exited 0 before #2851.
        h = _Harness(readings={("focus-ctr", True): 200.0,
                               ("focus-ctr", False): 0.5})
        rc, verdicts = _run(h, ["--blocks", "focus-ctr"])
        self.assertEqual(verdicts["focus-ctr-sdf@z4"], "DRIFT")
        self.assertEqual(verdicts["focus-ctr@z4"], "PINNED")
        self.assertEqual(rc, 1)

    def test_t2_measured_floor_still_passes_on_both_host_classes(self):
        # 2.00 framebuffer px on a 2x host and 1.00 on a 1x one are the SAME
        # one game pixel; a game-px bound has to clear both.
        for scale, floor in ((2.0, 2.0), (1.0, 1.0)):
            with self.subTest(output_scale_factor=scale):
                h = _Harness(readings={("focus-ctr", True): floor,
                                       ("focus-ctr", False): 0.5},
                             scale=scale)
                rc, verdicts = _run(h, ["--blocks", "focus-ctr"])
                self.assertEqual(verdicts["focus-ctr-sdf@z4"], "PINNED")
                self.assertEqual(rc, 0)

    def test_t3_twin_is_scored_against_its_own_scaled_bound(self):
        # Straddle SDF_BOUND_GAME_PX on both host classes. The 2.6px reading
        # passing at scale 2 and failing at scale 1 is what proves the bound
        # is scaled per host rather than a flat framebuffer-px constant; the
        # 2.4px reading passing at all proves it is not the 1.5px default.
        cases = [(2.0, 4.9, 0), (2.0, 5.1, 1), (1.0, 2.4, 0), (1.0, 2.6, 1),
                 (2.0, 2.6, 0)]
        for scale, dev, expected_rc in cases:
            with self.subTest(output_scale_factor=scale, dev=dev):
                h = _Harness(readings={("focus-ctr", True): dev,
                                       ("focus-ctr", False): 0.5},
                             scale=scale)
                rc, _ = _run(h, ["--blocks", "focus-ctr"])
                self.assertEqual(rc, expected_rc)

    def test_t3b_bound_is_the_floor_plus_the_default_budget(self):
        h = _Harness(scale=2.0)
        _run(h, ["--blocks", "focus-ctr"])
        self.assertAlmostEqual(h.bound_for("focus-ctr", True),
                               2.0 * pv.SDF_BOUND_GAME_PX)
        # The voxel arm of the same block keeps the generic default.
        self.assertAlmostEqual(h.bound_for("focus-ctr", False), 1.5)


class CensusEveryPassCanFail(unittest.TestCase):

    def test_t4_maximally_bad_reading_fails_all_eight_passes(self):
        # 7 blocks + the focus-ctr twin, every silhouette 200px off and every
        # [pivot-focus-assert] FAIL. The issue's census: 7/8 gated before,
        # 8/8 after.
        h = _Harness(default_reading=200.0, focus_result="FAIL")
        rc, verdicts = _run(h, [])
        self.assertEqual(len(verdicts), 8)
        self.assertEqual(rc, 1)
        self.assertEqual(verdicts, {
            "focus-ctr@z4": "DRIFT",
            "focus-ctr-sdf@z4": "DRIFT",
            "focus-off@z4": "DRIFT",
            "center-column@z4": "FOCUS-BAD",
            "center-depth@z4": "FOCUS-BAD",
            "background-center@z4": "FOCUS-BAD",
            "center-axis@z4": "FOCUS-BAD",
            "cursor-latch@z4": "FOCUS-BAD",
        })
        self.assertNotIn("REPORT", set(verdicts.values()))


class LoudClassification(unittest.TestCase):
    """A block no oracle covers must fail before any capture runs (#2851 §4)."""

    def _expect_systemexit_before_capture(self, argv):
        h = _Harness()
        with self.assertRaises(SystemExit) as ctx:
            _run(h, argv)
        self.assertEqual(h.captures, [], "captures ran before the guard fired")
        return str(ctx.exception)

    def test_t5_unclassified_block_exits_before_capture(self):
        with patch.object(pv, "ALL_BLOCKS", pv.ALL_BLOCKS + ["rogue-block"]):
            message = self._expect_systemexit_before_capture([])
        self.assertIn("rogue-block", message)
        self.assertIn("CENTROID_GATED_BLOCKS", message)
        self.assertIn("FOCUS_ASSERT_BLOCKS", message)

    def test_t5b_sdf_block_outside_the_centroid_gate_exits_before_capture(self):
        # center-column is classified (FOCUS_ASSERT_BLOCKS) but not
        # centroid-gated, and an SDF twin runs no focus oracle — so its twin
        # would reach the verdict lookup ungated.
        with patch.object(pv, "SDF_BLOCKS", ["focus-ctr", "center-column"]):
            message = self._expect_systemexit_before_capture([])
        self.assertIn("center-column", message)
        self.assertIn("CENTROID_GATED_BLOCKS", message)

    def test_unknown_block_still_rejected(self):
        message = self._expect_systemexit_before_capture(
            ["--blocks", "not-a-block"])
        self.assertIn("not-a-block", message)


class PerBlockBoundSurvives(unittest.TestCase):
    """#2758's zoom-scaled center-axis bound must not regress to the default."""

    def test_t6_center_axis_keeps_its_zoom_scaled_bound(self):
        # 6.0 game px sits above the 1.5px default and below center-axis's own
        # z4 bound (7.0 at scale 1, 14.0 at scale 2). A keep-both merge that
        # sent the voxel arm back to args.max_deviation turns this DRIFT.
        for scale in (1.0, 2.0):
            with self.subTest(output_scale_factor=scale):
                h = _Harness(readings={("center-axis", False): 6.0},
                             scale=scale)
                rc, verdicts = _run(h, ["--blocks", "center-axis"])
                self.assertEqual(verdicts["center-axis@z4"], "PINNED")
                self.assertEqual(rc, 0)
                px_per_zoom, floor_px = pv.CENTROID_BOUND_GAME_PX["center-axis"]
                self.assertAlmostEqual(h.bound_for("center-axis", False),
                                       scale * (px_per_zoom * 4.0 + floor_px))

    def test_t6b_center_axis_still_fails_above_its_bound(self):
        h = _Harness(readings={("center-axis", False): 40.0}, scale=1.0)
        rc, verdicts = _run(h, ["--blocks", "center-axis"])
        self.assertEqual(verdicts["center-axis@z4"], "DRIFT")
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
