# Plan: render — chunk occlusion pre-pass (occlusion cull child 2/3)

- **Issue:** #1799 (child 2/3 of #1294)
- **Blocked by:** #1798 (needs the Hi-Z depth pyramid)
- **Parent plan:** `.fleet/plans/issue-1294.md` — shared design, measurement gate, cross-system audit, constraints
- **Design:** `docs/design/voxel-occlusion-culling.md` § Implementation sketch step 2 + § Design space 1–2
- **Model:** opus
- **Date:** 2026-06-13

## Verified current state

- The frustum cull already writes a per-pool-chunk `0/1` mask into the
  `ChunkVisibility` SSBO (binding 24) via CPU `buildChunkVisibilityMask`
  (`system_voxel_to_trixel.hpp`). The GPU compact pass
  (`c_voxel_visibility_compact.glsl`) reads binding 24, the active-mask bit
  (binding 8), and a per-voxel iso-bounds test, then compacts survivors into
  binding 25 + indirect dispatch params (binding 26). **The compact pass is the
  natural place a per-chunk occlusion bit takes effect — it already reads the
  chunk mask.**
- Pool "chunks" are 256-consecutive-entry slices of the pool array, not spatial
  cells; `C_VoxelPool::getChunkBounds` gives each chunk's iso AABB
  (`rebuildChunkBounds`).
- Child 1 (#1798) produces last frame's Hi-Z max-depth pyramid.

## Approach (single path)

New `COMPUTE_CHUNK_OCCLUSION` system + `c_chunk_occlusion_cull.{glsl,metal}`,
one invocation per pool-chunk:
1. Read the chunk's iso AABB (`getChunkBounds`), **expand it by the shadow-feeder
   sweep** (`IRMath::shadowFeederIsoBounds`, same widening
   `buildChunkVisibilityMask` uses).
2. Project to iso, pick the Hi-Z mip whose texel ≥ the AABB footprint, expand the
   footprint by one texel (conservative).
3. Sample the **max** Hi-Z depth over that footprint; AND `0` into the chunk's
   `ChunkVisibility` bit (binding 24) **iff** the chunk's *nearest* depth is
   strictly behind the Hi-Z max over the *whole* footprint. On any partial
   coverage, keep the chunk.
4. Run before the compact pass; reads last frame's Hi-Z (#1798).

Per-chunk is the design's preferred start (cheapest, reuses chunk-mask plumbing).
Per-voxel is the documented follow-on if measurement (child 3) shows large
residual.

## Affected files

- `engine/render/src/shaders/c_chunk_occlusion_cull.{glsl,metal}` — new
- `engine/system/include/irreden/system/ir_system_types.hpp` — `SystemName` entry
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — pre-pass
  wiring before the compact dispatch (or new system file + pipeline registration)

## Acceptance criteria

- Occlusion bit ANDed into `ChunkVisibility` (binding 24) upstream of the compact
  pass — additive, no consumer migration.
- Conservative: footprint +1 texel; keep on partial coverage. A false positive is
  a visible hole; a false negative is only lost savings.
- With the cull forced on, output still correct on `voxel_set` (full pixel-identical
  verification is child 3).
- Builds clean; `render-debug-loop` + `attach-screenshots` on both hosts.

## Gotchas

- **Shadow-feeder coverage** (design's #1 hazard) — both the chunk-AABB projection
  and the Hi-Z footprint must use shadow-feeder-widened bounds, or off-screen sun
  casters get culled and shadows drop.
- **Lighting invariant 1** — do NOT wire the cull into
  `system_build_light_occlusion_grid` (it must keep iterating the full pool,
  independent of the visibility mask).
- Chunk-bounds tightness is the dominant cull-rate lever; if rate is poor, the fix
  is tighter spatial chunking (separate task), not forcing per-voxel here.
