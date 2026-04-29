# Screen-space sun shadow map (O(1) replacement for the per-pixel march)

**Status:** Proposed (not implemented). Replaces the 64-step per-pixel
ray march in `c_compute_sun_shadow.glsl` / `.metal` with a two-pass
screen-space shadow lookup.

**Problem owner:** rendering — `engine/prefabs/irreden/render/systems/system_compute_sun_shadow.hpp`
and the corresponding compute shaders.

**Audience:** an agent (Opus) picking up the next render-pipeline PR.
This doc captures the algorithm, the coordinate math, the engineering
trade-offs, and the migration order so we don't have to re-derive any
of it. Read this before touching the sun-shadow path again.

---

## Why this exists

Today `c_compute_sun_shadow.glsl` does, per visible pixel:

1. Reconstruct `pos3D` from the iso distance map.
2. Traverse the 256³ CPU-built occupancy bitfield with 3D DDA
   (Amanatides-Woo) up to 128 cells, looking for an occluder.
3. Separately, march analytically against an SSBO of `C_ShapeDescriptor`
   shape casters (sphere bound + iterative SDF refinement, up to 32
   march steps per shape).

The DDA in step 2 replaced an earlier fixed-step march that advanced
by `sunDir` per iteration and snapped to integer cells. That sampling
scheme could miss cells the ray clipped at a corner, producing
multi-pixel zigzag artifacts on cast shadows. DDA visits every cell
the ray crosses, so the boundary stair-step collapses to the projected
cell-face spacing (~one screen pixel per cell). The screen-space
approach below makes this further cheaper, but DDA is the correct
intermediate state.

The cost is `O(canvasPixels × stepsAlongRay)` plus the per-frame
`BUILD_OCCUPANCY_GRID` rebuild and 2 MB SSBO upload. We pay this even
though the iso distance map already encodes "the closest surface visible
along the iso depth axis" for every screen pixel — and shadow casting
is structurally just the same depth test from a different camera angle.

The screen-space approach replaces the inner loop with **one texel
read** (`O(canvasPixels)` total) and folds both the voxel-pool occupancy
path and the analytic SDF caster path into a single unified shadow
buffer. The occupancy grid and `SunShadowShapeCasterBuffer` can both go
away.

---

## Math

Pick a sun-aligned orthonormal basis `(uHat, vHat, sunDir)` once per
frame, where `sunDir` is the unit vector toward the sun
(`FrameDataSun.sunDirection_.xyz`). Both basis vectors are constant
within a frame; you can compute them on the CPU and ship them in the
existing `FrameDataSun` UBO (or extend it).

Given a 3D world point `p`, define the **sun-space coordinates**:

```
sunU(p)     = dot(p, uHat)        // basis ⊥ sunDir
sunV(p)     = dot(p, vHat)        // basis ⊥ sunDir, ⊥ uHat
sunDepth(p) = dot(p, sunDir)      // depth along sunDir; lower = closer to sun
```

For a fixed sun direction this is a static rotation: 4 dot products per
pixel, no branches.

### Special case: sun direction == iso depth axis

When `sunDir == (1,1,1)/√3` (sun is co-axial with the iso camera), the
sun-space depth is exactly the iso depth (`pos3DtoDistance(p) = x+y+z`)
up to a `√3` scale, and `(uHat, vHat)` align with the iso basis. The
shadow map is *literally* the iso distance map rescaled — no extra
buffer needed. For arbitrary sun directions you need a separate
sun-space buffer, but the algorithm is the same.

### Choosing the basis

Any orthonormal pair perpendicular to `sunDir` works; pick whichever is
numerically stable. Recommended:

```cpp
vec3 ref = (abs(sunDir.z) < 0.9f) ? vec3(0, 0, 1) : vec3(1, 0, 0);
vec3 uHat = normalize(cross(ref, sunDir));
vec3 vHat = cross(sunDir, uHat);  // already unit length given uHat & sunDir orthonormal
```

Compute on the CPU each frame in `system_compute_sun_shadow.hpp::beginTick`
and ship in `FrameDataSun` alongside the existing `sunDirection_`.

---

## Pipeline shape

Two compute passes, both `O(canvasPixels)` total work.

### Pass A — Bake the sun-space depth map

New compute shader (sketch name `c_bake_sun_shadow_map.glsl` /
`.metal`). Dispatched over the iso canvas, reads the same `trixelDistances`
texture the lighting pass already consumes, writes a sun-space depth
buffer using `imageAtomicMin` on a packed depth value.

```glsl
// Per iso pixel:
int encoded = imageLoad(trixelDistances, isoPixel).x;
if (encoded >= kEmptyDistanceEncoded) return;
int rawDepth = encoded >> 2;
vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
vec2 sunUV  = vec2(dot(pos3D, uHat), dot(pos3D, vHat));
float sunZ  = dot(pos3D, sunDir);

ivec2 sunPixel = ivec2(round((sunUV - sunBufferOriginUV) / sunBufferTexelSize));
if (insideSunBuffer(sunPixel)) {
    uint packed = packDepth(sunZ);  // 32-bit monotonic encoding
    imageAtomicMin(sunShadowDepthMap, sunPixel, packed);
}
```

