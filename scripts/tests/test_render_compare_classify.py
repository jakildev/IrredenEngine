"""Exercise opt-in spatial classification in render-compare."""

import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "render_compare", SCRIPTS / "render-compare.py"
)
rc = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rc)


def patterned_pixels(width, height, bpp):
    return bytes(
        ((x * 37 + y * 71 + channel * 83) % 256)
        for y in range(height)
        for x in range(width)
        for channel in range(bpp)
    )


def translated_pixels(reference, width, height, bpp, dx, dy):
    actual = bytearray(width * height * bpp)
    for y in range(height):
        for x in range(width):
            source_x = min(max(x + dx, 0), width - 1)
            source_y = min(max(y + dy, 0), height - 1)
            source = (source_y * width + source_x) * bpp
            target = (y * width + x) * bpp
            actual[target:target + bpp] = reference[source:source + bpp]
    return bytes(actual)


class RenderCompareClassifyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_pair(self, actual, reference, width=64, height=64, bpp=3):
        actual_path = self.root / "actual.png"
        reference_path = self.root / "reference.png"
        rc.write_png(str(actual_path), width, height, actual, bpp)
        rc.write_png(str(reference_path), width, height, reference, bpp)
        return actual_path, reference_path

    def classify(self, actual, reference, *, bpp=3, tolerance=0):
        actual_path, reference_path = self.write_pair(
            actual, reference, bpp=bpp
        )
        return rc.compare(
            str(actual_path),
            str(reference_path),
            per_pixel_tol=tolerance,
            classify=True,
        )

    def test_translation_reports_measured_sign_for_rgb(self):
        reference = patterned_pixels(64, 64, 3)
        actual = translated_pixels(reference, 64, 64, 3, -1, -1)

        result = self.classify(actual, reference)

        self.assertEqual(result["difference_class"], "shift")
        self.assertEqual(result["shift"], [-1, -1])

    def test_changed_patch_is_local_for_rgba(self):
        reference = patterned_pixels(64, 64, 4)
        actual = bytearray(reference)
        for y in range(4, 12):
            for x in range(4, 12):
                offset = (y * 64 + x) * 4
                actual[offset:offset + 4] = bytes((255, 0, 255, 255))

        result = self.classify(bytes(actual), reference, bpp=4)

        self.assertEqual(result["difference_class"], "local")
        self.assertNotIn("shift", result)

    def test_frame_wide_delta_is_global(self):
        reference = bytes([40]) * (64 * 64 * 3)
        actual = bytes([50]) * (64 * 64 * 3)

        result = self.classify(actual, reference, tolerance=8)

        self.assertEqual(result["difference_class"], "global")

    def test_identical_pair_is_identical(self):
        pixels = patterned_pixels(64, 64, 3)

        result = self.classify(pixels, pixels)

        self.assertEqual(result["difference_class"], "identical")
        self.assertNotIn("shift", result)

    def test_within_tolerance_pair_is_identical(self):
        reference = bytes([40]) * (64 * 64 * 3)
        actual = bytes([45]) * (64 * 64 * 3)

        result = self.classify(actual, reference, tolerance=8)

        self.assertEqual(result["difference_class"], "identical")

    def test_shape_mismatch_is_global(self):
        actual = self.root / "actual.png"
        reference = self.root / "reference.png"
        rc.write_png(str(actual), 32, 64, bytes(32 * 64 * 3), 3)
        rc.write_png(str(reference), 64, 64, bytes(64 * 64 * 3), 3)

        result = rc.compare(str(actual), str(reference), classify=True)

        self.assertEqual(result["difference_class"], "global")

    def test_cli_default_contract_and_opt_in_output(self):
        reference = patterned_pixels(64, 64, 3)
        actual = translated_pixels(reference, 64, 64, 3, -1, -1)
        actual_path, reference_path = self.write_pair(actual, reference)
        command = [sys.executable, str(SCRIPTS / "render-compare.py"),
                   str(actual_path), str(reference_path), "--json"]

        default = subprocess.run(command, capture_output=True, text=True)
        classified = subprocess.run(
            command + ["--classify"], capture_output=True, text=True
        )

        self.assertEqual(default.returncode, 1)
        self.assertEqual(classified.returncode, 1)
        self.assertEqual(
            set(json.loads(default.stdout)),
            {"pass", "match_pct", "max_delta", "psnr_db", "reason"},
        )
        self.assertEqual(json.loads(classified.stdout)["shift"], [-1, -1])

        out = io.StringIO()
        with redirect_stdout(out):
            rc._main([str(actual_path), str(reference_path), "--classify"])
        self.assertIn("difference: shift(-1,-1)", out.getvalue())


if __name__ == "__main__":
    unittest.main()
