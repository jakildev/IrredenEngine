"""Yaw crossing metric: receiver masks, the rotated-pair bound, and CLI gates."""

import importlib.machinery
import importlib.util
import json
import math
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-yaw-crossing-metric.py"
loader = importlib.machinery.SourceFileLoader("yaw_crossing_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("yaw_crossing_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

WIDTH = 40
HEIGHT = 4
LIT = metric.LIT_COLOR
SHADOW = metric.SHADOW_COLOR
OTHER = (200, 10, 10)
LIMITS = ("--min-shadow-px", "8", "--min-receiver-px", "100")
# Game-px columns of the fixture shadow band at offset k: [600, 620 + k), inside
# the receiver top at every yaw the default step reaches.
BAND_GAME_COLUMNS = (600, 620)

STUB_FLEET_RUN = """#!{python}
import re, sys
sys.path.insert(0, {tests!r})
from test_render_yaw_crossing_metric import fixture_row, rmu
args = sys.argv[1:]
config = open(args[args.index("--config-preset") + 1]).read()
shots = re.search(r'screenshot_output_dir="([^"]+)"', config).group(1)
yaw = float(next(a for a in args if a.startswith("--yaw=")).split("=")[1])
offset = round(((yaw + 3.141592653589793) % 6.283185307179586 - 3.141592653589793)
               / {step!r})
rmu.write_png(shots + "/screenshot_0.png", {width}, {height},
              bytes(fixture_row(1, offset)) * {height}, 3)
route = {route}
if route:
    print("[info] SURFACE-ROUTE cardinal=1 perAxis=0 fog=" + route + " fogComposed=0")
    print("[info] SURFACE-ROUTE cardinal=0 perAxis=1 fog=" + route + " fogComposed=1")
print("ir-run: RESULT=CLEAN")
"""
# The stub's logged route as an expression over its argv: the fog pass exists
# only when the fixture arguments carry the reveal radius.
ROUTE_FOLLOWS_THE_FLAG = (
    '"compose" if "--fog-reveal" in args and args[args.index("--fog-reveal") + 1] == "16"'
    ' else "none"')


def fixture_row(scale, offset):
    """One frame row at output ``scale``: lit receiver everywhere, shadowed over
    the band whose right edge moves one game px per yaw step."""
    first = BAND_GAME_COLUMNS[0] * scale
    last = (BAND_GAME_COLUMNS[1] + offset) * scale
    row = bytearray()
    for column in range(metric.GAME_SIZE[0] * scale):
        row.extend(SHADOW if first <= column < last else LIT)
    return row


class YawCrossingMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_frame(self, offset, shadow_columns, other_columns=(), bpp=3):
        """Every row: lit receiver, shadowed in ``shadow_columns``, foreign in ``other_columns``."""
        row = bytearray()
        for column in range(WIDTH):
            color = (OTHER if column in other_columns
                     else SHADOW if column in shadow_columns else LIT)
            row.extend(color)
            if bpp == 4:
                row.append(7)
        rmu.write_png(str(self.root / metric.frame_name(offset)), WIDTH, HEIGHT,
                      bytes(row) * HEIGHT, bpp)

    def write_edges(self, edges, **kwargs):
        """Shadow spans columns [10, edge) in the frame at each offset."""
        for offset, edge in zip(metric.OFFSETS, edges):
            self.write_frame(offset, range(10, edge), **kwargs)

    def run_cli(self, *args):
        """Grades the whole synthetic frame; the receiver-top region needs the fixture's size."""
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root), "--region", "frame", *args],
            capture_output=True, text=True)
        lines = result.stdout.splitlines()
        return result.returncode, json.loads(lines[0]), lines[1:]

    def test_smoothly_moving_edge_passes(self):
        self.write_edges([20, 21, 22, 23, 24])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)
        self.assertEqual(verdict, ["RESULT=PASS"])
        self.assertEqual(result["cardinal_differing_px"], HEIGHT)
        self.assertEqual(result["rotated_differing_px"], HEIGHT)

    def test_cardinal_frame_on_another_model_fails(self):
        self.write_edges([20, 20, 26, 20, 20])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertEqual(verdict, ["RESULT=FAIL"])
        self.assertEqual(result["rotated_differing_px"], 0)
        self.assertEqual(result["cardinal_differing_px"], 6 * HEIGHT)
        self.assertTrue(result["coverage_valid"])

    def test_one_sided_jump_fails(self):
        self.write_edges([20, 20, 20, 25, 25])
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertEqual(result["pairs"]["cardinal_before"]["differing_px"], 0)
        self.assertEqual(result["pairs"]["cardinal_after"]["differing_px"], 5 * HEIGHT)

    def test_pixels_off_the_receiver_in_either_frame_are_not_compared(self):
        for offset in metric.OFFSETS:
            self.write_frame(offset, range(10, 20),
                             other_columns=range(18, 24) if offset == 0 else ())
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["cardinal_differing_px"], 0)
        self.assertEqual(result["pairs"]["cardinal_before"]["common_px"],
                         (WIDTH - 6) * HEIGHT)

    def test_alpha_is_ignored(self):
        self.write_edges([20, 21, 22, 23, 24], bpp=4)
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)

    def test_missing_shadow_is_not_a_pass(self):
        self.write_edges([10, 10, 10, 10, 10])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertFalse(result["coverage_valid"])
        self.assertEqual(verdict, ["RESULT=FAIL"])

    def test_default_coverage_rejects_a_tiny_fixture(self):
        self.write_edges([20, 21, 22, 23, 24])
        code, result, _ = self.run_cli()
        self.assertEqual(code, 1, result)
        self.assertFalse(result["coverage_valid"])

    def test_missing_frame_is_unreadable(self):
        self.write_edges([20, 21, 22, 23, 24])
        (self.root / metric.frame_name(2)).unlink()
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("yaw_+2.png", json.loads(result.stdout)["error"])

    def test_receiver_region_rejects_a_frame_of_another_size(self):
        self.write_edges([20, 21, 22, 23, 24])
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("integer multiple of 1280x720", json.loads(result.stdout)["error"])

    def grade_fixture(self, scale):
        width, height = (side * scale for side in metric.GAME_SIZE)
        for offset in metric.OFFSETS:
            rmu.write_png(str(self.root / metric.frame_name(offset)), width, height,
                          bytes(fixture_row(scale, offset)) * height, 3)
        result = subprocess.run([sys.executable, str(SCRIPT), "grade", str(self.root)],
                                capture_output=True, text=True)
        lines = result.stdout.splitlines()
        return result.returncode, json.loads(lines[0])

    def test_receiver_region_and_floors_scale_with_the_output(self):
        readings = {}
        for scale in (1, 2):
            code, readings[scale] = self.grade_fixture(scale)
            self.assertEqual(code, 0, readings[scale])
            self.assertEqual(readings[scale]["output_scale"], scale)
            self.assertTrue(readings[scale]["coverage_valid"])
        one, two = readings[1], readings[2]
        self.assertEqual((two["min_shadow_px"], two["min_receiver_px"]),
                         (4 * one["min_shadow_px"], 4 * one["min_receiver_px"]))
        self.assertAlmostEqual(two["region_px"] / one["region_px"], 4.0, delta=0.05)
        self.assertGreater(one["cardinal_differing_px"], 0)
        self.assertEqual(one["cardinal_differing_px"], one["rotated_differing_px"])

    def run_capture(self, route, *args):
        """``capture`` against a stub fleet-run whose log follows ``route``, an
        expression over the stub's argv."""
        stub = self.root / "fleet-run"
        stub.write_text(STUB_FLEET_RUN.format(
            python=sys.executable, tests=str(Path(__file__).resolve().parent),
            step=math.radians(metric.DEFAULT_STEP_DEG),
            width=metric.GAME_SIZE[0], height=metric.GAME_SIZE[1], route=route),
            encoding="utf-8")
        stub.chmod(0o755)
        output = self.root / "capture"
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "capture", "--output", str(output), *args],
            capture_output=True, text=True,
            env={**os.environ, "FLEET_RUN": str(stub)})
        return result, output

    def test_capture_grades_a_scale_one_host(self):
        """The capture path end to end against a 1x host's 1280x720 frames."""
        result, output = self.run_capture(ROUTE_FOLLOWS_THE_FLAG)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        reading = json.loads(result.stdout.splitlines()[-2])
        self.assertEqual(reading["size"], list(metric.GAME_SIZE))
        self.assertEqual(reading["output_scale"], 1)
        self.assertTrue(reading["pass"])
        self.assertEqual(set(reading["fog_routes"].values()), {"none"})
        for offset in metric.OFFSETS:
            self.assertTrue((output / metric.frame_name(offset)).is_file())

    def test_fog_arm_passes_the_radius_and_records_each_route(self):
        result, output = self.run_capture(ROUTE_FOLLOWS_THE_FLAG, "--fog-reveal", "16")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        reading = json.loads(result.stdout.splitlines()[-2])
        self.assertEqual(reading["fog_routes"],
                         {f"{offset:+d}": "compose" for offset in metric.OFFSETS})
        regrade = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(output)],
            capture_output=True, text=True)
        self.assertEqual(json.loads(regrade.stdout.splitlines()[-2])["fog_routes"],
                         reading["fog_routes"])

    def test_fog_arm_rejects_a_run_without_a_fog_pass(self):
        for route, logged in (('"none"', "fog=none"), ("None", "no SURFACE-ROUTE line")):
            with self.subTest(route=route):
                result, _ = self.run_capture(route, "--fog-reveal", "16")
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn("RESULT=", result.stdout)
                self.assertIn(logged, json.loads(result.stdout.splitlines()[-1])["error"])

    def test_fog_free_capture_needs_no_route_log(self):
        result, _ = self.run_capture("None")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(set(json.loads(result.stdout.splitlines()[-2])["fog_routes"].values()),
                         {None})

    def test_fog_route_is_the_last_one_logged(self):
        log = ("SURFACE-ROUTE cardinal=0 perAxis=0 fog=fallback fogComposed=0\n"
               "noise fog=compose\n"
               "SURFACE-ROUTE cardinal=0 perAxis=1 fog=compose fogComposed=1\n")
        self.assertEqual(metric.logged_fog_route(log), "compose")
        self.assertIsNone(metric.logged_fog_route("ir-run: RESULT=CLEAN\n"))

    def test_yaws_wrap_below_the_cardinal(self):
        self.assertAlmostEqual(metric.yaw_radians(0.0, 0.25, -1), math.radians(359.75))
        self.assertAlmostEqual(metric.yaw_radians(0.0, 0.25, 2), math.radians(0.5))
        self.assertAlmostEqual(metric.yaw_radians(270.0, 0.25, 0), math.radians(270.0))

    def test_env_pairs(self):
        self.assertEqual(metric.parse_env(["A=1", "B=x=y"]), {"A": "1", "B": "x=y"})
        with self.assertRaises(ValueError):
            metric.parse_env(["A"])


if __name__ == "__main__":
    unittest.main()
