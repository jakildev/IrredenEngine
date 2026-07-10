## Plan: render — per-voxel occlusion culling (capture the 0.97 ceiling the per-chunk cull leaves)

- **Issue:** #1812 (follow-on to epic #1294; children #1798 Hi-Z / #1799 chunk pre-pass / #1800 gate all merged)
- **Model:** opus — hot-path GPU compute-shader work in the voxel-to-trixel pipeline with bit-identical + shadow-feeder + backend-parity invariants. The *mechanism* is documented (design § 1), so this is bounded implementation, not novel stage design (not fable); the perf/correctness judgment and the render-pipeline surface put it above sonnet.
- **Date:** 2026-07-03
- **Design source of truth:** `docs/design/voxel-occlusion-culling.md` § 1 "Granularity" (per-voxel row), § "Downstream-consumer matrix"; parent plan `.fleet/plans/issue-1294.md` § "Granularity gap".

### Verified current state + confirmed premise

Read the shipped code on `origin/master`, not just the design sketch:

- **Per-chunk cull shipped and is weak.** `dispatchChunkOcclusion` (`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp:604`) authors one CPU `ChunkQuery` per 256-voxel pool-chunk and `c_chunk_occlusion_cull.glsl` ANDs `0` into `chunkVisible[]` (binding 24) iff `encodedNearest_ > hiZMax + 4`. Measured realized `voxelStage1` reduction on `voxel_set` zoom8 is **−6.6 ms (~3 %)**, net-negative (+1.2 ms) at zoom1 — far below the **0.97 per-voxel `occludedExposedFraction`** ceiling (#1294 measurement table). Two structural causes (issue body): a 256-slot chunk spanning depth is rarely *fully* occluded, and the "chunk AABB fully inside the VISIBLE viewport" eligibility guard (`system_voxel_to_trixel.hpp:637-641`) excludes most chunks at high zoom.
- **Every piece the per-voxel path needs already exists** — this follow-on is refinement, not new infra:
  - Hi-Z max-depth mip chain (#1798): `C_TriangleCanvasTextures::hiZMips_`, accessors `hiZMipCount()` / `getHiZMip(int)` (`component_triangle_canvas_textures.hpp:146-155`); built by `System<COMPUTE_DISTANCE_HIZ>` after stage-2+AO. R32I encoding = rawDepth in bits [31:2], face slot [1:0], `65535` background sentinel = "never occlude".
  - Per-voxel depth metric primitive (#1462): the `voxelDepthAxis` uniform (`c_voxel_to_trixel_stage_1.glsl:58-64`, binding-7 offset 144) + GPU mirror `isoDepthAlongAxis(pos, axis)` in `ir_iso_common.{glsl,metal}`, collapsing to `pos3DtoDistance` for the identity/GRID canvas. This is exactly the front-most depth the Hi-Z compare needs, per-voxel.
  - Hi-Z sampling helpers `pickHiZLevel` / `hiZTexel` and `kOcclusionDepthMargin=4` in `c_chunk_occlusion_cull.glsl:74-109`.
  - Gate (#1800): `getVoxelOcclusionCullEnabled()` (default **false**, `render_manager.hpp:187`; enabled only by `perf_grid`/`shape_debug`), the NONE-mode/cardinal/non-rotating dispatch guard (`system_voxel_to_trixel.hpp:914-917`), and the one-frame camera-cut disable `occlusionLagSourceStale_` (`:1254-1264`).
- **Compact pass is the documented home** (design:60-65): `c_voxel_visibility_compact.glsl` main() already computes `voxelPosRaw` + `isoPos` and tests `chunkVisible[chunkIdx]` (b24), the active-mask bit (b8), the frustum (`cullIsoMin/Max`), and fog — the per-voxel Hi-Z test drops in right after those gates pass, before the survivor `atomicAdd` append.
- **No in-flight / sibling conflict.** No open engine PR touches the compact / Hi-Z / chunk-occlusion surface (checked all 11). #1294's three children are merged. This is the sanctioned single follow-on (#1294 §"Granularity gap": "per-voxel culling is the documented follow-on if chunk capture proves weak").

### Scope

Add a **per-voxel** Hi-Z occlusion test inside the compact pass, layered on top of the existing per-chunk pre-pass (coarse→fine hierarchical occlusion), so a voxel that is locally-exposed and in-frustum but globally occluded by closer geometry is dropped from the compacted list — capturing a clear majority of the 0.97 per-voxel ceiling on `voxel_set` zoom8, where the per-chunk cull realizes only ~3 %. Output stays bit-identical cull-on vs cull-off; the feature stays off by default. `dense_set` (0.00 cullable, solid-volume/LOD problem) stays out of scope.

### Approach (single committed path)

Keep the chunk pre-pass as the cheap coarse pre-filter (it already ANDs into `chunkVisible[]` *before* the compact), and refine per-voxel inside the compact loop on the survivors only.

1. **Factor the Hi-Z sampling helpers into a shared include.** Move `pickHiZLevel`, `hiZTexel` (the constant-index sampler-array ladder) and `kOcclusionDepthMargin` from `c_chunk_occlusion_cull.glsl` into `ir_iso_common.glsl` (+ the Metal mirror), so the chunk pre-pass and the compact pass share one copy. Chunk shader keeps byte-identical behavior (include instead of inline).
2. **Bind the Hi-Z chain for the compact dispatch.** In `system_voxel_to_trixel.hpp`, when the cull is active, bind Hi-Z levels `[0, mipCount)` (units 0..11, surplus → coarsest, same as `dispatchChunkOcclusion:665-667`) immediately before the compact `use()`+dispatch. Do **not** rely on the chunk pre-pass's residual bindings. Upload the new gate uniform (below) and `mipCount` into the binding-7 FrameData.
3. **Add the per-voxel test to `c_voxel_visibility_compact.glsl`** (declare `layout(binding=0) uniform isampler2D hiZLevels[12];` — GL image-unit vs texture-unit namespaces are separate, so this coexists with the `canvasFogOfWar` image at binding 0). Insert after the existing frustum+fog gate pass, before the append:
   - Guard on a new `occlusionCullActive` flag packed into binding-7 FrameData (1 only when `getVoxelOcclusionCullEnabled() && !occlusionLagSourceStale_ && NONE-mode && residualYaw==0` — the exact conditions the chunk dispatch already gates on) AND `mipCount > 0`. When 0, skip the branch → byte-identical to master.
   - Compute the voxel's encoded nearest depth: `int encoded = int(round(isoDepthAlongAxis(vec3(voxelPos), voxelDepthAxis.xyz))) * 4;` (matches `dispatchChunkOcclusion`'s `minDepth_ * 4` and the Hi-Z R32I encoding; identity canvas collapses to `pos3DtoDistance`). Use the same cardinal-rotated `voxelPos` the frustum test used.
   - Footprint ≈ 1 iso px → `pickHiZLevel(1, mipCount)` selects the finest level; sample `hiZTexel` over the 1-texel-expanded footprint at `isoPos`, take the max.
   - Cull (skip both the single-list and per-axis appends) iff `encoded > hiZMax + kOcclusionDepthMargin`. Conservative: background `65535` keeps `hiZMax` large → never culls a voxel that still sees background; a false positive is a hole, a false negative only lost savings.
4. **Metal mirror** `c_voxel_visibility_compact.metal` identically, using relaxed atomics as the existing occlusion shaders do, and **allocate `[[texture(n)]]` indices for the Hi-Z array that do not collide with `canvasFogOfWar`'s texture index** — Metal's argument table is one shared namespace, unlike GL's split image/sampler units. No new kernel → no `metal_pipeline.cpp` threadgroup entry.
5. **Gating stays as-is** — the `occlusionCullActive` uniform is derived from the already-computed `getVoxelOcclusionCullEnabled()` + `occlusionLagSourceStale_` in `beginTick`; zero added cost when disabled (branch not taken, Hi-Z not bound).
6. **Measure + verify:** record realized `voxelStage1` reduction on `voxel_set` zoom8 in the PR body; confirm net-positive across the perf_grid zoom range (or note the per-view gate). Add/refresh a `render-verify` baseline proving bit-identical cull-on vs cull-off.

### Affected files

- `engine/render/src/shaders/c_voxel_visibility_compact.glsl` — per-voxel Hi-Z test + `hiZLevels[12]` sampler + gate uniform (core change)
- `engine/render/src/shaders/metal/c_voxel_visibility_compact.metal` — Metal mirror (distinct texture indices)
- `engine/render/src/shaders/ir_iso_common.glsl` + `metal/ir_iso_common.metal` — hoist `pickHiZLevel`/`hiZTexel`/`kOcclusionDepthMargin` for reuse
- `engine/render/src/shaders/c_chunk_occlusion_cull.{glsl,metal}` — include the hoisted helpers (behavior unchanged)
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — bind Hi-Z levels before the compact dispatch when active; pack `occlusionCullActive` + `mipCount` into binding-7 FrameData
- `engine/render/src/shaders/` FrameData UBO struct (binding 7) — new gate field (mirror the CPU `FrameDataVoxelToTrixel` layout)
- `render-verify` baseline fixture for `voxel_set` cull-on/off bit-identity; optional perf-overlay note in `creations/demos/perf_grid/main.cpp`

### Cross-system audit (shared GPU resources touched)

- **`ChunkVisibility` SSBO (b24)** — unchanged producer/consumer; the per-voxel test reads it only to skip work (chunk-visible survivors), never writes it. No consumer migration.
- **Hi-Z mip chain (`getHiZMip`, R32I)** — a *new consumer* (the compact pass) of an existing texture; producer (`COMPUTE_DISTANCE_HIZ`) unchanged. Ordering already correct: Hi-Z is built from last frame's distances and read this frame (one-frame lag, by design). Must be bound to the compact dispatch's units.
- **binding-7 FrameData UBO** — additive field (`occlusionCullActive`/`mipCount`); the CPU `FrameDataVoxelToTrixel` struct and every shader that declares the binding-7 prefix must stay layout-synced (the compact and stage-1 share the block).
- **binding 0** — fog image (`canvasFogOfWar`, GL image unit) and `hiZLevels[0]` (GL texture unit) coexist in GL; **Metal needs distinct argument-table indices** (the one real parity hazard).
- **`system_build_light_occlusion_grid`** — **NOT touched** (lighting invariant 1: it iterates the full pool, never the compacted list). Do not wire the cull into it.

### Acceptance criteria (updated round 6 — positive-fire + marginal isolation)

The `--no-per-voxel-occlusion` split gate lets the per-voxel test be A/B'd in
isolation against the #1294 chunk cull. Both gates below compare
**chunk+per-voxel (`--occlusion-cull`) vs chunk-only
(`--occlusion-cull --no-per-voxel-occlusion`)** — the *marginal* delta the
per-voxel refine adds over the chunk cull — NOT cull-on vs cull-off (a
cull-off comparison folds in the chunk cull's own behavior; on Metal the chunk
cull is not itself byte-identical to cull-off — see "Root cause" — so only the
marginal isolation is a clean per-voxel gate).

- **Positive-fire gate (required).** Marginal capture > 0 at amp 0,
  `--no-sun-shadows`, zooms 1/4/8 — chunk+per-voxel vs chunk-only `avgVisible`
  (`--auto-profile`), non-zero. Zero = not done (a cull that never fires passes
  every byte-identity gate; #1812 was a zero-fire no-op for six rounds because
  the Hi-Z read returned the cleared sentinel — see "Root cause"). Record the
  numbers. **Measured: zoom1 233527→28200, zoom4 227847→27036, zoom8
  166666→19373 (marginal 205k/201k/147k).**
- **Marginal byte-identity (required).** chunk+per-voxel vs chunk-only md5 A/B
  (`IRPerfGrid --mode voxel_set --no-overlay --auto-screenshot
  --subdivision-mode none --wave-amplitude 0 --occlusion-cull` ±
  `--no-per-voxel-occlusion`) — byte-identical across the shot table now that
  the cull fires. Any diff is a too-tight emission-hull window (the
  counterexample-dump case). This became a *real* test of the emission-hull
  window + margin only once the cull fired. **Measured: byte-identical, 7/7.**
- **Net-positive / no per-view regression** — perf ms deferred to Linux/GL via
  the owed smoke (Metal timer rows read 0.000; the capture stat is the Metal
  gate).
- **Zero added cost** when `getVoxelOcclusionCullEnabled()==false` (default-config
  profile unchanged; the compact never reads the Hi-Z when
  `occlusionCullMipCount==0`, regardless of which texture is bound at its unit).
- Both backends build; carries both cross-host smoke labels.

### Root cause (round 6) — the Hi-Z read was a stale-image-shadowed sampler bind on Metal

The compact bound the finest Hi-Z (`getHiZMip(0)`) at texture unit 1 via a
SAMPLER bind (`Texture2D::bind`), but the Metal shader declared it `access::read`
(image) and `bindComputeResources` flushes the sticky image-binding table AFTER
the sampler table at the same encoder texture index. The prior frame's
stage-1/stage-2 leave `trixelDistances` bound as an IMAGE at unit 1 (sticky,
never cleared), and at compact time `trixelDistances` was just cleared to the
65535 sentinel — so the stale image bind shadowed the Hi-Z sampler bind and the
compact read all-sentinel, so the per-voxel test never fired (marginal capture
0 — the exact "unit collision with `trixelDistances`" the architect predicted).
**Fix:** bind the Hi-Z as a read-only IMAGE (matching the Metal `access::read`
declaration) so it occupies — and wins — that same table slot. GL is unaffected
(separate image/texture-unit namespaces). The SAME mechanism latently corrupts
the #1294 chunk cull's fine Hi-Z levels (0-3, shadowed by fog/distances/entityId
images) on Metal — so cull-on ≠ cull-off on Metal at cardinal poses (0.18-2.76%
drift, pre-existing, GL-verified-only) — filed as a follow-up (the chunk cull
reads coarse levels for 256-voxel chunks, so the corruption was never noticed).

### Design-doc lesson (i): identity gates require a paired positive-fire gate

Added to `docs/design/voxel-occlusion-culling.md` — a cull that never fires
passes every byte-identity test. #1812 surfaced three vacuous-PASS variants
(mode-gated-off, union mis-attribution, zero-fire) plus a stale-encoding fix a
fire gate would have caught. Cross-references the "default-off features need a
positive enabled-path test" rule in `engine/render/CLAUDE.md`.

### Gotchas

- **Shadow-feeder coverage (the #1 correctness hazard).** The chunk cull's eligibility guard only tests chunks fully inside the VISIBLE viewport, so it never drops an off-screen shadow feeder. The per-voxel test **must preserve the same safety**: it runs only inside the visible-frustum gate (`cullIsoMin/Max`), and a voxel in the shadow-feeder-widened swept region but outside the visible viewport is never per-voxel-culled. Do not sample the Hi-Z (which covers only the visible viewport) for shadow-feeder voxels — dropping a camera-occluded but shadow-relevant caster loses its sun shadow (design § "The one real hazard").
- **Encoding must match the Hi-Z.** Route through `encodeDepthWithFace(pos3DtoDistance(voxelPos), 0)` — the shared cardinal encode (depth [31:3] | flip [2] | slot [1:0], `* kDepthEncodeShift`), NOT an open-coded `* 4` (#2207 changed the cardinal layout; a hard-coded `*4` compares a half-scale depth against the Hi-Z's `*8` values so the test never fires). Margin = `kDepthEncodeShift` absorbs the low bits.
- **Cardinal / NONE-mode only.** Gate exactly on the chunk dispatch's conditions (`residualYaw==0`, NONE render mode, not rotating, no re-voxelize buffer); the continuous-yaw and per-axis paths keep every voxel (conservative), matching the shipped chunk cull.
- **Per-voxel cost in the hottest loop** (design § 1 flags this explicitly). One Hi-Z sample per surviving voxel per frame. Keep it behind the enable flag and on survivors only; the net-positive acceptance gate is the guard against shipping a regression.
- **One-frame-lag silhouette pop** — acceptable, same class as the chunk cull; note it as intentional drift so reviewers don't read it as a regression.
- **Backend parity** — the Metal mirror and the shared-include hoist must land in the same PR; a GL-only change would drift the backends (Metal build is the macOS host's — CI cross-host smoke covers it).