The buffer storage is `r32ui` (atomic-friendly). `packDepth` maps the
expected sunZ range to a monotonic uint32: usually
`uint(round((sunZ - sunZMin) * scale))` where `(sunZMin, scale)` are
chosen to cover the visible volume's swept bounds. The `imageAtomicMin`
guarantees we keep the closest-to-sun depth at each sun-space texel
across all surfaces that project there.

### Pass B — Lookup (replaces the existing shadow shader)

Same geometry as today's `c_compute_sun_shadow.glsl`, but the inner
march loop becomes a single texel read:

```glsl
vec2 sunUV  = vec2(dot(pos3D, uHat), dot(pos3D, vHat));
float sunZ  = dot(pos3D, sunDir);
ivec2 sunPixel = ivec2(round((sunUV - sunBufferOriginUV) / sunBufferTexelSize));

uint storedPacked = imageLoad(sunShadowDepthMap, sunPixel).x;
float nearestZ = unpackDepth(storedPacked);
bool shadowed = (sunZ - nearestZ) > biasEpsilon;

float factor = shadowed ? kShadowDarken : 1.0;
imageStore(canvasSunShadow, isoPixel, vec4(factor, 0.0, 0.0, 0.0));
```

Both passes are screen-space. **No occupancy grid is read. No analytic
caster SSBO is read. No inner loop.**

---

## Coordinate plumbing

The CPU side needs:

1. `(uHat, vHat)` — added to `FrameDataSun` (existing UBO; size grows
   from 48 B to 80 B; static_assert + std140 padding apply as before).
2. `sunBufferOriginUV` and `sunBufferTexelSize` — also in
   `FrameDataSun`. Compute once per frame from the visible volume's
   sun-space projection.
3. The sun-space buffer dimensions. Recommended: same as the iso canvas
   max edge × √2, rounded up to a multiple of 16 for compute
   workgroups. For a 642×722 canvas that's ~1024×1024, ~4 MB at r32ui.
   This buffer is GPU-only — never uploaded from CPU.

The visible volume's sun-space projection is the AABB of the eight
corners of the iso view's world-space frustum after applying
`(uHat, vHat, sunDir)`. The existing `IRRender::getCullViewport()`
already gives the iso-space bounds; convert to world-space corners via
`isoPixelToPos3D` at min and max iso depth.

---

## What this replaces

Once Pass A + Pass B land:

1. **Delete the occupancy grid path** — `BUILD_OCCUPANCY_GRID`,
   `C_OccupancyGrid`, the 2 MB SSBO upload, `kBufferIndex_OccupancyGrid`,
   the occupancy bit unpack in the lookup shader, and the 64-step march.
2. **Delete the analytic caster path** —
   `SunShadowShapeCasterBuffer`, `kBufferIndex_SunShadowShapeCasters`,
   `detail::collectShapeCastersForCanvas` and its sun-cone culling,
   `analyticShapeShadowHit`, and all the per-shape SDF helper functions
   in the shadow shader. SDF shapes participate in the shadow buffer
   automatically because they're rasterized into `trixelDistances` by
   `c_shapes_to_trixel`.
3. **Drop the `selfEntityId` self-shadow guard.** The sun-space depth
   bias replaces it.

Net deletion: ~200 lines of CPU code + ~200 lines of shader code.

---

## Trade-offs (be honest about these in the PR)

### What we gain
- O(canvasPixels) shadow cost, no inner loop. Order of magnitude faster
  on the GPU for typical 642×722 canvases at full zoom.
- Single shadow path for voxel-pool *and* SDF shapes — no double
  shadows, no per-renderer-kind branching.
- 2 MB CPU→GPU upload eliminated per frame.
- Caster collection / culling code on the CPU eliminated entirely.

### What we lose / what we have to mitigate

1. **Off-screen shadow casters.** `trixelDistances` only contains
   surfaces inside the iso viewport. A tall building outside the
   visible region but whose shadow falls onto a visible pixel is
   *invisible* to the screen-space approach.
   - **Mitigation A (preferred):** widen the iso rasterization passes
     (`c_voxel_to_trixel_*` and `c_shapes_to_trixel`) to cover not just
     the visible region but the **shadow-feeder region** —
     `visibleAABB ∪ (visibleAABB swept along -sunDir by maxShadowDistance)`.
     This is the same swept AABB `shadowRelevantIsoBounds()` already
     computes for caster culling. We're effectively rasterizing the
     same set of shapes the caster culling currently picks, just into
     iso instead of into a separate caster buffer. Cost: a moderately
     larger iso canvas; same shaders, no new dispatch.
   - **Mitigation B (fallback):** keep a small sun-space rasterization
     pass (the bake from Pass A above) but feed it from a *separate*
     world-space-traversal compute shader for shapes outside the iso
     canvas. Heavier; only do this if Mitigation A's canvas growth is
     unacceptable.

