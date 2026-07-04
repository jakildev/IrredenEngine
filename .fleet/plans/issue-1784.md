## Plan: render: swiss-cheese / dithered cast shadows persist on the SDF floor post-#1734 (canvas_stress)

- **Issue:** #1784 (epic #1717; surfaced by the T-1 premise-settling pass #1767)
- **Model:** opus
- **Date:** 2026-06-29

### Verified current state + confirmed repro

Premise verified against the **actual cast-shadow code path** (not the issue body's guess), the fresh #1767 measurement, the speckle morphology, and git ancestry:

- **Mechanism (code-confirmed).** Sun cast shadows are screen-space: `BAKE_SUN_SHADOW_MAP` (`c_bake_sun_shadow_map.glsl`) reconstructs `pos3D` for every non-empty `trixelDistances` pixel, projects it into the sun-aligned basis, and **`atomicMin`s the packed depth into exactly ONE floored texel** of the 1024² sun depth map (slot 28): `sunPx = ivec2(floor((sunUV - origin) / texelSz))` — `c_bake_sun_shadow_map.glsl:74,80`. The floor then reads that map via a small **2×2 PCF gather** in `ir_sun_shadow_sample.glsl:65-76` (`kMaxShadowDepthRange=24`, slope-scaled bias). A 2×2 gather cannot bridge a scatter gap wider than one texel.
- **Casters are NOT geometrically holed.** #1720/#1619 (REBUILD_GRID_VOXELS forward-map coverage holes) shipped its fix in **PR #1732**, which `git merge-base --is-ancestor` confirms is an ancestor of the #1784 repro commit (8fb2b934). So the GRID spin cubes are solid; the speckle is in the shadow path, not the geometry. (This rules out the "cast shadows mirror holey geometry" hypothesis #1720's body raised.)
- **Repro morphology = scatter-hole signature.** The #1767 baseline (`shadow_overlay_floor`, ROI 1010,540,450,250): `components=62`, `hole_ratio=0.873`, `largest_frac=0.525`. Pixel-level dithered speckle across 62 disconnected blobs is the textbook signature of **scatter-density holes** (random per-texel gaps), not coherent depth-range culling or bias acne (which produce coherent bands/edges).

### Root cause — the one caster path #1734 never densified

#1734 ("emit face footprint, not one pixel") already fixed this *exact* "pinhole casters dithered with interior gaps" symptom — but only for the paths that go through a **resolve** stage:
- the **per-axis** resolve (`RESOLVE_PER_AXIS_SCREEN_DEPTH`, `c_resolve_per_axis_screen_depth.glsl:108-144`) — runs **only under continuous (non-cardinal) yaw**;
- the **world-placed** detached resolve (#1596) — runs at every yaw (this is #1734's `canvas_stress_revox_floor_cardinal` screenshot, i.e. the grounded re-voxelize cast-proof cube is already clean at cardinal).

At **cardinal camera yaw**, the GRID spin cubes route straight through `VOXEL_TO_TRIXEL` -> the main `c_bake_sun_shadow_map.glsl` with **no resolve in between** (render/CLAUDE.md "Voxel face rasterization": per-axis canvases exist only under continuous yaw). So the cardinal GRID cast is the **single caster path still doing raw one-texel-per-source-pixel scatter** into the sun map. The screen->sun projection at the casters' depth scatters those texels sparser than the floor gathers them, so most floor pixels under the silhouette fall between scattered caster texels and read their own depth (`depthDiff~=0 < bias` => lit) -> 87% holes. The #1596 cube reads clean precisely because its resolve was footprint-densified by #1734; the GRID cubes were not.

### Approach (single, committed)

**Densify the main sun-shadow bake's screen->sun scatter to the source pixel's sun-space footprint — the direct cardinal-path analog of #1734's resolve-footprint densification.** Instead of `atomicMin` into one floored texel, each baked source pixel writes its packed depth across the small sun-texel box its world footprint (~ one trixel / voxel cell) projects to, so consecutive caster cells leave no inter-texel gap and the floor's 2×2 PCF gather lands on a written occluder everywhere inside the silhouette.

Step by step:
1. **Build + capture the baseline** (`fleet-run IRCanvasStress --auto-screenshot 120 --debug-overlay shadow`, shot `shadow_overlay_floor`; `scripts/render-shadow-metric.py --roi 1010,540,450,250`) to reproduce `62 / 0.873 / 0.525` on the current worktree before changing anything (before/after evidence for the PR).
2. **`c_bake_sun_shadow_map.glsl` — add the footprint splat** in `bakeCascade()`: compute the source cell's sun-UV extent (the projected span of one cell ~ `cellWorldSize / texelSz`, clamped to >=1 texel each axis), and `atomicMin` the packed depth across that small box around `sunPx` rather than the single floored texel. Keep the splat tight (cover the inter-cell gap, ~1 texel of margin) so the shadow does not bloat past the silhouette by more than the existing scatter conventions allow. Apply on the **single-canvas cardinal path** (`perAxisRoute==0 && residualYaw==0`); the per-axis / resolved inputs are already dense (their footprint was added upstream by #1734) — splatting an already-dense input is at worst redundant, but gate to the cardinal main path to keep continuous-yaw output untouched and avoid double-dilation.
3. **`metal/c_bake_sun_shadow_map.metal` — port in lockstep.** Identical math; this is a render shader so GLSL+Metal must stay equivalent.
4. **Re-capture + tune the splat footprint** against `render-shadow-metric.py` until the cast ROI clears `components <= 8`, `hole_ratio <= 0.30`, `largest_frac >= 0.85`. Add the **rotated-yaw SHADOW capture** the acceptance criteria require and confirm no regression there.
5. **Refresh references + baselines** that legitimately move (intentional drift — see cross-system note): `canvas_stress` shadow shots, `creations/demos/canvas_stress/test/baselines/metrics.json` + `STRUCTURAL-BASELINES.md`, and any `shape_debug` / lighting-demo render-verify references whose shadowed shots change. Call the intentional drift out in the PR body per render/CLAUDE.md.
6. **`render-debug-loop` + `attach-screenshots`** (mandatory for render PRs): full-frame + ROI-crop before/after pairs.

### Affected files

- `engine/render/src/shaders/c_bake_sun_shadow_map.glsl` — footprint-splat the cardinal main-bake scatter in `bakeCascade()`.
- `engine/render/src/shaders/metal/c_bake_sun_shadow_map.metal` — lockstep port.
- (optional) a shared splat-footprint constant — keep it local to the bake shader unless `ir_sun_shadow_sample.glsl` needs a matching value (it should not; this is bake-only).
- `creations/demos/canvas_stress/main.cpp` — add the rotated-yaw SHADOW capture shot (acceptance requires it).
- `creations/demos/canvas_stress/test/baselines/metrics.json` + `STRUCTURAL-BASELINES.md` — post-fix numbers.
- render-verify reference PNGs that legitimately change (canvas_stress shadow shots; any shadowed shape_debug / lighting-demo shots).

### Acceptance criteria

- `shadow_overlay_floor` cast ROI (1010,540,450,250): `components <= 8`, `hole_ratio <= ~0.30`, `largest_frac >= ~0.85` (vs `62 / 0.873 / 0.525`).
- A rotated-yaw SHADOW capture added and confirmed not regressed (the #1724 symptom was under-rotation).
- `metrics.json` updated with post-fix numbers.
- Default (non-shadow) and continuous-yaw paths: per-axis/world-placed cast output **byte-identical** (the splat is gated to the cardinal single-canvas path).
- GLSL + Metal in lockstep; `render-debug-loop` + `attach-screenshots` evidence in the PR.

### Sibling + in-flight reconciliation

- **#2092 / PR #2095 (SDF floor SELF-shadow acne — design-blocked).** Distinct root cause (off-cardinal smooth-yaw view-frame-depth reconstruction inflating per-texel sun-Z spread) and distinct trigger (**off-cardinal** yaw vs #1784's **cardinal**). #1784 is a **coverage/scatter** fix; #2092 is a **bias/depth-precision** issue. They touch the same bake shader but independent axes — **do NOT change `kShadowBiasTexelScale` / `kNormalBiasVoxels` / `kMaxShadowDepthRange`** (that is #2092's lane). The two changes compose cleanly.
- **#1734 / #1724 (resolve footprint — merged).** The precedent this mirrors; #1784 extends the same footprint idea to the one path (cardinal main bake) #1734 left out.
- **#1596 / PR #1626 (world-placed cast — merged).** The grounded revox cube is already densified at cardinal by #1734's world-placed resolve; #1784 targets the GRID cubes specifically.
- **#1718 / PR #2089 (AO crease — merged).** AO stage, not sun-shadow; independent.
- No open PR references #1784 (cross-checked against the engine open-PR list).

### Cross-system note (shadow consumers)

No buffer/binding/component is added or removed — this only changes the **write pattern** into the existing slot-28 sun depth map. But **every** sun-shadow consumer reads that map (`COMPUTE_SUN_SHADOW` -> `LIGHTING_TO_TRIXEL` for all demos), so the densified bake changes shadow output **globally**, not just on the canvas_stress floor. Expect render-verify reference refreshes anywhere a shadowed surface is captured (shape_debug, the lighting-demo family). Treat as intentional drift and refresh those references in the same PR; do not let a "shape_debug references changed" smoke result read as a regression.

### Gotchas

- **GLSL + Metal lockstep** — both bake shaders, identical math. Metal also needs the `threadgroupSizeForFunctionName` entry if the bake kernel is not already mapped (it is, but verify after any signature change).
- **Splat tightness.** Cover the inter-cell gap and no more — an over-wide splat bloats the shadow past the silhouette (the metric's `largest_frac`/edge cleanliness catches this). Reuse the spirit of the existing scatter-dilation conventions (`ir_iso_common.glsl` `scatterConservativeDilation`) rather than inventing a large kernel.
- **Gate to the cardinal single-canvas path** (`perAxisRoute==0 && residualYaw==0`) so continuous-yaw and per-axis output stays byte-identical and you do not double-dilate the already-footprinted resolve inputs.
- **Don't enlarge the sample-side gather instead.** A bigger PCF window leaks shadow beyond the silhouette and is a band-aid; the source-side densification is the correct, precedent-matched fix and keeps edges crisp.
- **Contingency (subordinate to the committed approach):** if, after tuning the splat, a residual coherent (non-speckle) deficit remains, it is a sample-side `kMaxShadowDepthRange=24` interaction (the floor sits only ~4-16 world units under the casters; at a shallow sun the cube-top `depthDiff` approaches 24). That is a depth-range tune, separable from this coverage fix — surface it on the PR rather than folding a bias/range change in.

