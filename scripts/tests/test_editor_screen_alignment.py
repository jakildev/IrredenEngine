"""Positive and corrupted-image controls for the editor's screen-pixel oracle."""

import importlib.util
import math
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

SPEC = importlib.util.spec_from_file_location(
    "editor_screen_alignment", Path(__file__).resolve().parents[1] / "editor-screen-alignment.py")
ORACLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORACLE)

SOURCE_HEXAGON = [(40, 40), (56, 32), (72, 40), (72, 56), (56, 64), (40, 56)]
TOP_FACE = [(40, 40), (56, 32), (72, 40), (56, 48)]


def paint_normals(image, side_color):
    draw = ImageDraw.Draw(image)
    draw.polygon(SOURCE_HEXAGON, fill=side_color)
    draw.polygon(TOP_FACE, fill=ORACLE.NORMALS[45])


def fixture():
    size = (112, 96)
    source_mask = Image.new("L", size)
    ImageDraw.Draw(source_mask).polygon(SOURCE_HEXAGON, fill=255)
    baseline = Image.new("RGB", size)
    baseline.paste(ORACLE.SOURCE, mask=source_mask)
    ImageDraw.Draw(baseline).rectangle((4, 4, 10, 10), fill=ORACLE.PAINT)
    grid = baseline.copy()
    draw = ImageDraw.Draw(grid)
    for point in ((38, 56), (74, 56), (56, 66)):
        draw.point(point, fill=(80, 100, 120))
    normals = baseline.copy()
    paint_normals(normals, ORACLE.NORMALS[0])
    hover = baseline.copy()
    ImageDraw.Draw(hover).rectangle((80, 34, 98, 52), fill=(30, 100, 60))
    commit = baseline.copy()
    ImageDraw.Draw(commit).rectangle((80, 34, 98, 52), fill=ORACLE.PAINT)
    ghost = Image.new("RGB", size)
    ImageDraw.Draw(ghost).rectangle((50, 50, 80, 72), fill=(30, 100, 60))
    occluded = ghost.copy()
    occluded.paste(baseline, mask=source_mask)
    floor_hover = baseline.copy()
    floor_hover.paste((30, 100, 60), mask=source_mask)
    floor_commit = baseline.copy()
    floor_commit.paste(ORACLE.PAINT, mask=source_mask)
    images = dict(zip(ORACLE.PHASES,
                      [baseline, grid, normals, hover, commit, baseline.copy(),
                       occluded, ghost, floor_hover, floor_commit]))
    sample = {"cursor": [56, 56], "sample": [56, 56], "framebuffer": list(size),
              "window": list(size), "floor_cursor": [56, 56], "game_resolution": list(size)}
    return images, sample


