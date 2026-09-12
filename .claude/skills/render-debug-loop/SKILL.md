---
name: render-debug-loop
description: >-
  Builds, runs, and visually evaluates a rendering demo in a loop: captures
  screenshots across the demo's configured shot list (zoom, camera offset,
  yaw, render-mode combinations), reads each image and ROI crop, diagnoses
  against topic-indexed references (trixel / SDF shapes, lighting, backend
  parity), applies fixes, and repeats. Use when iterating on render pipeline
  bugs, shape alignment, lighting, parity drift, or any visual regression in
  the engine.
---

# Render Debug Loop

## Prerequisites

- Any preset ([`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md)), driven
  through `fleet-build --target <TARGET>` and `fleet-run <EXE_NAME>` so the
  loop runs unattended; `fleet-run` launches from the exe directory so the
  sibling `data/`, `shaders/`, `scripts/` resolve.
- The demo opts into the auto-screenshot helper
  ([`engine/video/CLAUDE.md`](../../../engine/video/CLAUDE.md)
  §"Auto-screenshot helper"). If it does not, add the wire-up shown there or
  use `shape_debug` when it exercises the code path.

## Loop

Run sequentially; stop after 5 iterations or when evaluation passes.

### 1. Build

```
fleet-build --target <TARGET>
```

### 2. Clear old screenshots

The screenshot counter never resets, so remove the previous batch first. The
directory is `<dirname of EXE_PATH>/save_files/screenshots/` — find `EXE_PATH`
by Glob `build/**/<EXE_NAME>` (`.exe` on Windows) or from the
`fleet-run: <EXE_PATH>` line of the previous run — then `rm -rf` it.

### 3. Run

```
fleet-run <EXE_NAME> --auto-screenshot 10
```

The demo renders warmup frames (default 10), cycles its shots, captures one
screenshot per shot, and closes. Gate on the `ir-run: RESULT=` line, not on
screenshots existing: `RESULT=CRASH` (teardown included) fails the step even
with every shot saved — the crash is this iteration's finding
([`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) §"Clean-exit
policy"); the saved shots remain diagnosis evidence.

### 4. Read the screenshots

Glob `<demo-cwd>/save_files/screenshots/screenshot_*.png` (or the subdirectory
set via `IRVideo::configureScreenshotOutputDir`), sort by mtime, and Read the
latest batch.

**ROI crops.** Demos with `IRVideo::RoiCrop` tables also write
`screenshot_<n>_<shot>__crop_<crop_label>.png` at 128×128 native. Read every
crop as well as the full frame: the Read tool downscales 1080p+ frames, so
one-pixel artifacts (cube-edge zigzag, parity drift) are only visible in the
crops.

**Baseline crops.** Compare each crop with the committed reference for this
preset under `creations/demos/<demo>/test/references/<preset>/` (the
`render-verify` set). For drift that is real but subtle:

```
build/tools/img_diff/img_diff <baseline.png> <current.png> /tmp/diff.png
```

(`tools/img_diff`, built with `IRREDEN_BUILD_TOOLS=ON`) renders drifted pixels
solid red on a desaturated baseline — Read `/tmp/diff.png`. Use
`scripts/render-compare.py` for aggregate pass/fail metrics (PSNR, max delta,
match%) and `img_diff` for "show me where".

**Temporal stability.** Stills cannot prove a moving scene is jitter-free. When
the change touches the camera-offset decomposition, the per-axis scatter, the
anti-vibration split, or the framebuffer/screen blit, also run a fine
`--pan-sweep` / `--yaw-sweep` of an isolated shape and score it with
`tools/jitter_probe` (SMOOTH vs JITTER, exit 0/1) — recipe in
[`engine/render/CLAUDE.md`](../../../engine/render/CLAUDE.md) §"Verifying
temporal stability (per-frame jitter)". Jitter and cardinal byte-identity are
separate checks.

### 5. Evaluate

Apply to every screenshot and every crop — a bug may appear at one zoom, one
offset, one render mode, or one edge.

| Criterion | Look for |
|---|---|
| All entities visible | Expected shapes present |
| Correct silhouettes | Outlines match the shape type |
| Consistent shading | Face shades match the lighting model |
| No gaps or overlaps | Solid faces, no stray dots |
| Pixel-level edge fidelity | Every silhouette pixel in a crop matches the baseline; no zigzag, parity drift, or colour shift |
| Parity stable | Crops match pixel-for-pixel across camera-offset shots unless the PR intends the change |
| Zoom stable | The same crop at zoom 4 and zoom 8 shows the same edge geometry, larger; mismatched stairs are a subdivision / zoom-rounding bug |
| Rotation stable | yaw 0 vs π/2, π, 3π/2: every face stays solid (no checkerboard); yaw π/4 shows visible face deformation vs yaw 0; lighting falls on the rotated normals. Rotation-targeted demos put yaw-rotated shots in `kShots[]` (`AutoScreenshotShot::yawRadians_`) — a yaw-0-only list is blind to rotation bugs |
| Backend parity | OpenGL and Metal frames match; backend-only drift hands off to `backend-parity` |

### 6. Diagnose and fix

Load the diagnosis file for the surface; if the symptom matches none, load all
three — bugs cross surfaces (a lighting pass reading a stale trixel canvas).

| Surface | Load for | File |
|---|---|---|
| Trixel / SDF shapes | `VOXEL_TO_TRIXEL_STAGE_*`, `SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`, `TRIXEL_TO_FRAMEBUFFER` — sizes, missing faces, bowtie edges, parity, depth sorting | [`diagnosis/shapes-trixel-sdf.md`](diagnosis/shapes-trixel-sdf.md) |
| Lighting | `LIGHTING_TO_TRIXEL` — effect missing or wrong, AO at junctions, shadow direction, flood-fill, fog of war | [`diagnosis/lighting.md`](diagnosis/lighting.md) |
| Backend parity | a defect on one backend only; capture evidence here, port via `backend-parity` | [`diagnosis/backend-parity.md`](diagnosis/backend-parity.md) |

Apply the fix and return to step 1.

## Notes

- Zoom snaps to powers of 2 (1–64); higher zoom raises
  `effective_subdivisions` and exposes subdivision-specific bugs.
- Entities under test target the same canvas (log `canvasEntity_`).
- Pipeline order: compute shaders write canvas textures (depth via
  `imageAtomicMin`, then colour) → lighting modulates trixel-canvas pixels →
  the fragment shader draws one full-screen quad per canvas into the
  framebuffer.
- When one PR changes several stages, capture a shot set per stage's branch
  and diff pairwise to isolate the regressing stage.
