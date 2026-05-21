# Voxel-to-trixel feeder split (T-309 design doc)

**Status:** proposal. T-309 (#1020) follow-up to the diagnostic
instrumentation that shipped in PR #1019.

**Problem owner:** rendering —
`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
and the corresponding compute shaders
(`c_voxel_visibility_compact.glsl`, `c_voxel_to_trixel_stage_1.glsl`,
`c_voxel_to_trixel_stage_2.glsl`).

**Audience:** the agent picking up the multi-PR implementation series.
This doc fixes the algorithm and answers the three open questions from
the issue so each follow-up PR can stay narrow.

---

## Why this exists

PR #1019's `cull (vis/total)` diagnostic shows the voxel cull is 12–30×
looser than the `1/zoom²` ideal as the camera zooms in past
full-screen voxel coverage:

| zoom | visible | total | ratio | ideal (1/zoom²) | over-ideal |
|------|---------|-------|-------|-----------------|------------|
| 1    | 260K    | 262K  | 0.99  | 1.00            | 1.0×       |
| 4    | 205K    | 262K  | 0.78  | 0.06            | **13×**    |
| 8    | 124K    | 262K  | 0.47  | 0.02            | **23×**    |

Root cause: `system_voxel_to_trixel.hpp:248-257` inflates the cull
bounds via `IRMath::shadowFeederIsoBounds(visible, sunDir,
sweepDistance)` so off-screen shadow casters within
`kSunShadowMaxDistance` (64 voxels) still rasterize into
`trixelDistances` for the screen-space sun-shadow bake. This is a
documented invariant
(`engine/render/CLAUDE.md` §"Lighting culling invariants",
§"Sun shadow bake AABB sweep") — feeders **must** contribute to the
canvas distance texture so `c_bake_sun_shadow_map.glsl` can project
their depth into the sun map.

What's wasted: feeders go through the *same* code path as visible
voxels — compact, stage 1 at `sub²` invocations per voxel, stage 2
color + entity-id writes. None of those extra writes reach the
on-screen framebuffer (feeders are off-viewport by definition) and
they don't contribute to AO/lighting/fog (those shaders sample only
visible-region canvas pixels). The only output a feeder needs is
**iso-pixel depth on the canvas distance texture**, consumed by the
sun bake.

---

## Proposed split

The compact stage classifies every visible-pool voxel into one of two
indirect-dispatch lists:

- **`visibleIndices`** — iso position inside the un-swept visible
  AABB. Receives the full pipeline today: stage 1 at `sub²` per
  voxel (color + distance + face deformation), stage 2 (color +
  entity-id).
- **`feederIndices`** — iso position inside the swept AABB but
  outside the visible AABB. Receives a **depth-only fast path**: one
  emit per face (no `sub²` multiplier), writes only
  `triangleCanvasDistances` for the sun bake. No color, no entity-id,
  no AO contribution.

Indirect-dispatch params double: `numGroupsX/Y` and `numGroupsZ` per
list. For visible, `numGroupsZ = sub²` (today's behavior). For
feeder, `numGroupsZ = 1`.

A new shader `c_voxel_feeder_to_distances.glsl` handles the feeder
list. It is structurally `c_voxel_to_trixel_stage_1.glsl` with the
subdivision branch deleted — only the `voxelRenderOptions.x == 0`
arm survives, written as a standalone shader so the visible-path
stage 1 stays branch-free and bit-identical to today's behavior at
yaw=0.

```
RENDER pipeline (systems in order):
  VOXEL_TO_TRIXEL_STAGE_1
    • c_voxel_visibility_compact   → visibleIndices + feederIndices
    • c_voxel_to_trixel_stage_1    → visible: distance writes (sub²)
    • c_voxel_feeder_to_distances  → feeder:  distance writes (sub=1)   NEW
  VOXEL_TO_TRIXEL_STAGE_2
    • c_voxel_to_trixel_stage_2    → visible: color + entityID
                                     (feeders skipped — never bound)
```

The chunk-visibility mask (`buildChunkVisibilityMask`) keeps the
**swept** bounds — chunk-level pre-cull is cheap and feeders still
need to survive chunk filtering. Only the per-voxel compact stage
splits.

---

## Open questions, answered

### Q1. Does the sun-shadow bake need feeder voxels at full subdivision resolution?

**No.** A single emit per face is sufficient.

The sun bake (`c_bake_sun_shadow_map.glsl`) iterates the canvas
distance texture and, for each iso pixel with a valid depth,
reconstructs the world position and atomicMins its sun-space depth
into the 1024² sun map. The bake samples **canvas iso pixels**, not
voxel sub-pixels — so the relevant resolution is iso pixels per
sun-space pixel.

At typical configurations (256² canvas, sun map 1024² covering an
iso-frustum AABB swept by 64 voxels), the bake's
canvas-iso-to-sun-space ratio is ~10:1 or denser. Sub-pixel iso
detail from feeder voxels collapses to the same sun-space pixel — the
extra `sub²` emits per feeder are atomicMin redundant writes against
themselves.

For **visible** voxels the picture is different: their sub-pixel iso
detail produces crisper shadow boundaries where shadows fall *onto*
neighboring visible voxels (the on-screen sun-shadow lookup samples
those sun-map texels back). Visible voxels keep the full `sub²` path.

For feeders, dropping to single emit per face (3 faces × 2 sub-pixels
local work group = 6 emits per voxel, same as `SubdivisionMode::NONE`)
preserves the silhouette information the bake actually consumes.

### Q2. Does the chunk-visibility mask need the same split?

**No.** Keep the chunk mask at the swept bounds.

The mask is a coarse-grained pre-cull (one bit per `kVoxelChunkSize`
chunk). Feeder chunks still need to survive it. Splitting at chunk
granularity would only save the compact-stage filter cost on chunks
that the existing per-voxel iso test would catch anyway — and would
require uploading two masks per frame. Not worth it.

Where the split *does* live: the compact shader needs both AABBs in
the per-frame UBO:

- `cullIsoMin/Max` (current field, repurposed) → swept feeder AABB.
- `visibleIsoMin/Max` (new fields) → un-swept visible AABB.

Compact classifies into list A or B based on which AABB contains the
voxel's iso position.

### Q3. Are there other consumers of `trixelDistances` besides the sun-shadow bake?

**Yes, but none need feeder depth.**

Grep `imageLoad.*trixelDistances` across `engine/render/src/shaders/`
finds:

| Shader                              | Reads when         | Needs feeders? |
|-------------------------------------|--------------------|----------------|
| `c_bake_sun_shadow_map.glsl`        | all canvas pixels  | **yes**        |
| `c_compute_sun_shadow.glsl`         | all canvas pixels  | no (reads sun map, not feeders) |
| `c_compute_voxel_ao.glsl`           | visible + AO margin | no             |
| `c_lighting_to_trixel.glsl`         | visible pixels     | no             |
| `c_voxel_to_trixel_stage_2.glsl`    | visible pixels     | no             |

Visible-region readers (AO, lighting, stage 2 color tap) all live
inside `cull.isoViewport(kGpuMargin)` — the un-swept AABB plus a
small AO margin. They never sample canvas pixels outside that range.

Off-visible-region canvas pixels are read by **only** the sun bake.
Feeders write there; nobody else cares. The split is safe.

---

## Sun-shadow correctness invariant (preserved)

`engine/render/CLAUDE.md` §"Sun shadow bake AABB sweep" requires that
voxels within `kSunShadowMaxDistance` along `-sunDir` of the visible
region contribute to the canvas distance texture so the bake can
project their depth into the sun map. The feeder list preserves this
contract: every voxel that survived the swept-AABB chunk and per-voxel
filter still emits one depth-only tap per face. The bake reads the
same `trixelDistances` it does today.

What the split changes: feeders no longer pay `sub²` cost, no longer
write color, no longer write entity-id. The bake's input is bit-
identical at yaw=0 to the pre-split path for any voxel where
`SubdivisionMode::NONE` was already producing the same single-tap
output — i.e. all feeders, since the bake samples canvas iso pixels
and sub-pixel iso detail collapses in sun space (Q1).

`render-debug-loop` ROI-crop coverage of shadow boundaries is the
right reviewer-side check that nothing visually regresses; a missing
crop means a shadow-correctness regression got past the smoke pass.

---

## Implementation order (multi-PR series)

Each PR re-runs `scripts/perf/perf_grid_matrix.sh --quick --frames 120`
and includes the `cull (vis/total)` and per-stage GPU-timer diffs in
the body. The target invariant: visible/total at fixed
`(zoom, sub_mode, sub_base)` improves monotonically across the
series, with the largest jump at the compact + feeder-shader split.

1. **PR a — compact-shader split.** Add `visibleIsoMin/Max` to
   `FrameDataVoxelToCanvas`; teach `c_voxel_visibility_compact.glsl`
   to write into two compaction lists; new
   `kBufferIndex_FeederVoxelIndices` SSBO; second
   `VoxelIndirectDispatchParams` slot. Stage 1 still dispatches over
   `visibleIndices ∪ feederIndices` (compatibility path, no behavior
   change yet). Verify counts: `visibleCount + feederCount` at frame
   N equals the pre-split `visibleCount`.

2. **PR b — feeder shader + dispatch.** Add
   `c_voxel_feeder_to_distances.glsl` (stage-1 minus the subdivision
   branch). Add `VOXEL_TO_TRIXEL_FEEDER_STAGE` system (or a sibling
   dispatch inside `VOXEL_TO_TRIXEL_STAGE_1`). Bind feeder list to
   the new shader; stage 1 dispatches over `visibleIndices` only.
   Feeders no longer reach stage 2 (no bindings). Expected effect:
   `cull (vis/total)` returns to ~`1/zoom²` ideal; stage 2 throughput
   reflects the visible-only count.

3. **PR c — Metal parity.** Author all GLSL shaders in PRs a and b
   land via OpenGL only; Metal port follows in a dedicated PR per the
   `backend-parity` skill. Cross-host smoke validates on the lagging
   backend before merge.

PR a is bounded (one shader edit, two SSBO additions). PR b is the
load-bearing one — author it carefully, include before/after
`render-debug-loop` ROI-crop pairs for any shot in `kShots[]` that
exercises sun shadows at high zoom (the cull-loose regime).

---

## Out of scope

- LOD or distance-based subdivision falloff for visible voxels. This
  doc is strictly about removing wasted feeder cost; visible voxels
  keep their full pipeline.
- Replacing the screen-space sun bake with an alternative shadow
  algorithm. See `docs/design/screen-space-sun-shadow-map.md` for the
  bake's design and migration history; that path is stable.
- Sub-canvas culling for feeders (clipping feeders whose iso position
  lands outside the canvas pixel bounds). Already handled by
  `writeDistanceTap`'s `isInsideCanvas` no-op; the per-frame compute
  cost is the same regardless. The win from this doc is `sub²`
  multiplier removal, which dominates at high zoom.
