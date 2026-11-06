"""Continuous-zoom metric: log parsing, width / edge gates, silhouette match."""

import importlib.machinery
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-continuous-zoom-metric.py"
loader = importlib.machinery.SourceFileLoader("continuous_zoom_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("continuous_zoom_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

WIDTH, HEIGHT = 1000, 60
CENTRE = WIDTH // 2
SUBJECT = (255, 96, 0)


def write_capture(path, left, right, top=20, bottom=40):
    """A black frame with one subject rectangle spanning columns [left, right)."""
    row_empty = bytes(3 * WIDTH)
    row_subject = bytes(3 * left) + bytes(SUBJECT) * (right - left) + bytes(3 * (WIDTH - right))
    rows = [row_subject if top <= y < bottom else row_empty for y in range(HEIGHT)]
    rmu.write_png(str(path), WIDTH, HEIGHT, b"".join(rows), 3)


def state_line(index, zoom, edge_near, scale=2, residual=0, continuous=1, cam_x=16.0,
               edge_far=None):
    edge_far = edge_near + 10.0 * 2.0 * zoom if edge_far is None else edge_far
    return (
        f"[2026-01-01 00:00:00.000] [ClientLog] [info] [zoom-calibration] index={index} "
        f"continuous={continuous} zoom={zoom:.6f} density=3 cam={cam_x:.6f},16.000000 "
        f"scale={scale} phase=0.199996948,0.599998474 gather=-0.250000000,0.000000000 "
        f"residual={residual},0 edge_fb={edge_near:.9f},{edge_far:.9f}\n"
    )


class Sweep:
    """Writes a sweep's captures and log the way one IRShapeDebug run leaves them."""

    def __init__(self, root):
        self.root = root
        self.log = root / "run.log"
        self.lines = []

    def shot(self, zoom, near_fb, width_fb=None, scale=2, residual=0, logged_near=None,
             continuous=1, cam_x=16.0):
        index = len(self.lines)
        width_px = round((10 * 2 * zoom if width_fb is None else width_fb) * scale)
        near_px = CENTRE + round(near_fb) * scale + residual
        # The subject extends away from the centre on whichever side it sits.
        left, right = (near_px, near_px + width_px) if near_fb >= 0 else (near_px - width_px,
                                                                         near_px)
        write_capture(self.root / f"screenshot_{index + 1:06d}.png", left, right)
        logged = near_fb if logged_near is None else logged_near
        self.lines.append(state_line(index, zoom, logged, scale, residual, continuous, cam_x))
        self.log.write_text("engine noise\n" + "".join(self.lines) + "ir-run: RESULT=CLEAN\n")


class StubCapture:
    """Stands in for the IRShapeDebug runs of a GPU mode: each ``run`` hands
    back the next scripted sweep and records the demo arguments it was given."""

    def __init__(self, root, sweeps):
        self.root = root
        self.sweeps = list(sweeps)
        self.calls = []

    def run(self, demo_args, shots):
        directory = self.root / f"run{len(self.calls)}"
        directory.mkdir()
        sweep = Sweep(directory)
        for near in self.sweeps[len(self.calls)]:
            sweep.shot(2.5, near)
        self.calls.append(demo_args)
        return (sorted(directory.glob("screenshot_*.png")),
                metric.parse_states(sweep.log.read_text()))


class ContinuousZoomMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sweep = Sweep(self.root)

    def run_cli(self, mode, *args):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), mode, str(self.root), "--log", str(self.sweep.log),
             *args],
            capture_output=True, text=True)
        lines = result.stdout.strip().splitlines()
        return result.returncode, json.loads(lines[0]), lines[-1]

    def test_state_line_parses_every_field(self):
        states = metric.parse_states(
            state_line(1, 2.5, 100.2, residual=-1) + "noise\n" + state_line(0, 2.495, 99.8))
        self.assertEqual([state["index"] for state in states], [0, 1])
        self.assertEqual(states[1]["zoom"], 2.5)
        self.assertEqual(states[1]["scale"], 2)
        self.assertEqual(states[1]["residual_x"], -1)
        self.assertEqual(states[1]["gather_x"], -0.25)
        self.assertAlmostEqual(states[1]["edge_near"], 100.2)
        self.assertAlmostEqual(states[1]["edge_far"], 150.2)
        self.assertEqual(metric.parse_states("no calibration lines here"), [])

    def test_width_gate_passes_a_continuous_sweep(self):
        for step in range(5):
            zoom = 2.0 + 0.1 * step
            self.sweep.shot(zoom, 40 * zoom)
        code, result, verdict = self.run_cli("widths")
        self.assertEqual((code, verdict), (0, "RESULT=PASS"), result)
        self.assertEqual(result["frames"], 5)
        self.assertEqual(result["distinct_widths"], 5)
        self.assertTrue(result["monotone"])
        self.assertEqual(result["max_width_error_fb"], 0.0)

    def test_width_gate_fails_a_snapped_sweep(self):
        # Five requests between 2.0 and 2.4 with the policy off all render at 2.
        for _ in range(5):
            self.sweep.shot(2.0, 80, continuous=0)
        code, result, verdict = self.run_cli("widths")
        self.assertEqual((code, verdict), (1, "RESULT=FAIL"), result)
        self.assertEqual(result["distinct_widths"], 1)
        self.assertFalse(result["monotone"])

    def test_width_gate_fails_a_width_off_the_analytic_line(self):
        for step in range(5):
            zoom = 2.0 + 0.1 * step
            self.sweep.shot(zoom, 40 * zoom, width_fb=20 * zoom + (1.5 if step == 3 else 0))
        code, result, _ = self.run_cli("widths")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["distinct_widths"], 5)
        self.assertTrue(result["monotone"])
        self.assertEqual(result["max_width_error_fb"], 1.5)

    def test_edge_gate_passes_a_rigid_sweep(self):
        for step in range(6):
            self.sweep.shot(2.5 + 0.005 * step, 100.2 + 0.2 * step)
        code, result, verdict = self.run_cli("edge")
        self.assertEqual((code, verdict), (0, "RESULT=PASS"), result)
        self.assertEqual(result["frames"], 6)
        self.assertEqual(result["max_backward_px"], 0)
        self.assertEqual(result["model_mismatches"], 0)
        self.assertGreater(result["edge_motion_px"], 0)
        self.assertAlmostEqual(result["min_tie_distance_fb"], 0.1)
        self.assertEqual(result["edge_px"], result["predicted_edge_px"])

    def test_edge_gate_counts_a_backward_step(self):
        for near in (100.1, 100.3, 101.1, 99.9, 101.3, 101.7):
            self.sweep.shot(2.5, near)
        code, result, verdict = self.run_cli("edge")
        self.assertEqual((code, verdict), (1, "RESULT=FAIL"), result)
        self.assertEqual(result["max_backward_px"], 2)
        self.assertEqual(result["model_mismatches"], 0)

    def test_edge_gate_follows_the_sweep_direction(self):
        for near in (-100.1, -100.3, -101.1, -101.3):
            self.sweep.shot(2.5, near)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["edge_motion_px"], 2)
        self.assertEqual(result["max_backward_px"], 0)

    def test_edge_gate_requires_motion(self):
        for _ in range(4):
            self.sweep.shot(2.5, 100.2)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["edge_motion_px"], 0)

    def test_edge_gate_rejects_an_edge_on_a_raster_tie(self):
        for near in (100.1, 100.52, 101.1):
            self.sweep.shot(2.5, near)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 1, result)
        self.assertAlmostEqual(result["min_tie_distance_fb"], 0.02)
        self.assertEqual(result["max_backward_px"], 0)

    def test_edge_gate_requires_the_logged_placement_to_predict_the_capture(self):
        self.sweep.shot(2.5, 100.2)
        self.sweep.shot(2.5, 101.2, logged_near=102.2)
        self.sweep.shot(2.5, 102.2)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["model_mismatches"], 1)
        code, result, _ = self.run_cli("edge", "--no-model")
        self.assertEqual(code, 0, result)
        self.assertNotIn("model_mismatches", result)

    def test_edge_gate_adds_the_screen_residual(self):
        self.sweep.shot(3.3, 100.2, residual=0)
        self.sweep.shot(3.3, 100.2, residual=1)
        self.sweep.shot(3.3, 101.2, residual=0)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["edge_px"], [200, 201, 202])

    def test_edge_gate_needs_an_output_scale_above_one(self):
        for near in (100.1, 100.3, 101.1):
            self.sweep.shot(2.5, near, scale=1)
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["output_scale"], [1])
        code, result, _ = self.run_cli("edge", "--min-output-scale", "1")
        self.assertEqual(code, 0, result)

    def test_silhouettes_compare_by_shape_and_report_the_offset(self):
        a, b, c = (self.root / name for name in ("a.png", "b.png", "c.png"))
        write_capture(a, 100, 150)
        write_capture(b, 103, 153, top=21, bottom=41)
        write_capture(c, 100, 151)
        self.assertEqual(metric.compare_silhouettes(a, a),
                         {"same_shape": True, "offset_px": [0, 0]})
        self.assertEqual(metric.compare_silhouettes(a, b),
                         {"same_shape": True, "offset_px": [3, 1]})
        self.assertFalse(metric.compare_silhouettes(a, c)["same_shape"])

    def test_an_empty_capture_is_an_error_not_a_pass(self):
        write_capture(self.root / "screenshot_000001.png", 0, 0)
        write_capture(self.root / "screenshot_000002.png", 0, 0)
        self.sweep.log.write_text(state_line(0, 2.5, 100.2) + state_line(1, 2.5, 100.4))
        code, result, _ = self.run_cli("edge")
        self.assertEqual(code, 2)
        self.assertIn("no subject", result["error"])

    def test_a_log_that_does_not_cover_the_captures_is_an_error(self):
        for near in (100.1, 100.3, 101.1):
            self.sweep.shot(2.5, near)
        code, result, _ = self.run_cli("edge", "--shots", "5")
        self.assertEqual(code, 2)
        self.assertIn("error", result)
        self.sweep.log.write_text("ir-run: RESULT=CLEAN\n")
        code, result, _ = self.run_cli("widths")
        self.assertEqual(code, 2)
        self.assertIn("error", result)

    def pan_mode(self, snapped_yawed):
        rigid = (100.2, 101.2, 102.2)
        capture = StubCapture(self.root, [rigid, rigid, rigid, snapped_yawed])
        return capture, metric._pan_mode(capture)

    def test_pan_mode_grades_the_snapped_rotated_sweep(self):
        capture, result = self.pan_mode((100.2, 101.2, 102.2))
        self.assertTrue(result["pass"], result)
        self.assertEqual(result["frames"], [3, 3, 3, 3])
        self.assertEqual(result["edge_motion_px"], [4, 4, 4, 4])
        self.assertEqual(result["max_backward_px"], 0)
        self.assertEqual(
            capture.calls[3],
            ["--zoom-calibration", "--zoom", "4", "--pan-sweep", "--yaw", metric.YAW_45])

    def test_pan_mode_fails_on_a_backward_step_in_the_snapped_rotated_sweep(self):
        _, result = self.pan_mode((100.2, 102.2, 101.2, 103.2))
        self.assertFalse(result["pass"], result)
        self.assertEqual(result["frames"], [3, 3, 3, 4])
        self.assertEqual(result["max_backward_px"], 2)


