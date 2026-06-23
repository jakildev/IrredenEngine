## Plan: extend the jitter-validation harness to canvas_stress (mixed DETACHED + GRID rotation)

- **Issue:** #1954
- **Model:** opus
- **Date:** 2026-06-21
- **Part of:** epic #1881 (validation); follow-up to #1944 / #1953

### Scope
Add a `canvas_stress` sibling to the #1922 temporal-jitter harness so the
DETACHED-vs-GRID camera-yaw rotation jitter (#1944, fixed in #1953 — caught only by
manual observation; the multi-canvas-type coverage gap #1922 deferred) is gated by a
one-command check. **Reuse `scripts/render-jitter-metric.py` unchanged**; add a
`scripts/dev/canvas-stress-rotate-jitter-sweep` wrapper mirroring
`scripts/dev/shape-rotate-jitter-sweep`; add a `canvas_stress` baseline + threshold
section to `docs/design/jitter-validation-harness.md`.

### PREREQUISITE — sequence after #1953
Acceptance requires the gate to **FAIL pre-#1953** and **PASS post-#1953**. #1942
(the jitter) is on master; #1953 (the fix) is an open PR (not merged). To land #1954
green on `master`, #1953 must merge first — otherwise the dev tool would correctly
report FAIL on master (the live bug), an awkward landing state. The implementer can
validate both halves *now* without waiting (FAIL on current master, PASS via
`gh pr checkout 1953`), but the PR should merge after #1953. Marked **Blocked by
#1953** in the issue body for this reason.

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
     bounded steps so sub-degree per-frame jitter is sampled without 360 full-frame
     decodes — e.g. `--sweep-yaw 0 30 60` (0.5°/step, 60 frames). Make
     from/to/steps/zoom overridable (positional args + env, like the sibling).
   - Score: feed the captured full frames to `render-jitter-metric.py` with a
     CENTRAL `--roi` (the detached+GRID column), deriving the ROI from a frame's
     IHDR size (zoom/host robust) like the sibling — e.g. central ~40-50%.
   - Gate on `MAX_JITTER` (env-overridable); exit non-zero on FAIL.
3. Tune the threshold (mirror the doc's "Choosing the gate"): measure `jitter_score`
   on the clean baseline (post-#1953 — the inherent round-to-cell scatter speckle
   sets the floor) and on #1942 (the jitter). Set `MAX_JITTER` between them with
   margin.
4. Add a "canvas_stress (mixed DETACHED + GRID)" section to
   `jitter-validation-harness.md`: measured pre/post-#1953 numbers, the chosen ROI +
   range/steps, the threshold + rationale, and an OpenGL/Linux note (Linux baseline
   captured by running the identical command on a Linux host — defer the number like
   the shape_debug doc does).
5. Validate: wrapper exits non-zero on a pre-#1953 checkout (#1942) and zero on the
   #1953 branch / post-#1953 master. Record both numbers for the PR body + doc.
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
- `bash scripts/dev/canvas-stress-rotate-jitter-sweep` → one-command **PASS** on
  post-#1953 master (or the #1953 branch).
- The same command on a pre-#1953 checkout (#1942) → **FAIL** (non-zero),
  demonstrating it would have caught the #1944 jitter.
- `jitter-validation-harness.md` documents the canvas_stress baseline + threshold.

### Gotchas
- **Ordering:** land after #1953 (PREREQUISITE) so the dev tool is green on master.
- **Full-frame decode cost:** bound steps (~60); do NOT default to 360 full frames.
- **Inherent scatter speckle** (documented round-to-cell sub-cell CPU↔GPU
  divergence; it's why the zoom shot is excluded from pixel-diff) sets the clean
  jitter floor — set `MAX_JITTER` above it but below the #1942 jitter; too-tight
  fails the clean run.
- **ROI stability:** the central ROI must stay foreground across ALL swept frames
  (the metric AND-s per-frame masks → `interior_px` shrinks if entities sweep out).
  Verify `interior_px` is non-trivial; narrow the yaw range if the central column
  drifts out of a fixed ROI.
- The wrapper is a **manual/on-demand** gate (the sibling is not auto-CI), matching
  the issue's "one-command jitter gate." Auto-CI wiring is out of scope.
- Use `cmake --build` / `./IRCanvasStress` directly in the wrapper (mirror the
  sibling — these dev scripts are not the fleet-build/run wrappers).


