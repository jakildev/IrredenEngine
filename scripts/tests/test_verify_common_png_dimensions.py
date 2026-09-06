"""Tests for verify_common.png_dimensions (#3016 review follow-up).

``png_dimensions`` reads a PNG's declared width/height straight from its
IHDR chunk header without decoding pixels — it backs render-verify.py's
``roi_at`` scaling branch (``_run_structural_metric``), which needs the
actual capture's size to scale a reference-calibrated ROI proportionally.
Exercises the happy path plus the malformed-input ``ValueError`` (not a
PNG at all, and a PNG-signed file whose first chunk isn't IHDR) that
`_run_structural_metric`'s caller needs to surface loudly rather than
silently mis-scale.

Import ``render-compare.py`` via importlib, matching test_render_verify.py
— its ``write_png`` produces a real PNG so the happy-path test exercises
the actual IHDR layout, not a hand-rolled approximation of one.
"""
import importlib.machinery
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))

import verify_common  # noqa: E402  (needs the sys.path insert above)


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_cmp = _load("render_compare", "render-compare.py")
write_png = _cmp.write_png

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class TestPngDimensions(unittest.TestCase):
    def test_reads_width_and_height_from_ihdr(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "shot.png"
            write_png(str(path), 40, 30, bytes(40 * 30 * 3), 3)
            self.assertEqual(verify_common.png_dimensions(path), (40, 30))

    def test_non_square_dimensions_are_not_transposed(self):
        # A width != height catches an accidental (height, width) swap that
        # a square fixture (e.g. the 16x16/32x32 sizes used elsewhere in
        # this suite) would never expose.
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "shot.png"
            write_png(str(path), 64, 17, bytes(64 * 17 * 3), 3)
            self.assertEqual(verify_common.png_dimensions(path), (64, 17))

    def test_non_png_file_raises(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "not_a_png.png"
            path.write_bytes(b"this is not a PNG file at all, just text")
            with self.assertRaises(ValueError):
                verify_common.png_dimensions(path)

    def test_png_signature_without_ihdr_first_raises(self):
        # A file that opens with the correct 8-byte PNG signature but whose
        # first chunk isn't IHDR — the malformed-but-signature-valid case
        # ``header[12:16] != b"IHDR"`` guards against, distinct from the
        # bad-signature case above.
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "no_ihdr.png"
            path.write_bytes(PNG_SIGNATURE + b"\x00" * 16)
            with self.assertRaises(ValueError):
                verify_common.png_dimensions(path)


if __name__ == "__main__":
    unittest.main()
