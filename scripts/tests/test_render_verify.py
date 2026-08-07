"""Tests for render-verify.py — the ROI-crop + structural-metric gate (T-2)
and the manifest-driven demo resolution (#2919).

Proves the gate wiring added in epic #1766 T-2 without a GL/Metal build:

  * full-frame pixel-diff still passes/fails as before (backward compat);
  * a manifest-declared ROI crop is compared against a committed reference
    crop and FAILS on a seeded regression;
  * a manifest-declared structural gate runs render-shadow-metric.py and
    FAILS when the shadow exceeds its hole_ratio threshold (seeded
    swiss-cheese);
  * misconfigurations (unknown shot, missing reference, un-captured crop,
    unimplemented metric, threshold-less gate) are surfaced loudly.

Plus the resolution + sweep layer (#2919):

  * a target is resolved from the manifest that *declares* it, so a demo
    whose directory name doesn't match its target stays reachable;
  * the committed tree round-trips — every manifest resolves back to its own
    directory, so no demo can silently drop out of an --all sweep;
  * a demo that produces no checks reads as ERROR in the sweep summary
    rather than as a quietly smaller total.

Synthetic PNGs only — no engine, no committed references. Import the
dashed-name scripts via importlib, matching test_render_shadow_metric.py.
"""
import importlib.machinery
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
# render-verify.py does a bare `import verify_common` (#2461), which resolves
# only if scripts/ is on sys.path. Without this the suite dies at import when
# run on its own, and passes only when an alphabetically-earlier sibling in
# this directory happens to insert the path first (#2825).
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_rv = _load("render_verify", "render-verify.py")
_cmp = _load("render_compare", "render-compare.py")
write_png = _cmp.write_png

evaluate_shots = _rv.evaluate_shots
_run_structural_metric = _rv._run_structural_metric
_crop_capture_path = _rv._crop_capture_path
_parse_extra_runs = _rv._parse_extra_runs
_slice_capture = _rv._slice_capture
_declared_targets = _rv._declared_targets
_resolve_demo_dir = _rv._resolve_demo_dir
_target_to_demo_name = _rv._target_to_demo_name
_print_sweep_summary = _rv._print_sweep_summary

# The subject's own notion of the repo root, so a mis-rooted harness fails the
# tree-level guard below rather than being papered over by a local re-derivation.
REPO_ROOT = _rv.REPO_ROOT

MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)


