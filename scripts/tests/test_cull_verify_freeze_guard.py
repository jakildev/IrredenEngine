"""Positive controls for cull-verify's cull-freeze guard, without a GPU.

``scripts/cull-verify.py``'s image assertion is relative — ``cv_live_i`` against
``cv_frozen_i`` from the same run — and it exists because a freeze that stops
pinning the viewport turns phase 2 into a second live capture: every pair goes
byte-identical and the harness reports a green pass with the mechanism it exists
to exercise entirely disabled. The four freeze-guard arms read the cull viewport
the frames were culled against instead of the frames themselves.

A guard is only worth its runtime if it is shown to fire. These cases drive the
arms on synthetic transcripts — the failing transcripts are what the demo emits
under each defect — so every arm has a red case that runs on any machine, with
no engine, no build, no GL/Metal and no demo launch.

Arm (a)'s empty-transcript case is the load-bearing one: a matcher that silently
dropped every line would make this guard the second vacuous gate in the same
harness.

Import the dashed-name script via importlib, matching
test_cull_verify_contract.py.
"""
import importlib.machinery
import importlib.util
import sys
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_cv = _load("cull_verify", "cull-verify.py")

# The live phase's cull_cam values, in capture order. Lifted from a real
# macOS/Metal --cull-validate run so the synthetic transcripts below differ from
# a true one only where a case deliberately injects a defect.
_LIVE_CAMS = [
    (0.0, 0.0), (-4.1855, 2.5621), (-5.3333, 7.3333), (-2.7712, 11.5188),
    (2.0006, 12.6666), (6.1854, 10.1046), (7.3333, 5.3333), (4.7712, 1.1479),
    (-2.1820, -3.2383), (4.1820, 4.0668), (12.0780, 7.8256), (-17.2669, -4.0194),
]
_SWEEP_ZOOM = 4.0
_REF_ZOOM = 1.0
_CANVAS = (642, 722)


def _line(label, *, frozen, cull_frozen, cull_cam, cull_zoom, cam, zoom,
          cull_canvas=_CANVAS):
    return (
        f"[2026-09-12 00:00:00] [info] [cull-validate] DOMAIN-STATE "
        f"shot={label} index=0 frozen={int(frozen)} "
        f"cull_frozen={int(cull_frozen)} "
        f"cull_cam={cull_cam[0]:.4f},{cull_cam[1]:.4f} "
        f"cull_zoom={cull_zoom:.4f},{cull_zoom:.4f} "
        f"cull_canvas={cull_canvas[0]},{cull_canvas[1]} "
        f"cam={cam[0]:.4f},{cam[1]:.4f} zoom={zoom:.4f},{zoom:.4f}"
    )


def working_transcript(sweep_zoom=_SWEEP_ZOOM, ref_zoom=_REF_ZOOM):
    """What a correct build emits: live cull tracks, frozen cull is pinned."""
    lines = [
        "Cull-validate sweep: 12 poses/phase, 26 total shots (live + frozen)",
    ]
    for i, cam in enumerate(_LIVE_CAMS):
        lines.append(_line(
            f"cv_live_{i:03d}", frozen=False, cull_frozen=False,
            cull_cam=cam, cull_zoom=sweep_zoom, cam=cam, zoom=sweep_zoom))
    lines.append(_line(
        _cv.FREEZE_REF_LABEL, frozen=True, cull_frozen=True,
        cull_cam=(0.0, 0.0), cull_zoom=ref_zoom, cam=(0.0, 0.0), zoom=ref_zoom))
    for i, cam in enumerate(_LIVE_CAMS):
        lines.append(_line(
            f"cv_frozen_{i:03d}", frozen=True, cull_frozen=True,
            cull_cam=(0.0, 0.0), cull_zoom=ref_zoom, cam=cam, zoom=sweep_zoom))
    lines.append(_line(
        _cv.UNFREEZE_LABEL, frozen=False, cull_frozen=False,
        cull_cam=(0.0, 0.0), cull_zoom=sweep_zoom, cam=(0.0, 0.0),
        zoom=sweep_zoom))
    return "\n".join(lines) + "\n"


