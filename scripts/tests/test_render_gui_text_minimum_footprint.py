"""Minimum-footprint GUI text coverage and preview parity controls."""

import importlib.machinery
import importlib.util
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "engine/render/src/shaders"
sys.path.insert(0, str(ROOT / "scripts"))

loader = importlib.machinery.SourceFileLoader(
    "trixel_font_preview", str(ROOT / "scripts/trixel-font-preview.py")
)
spec = importlib.util.spec_from_loader("trixel_font_preview", loader)
preview = importlib.util.module_from_spec(spec)
loader.exec_module(preview)


def assert_shader_contract(source: str) -> None:
    compact = re.sub(r"\s+", "", source)
    if not re.search(
        r"effectiveDisplayScale=min\((?:0\.5f?\*texelScreenPx\.x|"
        r"0\.5f?\*frameData\.texelScreenPx\.x),(?:texelScreenPx\.y|"
        r"frameData\.texelScreenPx\.y)\)",
        compact,
    ):
        raise AssertionError("effective scale does not use the physical 2x1 footprint")
    if "effectiveDisplayScale<=1.0" not in compact.replace("1.0f", "1.0"):
        raise AssertionError("minimum-footprint cutoff is not exactly scale one")
    if "return1.0" not in compact.replace("1.0f", "1.0"):
        raise AssertionError("minimum-footprint samples do not receive full ownership")
    if "clamp(nearest+0.5" not in compact.replace("0.5f", "0.5"):
        raise AssertionError("larger footprints no longer use the edge box filter")


class GuiTextMinimumFootprintTest(unittest.TestCase):
    def test_both_shader_backends_keep_the_same_cutoff_and_filter(self):
        paths = (
            SHADERS / "f_gui_canvas_to_screen.glsl",
            SHADERS / "metal/gui_canvas_to_screen.metal",
        )
        for path in paths:
            source = path.read_text(encoding="utf-8")
            with self.subTest(backend=path.suffix, variant="production"):
                assert_shader_contract(source)
            mutations = (
                source.replace("return 1.0;", "return 0.0;").replace(
                    "return 1.0f;", "return 0.0f;"
                ),
                source.replace("<= 1.0)", "<= 2.0)").replace("<= 1.0f)", "<= 2.0f)"),
            )
            for index, mutant in enumerate(mutations):
                with self.subTest(backend=path.suffix, variant=f"mutation-{index}"):
                    self.assertNotEqual(mutant, source)
                    with self.assertRaises(AssertionError):
                        assert_shader_contract(mutant)

    def test_preview_scale_one_has_only_full_foreground_and_background(self):
        image = preview.render_smooth(
            {(1, 1), (2, 1)},
            (5, 4),
            gui_scale=1,
            output_scale=1,
            upscale=1,
            bg=(0, 0, 0),
            fg=(255, 255, 255),
        )
        pixels = image.load()
        values = {pixels[x, y][0] for y in range(image.height) for x in range(image.width)}
        self.assertEqual(values, {0, 255})

    def test_preview_scale_two_retains_fractional_edge_coverage(self):
        image = preview.render_smooth(
            {(1, 1), (2, 1)},
            (5, 4),
            gui_scale=2,
            output_scale=1,
            upscale=1,
            bg=(0, 0, 0),
            fg=(255, 255, 255),
        )
        pixels = image.load()
        values = {pixels[x, y][0] for y in range(image.height) for x in range(image.width)}
        self.assertTrue(any(0 < value < 255 for value in values), sorted(values))

    def test_scale_one_and_two_coverage_readings_fire(self):
        samples = {
            scale: [
                preview.composite_sample(x, y, 2.0 * scale, scale)[2]
                for y in range(2 * scale)
                for x in range(4 * scale)
            ]
            for scale in (1, 2)
        }
        self.assertEqual(set(samples[1]), {1.0})
        self.assertTrue(any(0.0 < value < 1.0 for value in samples[2]))


if __name__ == "__main__":
    unittest.main()
