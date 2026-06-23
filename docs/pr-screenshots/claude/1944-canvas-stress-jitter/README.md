# #1944 follow-up — camera Z-yaw pivot hardening evidence

Render-verification evidence for commit `5322c261` (exact-center pivot +
detached composite on the effective offset). macOS / Metal, full engine build.
Images downscaled to 1280px wide. The `so3_*` files are the earlier #1944
*revert* evidence (commit `8ac0f784`), unchanged.

## Per-axis camera-offset jitter fix (`peraxis_jitter_before_after_plot.png`)

The follow-up fix for the per-frame jitter that survived the pivot change: the
smooth-camera-yaw per-axis composite split the camera offset into a
density-scaled integer anchor (`floor(cameraIso·subdivisionScale)`) plus a
sub-pixel term scaled to the canvas, not to one anchor cell — so at every
sub-iso anchor crossing the scene snapped back ~1 cell along the world axes
(worse at higher zoom). Fix: a whole-iso base anchor (per-axis canvases are
base-resolution since #1458) + a continuous sub-cell term scaled to one anchor
cell (`screenPxPerCell·fract(cameraIso)`), so anchor + smooth = `K·cameraIso`,
continuous. See `system_trixel_to_framebuffer.hpp` `drawPerAxisScatter` and the
per-axis stores/recoveries.

The plot is a **centroid-residual trajectory** (temporal jitter can't be shown
in one screenshot): a single voxel shape is captured frame-by-frame while the
camera moves, and its centroid is plotted as deviation from the smooth motion
line.

- **Left — PAN at yaw 45° (zoom 4):** voxel box, camera panning. Before (red):
  ±12px sawtooth. After (green): flat — `dx = +1.0px`/frame, std 0.00,
  0 direction-reversals (was std 12.4, max 22px, 11 reversals).
- **Right — ROTATION (fine yaw sweep within one cardinal quadrant):** voxel
  cylinder (Z-yaw-invariant probe), camera yawing. Before (red): ±15px wander
  + snap. After (green): flat — ≤0.7px/frame, 0 reversals (was max 26px,
  7–8 reversals).

Verified separately: yaw-0 cardinal is byte-identical run-to-run (no static
"after move" jitter); the full multi-shape scene and `canvas_stress` (GRID +
orbit) still render correctly with lighting/shadows intact. The only cardinal
delta is an 8-pixel / max-Δ-8 (0.0002%) FP-scheduling shift on the lit
SDF/voxel edges — the unavoidable, sub-perceptual cost of editing the
per-axis blocks that share a compilation unit with the cardinal-active shaders.

The `--pan-sweep` / `--yaw-sweep` flags added to `shape_debug` are the
regression harness used to produce this (jitter shows as centroid-residual
oscillation; a clean pipeline is flat).

## shape_debug — the reported bug (camera panned to (16,16), then yawed)

| File | What it shows |
|---|---|
| `shape_debug_pan16_yaw0_BEFORE.png` / `..._yaw0_AFTER_identical.png` | yaw 0 — **byte-identical** (img_diff 0.0000%). The cardinal fast path is preserved. |
| `shape_debug_pan16_yaw180_BEFORE_swing.png` | **Before:** at yaw 180 the panned scene swings off-frame to the lower-left — the "vibration / swing on rotation" the issue reports. |
| `shape_debug_pan16_yaw180_AFTER_pinned.png` | **After:** content rotates in place about screen center. A landmark at screen-offset `(-dx,-dy)` from center maps to `(+dx,+dy)` — an exact point-reflection through center (correct 180° pivot). |
| `shape_debug_pan16_yaw180_DIFF.png` | img_diff before↔after = 15.9% (the swing being corrected). |

## canvas_stress — regression guard (un-panned, --no-spin --no-auto-rotate)

| File | What it shows |
|---|---|
| `canvas_stress_yaw45_BEFORE.png` / `..._AFTER.png` | The detached canary + GRID + floor stay coherent. yaw 0 is byte-identical (0.0000%); yaw 45 differs only **0.21%** (the small whole-composition pivot correction, detached + GRID moving **together**). |
| `canvas_stress_yaw45_DIFF.png` | img_diff before↔after = 0.21% — thin boundary pixels only. Contrast the **1.3–3.4%** detached *drift* that #1942 alone caused: the detached-follows-effective change keeps the #1944 fix intact. |

## Not addressed here

The same `shape_debug` scene shows "flat-top, no-depth" shapes near the floor
(e.g. the orange/green shapes sitting on the platform). That is **Bug A** from
the #1884 depth-unification investigation — the iso-depth (`x+y+z`) convention
ranks a grounded solid's lower faces behind the SDF floor, which clips them
(spreads with zoom). It is the design-escalated **#1958** sub-epic (unified
quadrant-stable depth encoding + priority bands, blocked by #1957), independent
of this pivot change.