class EditorScreenAlignmentTest(unittest.TestCase):
    def test_complete_translucent_cell_and_depth_tested_grid_pass(self):
        images, sample = fixture()
        result = ORACLE.measure_case(images, 0, sample)
        self.assertTrue(result["pass"], result)

    def test_shifted_and_line_only_previews_fail_coverage(self):
        for rectangle in [(90, 34, 108, 52), (80, 34, 98, 35)]:
            with self.subTest(rectangle=rectangle):
                images, sample = fixture()
                images["hover"] = images["grid_off"].copy()
                ImageDraw.Draw(images["hover"]).rectangle(rectangle, fill=(30, 100, 60))
                result = ORACLE.measure_case(images, 0, sample)
                self.assertIn("ghost_covers_whole_committed_cell", result["failures"])

    def test_opaque_preview_fails(self):
        images, sample = fixture()
        images["hover"] = images["commit"].copy()
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("ghost_preserves_background_visibility", result["failures"])

    def test_oversized_fill_cannot_pass_by_covering_the_target(self):
        images, sample = fixture()
        ImageDraw.Draw(images["hover"]).rectangle((75, 29, 103, 57), fill=(30, 100, 60))
        result = ORACLE.measure_case(images, 0, sample)
        self.assertEqual(result["ghost_interior_coverage"], 1.0)
        self.assertIn("ghost_stays_inside_committed_cell_footprint", result["failures"])

    def test_outline_has_a_separate_bounded_stroke_band(self):
        for expansion, passes in [(2, True), (3, False)]:
            with self.subTest(expansion=expansion):
                images, sample = fixture()
                ImageDraw.Draw(images["hover"]).rectangle(
                    (80 - expansion, 34 - expansion, 98 + expansion, 52 + expansion),
                    outline=(220, 230, 240))
                result = ORACLE.measure_case(images, 0, sample)
                self.assertEqual(result["pass"], passes, result)
                if not passes:
                    self.assertIn("ghost_outline_stays_inside_stroke_band", result["failures"])

    def test_grid_in_front_of_voxel_fails(self):
        images, sample = fixture()
        ImageDraw.Draw(images["grid_on"]).line((44, 48, 68, 48), fill=(80, 100, 120))
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("solid_occludes_grid", result["failures"])

    def test_misaligned_grid_fails(self):
        images, sample = fixture()
        images["grid_on"] = images["grid_off"].copy()
        ImageDraw.Draw(images["grid_on"]).line((10, 78, 100, 78), fill=(80, 100, 120))
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("grid_meets_rendered_bottom_footprint", result["failures"])

    def test_ghost_through_solid_fails(self):
        images, sample = fixture()
        images["occluded_ghost"] = images["ghost_only"].copy()
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("solid_occludes_ghost", result["failures"])

    def test_missing_occlusion_overlap_cannot_pass(self):
        images, sample = fixture()
        images["ghost_only"] = Image.new("RGB", images["ghost_only"].size)
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("occlusion_control_has_real_overlap", result["failures"])

    def test_wrong_dpi_conversion_fails(self):
        images, sample = fixture()
        sample["window"] = [56, 48]
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("cursor_roundtrip_matches_sample", result["failures"])
        self.assertIn("raw_cursor_hits_observed_face_interior", result["failures"])

    def test_matching_dpi_ratio_passes(self):
        images, sample = fixture()
        sample["window"] = [56, 48]
        sample["cursor"] = [28, 28]
        sample["sample"] = [56, 56]
        sample["floor_cursor"] = [28, 28]
        result = ORACLE.measure_case(images, 0, sample)
        self.assertTrue(result["pass"], result)

    def test_raster_band_uses_game_output_scale_not_window_dpi(self):
        images, sample = fixture()
        images = {phase: image.resize((224, 192), Image.Resampling.NEAREST)
                  for phase, image in images.items()}
        sample.update(framebuffer=[224, 192], window=[224, 192], cursor=[112, 112],
                      sample=[112, 112], floor_cursor=[112, 112])
        result = ORACLE.measure_case(images, 0, sample)
        self.assertTrue(result["pass"], result)
        self.assertEqual(result["framebuffer_per_window_point"], [1, 1])
        self.assertEqual(result["fit_output_scale"], 2)
        self.assertEqual(result["ghost_fill_band_pixels"], 2)

    def test_flat_bottom_checks_its_endpoints_not_its_midpoint(self):
        mask = Image.new("L", (112, 96))
        ImageDraw.Draw(mask).rectangle((40, 32, 72, 64), fill=255)
        points = ORACLE.silhouette_support_points(mask)
        self.assertEqual(points[-2:], ((40, 64), (72, 64)))
        grid = Image.new("L", mask.size)
        draw = ImageDraw.Draw(grid)
        draw.point((40, 66), fill=255)
        draw.point((72, 66), fill=255)
        self.assertTrue(all(ORACLE.near(grid, point) for point in points))
        self.assertFalse(ORACLE.near(grid, (56, 64)))

    def test_wrong_face_and_wrong_floor_cell_fail(self):
        images, sample = fixture()
        paint_normals(images["normals"], ORACLE.NORMALS[90])
        images["floor_commit"] = images["commit"].copy()
        result = ORACLE.measure_case(images, 0, sample)
        self.assertIn("raw_cursor_hits_observed_face_interior", result["failures"])
        self.assertIn("floor_click_recreates_source_cell_footprint", result["failures"])


def completed_log():
    lines = ["Saved screenshot: screenshot_manual.png"]
    for yaw in ORACLE.NORMALS:
        for phase in ORACLE.PHASES:
            label = f"screen_alignment_yaw{yaw}_{phase}"
            lines.append(f"GUI-ASSERT label={label} result=PASS actual=cursor=28,24 "
                         "sample=56,48 framebuffer=112,96 window=56,48 floor_cursor=28,28")
            zoom = {30: 4.25, 67: 7.75, 90: 4}.get(yaw, 8)
            lines.append(f"CaptureCamera: label={label} yaw={math.radians(yaw)} "
                         f"zoom=({zoom},{zoom}) effectiveIso=({yaw},0) gameResolution=(112,96)")
            lines.append(f"Saved screenshot: screenshot_{label}.png")
    lines.extend(["ir-run: RESULT=CLEAN exe=IRVoxelEditor exit=0",
                  "[gui-verify] 70/70 assertions passed", "[gui-verify] 0 layout finding(s)"])
    return "\n".join(lines)


class EditorScreenAlignmentLogTest(unittest.TestCase):
    def test_complete_run_maps_seventy_named_captures_and_f12(self):
        captures, samples = ORACLE.parse_run(completed_log(), Path("/screenshots"))
        self.assertEqual(len(captures), 70)
        self.assertEqual(len(samples), 7)

    def test_missing_assertion_camera_capture_or_f12_fails(self):
        for token in ["GUI-ASSERT label=screen_alignment_yaw30_hover",
                      "CaptureCamera: label=screen_alignment_yaw67_commit",
                      "Saved screenshot: screenshot_manual.png", "CLEAN"]:
            with self.subTest(token=token):
                log = "\n".join(line for line in completed_log().splitlines()
                                if token not in line)
                with self.assertRaises(ValueError):
                    ORACLE.parse_run(log, Path("/screenshots"))

    def test_actual_camera_yaw_must_match_case(self):
        log = completed_log().replace("yaw=0.5235987755982988", "yaw=0.0")
        with self.assertRaisesRegex(ValueError, "yaw differs"):
            ORACLE.parse_run(log, Path("/screenshots"))

    def test_snapped_zoom_at_an_intermediate_yaw_fails(self):
        log = completed_log().replace("zoom=(4.25,4.25)", "zoom=(4,4)")
        with self.assertRaisesRegex(ValueError, "expected fractional zoom"):
            ORACLE.parse_run(log, Path("/screenshots"))


if __name__ == "__main__":
    unittest.main()
