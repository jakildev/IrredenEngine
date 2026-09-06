"""Tests for feeder-margin-verify.py — the #3010 classify-margin adequacy gate.

Covers the pure half of the harness: the pixel census, the witness parse, the
SHOT_LABELS drift guard, and all three verdict rules — including the two
distinct vacuity cases, which are the whole reason the gate exists (a gate that
cannot fail is worth nothing; ``engine/render/CLAUDE.md`` §"Verifying render
changes"). Synthetic images only — no GL/Metal context, no demo run. Import via
importlib (dashed name).
"""
import importlib.machinery
import importlib.util
import sys
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_mod = _load("feeder_margin_verify", "feeder-margin-verify.py")

census = _mod.census
evaluate = _mod.evaluate
parse_feeder_classify = _mod.parse_feeder_classify
ring_non_empty = _mod.ring_non_empty
check_shot_labels = _mod.check_shot_labels
SHOT_LABELS = _mod.SHOT_LABELS
COMPARED_INDICES = _mod.COMPARED_INDICES
LIVENESS_INDEX = _mod.LIVENESS_INDEX


def _img(w: int, h: int, fn, bpp: int = 3):
    """A ``read_png``-shaped tuple built from a per-pixel callable."""
    buf = bytearray(w * h * bpp)
    for y in range(h):
        for x in range(w):
            value = fn(x, y)
            offset = (y * w + x) * bpp
            buf[offset : offset + bpp] = bytes(value)
    return (w, h, bpp, bytes(buf))


def _flat(w: int, h: int, value=(0, 0, 0)):
    return _img(w, h, lambda x, y: value)


def _clean(indices=COMPARED_INDICES):
    return {i: {"changed": 0, "bbox": None} for i in indices}


def _firing_liveness():
    out = _clean()
    out[LIVENESS_INDEX] = {"changed": 103168, "bbox": (320, 0, 1279, 719)}
    return out


class TestCensus(unittest.TestCase):
    def test_identical_images_report_zero_and_no_bbox(self):
        a = _flat(8, 4, (10, 20, 30))
        result = census(a, _flat(8, 4, (10, 20, 30)))
        self.assertEqual(result["changed"], 0)
        self.assertIsNone(result["bbox"])

    def test_counts_pixels_not_bytes(self):
        # One pixel differing in all three channels is ONE changed pixel. A
        # byte-wise count would say three and inflate every reported number.
        a = _flat(4, 4)
        b = _img(4, 4, lambda x, y: (255, 255, 255) if (x, y) == (1, 2) else (0, 0, 0))
        result = census(a, b)
        self.assertEqual(result["changed"], 1)
        self.assertEqual(result["bbox"], (1, 2, 1, 2))

    def test_bbox_spans_every_changed_pixel(self):
        a = _flat(10, 10)
        changed = {(2, 3), (7, 8), (4, 1)}
        b = _img(10, 10, lambda x, y: (9, 9, 9) if (x, y) in changed else (0, 0, 0))
        result = census(a, b)
        self.assertEqual(result["changed"], 3)
        self.assertEqual(result["bbox"], (2, 1, 7, 8))

    def test_single_channel_difference_is_detected(self):
        a = _flat(4, 4, (10, 10, 10))
        b = _img(4, 4, lambda x, y: (10, 11, 10) if (x, y) == (0, 0) else (10, 10, 10))
        self.assertEqual(census(a, b)["changed"], 1)

    def test_geometry_mismatch_raises(self):
        with self.assertRaises(ValueError):
            census(_flat(4, 4), _flat(4, 5))
        with self.assertRaises(ValueError):
            census(_flat(4, 4, (0, 0, 0)), _img(4, 4, lambda x, y: (0, 0, 0, 0), bpp=4))


class TestWitnessParse(unittest.TestCase):
    LOG = (
        "[info] FEEDER-CLASSIFY idx=0 label=fit_grid pad=0 ring_non_empty=1\n"
        "[info] unrelated line\n"
        "[info] FEEDER-CLASSIFY idx=5 label=zoom4_pan pad=0 ring_non_empty=0\n"
    )

    def test_parses_every_record_in_order(self):
        records = parse_feeder_classify(self.LOG)
        self.assertEqual([r["idx"] for r in records], [0, 5])
        self.assertEqual([r["label"] for r in records], ["fit_grid", "zoom4_pan"])
        self.assertEqual([r["ring_non_empty"] for r in records], [True, False])

    def test_parses_negative_pad(self):
        records = parse_feeder_classify(
            "FEEDER-CLASSIFY idx=5 label=zoom4_pan pad=-16 ring_non_empty=1"
        )
        self.assertEqual(records[0]["pad"], -16)

    def test_ring_is_true_when_any_shot_saw_a_ring(self):
        self.assertTrue(ring_non_empty(parse_feeder_classify(self.LOG)))

    def test_ring_is_false_when_no_shot_saw_one(self):
        log = "FEEDER-CLASSIFY idx=0 label=fit_grid pad=0 ring_non_empty=0"
        self.assertFalse(ring_non_empty(parse_feeder_classify(log)))

    def test_ring_fails_closed_on_no_witness_lines(self):
        # A demo too old for the knob emits nothing; that must not read as a
        # satisfied witness.
        self.assertFalse(ring_non_empty(parse_feeder_classify("no witness here")))