def vacuous_transcript():
    """The issue's repro: setCullingFrozen stubbed to a no-op.

    The flag never latches, so the frozen phase re-walks the pose list with a
    live cull. In pixels this is byte-identical to the working run at nine of
    twelve poses and *better* than it at the other three; in viewport state it
    is unmistakable.
    """
    lines = []
    for i, cam in enumerate(_LIVE_CAMS):
        lines.append(_line(
            f"cv_live_{i:03d}", frozen=False, cull_frozen=False,
            cull_cam=cam, cull_zoom=_SWEEP_ZOOM, cam=cam, zoom=_SWEEP_ZOOM))
    lines.append(_line(
        _cv.FREEZE_REF_LABEL, frozen=False, cull_frozen=False,
        cull_cam=(0.0, 0.0), cull_zoom=_REF_ZOOM, cam=(0.0, 0.0),
        zoom=_REF_ZOOM))
    for i, cam in enumerate(_LIVE_CAMS):
        lines.append(_line(
            f"cv_frozen_{i:03d}", frozen=False, cull_frozen=False,
            cull_cam=cam, cull_zoom=_SWEEP_ZOOM, cam=cam, zoom=_SWEEP_ZOOM))
    lines.append(_line(
        _cv.UNFREEZE_LABEL, frozen=False, cull_frozen=False,
        cull_cam=(0.0, 0.0), cull_zoom=_SWEEP_ZOOM, cam=(0.0, 0.0),
        zoom=_SWEEP_ZOOM))
    return "\n".join(lines) + "\n"


def _mutate(text, old, new, count=-1):
    """Replace and assert the replacement landed.

    A case whose mutation silently matched nothing would assert a defect it
    never injected — a red test that cannot go green, or worse, a green one
    that proves nothing. That is the failure class this whole guard exists for,
    so the test bodies do not get to make it either.
    """
    assert old in text, f"mutation target not present: {old!r}"
    return text.replace(old, new) if count < 0 else text.replace(old, new, count)


def _rewrite_frozen_cull_viewport_to_live(text):
    """The flag latched, but updateCullViewport stopped honouring it.

    Every cv_frozen_* line keeps frozen=1/cull_frozen=1 and reports the live
    camera's viewport, so arm (b) stays silent and only arm (c) can catch it.
    """
    out = []
    for line in text.splitlines():
        if "shot=cv_frozen_" in line:
            live_cam = line.split(" cam=")[1].split(" ")[0]
            live_zoom = line.split(" zoom=")[1].split(" ")[0]
            pinned_cam = line.split("cull_cam=")[1].split(" ")[0]
            pinned_zoom = line.split("cull_zoom=")[1].split(" ")[0]
            line = line.replace(f"cull_cam={pinned_cam}", f"cull_cam={live_cam}")
            line = line.replace(f"cull_zoom={pinned_zoom}", f"cull_zoom={live_zoom}")
        out.append(line)
    return "\n".join(out) + "\n"


def _pin_live_cull_cam(text):
    """A cull that never updates: the live phase's cull_cam never moves."""
    out = []
    for line in text.splitlines():
        if "shot=cv_live_" in line:
            pinned = line.split("cull_cam=")[1].split(" ")[0]
            line = line.replace(f"cull_cam={pinned}", "cull_cam=0.0000,0.0000")
        out.append(line)
    return "\n".join(out) + "\n"


class TestWorkingBuildIsGreen(unittest.TestCase):
    """The control: every arm must be silent on a correct run.

    Without this, an arm that always fires would look like a working guard.
    """

    def test_no_failures(self):
        failures, _ = _cv.run_freeze_guard(working_transcript())
        self.assertEqual(failures, [], msg="\n".join(failures))

    def test_parses_one_line_per_captured_frame(self):
        states = _cv.parse_cull_states(working_transcript())
        self.assertEqual(len(states), _cv.TOTAL_SHOTS)
        self.assertEqual([s["shot"] for s in states], _cv.ALL_LABELS)

    def test_unknown_fields_are_ignored(self):
        # Lenient parse: the demo may grow a field without breaking the harness.
        text = _mutate(working_transcript(), "index=0", "index=0 future_field=7", 1)
        failures, _ = _cv.run_freeze_guard(text)
        self.assertEqual(failures, [], msg="\n".join(failures))


class TestArmACensus(unittest.TestCase):
    """(a) Zero parsed lines is a failure, never a silent pass."""

    def test_empty_transcript_fails(self):
        failures = _cv.check_state_census([], _cv.ALL_LABELS)
        self.assertTrue(failures)
        self.assertIn("cull state not emitted", failures[0])

    def test_empty_transcript_fails_through_the_entry_point(self):
        failures, _ = _cv.run_freeze_guard("")
        self.assertTrue(
            any("cull state not emitted" in f for f in failures),
            msg=("a transcript with no state lines must fail loudly — a matcher "
                 "that drops every line is how this guard becomes vacuous"))

    def test_short_transcript_fails(self):
        text = "\n".join(working_transcript().splitlines()[:6]) + "\n"
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("cull state census" in f for f in failures))

    def test_label_drift_fails(self):
        text = _mutate(working_transcript(), "shot=cv_frozen_003", "shot=cv_frozen_004")
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("no longer align" in f for f in failures))

    def test_missing_required_field_is_named(self):
        text = _mutate(working_transcript(), " cull_canvas=642,722", "", 1)
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("omits cull_canvas" in f for f in failures))