import gather_filter_metric as gfm  # noqa: E402

FW, FH = 96, 24
RED, GREEN = gfm.FLAT_OWNERS[gfm.PAIR_RED], gfm.FLAT_OWNERS[gfm.PAIR_GREEN]


def filter_scene(dx=0, body_color=None, pair_join=None):
    """A framebuffer-resolution scene: two checker bodies (columns 2-11 and
    70-79, far enough right that the second's source-face fence clears the
    pair) and a touching red/green pair (rows 16-21), on black."""
    pixels = [[(0, 0, 0)] * FW for _ in range(FH)]
    for left in (2, 70):
        for y in range(2, 12):
            for x in range(left + dx, left + dx + 10):
                pixels[y][x] = body_color or gfm.CHECKER_PALETTE[(x & 1) + 2 * (y & 1)]
    for y in range(16, 22):
        for x in range(14 + dx, 26 + dx):
            pixels[y][x] = RED if x < 20 + dx else GREEN
        if pair_join is not None:
            pixels[y][20 + dx] = pair_join
    return pixels


def write_filter_capture(path, pixels, scale=2):
    rows = []
    for row in pixels:
        line = b"".join(bytes(rgb) * scale for rgb in row)
        rows.extend([line] * scale)
    rmu.write_png(str(path), FW * scale, FH * scale, b"".join(rows), 3)


class GatherFilterGradingTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())

    def frame(self, name, pixels):
        path = self.root / f"{name}.png"
        write_filter_capture(path, pixels)
        return gfm.Frame(path, 2)

    def test_reads_one_sample_per_framebuffer_pixel(self):
        frame = self.frame("nearest", filter_scene())
        self.assertEqual((frame.width, frame.height), (FW, FH))
        self.assertEqual(frame.at(14, 16), RED)
        owners = gfm.Owners(frame)
        self.assertEqual(owners.unknown, 0)
        self.assertEqual(owners.name(2 * FW + 3), "checker_grid")
        self.assertEqual(owners.name(2 * FW + 71), "checker_detached")

    def test_in_plane_checker_mix_is_not_bleed(self):
        nearest = gfm.Owners(self.frame("nearest", filter_scene()))
        mixed = self.frame("owner", filter_scene(body_color=(130, 130, 120)))
        counts = gfm.ownership(nearest, mixed)
        self.assertEqual(counts["bleed"], 0)
        self.assertEqual(counts["owner_disagreements"], 0)
        self.assertEqual(counts["coverage_changes"], 0)
        self.assertGreater(counts["changed"]["checker_grid"], 0)

    def test_cross_owner_mix_at_the_touching_boundary_is_bleed(self):
        nearest = gfm.Owners(self.frame("nearest", filter_scene()))
        blended = self.frame("unrestricted", filter_scene(pair_join=(145, 145, 40)))
        counts = gfm.ownership(nearest, blended)
        self.assertEqual(counts["bleed"], 6)
        self.assertEqual(counts["bleed_boundary"], 6)
        self.assertEqual(counts["bleed_by_owner"], {"pair_green": 6})

    def test_coverage_change_is_counted_and_fenced_owners_are_not(self):
        nearest = gfm.Owners(self.frame("nearest", filter_scene()))
        pixels = filter_scene()
        pixels[22][14] = RED
        pixels[5][75] = (0, 0, 0)
        counts = gfm.ownership(nearest, self.frame("arm", pixels))
        self.assertEqual(counts["coverage_changes"], 1)
        self.assertEqual(counts["source_face_differing"], 1)

    def test_registration_recovers_a_whole_pixel_shift(self):
        first = gfm.Owners(self.frame("a", filter_scene()))
        second = gfm.Owners(self.frame("b", filter_scene(dx=2)))
        reg = gfm.registration(first, second)
        self.assertAlmostEqual(reg["scale"], 1.0, places=6)
        self.assertAlmostEqual(reg["translation"][0], 2.0, places=6)
        self.assertAlmostEqual(reg["translation"][1], 0.0, places=6)
        self.assertNotIn("checker_detached", reg["owners"])

    def test_temporal_difference_is_zero_for_a_rigid_shift_and_not_for_a_recolour(self):
        a, b = self.frame("a", filter_scene()), self.frame("b", filter_scene(dx=2))
        first, second = gfm.Owners(a), gfm.Owners(b)
        reg = gfm.registration(first, second)
        rigid = gfm.temporal(first, second, reg, a, b)
        self.assertAlmostEqual(rigid["grid"][0], 0.0, places=9)
        self.assertGreater(rigid["grid"][1], 0)
        recoloured = self.frame("c", filter_scene(dx=2, body_color=(130, 130, 120)))
        self.assertGreater(gfm.temporal(first, second, reg, a, recoloured)["grid"][0], 0.0)

    def test_interior_mean_tracks_the_palette_average(self):
        frame = self.frame("nearest", filter_scene())
        means = gfm.interior_mean(gfm.Owners(frame), frame)
        self.assertEqual(set(means), {"grid"})
        self.assertAlmostEqual(sum(means["grid"]), gfm.CHECKER_PLANE, places=6)


