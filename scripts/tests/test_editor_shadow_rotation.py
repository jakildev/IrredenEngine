"""Positive and negative controls for final-pixel shadow orbit evidence."""

import importlib.util
import math
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

SPEC = importlib.util.spec_from_file_location(
    "editor_shadow_rotation", Path(__file__).resolve().parents[1] / "editor-shadow-rotation.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)


class ShadowRotationMetricTest(unittest.TestCase):
    def test_wrapped_yaw_preserves_pose_but_rejects_neighbor(self):
        self.assertTrue(METRIC.yaw_matches(-math.pi + .0001, 180))
        self.assertTrue(METRIC.yaw_matches(-math.pi / 2, 270))
        self.assertFalse(METRIC.yaw_matches(math.radians(269.75), 270))

    def fixture(self, directory):
        captures, lines = {}, []
        for pose, yaw in enumerate(METRIC.YAWS):
            for state in ("on", "off"):
                path = directory / f"pose{pose}-{state}.png"
                image = Image.new("RGB", (64, 64), (150, 150, 150))
                if state == "on":
                    ImageDraw.Draw(image).rectangle((25, 25, 38, 38), fill=(60, 65, 70))
                image.save(path)
                captures[pose, state] = path
                label = f"shadow_rotation_pose{pose}_{state}"
                radians = math.radians(yaw)
                lines.extend([
                    f"GUI-ASSERT label={label} result=PASS actual=active=219 hash=123 "
                    f"yaw={radians} shadows={int(state == 'on')} surface=0",
                    f"CaptureCamera: label={label} yaw={radians} zoom=(8,8) "
                    "effectiveIso=(0,0) subdivisions=8",
                    f"Saved screenshot: {path}",
                ])
        lines.extend(["CLEAN", "[gui-verify] 0 layout finding(s)"])
        return captures, "\n".join(lines)

    def test_exact_return_and_shadow_response_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            captures, log = self.fixture(directory)
            parsed, fingerprint = METRIC.parse_run(log, directory, 0)
            self.assertEqual(parsed, captures)
            self.assertEqual(fingerprint, "123")
            self.assertTrue(METRIC.measure(captures)["pass"])

    def test_changed_authored_geometry_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, log = self.fixture(directory)
            with self.assertRaisesRegex(ValueError, "geometry changed"):
                METRIC.parse_run(log.replace("hash=123", "hash=124", 1), directory, 0)

    def test_one_return_pixel_cannot_hide_in_shadow_area(self):
        with tempfile.TemporaryDirectory() as temporary:
            captures, _ = self.fixture(Path(temporary))
            path = captures[18, "on"]
            with Image.open(path) as opened:
                image = opened.convert("RGB")
            image.putpixel((30, 30), (61, 65, 70))
            image.save(path)
            result = METRIC.measure(captures)
            self.assertFalse(result["pass"])
            self.assertEqual(result["poses"][18]["return_on_changed_pixels"], 1)

    def test_missing_shadow_response_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            captures, _ = self.fixture(Path(temporary))
            with Image.open(captures[0, "off"]) as image:
                image.save(captures[0, "on"])
            self.assertFalse(METRIC.measure(captures)["pass"])

    def test_missing_failed_or_duplicate_assertions_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, log = self.fixture(directory)
            first, rest = log.split("\n", 1)
            for damaged in (rest, log.replace("result=PASS", "result=FAIL", 1),
                            log + "\n" + first,
                            log + "\n" + first.replace("result=PASS", "result=FAIL")):
                with self.subTest(damaged=damaged[-100:]), self.assertRaises(ValueError):
                    METRIC.parse_run(damaged, directory, 0)

    def test_wrong_capture_pose_rejected_even_when_assertion_passed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, log = self.fixture(directory)
            wrong = log.replace("zoom=(8,8)", "zoom=(4,4)", 1)
            with self.assertRaisesRegex(ValueError, "zoom, pan, or subdivision"):
                METRIC.parse_run(wrong, directory, 0)

    def test_shadow_difference_outside_world_roi_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            captures, _ = self.fixture(Path(temporary))
            for key, path in captures.items():
                image = Image.new("RGB", (64, 64), (150, 150, 150))
                if key[1] == "on":
                    ImageDraw.Draw(image).rectangle((0, 0, 10, 10), fill=(60, 65, 70))
                image.save(path)
            result = METRIC.measure(captures)
            self.assertFalse(result["pass"])
            self.assertTrue(all(pose["shadow_pixels"] == 0 for pose in result["poses"]))

    def test_wrong_surface_arm_or_incomplete_run_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, log = self.fixture(directory)
            with self.assertRaises(ValueError):
                METRIC.parse_run(log, directory, 1)
            with self.assertRaises(ValueError):
                METRIC.parse_run(log.replace("CLEAN", ""), directory, 0)


if __name__ == "__main__":
    unittest.main()