class TestShotLabelDriftGuard(unittest.TestCase):
    def test_matching_labels_pass(self):
        records = [
            {"idx": i, "label": SHOT_LABELS[i], "pad": 0, "ring_non_empty": True}
            for i in range(len(SHOT_LABELS))
        ]
        self.assertIsNone(check_shot_labels(records))

    def test_reordered_demo_table_is_caught(self):
        records = [{"idx": 5, "label": "some_new_shot", "pad": 0, "ring_non_empty": True}]
        self.assertIn("SHOT_LABELS", check_shot_labels(records) or "")

    def test_out_of_range_index_is_caught(self):
        records = [{"idx": 99, "label": "x", "pad": 0, "ring_non_empty": True}]
        self.assertIn("outside SHOT_LABELS", check_shot_labels(records) or "")


class TestVerdicts(unittest.TestCase):
    def test_the_measured_pass_case(self):
        # The state measured on Windows/GL at origin/master f3e79a54: adequacy
        # moves nothing, liveness moves 103,168 px on zoom4_pan, ring non-empty.
        self.assertEqual(evaluate(_clean(), _firing_liveness(), True, -16), [])

    def test_vacuous_instrument_when_liveness_arm_moves_nothing(self):
        failures = evaluate(_clean(), _clean(), True, 0)
        self.assertEqual([v for v, _ in failures], ["vacuous instrument"])

    def test_vacuous_adequacy_arm_when_the_ring_is_empty(self):
        # The C1 case: the liveness arm still fires (it manufactures feeders
        # out of on-screen winners regardless of the ring), so rule 1 passes
        # and only rule 3 can catch the tautology.
        failures = evaluate(_clean(), _firing_liveness(), False, -16)
        self.assertEqual([v for v, _ in failures], ["vacuous adequacy arm"])

    def test_the_two_vacuity_verdicts_are_distinct(self):
        instrument = evaluate(_clean(), _clean(), True, 0)[0][0]
        adequacy = evaluate(_clean(), _firing_liveness(), False, -16)[0][0]
        self.assertNotEqual(instrument, adequacy)

    def test_inadequate_margin_is_reported_with_count_and_bbox(self):
        adequacy = _clean()
        adequacy[LIVENESS_INDEX] = {"changed": 42, "bbox": (0, 0, 7, 3)}
        failures = evaluate(adequacy, _firing_liveness(), True, -16)
        self.assertEqual([v for v, _ in failures], ["feeder-won on-screen pixels"])
        detail = failures[0][1]
        self.assertIn("42 px", detail)
        self.assertIn("(0, 0, 7, 3)", detail)
        self.assertIn(SHOT_LABELS[LIVENESS_INDEX], detail)

    def test_adequacy_failure_on_a_non_liveness_shot_is_still_caught(self):
        # Rule 2 covers every compared shot, not just the one rule 1 watches.
        adequacy = _clean()
        adequacy[COMPARED_INDICES[0]] = {"changed": 5, "bbox": (1, 1, 2, 2)}
        failures = evaluate(adequacy, _firing_liveness(), True, -16)
        self.assertEqual([v for v, _ in failures], ["feeder-won on-screen pixels"])

    def test_all_three_rules_can_fire_together(self):
        adequacy = _clean()
        adequacy[COMPARED_INDICES[0]] = {"changed": 5, "bbox": (1, 1, 2, 2)}
        verdicts = [v for v, _ in evaluate(adequacy, _clean(), False, 0)]
        self.assertEqual(
            verdicts,
            ["vacuous instrument", "vacuous adequacy arm", "feeder-won on-screen pixels"],
        )

    def test_a_missing_liveness_shot_fails_rather_than_passes(self):
        # Defensive: an arm that captured nothing must not read as a pass.
        self.assertEqual(
            [v for v, _ in evaluate(_clean(), {}, True, -16)], ["vacuous instrument"]
        )


class TestHarnessConstants(unittest.TestCase):
    def test_compared_indices_are_within_the_label_table(self):
        for i in COMPARED_INDICES:
            self.assertLess(i, len(SHOT_LABELS))

    def test_liveness_shot_is_one_of_the_compared_shots(self):
        self.assertIn(LIVENESS_INDEX, COMPARED_INDICES)

    def test_no_rotated_shot_is_compared(self):
        # perf_grid's two yaw-0.35 shots are run-to-run non-deterministic on
        # this demo, so an identity claim over them would be noise (#3010).
        for label in ("zoom1_rot", "zoom4_rot", "zoom4_rot_pan"):
            self.assertNotIn(SHOT_LABELS.index(label), COMPARED_INDICES)


if __name__ == "__main__":
    unittest.main()
