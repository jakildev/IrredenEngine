---
name: render-debug-loop
description: >-
  Build, run, and visually evaluate a rendering demo in an automated loop.
  Captures screenshots across a demo's configured shot list (zoom, camera
  offset, render-mode combinations), reads each image, diagnoses rendering
  issues against a topic-indexed reference (trixel / SDF shapes, lighting,
  backend parity), applies fixes, and repeats. Use whenever iterating on
  render pipeline bugs, shape alignment, lighting, parity drift, or any
  visual regression in the engine.
---

# Render Debug Loop

## Prerequisites

### Platform

Works on all three presets. Use the fleet wrappers so the loop runs
unattended (no command-substitution or compound-command gates).

| Host          | Preset          | Build command                     | Run command              |
|---------------|-----------------|-----------------------------------|--------------------------|
| WSL2 Ubuntu   | `linux-debug`   | `fleet-build --target <TARGET>`   | `fleet-run <EXE_NAME>`   |
| macOS         | `macos-debug`   | `fleet-build --target <TARGET>`   | `fleet-run <EXE_NAME>`   |
| Windows-native| `windows-debug` | see `docs/agents/BUILD.md` PATH-fix section | see `docs/agents/BUILD.md` |

`fleet-build` auto-detects the worktree root and the corresponding
`<worktree>/build/` tree, and configures the preset on first use.
`fleet-run` finds the executable in the build tree and `cd`'s into its
directory before launching, so the demo picks up its sibling `data/`,
`shaders/`, and `scripts/` paths.

### Demo requirement: `--auto-screenshot`

The target demo must opt into the auto-screenshot helper. See
[`engine/video/CLAUDE.md` § "Auto-screenshot helper"](../../../engine/video/CLAUDE.md)
for the API, wire-up steps, and reference implementations. If your target
demo does not yet opt in, either (a) add the wire-up shown in the reference
callers, or (b) use `shape_debug` if it exercises the code path you care about.

## Loop Steps

Run sequentially. **Stop after 5 iterations** or when evaluation passes.

### 1. Build

```
fleet-build --target <TARGET>
```

If the build fails, fix compile errors before continuing.

### 2. Clear old screenshots

Screenshots accumulate across runs (the counter does not reset). Before a
new run, remove the previous batch so the Glob in step 4 picks up only
the current iteration. Locate the demo's `save_files/screenshots/`
directory via either:

- **Glob** `build/**/<EXE_NAME>` (or `build/**/<EXE_NAME>.exe` on
  Windows) to find the executable; screenshots live at
  `<dirname of EXE_PATH>/save_files/screenshots/`.
- Read the `fleet-run: <EXE_PATH>` line printed to stdout by the
  previous run's `fleet-run` invocation.

Then `rm -rf` that directory.

### 3. Run with `--auto-screenshot`

```
fleet-run <EXE_NAME> --auto-screenshot 10
```

The demo renders warmup frames, cycles through its configured shots,
captures one screenshot per shot, and closes the window. `fleet-run`
handles the working-directory requirement automatically.

### 4. Read the screenshots

Use the **Glob** tool against
`<demo-cwd>/save_files/screenshots/screenshot_*.png` (or the subdirectory
your demo configures via `IRVideo::configureScreenshotOutputDir`). Sort
by mtime and read the latest batch with the Read tool.

#### 4a. Read the per-shot ROI crops (mandatory when present)