def _write(path: Path, w: int, h: int, fn) -> None:
    """fn(x, y) -> (r, g, b); written as an 8-bit RGB PNG."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y)
            o = (y * w + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    write_png(str(path), w, h, bytes(buf), 3)


class RenderVerifyHarness(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.caps = root / "caps"            # captured screenshots
        self.refs = root / "refs"            # committed references (backend dir)
        self.diffs = root / "diffs"
        self.caps.mkdir()
        self.refs.mkdir()
        # One full frame per label, in manifest order: screenshot_<idx>.png.
        self.labels = ["shotA", "shotB"]
        self.frames = []
        for i, _label in enumerate(self.labels):
            p = self.caps / f"screenshot_{i:06d}.png"
            _write(p, 16, 16, lambda x, y: BLACK)
            self.frames.append(p)

    def tearDown(self):
        self._tmp.cleanup()

    def _ref(self, name: str, fn=lambda x, y: BLACK, w=16, h=16):
        _write(self.refs / name, w, h, fn)

    def _eval(self, crops=None, structural=None):
        return evaluate_shots(
            captured=self.frames,
            shot_labels=self.labels,
            ref_dir=self.refs,
            diff_dir=self.diffs,
            thresholds={},
            crops=crops,
            structural=structural,
        )

    def _row(self, rows, label):
        matches = [r for r in rows if r["label"] == label]
        self.assertEqual(len(matches), 1, f"expected exactly one row for {label}")
        return matches[0]

    # ── full-frame (backward compat) ─────────────────────────────────────
    def test_clean_frame_passes(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        rows = self._eval()
        self.assertEqual([r["kind"] for r in rows], ["frame", "frame"])
        self.assertTrue(all(r["pass"] for r in rows))

    def test_regressed_frame_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png", fn=lambda x, y: WHITE)  # ref differs from capture
        rows = self._eval()
        self.assertTrue(self._row(rows, "shotA")["pass"])
        self.assertFalse(self._row(rows, "shotB")["pass"])

    def test_missing_frame_reference_fails(self):
        self._ref("shotA.png")  # shotB.png absent
        rows = self._eval()
        b = self._row(rows, "shotB")
        self.assertFalse(b["pass"])
        self.assertIn("no reference", b["reason"])

    def test_backward_compat_no_gate_blocks(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        rows = self._eval(crops=None, structural=None)
        self.assertTrue(all(r["kind"] == "frame" for r in rows))
        self.assertEqual(len(rows), 2)

    # ── ROI crops ────────────────────────────────────────────────────────
    def _write_crop_capture(self, label, crop_label, fn):
        idx = self.labels.index(label)
        path = _crop_capture_path(self.frames[idx], label, crop_label)
        _write(path, 8, 8, fn)
        return path

    def test_clean_crop_passes(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: MAGENTA)
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        rows = self._eval(crops={"shotA": ["center"]})
        self.assertTrue(self._row(rows, "shotA:center")["pass"])

    def test_regressed_crop_fails(self):
        # Seeded regression: the captured crop diverges from its reference.
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: WHITE)
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertEqual(crop["kind"], "crop")
        self.assertFalse(crop["pass"])

    def test_missing_crop_reference_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: MAGENTA)
        # no shotA__crop_center.png reference committed
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertFalse(crop["pass"])
        self.assertIn("no reference", crop["reason"])

    def test_uncaptured_crop_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        # the crop PNG was never emitted by the demo
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertFalse(crop["pass"])
        self.assertIn("not captured", crop["reason"])

    def test_unknown_shot_in_crops_raises(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        with self.assertRaises(SystemExit):
            self._eval(crops={"nonexistent": ["center"]})

    # ── structural-metric gates ──────────────────────────────────────────
    def test_structural_shadow_clean_passes(self):
        # A solid magenta SHADOW overlay reads 0 holes -> within threshold.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32, lambda x, y: MAGENTA)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow", "max_hole_ratio": 0.05}]})
        struct = self._row(rows, "shotA:shadow")
        self.assertEqual(struct["kind"], "struct")
        self.assertTrue(struct["pass"])

    def test_structural_shadow_swiss_cheese_fails(self):
        # Seeded regression: a checkerboard SHADOW overlay (~50% holes,
        # exploded component count) exceeds the gate.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32,
               lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow",
                       "max_hole_ratio": 0.05, "max_components": 8}]})
        struct = self._row(rows, "shotA:shadow")
        self.assertFalse(struct["pass"])
        self.assertTrue(struct.get("reason"))

    def test_structural_roi_forwarded(self):
        # Left half shadow, right half lit; an ROI over the lit half is all
        # holes and fails, proving --roi is forwarded to the metric.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32, lambda x, y: MAGENTA if x < 16 else BLACK)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow", "roi": [16, 0, 16, 32],
                       "max_hole_ratio": 0.05}]})
        self.assertFalse(self._row(rows, "shotA:shadow")["pass"])

    def test_structural_unimplemented_metric_raises(self):
        # shadow/coverage/silhouette/clip are implemented; an unknown metric
        # name must still fail loudly (no render-<metric>-metric.py script).
        with self.assertRaises(SystemExit) as cm:
            _run_structural_metric(self.frames[0],
                                   {"metric": "nonexistent", "max_hole_ratio": 0.9},
                                   "shotA")
        self.assertIn("not", str(cm.exception).lower())

    def test_structural_no_thresholds_raises(self):
        with self.assertRaises(SystemExit):
            _run_structural_metric(self.frames[0], {"metric": "shadow"}, "shotA")

    def test_structural_missing_metric_key_raises(self):
        with self.assertRaises(SystemExit):
            _run_structural_metric(self.frames[0], {"max_hole_ratio": 0.05}, "shotA")

    def test_unknown_shot_in_structural_raises(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        with self.assertRaises(SystemExit):
            self._eval(structural={"nope": [{"metric": "shadow",
                                             "max_hole_ratio": 0.05}]})

    # ── structural-only shots (analytic oracle, no reference PNG) ─────────
    def _eval_so(self, structural, structural_only):
        return evaluate_shots(
            captured=self.frames,
            shot_labels=self.labels,
            ref_dir=self.refs,
            diff_dir=self.diffs,
            thresholds={},
            structural=structural,
            structural_only=structural_only,
        )

    def test_structural_only_skips_frame_diff_and_needs_no_reference(self):
        # shotA is structural_only: a clean magenta SHADOW capture with NO
        # committed reference. It produces no frame row (the pixel-diff is
        # skipped) but its structural gate still runs and passes. shotB keeps
        # its normal full-frame pixel-diff.
        self._ref("shotB.png")  # only shotB has a reference
        _write(self.frames[0], 32, 32, lambda x, y: MAGENTA)
        rows = self._eval_so(
            structural={"shotA": [{"metric": "shadow",
                                   "min_largest_frac": 0.8, "max_components": 6}]},
            structural_only={"shotA"},
        )
        frame_labels = [r["label"] for r in rows if r["kind"] == "frame"]
        self.assertNotIn("shotA", frame_labels)  # no pixel-diff, no ref needed
        self.assertIn("shotB", frame_labels)
        struct = self._row(rows, "shotA:shadow")
        self.assertEqual(struct["kind"], "struct")
        self.assertTrue(struct["pass"])

    def test_structural_only_gate_still_fails_on_regression(self):
        # The structural gate must still bite for a structural_only shot — a
        # swiss-cheese capture fails even though it's exempt from pixel-diff.
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32,
               lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        rows = self._eval_so(
            structural={"shotA": [{"metric": "shadow",
                                   "min_largest_frac": 0.8, "max_components": 6}]},
            structural_only={"shotA"},
        )
        self.assertFalse(self._row(rows, "shotA:shadow")["pass"])

    # ── crop-path derivation ─────────────────────────────────────────────
    def test_crop_capture_path_pairs_with_frame_index(self):
        frame = Path("/x/save/screenshot_000007.png")
        got = _crop_capture_path(frame, "shotA", "center_cube_top")
        self.assertEqual(
            got.name, "screenshot_000007_shotA__crop_center_cube_top.png")
        self.assertEqual(got.parent, frame.parent)

    # ── extra-run missing-reference skip (cross-host handoff) ────────────
    def test_missing_ref_is_skip_yields_nonfatal_skip(self):
        # An extra_runs pass whose backend reference isn't committed yet must
        # SKIP (pass=True, kind=skip) instead of failing — the reference is
        # blessed per-host, so a host that hasn't captured it should not fail.
        rows = evaluate_shots(
            captured=self.frames, shot_labels=self.labels, ref_dir=self.refs,
            diff_dir=self.diffs, thresholds={},
            missing_ref_is_skip=True, backend="macos-debug")
        self.assertEqual([r["kind"] for r in rows], ["skip", "skip"])
        self.assertTrue(all(r["pass"] for r in rows))
        self.assertIn("macos-debug", self._row(rows, "shotA")["reason"])

    def test_missing_ref_is_skip_still_gates_present_reference(self):
        # When the reference IS present the skip flag is inert — a real
        # regression still fails even in extra-run mode.
        self._ref("shotA.png")                         # matches capture -> pass
        self._ref("shotB.png", fn=lambda x, y: WHITE)  # diverges -> fail
        rows = evaluate_shots(
            captured=self.frames, shot_labels=self.labels, ref_dir=self.refs,
            diff_dir=self.diffs, thresholds={},
            missing_ref_is_skip=True, backend="macos-debug")
        self.assertEqual(self._row(rows, "shotA")["kind"], "frame")
        self.assertTrue(self._row(rows, "shotA")["pass"])
        self.assertFalse(self._row(rows, "shotB")["pass"])  # diverged -> fail


class SliceCapture(unittest.TestCase):
    def _caps(self, n):
        return [Path(f"screenshot_{i:06d}.png") for i in range(n)]

    def test_positive_offset(self):
        caps = self._caps(15)
        got = _slice_capture(caps, 12, 2, "compare")
        self.assertEqual([p.name for p in got],
                         ["screenshot_000012.png", "screenshot_000013.png"])

    def test_negative_offset_indexes_from_tail(self):
        # -3 with 2 shots over 15 captures -> indices 12,13 (skips the last,
        # e.g. an ungated compare_detached). Robust to leading-shot growth.
        caps = self._caps(15)
        got = _slice_capture(caps, -3, 2, "compare")
        self.assertEqual([p.name for p in got],
                         ["screenshot_000012.png", "screenshot_000013.png"])
        # Same -3 still lands the tail block after a leading shot is added.
        caps2 = self._caps(16)
        got2 = _slice_capture(caps2, -3, 2, "compare")
        self.assertEqual([p.name for p in got2],
                         ["screenshot_000013.png", "screenshot_000014.png"])

    def test_offset_out_of_range_raises(self):
        with self.assertRaises(SystemExit):
            _slice_capture(self._caps(3), 12, 2, "compare")
        with self.assertRaises(SystemExit):
            _slice_capture(self._caps(2), -3, 2, "compare")


class ParseExtraRuns(unittest.TestCase):
    def test_absent_block_is_empty(self):
        self.assertEqual(_parse_extra_runs({}), [])

    def test_valid_entry_normalizes_defaults(self):
        runs = _parse_extra_runs({"extra_runs": [{
            "name": "compare",
            "demo_args": ["--only", "compare"],
            "shots": ["compare_yaw0", "compare_yaw_q"],
            "capture_offset": -3,
        }]})
        self.assertEqual(len(runs), 1)
        r = runs[0]
        self.assertEqual(r["name"], "compare")
        self.assertEqual(r["demo_args"], ["--only", "compare"])
        self.assertEqual(r["shots"], ["compare_yaw0", "compare_yaw_q"])
        self.assertEqual(r["capture_offset"], -3)
        self.assertIsNone(r["warmup"])          # inherits top-level
        self.assertIsNone(r["thresholds"])      # inherits top-level
        self.assertEqual(r["crops"], {})
        self.assertEqual(r["structural_only"], set())

    def test_default_offset_is_zero(self):
        runs = _parse_extra_runs({"extra_runs": [{
            "name": "x", "demo_args": ["--flag"], "shots": ["s"]}]})
        self.assertEqual(runs[0]["capture_offset"], 0)

    def test_rejects_bad_entries(self):
        bad = [
            {"demo_args": ["--x"], "shots": ["s"]},                    # no name
            {"name": "", "demo_args": ["--x"], "shots": ["s"]},        # empty name
            {"name": "x", "shots": ["s"]},                            # no demo_args
            {"name": "x", "demo_args": [], "shots": ["s"]},            # empty demo_args
            {"name": "x", "demo_args": "--x", "shots": ["s"]},        # demo_args not list
            {"name": "x", "demo_args": ["--x"]},                      # no shots
            {"name": "x", "demo_args": ["--x"], "shots": []},          # empty shots
            {"name": "x", "demo_args": ["--x"], "shots": ["s"],
             "capture_offset": "tail"},                               # offset not int
        ]
        for entry in bad:
            with self.assertRaises(SystemExit):
                _parse_extra_runs({"extra_runs": [entry]})

    def test_rejects_duplicate_names(self):
        with self.assertRaises(SystemExit):
            _parse_extra_runs({"extra_runs": [
                {"name": "dup", "demo_args": ["--a"], "shots": ["s"]},
                {"name": "dup", "demo_args": ["--b"], "shots": ["t"]},
            ]})

    def test_rejects_non_list_block(self):
        with self.assertRaises(SystemExit):
            _parse_extra_runs({"extra_runs": {"name": "x"}})


class DemoResolution(unittest.TestCase):
    """Manifest-declared target -> demo dir (#2919).

    The pre-fix harness inferred the directory from the target name, which is
    wrong for any demo whose directory doesn't echo its target. These build a
    synthetic demo tree so the cases are exercised without the real manifests.
    """

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.worktree = Path(self._tmp.name)
        self.demos = self.worktree / "creations" / "demos"

    def tearDown(self):
        self._tmp.cleanup()

    def _demo(self, name: str, target: str | None, *, raw: str | None = None):
        d = self.demos / name
        refs = d / "test" / "references"
        refs.mkdir(parents=True, exist_ok=True)
        body = raw if raw is not None else json.dumps(
            {"demo": name, "target": target, "shots": ["s0"]})
        (refs / "manifest.json").write_text(body)
        return d

    # ── _declared_targets ────────────────────────────────────────────────
    def test_maps_every_declaring_manifest(self):
        self._demo("shape_debug", "IRShapeDebug")
        self._demo("lighting", "IRLightingSdfBlocker")
        self.assertEqual(_declared_targets(self.worktree),
                         {"IRLightingSdfBlocker": "lighting",
                          "IRShapeDebug": "shape_debug"})

    def test_manifest_without_target_is_ignored_not_fatal(self):
        self._demo("legacy", None)          # "target": null
        self._demo("lighting", "IRLightingSdfBlocker")
        self.assertEqual(_declared_targets(self.worktree),
                         {"IRLightingSdfBlocker": "lighting"})

    def test_unreadable_manifest_is_skipped_with_a_warning(self):
        # One malformed manifest must not make every OTHER target
        # unresolvable — the sibling still resolves, and the skip is announced.
        self._demo("broken", None, raw="{not json")
        self._demo("lighting", "IRLightingSdfBlocker")
        err = io.StringIO()
        with redirect_stderr(err):
            got = _declared_targets(self.worktree)
        self.assertEqual(got, {"IRLightingSdfBlocker": "lighting"})
        self.assertIn("broken", err.getvalue())

    def test_two_manifests_declaring_one_target_raises(self):
        self._demo("a", "IRDup")
        self._demo("b", "IRDup")
        with self.assertRaises(SystemExit) as cm:
            _declared_targets(self.worktree)
        self.assertIn("IRDup", str(cm.exception))

    # ── _resolve_demo_dir ────────────────────────────────────────────────
    def test_declaration_beats_name_inference(self):
        # IRLightingSdfBlocker infers `lighting_sdf_blocker`, which does not
        # exist; the manifest says `lighting`, and the manifest wins.
        self._demo("lighting", "IRLightingSdfBlocker")
        self.assertEqual(_target_to_demo_name("IRLightingSdfBlocker"),
                         "lighting_sdf_blocker")   # the inference is still wrong
        got = _resolve_demo_dir(self.worktree, "IRLightingSdfBlocker", None)
        self.assertEqual(got, self.demos / "lighting")

    def test_matching_demo_is_unchanged(self):
        self._demo("shape_debug", "IRShapeDebug")
        got = _resolve_demo_dir(self.worktree, "IRShapeDebug", None)
        self.assertEqual(got, self.demos / "shape_debug")

    def test_explicit_demo_overrides_the_declaration(self):
        self._demo("lighting", "IRLightingSdfBlocker")
        self._demo("other", "IROther")
        got = _resolve_demo_dir(self.worktree, "IRLightingSdfBlocker", "other")
        self.assertEqual(got, self.demos / "other")

    def test_explicit_demo_that_does_not_exist_raises(self):
        self._demo("lighting", "IRLightingSdfBlocker")
        with self.assertRaises(SystemExit) as cm:
            _resolve_demo_dir(self.worktree, "IRLightingSdfBlocker", "nope")
        self.assertIn("demo dir not found", str(cm.exception))

    def test_falls_back_to_inference_when_nothing_declares_the_target(self):
        # A demo dir that exists but whose manifest names no target still
        # resolves the old way, so pre-declaration demos keep working.
        self._demo("fog_demo", None)
        got = _resolve_demo_dir(self.worktree, "IRFogDemo", None)
        self.assertEqual(got, self.demos / "fog_demo")

    def test_unknown_target_names_both_attempts(self):
        self._demo("lighting", "IRLightingSdfBlocker")
        with self.assertRaises(SystemExit) as cm:
            _resolve_demo_dir(self.worktree, "IRNoSuchThing", None)
        msg = str(cm.exception)
        self.assertIn("manifest.json", msg)      # where it looked for a decl
        self.assertIn("no_such_thing", msg)      # what it inferred
        self.assertIn("--demo", msg)             # how to fix it


class CommittedManifestsResolve(unittest.TestCase):
    """Coverage guard against the real tree — no build, no demo run.

    #2919's damage was a demo silently dropping out of a multi-target sweep.
    The property that prevents it is that every committed manifest is
    reachable from its own declared target with no `--demo` override, so this
    asserts the round-trip over whatever manifests the tree currently ships
    rather than pinning a demo list that would need editing on every addition.
    """

    def _manifests(self):
        return sorted((REPO_ROOT / "creations" / "demos")
                      .glob(_rv.MANIFEST_GLOB))

    def test_every_committed_manifest_declares_a_target(self):
        missing = [str(p.relative_to(REPO_ROOT)) for p in self._manifests()
                   if not json.loads(p.read_text()).get("target")]
        self.assertEqual(missing, [], "manifests with no 'target' are "
                                      "unreachable from --all")

    def test_every_committed_manifest_round_trips(self):
        declared = _declared_targets(REPO_ROOT)
        self.assertEqual(len(declared), len(self._manifests()),
                         "a committed manifest dropped out of --all's demo set")
        for target, demo in declared.items():
            with self.subTest(target=target):
                self.assertEqual(
                    _resolve_demo_dir(REPO_ROOT, target, None),
                    REPO_ROOT / "creations" / "demos" / demo)

    def test_the_tree_still_contains_a_demo_inference_gets_wrong(self):
        # If this ever fails because every demo dir matches its target, the
        # inference fallback is no longer load-bearing — but the failure must
        # be read, not silenced: it means the fixture for this bug is gone.
        declared = _declared_targets(REPO_ROOT)
        mismatched = {t: d for t, d in declared.items()
                      if _target_to_demo_name(t) != d}
        self.assertIn("IRLightingSdfBlocker", mismatched)
        self.assertEqual(mismatched["IRLightingSdfBlocker"], "lighting")


class SweepSummary(unittest.TestCase):
    def _summary(self, results):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            _print_sweep_summary(results)
        return out.getvalue(), err.getvalue()

    def _row(self, target, demo, rc, checked, failed=0, skipped=0, error=None):
        r = {"target": target, "demo": demo, "rc": rc, "checked": checked,
             "failed": failed, "skipped": skipped}
        if error:
            r["error"] = error
        return r

    def test_total_sums_every_demo(self):
        out, err = self._summary([
            self._row("IRShapeDebug", "shape_debug", 0, 24),
            self._row("IRLightingSdfBlocker", "lighting", 1, 5, failed=4),
        ])
        self.assertIn("total: 29 checks across 2 demos, 4 FAIL", out)
        self.assertEqual(err, "")

    def test_a_demo_that_produced_no_checks_reads_as_error(self):
        # The #2919 failure mode, one level up: a demo contributing zero must
        # not look like a demo that simply had less to check. It is named on
        # stderr AND its row says ERROR, so a smaller total can't pass as a
        # complete sweep.
        out, err = self._summary([
            self._row("IRShapeDebug", "shape_debug", 0, 24),
            self._row("IRLightingSdfBlocker", "lighting", 1, 0,
                      error="demo dir not found"),
        ])
        self.assertIn("ERROR (demo dir not found)", out)
        self.assertIn("produced NO checks", err)
        self.assertIn("IRLightingSdfBlocker", err)

    def test_clean_sweep_says_nothing_on_stderr(self):
        out, err = self._summary([
            self._row("IRShapeDebug", "shape_debug", 0, 24),
            self._row("IRFogDemo", "fog_demo", 0, 15),
        ])
        self.assertIn("total: 39 checks across 2 demos, 0 FAIL", out)
        self.assertEqual(err, "")


if __name__ == "__main__":
    unittest.main()
