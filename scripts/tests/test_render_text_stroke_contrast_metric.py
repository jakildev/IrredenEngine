"""Controls for the scale-one GUI text stroke/halo metric."""

import contextlib
import importlib.machinery
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
ROOT = SCRIPTS.parent
sys.path.insert(0, str(SCRIPTS))

import render_metric_util as util  # noqa: E402

loader = importlib.machinery.SourceFileLoader(
    "render_text_stroke_contrast_metric",
    str(SCRIPTS / "render-text-stroke-contrast-metric.py"),
)
spec = importlib.util.spec_from_loader("render_text_stroke_contrast_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)


def run(argv: list[str]) -> int:
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return metric._main(argv)


def write_fixture(path: Path, old_filter: bool) -> None:
    width, height = 1280, 960
    lit, _ = metric.fixture_masks()
    pixels = bytearray(width * height * 3)
    sx = metric.QUAD_SIZE[0] / metric.CANVAS_SIZE[0]
    sy = metric.QUAD_SIZE[1] / metric.CANVAS_SIZE[1]
    min_x = round(metric.QUAD_ORIGIN[0] + sx * (min(x for x, _ in lit) - 1))
    max_x = round(metric.QUAD_ORIGIN[0] + sx * (max(x for x, _ in lit) + 2))
    min_y = round(metric.QUAD_ORIGIN[1] + sy * (min(y for _, y in lit) - 2))
    max_y = round(metric.QUAD_ORIGIN[1] + sy * (max(y for _, y in lit) + 2))
    for py in range(min_y, max_y):
        for px in range(min_x, max_x):
            owner, across = metric._sample(px, py, sx, sy)
            owner_lit, across_lit = owner in lit, across in lit
            if old_filter and owner_lit != across_lit:
                value = 185 if owner_lit else 70
            else:
                value = 255 if owner_lit else 0
            offset = (py * width + px) * 3
            pixels[offset : offset + 3] = bytes((value, value, value))
    util.write_png(path, width, height, bytes(pixels), 3)


class TextStrokeContrastMetricTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        cls.good = cls.directory / "good.png"
        cls.old = cls.directory / "old.png"
        write_fixture(cls.good, old_filter=False)
        write_fixture(cls.old, old_filter=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_hard_ownership_passes_both_glyph_sets(self):
        result = metric.contrast_metrics(str(self.good))
        self.assertGreater(result["set_row_counts"]["large"], 0)
        self.assertGreater(result["set_row_counts"]["small"], 0)
        self.assertEqual(result["min_stroke_coverage"], 1.0)
        self.assertEqual(result["max_halo_coverage"], 0.0)
        self.assertGreaterEqual(result["min_stroke_to_halo_ratio"], 10.0)
        self.assertEqual(
            run(
                [
                    str(self.good),
                    "--min-stroke-coverage",
                    "0.95",
                    "--max-halo-coverage",
                    "0.05",
                    "--min-stroke-to-halo-ratio",
                    "10",
                ]
            ),
            0,
        )

    def test_old_box_filter_fails_with_the_expected_reading(self):
        result = metric.contrast_metrics(str(self.old))
        self.assertAlmostEqual(result["min_stroke_coverage"], 185 / 255, places=3)
        self.assertAlmostEqual(result["max_halo_coverage"], 70 / 255, places=3)
        self.assertAlmostEqual(result["min_stroke_to_halo_ratio"], 185 / 70, places=2)
        self.assertEqual(
            run(
                [
                    str(self.old),
                    "--min-stroke-coverage",
                    "0.95",
                    "--max-halo-coverage",
                    "0.05",
                    "--min-stroke-to-halo-ratio",
                    "10",
                ]
            ),
            1,
        )

    def test_blank_and_wrong_sized_captures_do_not_pass(self):
        blank = self.directory / "blank.png"
        util.write_png(blank, 1280, 960, bytes(1280 * 960 * 3), 3)
        self.assertEqual(run([str(blank), "--min-stroke-coverage", "0.95"]), 1)
        small = self.directory / "small.png"
        util.write_png(small, 8, 8, bytes(8 * 8 * 3), 3)
        self.assertEqual(run([str(small), "--min-stroke-coverage", "0.95"]), 1)

    def test_manifest_names_the_fixed_positive_gate(self):
        manifest = json.loads(
            (ROOT / "creations/demos/shape_debug/test/references/manifest.json").read_text(
                encoding="utf-8"
            )
        )
        run_spec = next(
            entry for entry in manifest["extra_runs"] if entry["name"] == "text_scale1_legibility"
        )
        self.assertEqual(run_spec["structural_only"], ["text_size_sample"])
        (gate,) = run_spec["structural"]["text_size_sample"]
        self.assertEqual(
            gate,
            {
                "metric": "text-stroke-contrast",
                "min_stroke_coverage": 0.95,
                "max_halo_coverage": 0.05,
                "min_stroke_to_halo_ratio": 10.0,
            },
        )


if __name__ == "__main__":
    unittest.main()