def graded_sweep(owner_bleed=0, repeat_coverage=0, unrestricted_bleed=5):
    """A _filter_sweep result with one shot and one shot pair."""
    def counts(bleed=0, coverage=0):
        return {"coverage_changes": coverage, "bleed": bleed, "owner_disagreements": 0,
                "source_face_differing": 0, "changed": {}}
    shot = {"zoom": 2.0, "density": 2, "scale": 2, "unknown_colors": 0,
            "nearest_repeat": counts(coverage=repeat_coverage),
            "unrestricted": counts(bleed=unrestricted_bleed), "owner": counts(bleed=owner_bleed)}
    temporal = {"grid": {"nearest": [10.0], "nearest_repeat": [10.0],
                         "unrestricted": [5.0], "owner": [5.5]}}
    means = {"grid": {arm: [(126.0, 126.0, 128.0), (127.0, 125.0, 128.0)]
                      for arm in ("nearest", "nearest_repeat", "unrestricted", "owner")}}
    return {"shots": [shot], "temporal": temporal, "interior_means": means,
            "max_registration_residual_px": 0.5}


class GatherFilterSummaryTest(unittest.TestCase):
    def test_passes_a_clean_candidate_with_a_bleeding_control(self):
        summary = metric._filter_summary({"yaw_0/forward": graded_sweep()})
        self.assertTrue(summary["pass"], summary)
        sweep = summary["sweeps"]["yaw_0/forward"]
        self.assertEqual(sweep["temporal"]["grid"]["owner_minus_nearest"], -4.5)
        self.assertEqual(sweep["temporal"]["grid"]["repeat_noise"], 0.0)
        self.assertEqual(sweep["interior_mean_spread"]["grid"]["owner"], 0.5)

    def test_fails_on_candidate_bleed_repeat_drift_or_a_silent_control(self):
        for graded in (graded_sweep(owner_bleed=1), graded_sweep(repeat_coverage=1),
                       graded_sweep(unrestricted_bleed=0)):
            with self.subTest(graded=graded["shots"][0]):
                self.assertFalse(metric._filter_summary({"s": graded})["pass"])


if __name__ == "__main__":
    unittest.main()
