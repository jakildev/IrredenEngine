# Voxel-pool solid-coverage occlusion culling

Design exploration for issue #1290. This doc scopes a third culling phase for
the voxel-pool render path — culling voxels that are inside the frustum and
locally exposed but **globally occluded by closer solid geometry** — and makes
a documented implement-vs-defer call.

**Recommendation: defer implementation.** Build the design now, gate the
trigger on a measurement that does not exist yet (a dense-indoor scene whose
raster stages dominate frame time). The full design below is ready to
implement when that trigger fires. Rationale and the trigger condition are in
[§ Recommendation](#recommendation).

This is the architect-ratification surface per the docs-first pattern: the
analysis is here; if the architect flips the implement-vs-defer call, the
implementation sketch in [§ Implementation sketch](#implementation-sketch-when-the-trigger-fires)
is the starting point.

---

## What "occlusion culling" means here, precisely

The voxel raster already drops two large classes of voxels before they cost a
distance write. Solid-coverage occlusion culling targets a **third, residual**
class. Naming the three precisely is the whole game — most of the imagined
payoff in the issue is already captured by the first two.

| Cull | Granularity | What it drops | Where it runs |
|---|---|---|---|
| **Frustum AABB** (existing) | per pool-chunk (256 voxels) | chunks whose iso AABB is fully outside the (shadow-feeder-widened) viewport | CPU `buildChunkVisibilityMask` → chunk mask SSBO |
| **Exposed-face mask** (landing, #1256/#1278) | per voxel × per face | faces whose neighbor cell is solid — so a voxel fully interior to a solid block emits nothing | GPU, baked into `exposedFaces` at pool build/mutate |
| **Solid-coverage occlusion** (this doc, #1290) | per chunk or per voxel | voxels that are locally exposed *and* in-frustum but whose every projected iso pixel is already owned by closer geometry from a **different** object | new GPU pre-pass |

The distinction that matters: the exposed-face mask is **intra-object** — it
removes the inside of a solid wall, terrain volume, or filled shape, which is
exactly the "deep voxel terrain" case the issue motivates. That payoff is
*already booked* by #1278's visible-triplet × exposed-mask model
([`voxel-face-rasterization.md`](voxel-face-rasterization.md)). What remains
for #1290 is **inter-object** occlusion: object B's exposed surface hidden
behind object A's exposed surface (a crate behind a wall, an enemy behind a
pillar, a room behind a closed door). Whether that residual is worth a new
pipeline stage is the question this doc answers.

---

## Current pipeline (grounding)

The voxel-to-trixel path runs three GPU passes inside one per-canvas tick
(`system_voxel_to_trixel.hpp`), preceded by a CPU chunk cull:

1. **CPU frustum cull** — `buildChunkVisibilityMask`
   (`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp:45`)
   walks per-pool-chunk iso AABBs (`C_VoxelPool::rebuildChunkBounds`,
   `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp:244`)
   against the iso viewport, widened by the shadow-feeder sweep
   (`IRMath::shadowFeederIsoBounds`, `engine/math/include/irreden/ir_math.hpp:792`;
   `kSunShadowMaxDistance = 64`). Writes a per-chunk `0/1` visibility mask
   into the `ChunkVisibility` SSBO (binding 24).

2. **GPU compact pass** — `c_voxel_visibility_compact.glsl:54`. Per voxel:
   tests the chunk-visibility bit (binding 24), the per-voxel active-mask bit
   (binding 8), and a per-voxel iso-bounds test, then `atomicAdd`s survivors
   into a compacted index list (binding 25) and finalizes the indirect
   dispatch params (binding 26) for the raster passes. **This is the natural
   home for a per-voxel occlusion test, and the `ChunkVisibility` SSBO is the
   natural home for a per-chunk one** — both inputs already flow into this
   pass.

3. **GPU raster stage 1** — `c_voxel_to_trixel_stage_1.glsl`. Each compacted
   voxel projects its visible faces to iso pixels and `imageAtomicMin`s a
   packed depth into the `R32I` distance texture (`triangleCanvasDistances`,
   binding 1; cleared to `kTrixelDistanceMaxDistance = 65535`,
   `component_triangle_canvas_textures.hpp`).

4. **GPU raster stage 2** — `c_voxel_to_trixel_stage_2.glsl:67`. Re-projects,
   reads the now-final distance at each pixel, and writes color + entity id
   **only where `voxelDistance == canvasDistance`** (the voxel won the
   atomicMin). Voxels that lost every tap write nothing here.

Dispatch size is driven by the compacted count: `voxelDispatchGridForCount`
(`system_voxel_to_trixel.hpp:37`) maps the survivor count to a
`(min(n,1024), divCeil(n,1024))` grid, and the compact pass writes those
counts into the indirect-dispatch buffer. **So any cull that reduces the
compacted count directly shrinks the stage-1/stage-2 dispatch — the savings
mechanism already exists.**

### The load-bearing invariant: stage 2 is already self-occluding

Stage 2 only writes where a voxel won the atomicMin. A voxel that is **fully
occluded** — every projected tap loses to closer geometry — therefore writes
**zero** color, zero entity id, and its losing `imageAtomicMin` calls in stage
1 leave the distance texture unchanged. **The final `trixelDistances`
(the `R32I` distance texture; called `triangleCanvasDistances` in stage 1/2
shaders at binding 1, and `trixelDistances` in the sun-shadow bake shader at
binding 0 — both refer to the same `C_TriangleCanvasTextures::textureTriangleDistances_`),
`triangleCanvasColors`, and `triangleCanvasEntityIds` are bit-identical
whether or not a fully-occluded voxel was dispatched.** Occlusion culling buys
exactly the *compute* of those losing taps — never a change in output. This is
what makes the cull safe (see [§ Downstream-consumer matrix](#downstream-consumer-matrix)),
and also what bounds its payoff: it removes ALU/atomic work, not pixels.

---

## Design space

### 1. Granularity

| Option | Cost | Cull rate | Verdict |
|---|---|---|---|
| **Per pool-chunk (256 voxels)** | one HZB test per chunk; reuses `ChunkVisibility` SSBO | only culls a chunk if its *entire* iso AABB is covered — rare unless chunks are spatially tight | **preferred starting point**; cheapest, zero new per-voxel cost, drops into the existing chunk-mask plumbing |
| **Per voxel (in compact pass)** | one HZB sample per voxel per frame | tightest cull rate | high per-voxel cost added to the hottest compact loop; only worth it if per-chunk leaves large residual |
| **Per spatial region (world-chunk / voxel-set)** | needs a spatial index the pool doesn't maintain | best coherence | out of scope — requires reorganizing the pool layout (separate task) |

Pool "chunks" are **slices of the pool array** (256 consecutive entries), not
spatial cells. They are coherent enough for frustum culling today because
voxel-sets are appended contiguously and tend to be spatially local. Occlusion
is more sensitive: a chunk is cullable only when its full iso AABB is behind
closer geometry, so a chunk with a wide or scattered AABB rarely qualifies.
Chunk-bounds tightness is therefore the dominant tuning lever — if per-chunk
cull rate is poor in practice, the fix is tighter spatial chunking, not
per-voxel testing.

**Choice: per pool-chunk Hi-Z test, written into the existing
`ChunkVisibility` SSBO**, AND-ed with the frustum bit. Per-voxel is a documented
follow-on only if measurement shows per-chunk leaves a large exposed residual.

### 2. Pre-pass location

| Option | Verdict |
|---|---|
| **CPU pre-pass** | Rejected. Needs a CPU mirror of the distance texture — a GPU→CPU readback on the per-frame critical path, which stalls the pipeline. Non-starter. |
| **GPU compute pre-pass → `ChunkVisibility` SSBO** | **Chosen.** A compute shader samples a Hi-Z buffer at each chunk's projected iso AABB and ANDs an occlusion bit into the chunk mask the compact pass already reads. No new readback, no new consumer wiring. |
| **GPU per-voxel in compact pass** | Deferred follow-on (granularity §1). |

### 3. Hi-Z (hierarchical depth) source

There is no Hi-Z buffer today; the distance texture is a flat `R32I` image.
A Hi-Z is a mip chain of **max** depth (so a coarse texel says "the *farthest*
thing in this footprint is at depth D — anything strictly behind D is hidden").
It is built by a downsample-max compute pass over the distance texture after
the raster + AO complete.

The chicken-and-egg: the cull wants depth to cull the raster, but the depth is
produced *by* the raster. Standard resolution — **use last frame's distance
texture as the Hi-Z source**, accepting one-frame lag. Occlusion errors from
lag are false *negatives* at moving silhouettes (a voxel revealed this frame
that last frame's depth still showed as covered) — visually a one-frame pop,
the same artifact every Hi-Z occlusion system accepts. The cull must be
**conservative**: when in doubt, keep the voxel. A false positive (culling a
visible voxel) is a hole; a false negative (keeping an occluded voxel) is just
lost savings.

### 4. Latency / correctness budget

- Read **last frame's** depth (one-frame lag). Current-frame depth is
  unavailable — it is mid-write by the pass we want to cull.
- Conservative rounding: project the chunk AABB to iso, **expand by one
  texel** at the sampled mip, and cull only if the *nearest* chunk depth
  exceeds the *farthest* (max) Hi-Z depth over that footprint.
- On camera cut / teleport / first frame, the lag source is stale — **disable
  the cull for one frame** after any discontinuous camera move
  (`C_RenderCamera` already tracks per-frame iso position; a delta threshold
  gates it).

---

## Downstream-consumer matrix

Every consumer of the voxel raster, and whether the cull is safe for it. This
is the section a reviewer must check before any implementation lands.

| Consumer | Reads | Effect of a contribution-preserving cull | Safe? |
|---|---|---|---|
| **Stage 1/2 raster** (final color/depth/id) | the pool | identical output (fully-occluded voxels write nothing anyway) | ✅ exact |
| **Sun-shadow bake** `c_bake_sun_shadow_map.glsl:64` | `trixelDistances` (R32I) | reads the *final* distance texture, not the pool — identical iff `trixelDistances` is identical | ✅ **conditional** — see below |
| **Voxel AO** `c_compute_voxel_ao.glsl` | `trixelDistances` neighbors (screen-space, post-T-091) | reads the final distance texture — identical iff `trixelDistances` is identical | ✅ conditional |
| **Light-occlusion-grid build** `system_build_light_occlusion_grid.hpp:242` | the full pool (`getLiveVoxelCount`), **independent of the visibility mask** (lighting invariant 1) | must NOT be culled; it never consults the compacted list, so it is structurally unaffected | ✅ by construction — **do not wire the cull into it** |
| **Light volume propagate** | the occlusion grid above | downstream of an unculled producer | ✅ |
| **Picking / hover** `triangleCanvasEntityIds` | stage-2 entity ids | a fully-occluded voxel is not on top, so not pickable through normal (non-x-ray) picking; identical for visible picks | ✅ for visible picks; ⚠️ would break a future "pick occluded / x-ray" mode |

### The one real hazard: approximate culls and the shadow path

The "identical iff `trixelDistances` is identical" rows are safe **only for an
exact, contribution-preserving cull** (cull a voxel ⟺ it provably loses every
tap this frame). The chosen design is *approximate* — it uses a coarse,
one-frame-lagged Hi-Z. An approximate cull can drop a voxel that *would* have
won a tap this frame, which changes `trixelDistances`, which the sun-shadow
bake and AO then read. Consequences:

- **Sun shadows.** The bake's AABB is the camera iso-frustum swept along
  `-sunDir` by `kSunShadowMaxDistance`, precisely so off-screen casters within
  shadow range still write `trixelDistances` and project into the sun depth
  map. If the occlusion Hi-Z does **not** cover that swept region, the cull
  could drop a caster that is camera-occluded but shadow-relevant, dropping its
  shadow. **Mitigation: the Hi-Z must cover the shadow-feeder-widened bounds,
  not just the visible viewport — the same widening `buildChunkVisibilityMask`
  already applies.** A chunk inside the swept region is never occlusion-culled
  unless the Hi-Z over the *swept* footprint covers it.
- **AO.** AO samples a 3-pixel neighborhood; a one-frame-lag hole at a moving
  silhouette produces a one-frame AO shimmer at that edge. Acceptable, same
  class as the color pop.

These are the reasons the cull ships **off by default and gated** (issue's own
"gated on a heuristic or off by default"). When enabled, the conservative
rounding + shadow-feeder Hi-Z coverage above keep it correct; without them an
approximate cull is unsafe for the lighting path.

---

## Expected payoff (analytical)

The cull removes **voxel-count-proportional** work only. It does nothing for
the screen-space passes, which dominate many current profiles:

- **Helped:** compact pass survivors, stage-1 distance writes, stage-2 color
  writes — all O(voxels × visible faces).
- **Not helped:** the distance/color **clear** (fixed ~4.4 ms, #1050 — runs
  regardless of voxel count), AO (O(screen pixels)), sun-shadow bake
  (O(distance-texture pixels)), light-volume chain (fixed 128³ × 32 iterations),
  the light-occlusion-grid build (full pool by invariant), and the entire
  UPDATE pipeline (#1161 measured it dominating IRPerfGrid at ~8.6 s/frame).

So the payoff ceiling is `(stage1 + stage2 + compact) × occludedExposedFraction`,
where `occludedExposedFraction` is the share of *locally-exposed, in-frustum*
voxels that are *also* inter-object-occluded — i.e., the residual left after
the frustum cull and the exposed-face mask. For the scenes the engine renders
today:

- **Sparse outdoor / IRPerfGrid:** `occludedExposedFraction ≈ 0`. Almost every
  exposed voxel contributes a pixel. The cull is pure overhead (Hi-Z build +
  per-chunk test) with no payoff — which is why it must default off.
- **Dense indoor (walls, rooms, corridors):** potentially large — a closed
  room's far wall, the contents behind a near wall. **But there is no
  dense-indoor demo today**, so this fraction is currently unmeasured and
  unmeasurable.

The honest conclusion: the case that motivates the feature (dense indoor) is
exactly the case the engine cannot yet measure, and the case it can measure
(sparse grids) shows no benefit. Combined with the exposed-face mask already
booking the intra-object payoff, the expected near-term value is low.

---

## Recommendation

**Defer implementation.** Trigger condition — implement when **all** hold:

1. A representative **dense-indoor scene exists** as a demo or `IRPerfGrid`
   mode (depends on a dense-indoor authoring path; tracked as the precondition
   in the follow-up issue #1294).
2. On that scene, `VOXEL_TO_TRIXEL_STAGE_1` + `STAGE_2` + the compact pass
   together exceed **~25 % of frame time** (profiler blocks already exist via
   `IR_PROFILE_BLOCK` in these systems).
3. A measured `occludedExposedFraction` on that scene exceeds **~2×** overdraw
   on the exposed-surface set (i.e., the exposed-face mask alone leaves
   substantial inter-object occlusion on the table).

Until then the design is parked here, ready to lift. This matches the issue's
"decision to implement OR explicit decision to defer with documented trigger"
acceptance and the architect docs-first pattern. Deferring also lets the
in-flight rasterization rework (#1278 visible-triplet × exposed-mask, #1288
cull-gated `REBUILD_GRID_VOXELS`) settle first — both change the exact cost
structure this cull would optimize, so measuring before they land would
measure the wrong baseline.

If the architect prefers to land a gated, off-by-default skeleton now (to have
the plumbing ready), the implementation sketch below is the scoped first PR.

---

## Implementation sketch (when the trigger fires)

Ordered, each a bounded PR:

1. **Hi-Z build stage.** New `COMPUTE_DISTANCE_HIZ` system + shader
   (`c_build_distance_hiz.glsl`): downsample-**max** mip chain of
   `triangleCanvasDistances` into a new mipped `R32I` texture on
   `C_TriangleCanvasTextures`. Runs after stage 2 (and before next frame's
   cull reads it). `SystemName` enum entry required. Per-canvas.

2. **Occlusion pre-pass.** New `COMPUTE_CHUNK_OCCLUSION` system + shader
   (`c_chunk_occlusion_cull.glsl`): one invocation per pool-chunk; projects
   the chunk's iso AABB (`C_VoxelPool::getChunkBounds`, expanded by the
   shadow-feeder sweep), picks the Hi-Z mip whose texel ≥ AABB footprint,
   samples max-depth over the footprint, and ANDs `0` into the chunk's
   `ChunkVisibility` bit (binding 24) iff the chunk's nearest depth is strictly
   behind the Hi-Z max. Conservative: expand the footprint by one texel; skip
   (keep) on any partial coverage. Reads **last frame's** Hi-Z.

3. **Gating.** `C_RenderCamera` (or a render option) carries an
   `occlusionCullEnabled_` flag, default false, plus a one-frame disable after
   a camera-position delta over threshold. The pre-pass early-returns when off,
   so the cost is zero in the default configuration.

4. **Demo + measurement.** A dense-indoor `IRPerfGrid` mode or new demo with a
   per-half on/off perf overlay, plus `render-verify` baselines proving
   pixel-identical output with the cull on vs off (it must be — fully-occluded
   voxels write nothing; any diff is a cull bug). This PR is what proves the
   trigger's payoff number.

Each PR touches `engine/render/src/shaders/` and a render system, so each runs
`render-debug-loop` + `attach-screenshots` and carries both cross-host smoke
labels.

### Risks to flag for the implementing worker

- **Shadow-feeder coverage** — the #1 correctness hazard above. The Hi-Z and
  the chunk-AABB projection must both use the shadow-feeder-widened bounds, or
  sun shadows lose off-screen casters.
- **Pool-chunk spatial coherence** — if per-chunk cull rate is poor, the fix is
  tighter spatial chunking of the pool, not per-voxel testing. That is a larger,
  separate change (pool layout).
- **Lighting invariant 1** — never wire the cull into
  `system_build_light_occlusion_grid` (it must keep iterating the full pool).
- **One-frame-lag pop** — acceptable but call it out in the demo's intentional-
  drift note so reviewers don't read it as a regression.

## Acceptance-gate lessons (#1812 per-voxel refine)

The per-voxel refine (#1812) went through six rounds, three of which shipped a
**vacuous PASS** — a cull that measured as correct while doing nothing. The
durable lessons:

- **(i) An identity gate requires a paired positive-fire gate.** Byte-identity
  cull-on vs cull-off (or marginal-on vs marginal-off) only proves the OFF path
  is a no-op — it can never prove the feature *works*. A cull that never fires
  passes every byte-identity test. #1812's three vacuous-PASS variants were:
  mode-gated-off (the enable flag never reached the tested state), union
  mis-attribution (the byte-identity + "capture" numbers were entirely the
  #1294 chunk cull; the per-voxel marginal was zero), and zero-fire (the Hi-Z
  read returned the cleared sentinel — see below). Every default-off cull needs
  a **positive-fire gate**: a measured, non-zero marginal capture at a pose
  where the cull is legally active. This is the cull-shaped instance of the
  "default-off features need a positive enabled-path test" rule in
  [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md).
- **(h) Isolate the marginal.** `--occlusion-cull` measures the UNION of the
  chunk pre-pass and the per-voxel refine; a split gate
  (`--no-per-voxel-occlusion`) is required so each mechanism's acceptance is
  measured *marginally*, against the other running. Without it, a zero-fire
  per-voxel test rides the chunk cull's capture and reads as working.
- **Metal: a sampler bind at a texture unit is shadowed by a stale IMAGE bind
  at the same unit.** `bindComputeResources` flushes the (sticky, never-cleared)
  image-binding table AFTER the sampler table at the same encoder texture index,
  so binding a Hi-Z read at a unit that a prior dispatch left an image bound to
  reads that stale image, not the Hi-Z. The compact read `trixelDistances`
  (freshly cleared to the 65535 sentinel) instead of `getHiZMip(0)`, so the test
  never fired. Bind Hi-Z reads as **images** (matching the Metal `access::read`
  declaration) so they win the flush. GL is immune (separate image/texture-unit
  namespaces) — a Metal-only defect a GL-only smoke will miss. The same
  mechanism latently corrupts the #1294 chunk cull's fine Hi-Z levels on Metal.

---

## See also

- [`voxel-face-rasterization.md`](voxel-face-rasterization.md) — the
  exposed-face mask that already books the intra-object occlusion payoff
- [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) "Lighting culling
  invariants" (invariant 1) and "Sun shadow bake AABB sweep"
- [`screen-space-sun-shadow-map.md`](screen-space-sun-shadow-map.md) — the
  shadow path that reads `trixelDistances`
- Issue #1290 (this design), #1294 (gated implementation follow-up), #1050
  (clear cost), #1161 (UPDATE-dominated profile), #1278 / #1288 (in-flight
  rasterization rework)