2. **Self-shadow acne.** Standard shadow-mapping bias problem. Two
   complementary fixes:
   - Constant `biasEpsilon` based on sun-space texel size (`~0.5 *
     sunBufferTexelSize`).
   - Slope-scaled bias: `bias *= 1 / max(0.05, dot(faceNormal, sunDir))`
     so faces grazing the sun bias more than faces facing it.
   The face is already encoded into the depth buffer (`encoded & 3`),
   so the lookup pass has the normal for slope-scale bias for free.

3. **Resolution mismatch.** Sun-space pixel size ≠ iso pixel size in
   general. Aliasing manifests as stair-stepped shadow edges when the
   sun is grazing.
   - **Cheap fix:** size the sun buffer at √2× the iso canvas's longer
     edge so the worst-case projection stays ≥1 sun-pixel per iso
     pixel.
   - **Better fix:** percentage-closer filtering (sample a 2×2 window
     and average). Adds 4 reads per pixel — still O(1).
   - **Future:** mip-map the sun buffer and pick the LOD that matches
     the local sun→iso pixel ratio (variance shadow maps territory).

4. **Atomic contention.** Many iso pixels may project to the same
   sun-space texel when the sun is steep. `imageAtomicMin` on a single
   texel from many threads is sequential. Acceptable in practice
   (same as any shadow-map atomic-min path); if a profile shows it
   matters, switch to a two-pass min reduction.

---

## Migration order

Land this in **two PRs** to keep diffs reviewable, not one:

### PR 1 — "Add screen-space sun shadow bake; keep both paths"

Behind a feature flag in `FrameDataSun` (`int useScreenSpaceShadow_`):

1. Extend `FrameDataSun` with `uHat`, `vHat`, `sunBufferOriginUV`,
   `sunBufferTexelSize`, `useScreenSpaceShadow_` (size goes to 96 B,
   re-do std140 padding + static_assert + the
   `frame_data_sun_layout_test.cpp`).
2. New compute shader `c_bake_sun_shadow_map.glsl` / `.metal` and a new
   system `system_bake_sun_shadow_map.hpp` that runs immediately before
   `COMPUTE_SUN_SHADOW`.
3. Modify `c_compute_sun_shadow.glsl` / `.metal` to branch on
   `useScreenSpaceShadow_`: when 1, do the sun-space lookup; when 0,
   keep today's march. New code lives next to old code.
4. Add `IRRender::setScreenSpaceShadowsEnabled(bool)` toggle for demos.
5. Validate with `render-debug-loop` on `IRLightingCombined` — both
   modes side-by-side, bias tuned, edges clean.

### PR 2 — "Remove occupancy grid + analytic caster paths"

Once PR 1 ships and the new path is the default:

1. Delete `BUILD_OCCUPANCY_GRID`, `C_OccupancyGrid`, the SSBO, the
   binding index, the bit-unpack helpers in shaders.
2. Delete `SunShadowShapeCasterBuffer`,
   `detail::collectShapeCastersForCanvas`, the sun-cone culling, the
   analytic caster march in the shadow shader.
3. Delete the `useScreenSpaceShadow_` flag (it becomes always-on).
4. Update `engine/render/CLAUDE.md` and
   `engine/prefabs/irreden/render/CLAUDE.md` to drop the occupancy /
   analytic caster sections.
5. Re-run all lighting demos via `render-debug-loop` — visual
   regression check.

PR 2 is mechanical once PR 1 is correct.

---

## Validation oracle

The shape-debug demo's existing `--auto-screenshot` shot list at
multiple zoom levels is the right oracle: capture before/after, compare
the resulting shadow silhouettes pixel-wise. Acceptable diff: bias-only
at shadow edges (≤ 1 sun-space texel of edge migration). Anything else
is a bug.

---

## Open questions for the next agent

1. **Should the sun-space buffer be persistent across frames?** If sun
   direction is mostly static (cinematic / debug), we could amortize
   the bake. But staleness on a moving voxel pool is a real risk;
   default to "rebake every frame" and only optimize if profiling
   demands.
2. **Should the bake use the same workgroup size as the lookup?**
   16×16 (matching today's `c_compute_sun_shadow.glsl`) is a fine
   starting point. The bake's atomic contention may favor a smaller
   workgroup; benchmark on Apple M-series and an Intel/AMD GPU before
   committing.
3. **Soft shadows / penumbra?** Doable as a follow-up by averaging
   neighborhood depth values during the lookup (variance-shadow-map
   tricks). Out of scope for this rewrite — keep the move from
   "march" to "lookup" focused.