class TestArmBEngagement(unittest.TestCase):
    """(b) The arm that fires on the issue's stubbed-setter repro."""

    def test_vacuous_run_fails_and_names_the_freeze(self):
        failures, _ = _cv.run_freeze_guard(vacuous_transcript())
        self.assertTrue(
            any("cull freeze not engaged" in f for f in failures),
            msg=("with setCullingFrozen stubbed to a no-op the images are "
                 "byte-identical; only the state lines can say so"))

    def test_frozen_phase_reporting_unfrozen_fails(self):
        text = _mutate(
            working_transcript(),
            "shot=cv_frozen_005 index=0 frozen=1",
            "shot=cv_frozen_005 index=0 frozen=0")
        failures = _cv.check_freeze_engagement(_cv.parse_cull_states(text))
        self.assertTrue(any("cull freeze not engaged" in f for f in failures))

    def test_honoured_flag_fails_independently_of_the_written_flag(self):
        # The setter latched but updateCullViewport stopped honouring it — the
        # defect class arm (b) can only separate because both flags are emitted.
        text = _mutate(
            working_transcript(), "frozen=1 cull_frozen=1", "frozen=1 cull_frozen=0")
        failures = _cv.check_freeze_engagement(_cv.parse_cull_states(text))
        self.assertTrue(any("cull_frozen" in f for f in failures))
        self.assertFalse(any("frozen=1, expected" in f for f in failures))

    def test_live_phase_reporting_frozen_fails(self):
        text = _mutate(
            working_transcript(),
            "shot=cv_live_002 index=0 frozen=0 cull_frozen=0",
            "shot=cv_live_002 index=0 frozen=1 cull_frozen=1")
        failures = _cv.check_freeze_engagement(_cv.parse_cull_states(text))
        self.assertTrue(any("cull freeze not released" in f for f in failures))


class TestArmCPinning(unittest.TestCase):
    """(c) Survives a freeze whose flag stays true while the viewport moves."""

    def test_frozen_phase_tracking_the_live_camera_fails(self):
        # Flags still report frozen; the viewport is live anyway.
        text = _rewrite_frozen_cull_viewport_to_live(working_transcript())
        self.assertNotEqual(text, working_transcript())
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("cull viewport not pinned" in f for f in failures))
        self.assertFalse(
            any("cull freeze not engaged" in f for f in failures),
            msg="arm (b) reads flags only; this defect leaves them correct")

    def test_one_drifting_frozen_shot_fails(self):
        text = _mutate(
            working_transcript(),
            "shot=cv_frozen_007 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=0.0000,0.0000",
            "shot=cv_frozen_007 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=3.5000,-1.2500")
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("cv_frozen_007" in f and "not pinned" in f
                            for f in failures))

    def test_canvas_drift_fails(self):
        text = _mutate(
            working_transcript(),
            "shot=cv_frozen_001 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=0.0000,0.0000 cull_zoom=1.0000,1.0000 "
            "cull_canvas=642,722",
            "shot=cv_frozen_001 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=0.0000,0.0000 cull_zoom=1.0000,1.0000 "
            "cull_canvas=640,720")
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("cull_canvas" in f for f in failures))

    def test_zoom_conjunct_degrades_when_sweep_zoom_equals_the_reference(self):
        # `--zoom 1` makes pinned zoom equal live zoom in a CORRECT build, so
        # the differs-from-live half must stand down rather than false-red.
        text = working_transcript(sweep_zoom=1.0, ref_zoom=1.0)
        self.assertFalse(
            _cv.pinning_is_zoom_discriminating(_cv.parse_cull_states(text)))
        failures, notes = _cv.run_freeze_guard(text)
        self.assertEqual(failures, [], msg="\n".join(failures))
        self.assertTrue(any("constancy-only" in n for n in notes))

    def test_constancy_half_still_fires_at_sweep_zoom_one(self):
        # The half that survives the degradation above must still catch a
        # frozen phase whose cull viewport moves.
        text = _mutate(
            working_transcript(sweep_zoom=1.0, ref_zoom=1.0),
            "shot=cv_frozen_004 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=0.0000,0.0000",
            "shot=cv_frozen_004 index=0 frozen=1 cull_frozen=1 "
            "cull_cam=2.0006,12.6666")
        failures, _ = _cv.run_freeze_guard(text)
        self.assertTrue(any("cull viewport not pinned" in f for f in failures))


class TestArmDLiveTracking(unittest.TestCase):
    """(d) Closes the escape where a cull that never updates passes (c)."""

    def test_live_phase_that_never_moves_the_cull_fails(self):
        text = _pin_live_cull_cam(working_transcript())
        self.assertNotEqual(text, working_transcript())
        failures = _cv.check_live_tracking(_cv.parse_cull_states(text))
        self.assertTrue(failures)
        self.assertIn("cull viewport never moved", failures[0])

    def test_working_live_phase_passes(self):
        failures = _cv.check_live_tracking(
            _cv.parse_cull_states(working_transcript()))
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