Demos that opt into `IRVideo::RoiCrop` tables (added in #432) write
small `screenshot_<n>_<shot>__crop_<crop_label>.png` files alongside
each full-frame PNG. The Read tool downscales 1080p+ full-frames so
1-pixel artifacts (cube-edge zigzag, single-pixel parity drift) sit
below the agent's perceptual floor — exactly the failure mode behind
the metal trixel-parity investigation. Crops are 128×128 native, so
any drift is impossible to miss.

For every shot that has crops, **Read every crop PNG** in addition to
the full-frame. Don't skip them — that's where the silhouette-edge
detail lives.

#### 4b. Read the matching baseline crops (when available)

If `engine/render/tests/render-baselines/<demo>/<SHA>/` exists for the
target demo, Read each baseline crop alongside its current counterpart.
(`<SHA>` is the HEAD commit SHA at baseline-capture time; the capture
procedure and directory convention are defined by #433.)
Pairwise eyeballing catches drift that any single
shot would not flag in isolation.

Use **`tools/img_diff`** (built via `IRREDEN_BUILD_TOOLS=ON`) when a
crop's drift is genuine but subtle:

```
build/tools/img_diff/img_diff <baseline.png> <current.png> /tmp/diff.png
```

The output PNG renders drifted pixels solid red against a desaturated
baseline, written to `/tmp/diff.png` — use the **Read** tool
(`Read /tmp/diff.png`) to inspect it directly in the agent context.
A single-pixel regression jumps out immediately. Companion
to `scripts/render-compare.py`, which gives aggregate metrics
(PSNR, max delta, match%) — use the python tool for pass/fail gates
and `img_diff` for "show me where the drift is".

### 5. Evaluate

Check these **always-on** criteria across every screenshot AND every
ROI crop — a bug may surface at only one zoom, one camera offset, one
render mode, or one specific edge.

| Criterion                  | What to look for                                                                                |
|----------------------------|--------------------------------------------------------------------------------------------------|
| All entities visible       | Expected shapes present, nothing missing                                                         |
| Correct silhouettes        | Shape outlines match their type                                                                  |
| Consistent shading         | Face shades match expected lighting model                                                        |
| No gaps or overlaps        | Solid faces, no missing pixels or stray dots                                                     |
| Pixel-level edge fidelity  | Per ROI crop, every pixel along cube/voxel silhouettes matches the baseline; no zigzag, single-pixel parity drift, or color shift |
| Parity stable              | Compare each crop pixel-by-pixel across camera-offset shots — any visible drift on a silhouette is a regression unless the PR explicitly intended it |
| Zoom stable                | Same crop position at zoom 4 and zoom 8 should show the *same* edge geometry, just larger; mismatched stairs are a subdivision/zoom-rounding bug |
| Backend parity             | OpenGL and Metal produce visually matching frames; backend-only drift in a crop signals a parity port (handoff to `backend-parity` skill) |

Then open whichever diagnosis section below applies to the surface you're
changing.

### 6. Diagnose and fix

Each diagnosis surface lives in its own sibling file under [`diagnosis/`](diagnosis/) — load only the one(s) matching your symptom. If the symptom doesn't match any of them, widen to all three; bugs often cross surfaces (e.g. a lighting pass reading a stale trixel canvas).

| Surface | When to load | File |
|---------|--------------|------|
| Trixel / SDF shapes | Anything in `VOXEL_TO_TRIXEL_STAGE_*`, `SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`, `TRIXEL_TO_FRAMEBUFFER` (the original diagnosis surface — wrong shape sizes, missing faces, bowtie edges, parity issues, depth sorting). | [`diagnosis/shapes-trixel-sdf.md`](diagnosis/shapes-trixel-sdf.md) |
| Lighting (T-011 onward) | `LIGHTING_TO_TRIXEL` stage — lighting effect missing/wrong, AO at voxel junctions, shadow direction, flood-fill propagation, fog of war. | [`diagnosis/lighting.md`](diagnosis/lighting.md) |
| Backend parity (OpenGL ↔ Metal) | A defect appears on only one backend (Linux/OpenGL or macOS/Metal). Use this surface to capture the evidence; hand off to the `backend-parity` skill for the actual port. | [`diagnosis/backend-parity.md`](diagnosis/backend-parity.md) |

After applying fixes, return to **Step 1**.

---

## Key Files

| File | Role |
|------|------|
| `engine/render/src/shaders/c_shapes_to_trixel.glsl` | SDF shape compute shader |
| `engine/render/src/shaders/ir_iso_common.glsl` | Shared iso math (face offsets, depth encoding) |
| `engine/render/src/shaders/f_trixel_to_framebuffer.glsl` | Fragment shader: canvas to framebuffer with parity |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` | Voxel pool stage 1 (reference) |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` | Voxel pool stage 2 (reference) |
| `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp` | CPU-side shape gather and dispatch |
| `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` | CPU-side framebuffer draw, zoom, camera |
| `engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp` | Screen-space lighting application (T-011) |
| `engine/render/include/irreden/render/ir_render_types.hpp` | GPU struct definitions, ShapeFlags |
| `engine/math/include/irreden/ir_math.hpp` | `trixelOriginOffsetZ1`, `pos2DIsoToTriangleIndex` |

## Notes

- `--auto-screenshot` accepts an optional warmup frame count (default 10).
- Zoom snaps to powers of 2 (range 1–64). Higher zoom may increase
  `effective_subdivisions`, exposing subdivision-specific bugs.
- All entities under test should target the same canvas. Verify by logging
  `canvasEntity_` from component data.
- Pipeline order: compute shaders write canvas textures (depth via
  `imageAtomicMin`, then color) → lighting pass modulates trixel canvas
  pixels → fragment shader reads canvas and draws a full-screen quad per
  canvas into the framebuffer.
- When changing multiple pipeline stages in one PR, capture a screenshot
  set per stage's branch and diff pairwise — it isolates which stage
  introduced the regression faster than re-reading shader source.
