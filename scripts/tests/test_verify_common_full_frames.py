"""Tests for the shared full-frame / ROI-crop glob guard (#2819).

``VideoManager::writePendingRoiCrops`` writes ROI crops as
``screenshot_<n>_<shotLabel>__crop_<cropLabel>.png`` into the *same* directory
as the full frames (``screenshot_<n>.png``). A bare ``screenshot_*.png`` glob
matches both, and because ``.`` < ``_`` the crops sort *between* full frames —
so any positional shot->label/reference mapping built on that glob silently
shifts.

These tests pin the guard at its shared home (``collect_full_frames``) and at
every consumer, so a harness cannot silently regain an unfiltered glob — a
consumer is only ever *accidentally* safe when its demo happens to attach no
crops, which is a property of the demo's shot table, not of the harness
(see #2819).

Filenames only — no PNG contents, no engine, no GL/Metal build. Import the
dashed-name scripts via importlib, matching test_render_verify.py.
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


_cv = _load("cull_verify", "cull-verify.py")
_rv = _load("render_verify", "render-verify.py")


def _seed(shots_dir: Path, num_frames: int, crops_on: tuple[int, ...] = ()) -> None:
    """Write ``num_frames`` full frames, with ROI crops on the given indices."""
    for i in range(num_frames):
        (shots_dir / f"screenshot_{i:06d}.png").touch()
        if i in crops_on:
            (shots_dir / f"screenshot_{i:06d}_cv_live_{i:03d}__crop_center.png").touch()


class TestSortInterleave(unittest.TestCase):
    """The premise the guard rests on: crops sort between full frames."""

    def test_crops_sort_between_full_frames(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 3, crops_on=(0, 1, 2))
            unfiltered = sorted(p.name for p in d.glob("screenshot_*.png"))
            # Interleaved, not grouped: frame, its crop, next frame, ...
            self.assertEqual(
                unfiltered,
                [
                    "screenshot_000000.png",
                    "screenshot_000000_cv_live_000__crop_center.png",
                    "screenshot_000001.png",
                    "screenshot_000001_cv_live_001__crop_center.png",
                    "screenshot_000002.png",
                    "screenshot_000002_cv_live_002__crop_center.png",
                ],
            )
            # The positional hazard, stated as an assertion: a first-N slice of
            # the unfiltered glob puts a CROP where full frame 1 belongs.
            self.assertTrue(unfiltered[1].endswith("__crop_center.png"))


class TestCollectFullFrames(unittest.TestCase):
    def test_excludes_crops_and_preserves_index_order(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 3, crops_on=(0, 1, 2))
            got = [p.name for p in verify_common.collect_full_frames(d)]
            self.assertEqual(
                got,
                ["screenshot_000000.png",
                 "screenshot_000001.png",
                 "screenshot_000002.png"],
            )

    def test_no_crops_present_is_unchanged(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 4)
            self.assertEqual(len(verify_common.collect_full_frames(d)), 4)

    def test_empty_dir(self):
        with tempfile.TemporaryDirectory() as td:
            self.assertEqual(verify_common.collect_full_frames(Path(td)), [])

    def test_unrelated_files_ignored(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 2)
            (d / "notes.txt").touch()
            (d / "screenshot_000000.diff.png").touch()
            self.assertEqual(len(verify_common.collect_full_frames(d)), 2)


class TestCullVerifyCollectShots(unittest.TestCase):
    """Positive control: this is the site that does the first-N slice.

    Against the pre-#2819 body (``sorted(shots_dir.glob("screenshot_*.png"))``)
    the crop case returns a crop at index 1 and the live/frozen pairing shifts.
    """

    def test_crops_do_not_enter_the_positional_index(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, _cv.TOTAL_SHOTS, crops_on=tuple(range(_cv.TOTAL_SHOTS)))
            shots = _cv._collect_shots(d)
            self.assertEqual(len(shots), _cv.TOTAL_SHOTS)
            for p in shots:
                self.assertNotIn("__crop_", p.name)
            # The live/frozen pairing this harness performs stays aligned.
            for i in range(_cv.POSES_PER_PHASE):
                self.assertEqual(shots[i].name, f"screenshot_{i:06d}.png")
                frozen = i + _cv.POSES_PER_PHASE + 1
                self.assertEqual(shots[frozen].name, f"screenshot_{frozen:06d}.png")

    def test_short_capture_still_raises(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            # Enough files to satisfy a *bare* glob, but one full frame short —
            # the crops must not paper over a genuinely truncated capture.
            _seed(d, _cv.TOTAL_SHOTS - 1, crops_on=(0, 1))
            with self.assertRaises(SystemExit):
                _cv._collect_shots(d)


class TestRenderVerifyUnchanged(unittest.TestCase):
    """render-verify already filtered; routing it through the shared helper
    must not change what it returns."""

    def test_collect_all_shots_still_excludes_crops(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 3, crops_on=(0, 2))
            got = [p.name for p in _rv._collect_all_shots(d)]
            self.assertEqual(
                got,
                ["screenshot_000000.png",
                 "screenshot_000001.png",
                 "screenshot_000002.png"],
            )

    def test_collect_shots_slice_maps_to_full_frames(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            _seed(d, 5, crops_on=(0, 1, 2, 3, 4))
            got = [p.name for p in _rv._collect_shots(d, 3)]
            self.assertEqual(
                got,
                ["screenshot_000000.png",
                 "screenshot_000001.png",
                 "screenshot_000002.png"],
            )


if __name__ == "__main__":
    unittest.main()
