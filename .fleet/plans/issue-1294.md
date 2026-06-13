# Plan: render — voxel-pool solid-coverage occlusion culling

- **Issue:** #1294 (umbrella/tracking; 3 child impl tasks filed under it)
- **Design source of truth:** `docs/design/voxel-occlusion-culling.md` (#1290)
- **Model:** opus (shader + hot-path render-system work)
- **Date:** 2026-06-13

## Scope

Implement the third voxel-cull phase: drop voxels that are locally exposed and
in-frustum but **globally occluded by closer geometry from a different object**
(inter-object occlusion). Target = the `voxelStage1` GPU pass, the dominant frame
cost at zoom-out.

## Measurement gate — CLEARED (this session, 2026-06-13)

The design deferred implementation behind three trigger conditions. All are now
resolved:

1. **Raster stages > ~25% of frame** — ✅ **MET.** Fresh profile (post-#1778,
   M4 Max, 262K): `voxelStage1` GPU = 32.1ms = **73% of the zoom8 frame**.
2. **In-flight rasterization rework settled** — ✅ **RESOLVED.** #1278
   (exposed-face mask) + #1288 (cull-gated REBUILD_GRID_VOXELS) both
   merged 2026-05-28; the cost baseline this cull optimizes is now stable.
3. **`occludedExposedFraction` measured** — ✅ **CLEARED.** Measured directly via
   a throwaway stage-2 win/attempt atomic counter (since reverted):

| scene @ zoom8 | frame / FPS | voxelStage1 GPU | overdraw | **occludedExposedFraction** | cull helps? |
|---|---|---|---|---|---|
| **voxel_set** (the 32ms case) | 50ms / 21 | 32.1ms | 38× | **0.97** | ✅ massively |
| **dense_set** (one solid set) | 52ms / 20 | 49.9ms | 1× | **0.00** | ❌ exposed-face mask already prunes |
| **hollow_set** (sphere shell) | 8.7ms / 117 | 6.6ms | 2× | 0.50 | ⚠️ moderate, already fast |

The design's analytical prior ("IRPerfGrid ≈ 0 occlusion") was **wrong** for the
per-entity grid: `voxel_set` is 97% occluded (18M stage-2 taps, only 472K
visible). So the cull is justified.

### Scope caveats (carry into review, NOT blockers)

- **Payoff is scene-shape-dependent.** 0.97 holds for **many-small-entity**
  scenes (per-voxel/per-object grids: crowds, item fields, particles zoomed
  out), where neighbors are different objects so the exposed-face mask can't
  prune between them. For a **single merged solid** (`dense_set`) the residual is
  **0** — the exposed-face mask already handles it.
- **The hardest scene is the one this cull can't help.** `dense_set` is the
  *slowest* (49.9ms) and 0% cullable. Solid-volume/terrain cost is a **separate**
  problem (LOD / drawn-face reduction / raster resolution) — out of scope here.
- **Granularity gap.** 0.97 is the *per-voxel* ceiling. v1 culls per **256-voxel
  pool-chunk** (design's preferred start — only chunks whose entire iso AABB is
  occluded). Realized capture will be < 97% and depends on pool-chunk spatial
  coherence; **per-voxel culling is the documented follow-on** (design § 1) if
  chunk capture proves weak. Task 3's verification measures the realized
  `voxelStage1` delta — if it under-delivers, file the per-voxel follow-on rather
  than forcing per-chunk.

## Approach — three sequential bounded PRs (child issues, blocked_by chain)

Strictly sequential, same surface, all `[opus]`. Each is a child of #1294 with a
standalone `**Blocked by:** #<prior>` line so the stack claims in order.

### Child 1 — Hi-Z build stage  [opus, blocked by: none]

`COMPUTE_DISTANCE_HIZ` system + `c_build_distance_hiz.{glsl,metal}`:
downsample-**max** mip chain of `triangleCanvasDistances` (binding 1, R32I) into a
new mipped R32I texture on `C_TriangleCanvasTextures`. Runs after stage 2 + AO,
before next frame's cull reads it. New `SystemName` enum entry. Per-canvas.

### Child 2 — Chunk occlusion pre-pass  [opus, blocked by: Child 1]

`COMPUTE_CHUNK_OCCLUSION` system + `c_chunk_occlusion_cull.{glsl,metal}`: one
invocation per pool-chunk; projects the chunk's iso AABB
(`C_VoxelPool::getChunkBounds`, **expanded by the shadow-feeder sweep**), picks
the Hi-Z mip whose texel ≥ AABB footprint, samples max-depth, ANDs `0` into the
chunk's `ChunkVisibility` entry (binding 24) iff the chunk's nearest depth is
strictly behind the Hi-Z max. Conservative: expand footprint one texel; keep on
partial coverage. Reads **last frame's** Hi-Z.

### Child 3 — Gating + verification  [opus, blocked by: Child 2]

`occlusionCullEnabled_` flag (default **false**) on `C_RenderCamera`/render
option; one-frame disable after a camera-position delta over threshold. Pre-pass
early-returns when off (zero cost in default config). `render-verify` baselines
proving **pixel-identical** output cull-on vs cull-off (must be — fully-occluded
voxels write nothing; any diff is a cull bug). **Measure and record the realized
`voxelStage1` reduction on `voxel_set` zoom8** (the 0.97 ceiling); if weak, file
the per-voxel follow-on. Document the one-frame-lag pop as intentional drift.

## Affected files

- `engine/render/src/shaders/c_build_distance_hiz.{glsl,metal}` — Child 1 (new)
- `engine/render/src/shaders/c_chunk_occlusion_cull.{glsl,metal}` — Child 2 (new)
- `engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp` — Child 1 Hi-Z texture
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — chunk-mask plumbing (already here); pre-pass wiring
- `engine/system/include/irreden/system/ir_system_types.hpp` — `SystemName` entries (Children 1,2)
- `C_RenderCamera` / render options — Child 3 flag

## Cross-system audit (shared resources touched)

- **`ChunkVisibility` SSBO (binding 24)** — produced by CPU
  `buildChunkVisibilityMask`, consumed by `c_voxel_visibility_compact.glsl`. The
  cull AND-s `0` into an occluded chunk's entry: **additive, no consumer migration.**
- **`triangleCanvasDistances` (binding 1, R32I)** — Hi-Z downsample-max source;
  also read by sun-shadow bake (binding 0) + AO. Hi-Z is a **new derived
  texture**; the existing distance texture is unmodified — no consumer migration,
  but the Hi-Z build must be ordered after stage2+AO and before next-frame cull.

## Acceptance criteria

- Child 1: Hi-Z mip chain built each frame from last frame's distances; renders
  unchanged (Hi-Z is write-only-consumed-next-frame at this stage).
- Child 2: occluded chunks' `ChunkVisibility` entries ANDed to `0`; with the flag forced on,
  output still correct on `voxel_set`.
- Child 3: cull-on vs cull-off **bit-identical** in `render-verify`; **measured
  `voxelStage1` reduction recorded** on `voxel_set` zoom8; zero cost when
  `occlusionCullEnabled_=false`.

## Gotchas / hard constraints (design § Downstream-consumer matrix)

- **Shadow-feeder coverage is the #1 hazard** — Hi-Z *and* chunk-AABB projection
  must use the shadow-feeder-widened bounds, or sun shadows lose off-screen
  casters.
- **Lighting invariant 1** — NEVER wire the cull into
  `system_build_light_occlusion_grid` (it must keep iterating the full pool).
- **Conservative** — a false positive is a visible hole; a false negative is only
  lost savings. When in doubt, keep the voxel.
- Each shader PR runs `render-debug-loop` + `attach-screenshots` and carries both
  cross-host smoke labels.
