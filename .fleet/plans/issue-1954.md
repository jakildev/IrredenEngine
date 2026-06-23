## Plan: extend the jitter-validation harness to canvas_stress (mixed DETACHED + GRID rotation)

- **Issue:** #1954
- **Model:** opus
- **Date:** 2026-06-21
- **Part of:** epic #1881 (validation); follow-up to #1944 / #1953

### Revision history
- **2026-06-23 — re-scoped to Option A (architect ruling on PR #1972).** The
  original plan assumed `render-jitter-metric.py` (temporal luminance-curvature)
  would separate the #1942 build from the #1953-fixed build, so the acceptance
  required the gate to **FAIL pre-#1953**. Implementer verify-before-implement
  disproved that empirically: the temporal metric is **blind to #1942 by
  construction** — #1942 is a *smooth* ~½-trixel world↔detached **spatial**
  registration drift that lives at the moving silhouette boundary, and the metric
  (a) keys on the *second* temporal difference precisely to ignore smooth sub-pixel
  motion and (b) AND-masks the moving silhouette out of the interior. Measured Δ
  (≤0.07 jitter_score) sat below the re-voxelize speckle floor's own run-to-run
  variation. The #1942 spatial drift is already gated by **#1944's render-verify
  reference refresh** (pixel-diff vs committed refs). Architect ruling: **Option A**
  — re-scope this harness to the temporal rotation-**crawl** family (seam / band /
  speckle shimmer) it actually measures, **drop the "#1942 must FAIL" acceptance**,
  and **threshold against the measured master speckle floor (~4.4), not zero**. The
  continuous-yaw spatial-drift metric (Option B) is a genuinely useful but *distinct*
  tool; it is NOT filed now (static render-verify refs cover #1942 today) and would
  belong in its own task under #1884/#1881 if a later regression slips between
  committed shots. Task-local re-scope; no design doc.

### Scope
Add a `canvas_stress` sibling to the #1922 temporal-jitter harness so the per-entity
rotation **temporal crawl** (seam / band / speckle shimmer) on the mixed
DETACHED + GRID rotation sweep is gated by a one-command check — the multi-canvas-type
temporal coverage gap #1922 deferred. **Reuse `scripts/render-jitter-metric.py`
unchanged**; add a `scripts/dev/canvas-stress-rotate-jitter-sweep` wrapper mirroring
`scripts/dev/shape-rotate-jitter-sweep`; add a `canvas_stress` baseline + threshold
section to `docs/design/jitter-validation-harness.md`. The gate fails on temporal-crawl
regressions **above** the measured master speckle floor and passes on current master.
The #1942 *spatial*-registration drift is out of scope here — it is owned by #1944's
render-verify reference gate, a different metric class.

### Landing state
No #1953 sequencing prerequisite remains (that was tied to the dropped "FAIL
pre-#1953" acceptance). The re-scoped gate passes on **current master** by
construction — it brackets temporal crawl above the inherent round-to-cell speckle
floor, which is present on master regardless of #1953. The wrapper is a manual /
on-demand gate, so a future temporal-crawl regression (e.g. a re-voxelize change that
worsens the speckle) is what would turn it red.

### Verified current state
- canvas_stress already has the sweep mode: `--sweep-yaw <from> <to> <count>`
  (`main.cpp:743`) emits `count` evenly-spaced full-frame shots `sweep_yaw_0..N-1`
  from `from`→`to` yaw. **Sweep mode auto-disables entity auto-rotation**
  (`main.cpp:772-777`), so `--no-auto-rotate` is redundant-but-harmless — the sweep
  isolates pure camera-yaw motion (entities static in world space, which is the
  #1944 surface).
- Unlike shape_debug's `--spin-shape` (single centred shape → auto `__crop_center`
  PNG), the canvas_stress sweep emits **full frames, no auto crop**. The metric
  scopes via `--roi` but still decodes each full PNG, so the step count must be
  bounded (pure-Python decode of 2560×1440 frames is the cost the sibling sidesteps
  with small crops).
- The detached re-voxelize solids + GRID spin cubes sit on the **screen-center
  column** (`main.cpp` framing comments), so under yaw-about-center they stay near
  center → a CENTRAL ROI has a stable all-frames interior holding the DETACHED +
  GRID comparison.
- Existing harness pieces: `render-jitter-metric.py` (second-temporal-difference of
  luminance; backend-agnostic, zoom-robust; emits JSON + pass/fail), wrapper
  `shape-rotate-jitter-sweep`, doc `jitter-validation-harness.md` (Metal baseline
  table + "Choosing the gate" + OpenGL/Linux deferred-baseline section).

### Approach
1. Branch off `origin/master`; commit `.fleet/plans/issue-1954.md` first.
2. Add `scripts/dev/canvas-stress-rotate-jitter-sweep` (chmod +x), mirroring the
   sibling's structure (self-locating; cmake + python3 stdlib only;
   nproc/macOS-sysctl fallback; `find -delete` not `rm` glob):
   - Build: `cmake --build "$BUILD_DIR" --target IRCanvasStress`.
   - Capture: `./IRCanvasStress --sweep-yaw <from> <to> <steps> --auto-screenshot
     <warmup>` (sweep auto-disables auto-rotate). Default a **focused** range +
     bounded steps so sub-degree per-frame jitter is sampled without minutes of
     full-frame decode. `--sweep-yaw` interpolates yaw in **radians** and full-frame
     decode is ~2.3 s/frame at 2560×1440, so keep steps bounded (~30–40) over a
     focused range — e.g. `--sweep-yaw 0 0.2618 36` (15°, ~0.42°/step). Make
     from/to/steps/zoom overridable (positional args + env, like the sibling).
   - Score: feed the captured full frames to `render-jitter-metric.py` with a
     CENTRAL `--roi` (the detached+GRID column), deriving the ROI from a frame's
     IHDR size (zoom/host robust) like the sibling — e.g. central ~40-50%.
   - Gate on `MAX_JITTER` (env-overridable); exit non-zero on FAIL.
3. Tune the threshold (mirror the doc's "Choosing the gate"): measure `jitter_score`
   on **current master**. CRITICAL measurement correction (see Revision history /
   Gotchas): the plan + the architect's escalation reply both assumed the sweep
   *froze* the entities ("isolates pure camera-yaw motion, entities static"). It
   does NOT — `--sweep-yaw` disables only the *camera* auto-yaw (`autoRotate_`,
   main.cpp:841); the per-entity self-spin is gated by `noSpin_` (main.cpp:1228/
   1407/1423) and keeps running ~30° between the 60-settle-frame shots. That coarse
   self-spin aliases the metric (it needs a fine ≤~2°/step sweep) up to ~3.7–4.4 —
   which is where the architect's ~4.4 "floor" came from, NOT speckle. Passing
   `--no-spin` actually delivers the intended frozen-entity / pure-camera-yaw fine
   sweep, and the true floor is **jitter_score = 1.19** (central-half ROI, 15°/
   36-step), the same clean regime as the sibling's smooth shapes (~1.2). Run-to-run
   is **byte-identical** (fixed poses + frozen entities + deterministic re-voxelize
   bake → deterministic capture), so the gate is a hard oracle, not flaky. Set
   `MAX_JITTER = 4.0` — mirrors the sibling's gate (same metric, same ~1.2 floor),
   >3× margin above 1.19, below where a real crawl regression lands (the sibling's
   failing shapes hit 6–11). Document the floor + the --no-spin requirement in the
   wrapper so a future reader doesn't re-derive the false ~4.4.
4. Add a "canvas_stress (mixed DETACHED + GRID)" section to
   `jitter-validation-harness.md`: measured pre/post-#1953 numbers, the chosen ROI +
   range/steps, the threshold + rationale, and an OpenGL/Linux note (Linux baseline
   captured by running the identical command on a Linux host — defer the number like
   the shape_debug doc does).
5. Validate: wrapper exits zero on current master with the chosen threshold, and a
   deliberately tightened `MAX_JITTER` below the floor flips it to non-zero — proving
   the gate has teeth (would catch a crawl regression that pushes the score above the
   floor). Record the measured master numbers for the PR body + doc.
6. `commit-and-push` (`Closes #1954`).

### Affected files
- `scripts/dev/canvas-stress-rotate-jitter-sweep` — NEW wrapper (executable)
- `docs/design/jitter-validation-harness.md` — add canvas_stress section
- `.fleet/plans/issue-1954.md` — new (first commit)
- **Not touched:** `render-jitter-metric.py` (reused as-is), `canvas_stress/main.cpp`
  (sweep mode already exists), `manifest.json`. If decode cost proves intolerable,
  emitting a centred crop from sweep mode is a `main.cpp` change — flag it, but try
  the bounded-steps + ROI approach first.

### Acceptance criteria
- A one-command `canvas_stress` temporal-jitter gate over the mixed DETACHED + GRID
  rotation sweep, using `render-jitter-metric.py`, that **fails on temporal-crawl
  regressions above the measured master speckle floor and passes on current master**:
  `bash scripts/dev/canvas-stress-rotate-jitter-sweep` → **PASS** (exit 0) on master.
- The "#1942 must FAIL" criterion is **removed** (#1942 is a spatial-registration
  artifact owned by #1944's render-verify gate, not a temporal one).
- The wrapper documents the measured ~1.2 camera-yaw floor (and why the naive
  no-freeze ~4.4 is wrong) + the `--no-spin` requirement + the radian / decode-cost
  constraints; `jitter-validation-harness.md` documents the canvas_stress baseline +
  threshold.

### Gotchas
- **`--no-spin` is MANDATORY for a valid camera-yaw fine sweep.** `--sweep-yaw`
  freezes only the camera auto-yaw (`autoRotate_`); the per-entity self-spin
  (`noSpin_`) keeps running ~30° between the 60-settle-frame shots, which aliases the
  metric to ~3.7–4.4. That is the false "floor" the original plan + architect cited
  (both wrongly assumed the sweep froze entities). Freeze entities and the true
  camera-yaw floor is **1.19**.
- **`--sweep-yaw` is in radians** and full-frame decode is ~2.3 s/frame at 2560×1440 —
  bound steps (~30–40) over a focused range; do NOT default to 360 full frames or to
  the original plan's `0 30 60` (30 rad ≈ 4.7 revolutions).
- **The floor is deterministic, not noisy.** With frozen entities + fixed camera
  poses + the deterministic re-voxelize bake, the capture is byte-identical
  run-to-run, so `jitter_score = 1.19` is a hard number. `MAX_JITTER = 4.0` mirrors
  the sibling's gate (same metric, same ~1.2 clean floor) with >3× margin; the
  inherent round-to-cell scatter speckle is sub-cell and per-pose-deterministic here,
  so it does not push the gate.
- **ROI stability:** the central ROI must stay foreground across ALL swept frames
  (the metric AND-s per-frame masks → `interior_px` shrinks if entities sweep out).
  Verify `interior_px` is non-trivial; narrow the yaw range if the central column
  drifts out of a fixed ROI.
- The wrapper is a **manual/on-demand** gate (the sibling is not auto-CI), matching
  the issue's "one-command jitter gate." Auto-CI wiring is out of scope.
- Use `cmake --build` / `./IRCanvasStress` directly in the wrapper (mirror the
  sibling — these dev scripts are not the fleet-build/run wrappers).


