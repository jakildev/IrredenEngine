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
they don't contribute to AO/lighting/fog (those shaders early-out on
empty pixels; any feeder-written canvas pixels produce results unused by
the framebuffer). The only output a feeder needs is
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

- `cullIsoMin/Max` (current field, unchanged) — swept feeder AABB.
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
| `c_compute_sun_shadow.glsl`         | all canvas pixels  | no (reads canvas distance texture for surface reconstruction only; shadow comparison comes from `sunDepthBuf`, baked with feeder contributions by `BAKE_SUN_SHADOW_MAP`) |
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

---

## #2258 Step B — feeder dispatch partition (implemented)

The `sub²`-multiplier-removal idea above lands as **#2258 Step B**, on top of
Step A (micro-slice packing; #2258). The mechanism, the durable perf lesson it
surfaced, and the shader-specialization idiom the fix established are recorded
here as the engine's pattern for future hot-path kernel variants.

### Mechanism (single-canvas mode)

The compact (`c_voxel_visibility_compact`) already computes each survivor's
`isoPos` for the widened-bounds cull, so it classifies each survivor for free:
**visible** (inside `visibleIsoBounds_`) → forward-append to indirect struct 0
as today; **off-screen shadow feeder** (outside it — the exact stage-2 #1740
skip convention) → tail-append into a **second** indirect struct (struct 1,
`kPerAxisSsboAlignBytes` into the shared `indirectBuf_`). Stage 1 then runs
**two** dispatches per canvas tick:

- **Visible dispatch** — struct 0, full `effSub²` micro-grid (unchanged).
- **Feeder dispatch** — struct 1, a **strided** `feederSubCap²` micro-grid
  (`u = (i/cap)*sub/cap`, monotone + full-span), so an off-screen caster's
  coarse trixel depth still lands ≥1 sample per sun-map texel. `feederSubCap`
  tracks the finest sun-bake cascade texel density (`IRPrefab::SunShadow::feederSubCap`).

Stage 2 dispatches **struct 0 only** — feeders contribute nothing on-screen
(the #1740 skip already proved byte-identity; now they stop *launching* there).
Sun shadows OFF ⇒ the compact classifies zero feeders ⇒ the feeder dispatch is
empty ⇒ the whole partition is structurally inert (byte-identical). `feederSubCap
== effSub` (or zero feeders) makes the feeder pass byte-identical to the
pre-Step-B single dispatch.

### The durable lesson: a runtime-disarmed uniform branch still taxes the hottest kernel

Step B first shipped the visible/feeder selection as a **runtime uniform**
(`feederPass`) branching inside the ONE shared stage-1 kernel. Same-session
profiling (`IRPerfGrid --auto-profile 120`, macOS/Metal) found a **+16 % zoom8
regression on `voxelStage1`** (5.15 → 5.97 ms) even though the zoom16 win was
large (−24 % combined). A disarm experiment isolated the cause: gating the
partition OFF below effSub 12 (compact keeps all survivors visible, CPU skips
the feeder dispatch) restored stage-2 to master but **left stage-1 at 5.97**. So
the tax was **not** the extra dispatch — it was the shared kernel now carrying
the feeder branches (the strided `((i/cap)*sub)/cap` divides) as **predicated
instructions on every visible-path invocation**. The Metal compiler predicates a
uniform branch rather than skipping it, so the divides are decoded on the hottest
kernel in the engine at *all* zooms; the feeder saving only outweighs the tax once
effSub is large.

**Lesson:** a uniform-controlled branch on a hot compute kernel is not free even
when it is never taken — the compiler predicates it, taxing every invocation. A
runtime disarm gate cannot remove a shader-side cost.

### The idiom: compile-time specialization from one shared body (architect option a′)

The fix (#2258 architect ruling **a′**) removes the tax **by construction**:
split the kernel into two **compile-time specializations of one shared source
body**, so the visible variant compiles with the feeder code textually absent —
codegen is master's kernel, no predication.

Both backends already run their own `#include` preprocessor before compiling, so
this needs zero engine infra:

1. **Extract the body** into `c_voxel_to_trixel_stage_1_body.{glsl,metal}` — an
   include-**fragment** (no `#version`, and on the GLSL side no `#include`s,
   since the GLSL resolver is non-recursive; the body lists its prerequisites in
   a header comment — the `ir_sun_shadow_sample.glsl` idiom).
2. **Two thin wrappers** supply the `#version` + `#define IR_FEEDER_PASS {0|1}` +
   the prerequisite includes, then `#include` the body:
   `c_voxel_to_trixel_stage_1.{glsl,metal}` (IR_FEEDER_PASS 0 = visible) and
   `c_voxel_to_trixel_stage_1_feeder.{glsl,metal}` (IR_FEEDER_PASS 1 = feeder).
   The Metal body names its kernel via an `IR_STAGE1_KERNEL_NAME` macro each
   wrapper defines (`metalFunctionNameForStage` keys off the file stem).
3. **Fence** the feeder-only code (`feederPass` reads, the strided divides) under
   `#if IR_FEEDER_PASS`. The runtime `feederPass` UBO lane is retired to a
   reserved std140 pad (`feederLanesPad_`, offset 204) — the flag is compile-time
   now.
4. **C++** builds the feeder program as a second `ShaderProgram` and dispatches
   it unconditionally after the visible dispatch — empty (every workgroup
   early-returns) when the compact classified zero feeders, and `feederSubCap()`
   floors at `cappedEffSub` so a `> 0` guard would be vacuous — `bindRange`-ing
   binding 26 onto struct 1; the visible dispatch keeps the original program.
   Metal bookkeeping: the feeder function name must be added to BOTH
   `threadgroupSizeForFunctionName` (2,3,8) **and** `functionUsesImageAtomicScratch`
   (both are explicit lists that do not self-detect — an omission silently
   dispatches a 1×1×1 grid or drops the feeder's atomic distance writes).

**Measured result** (same-session A/B, macOS/Metal, `IRPerfGrid --auto-profile 120`):

| zoom · stage | runtime-`feederPass` (Step B v1) | compile-time `IR_FEEDER_PASS` (a′) | Δ |
|---|---|---|---|
| 8 · `voxelStage1` | 5.97 | 5.33 | **−0.64 (tax removed)** |
| 8 · `voxelStage2` | 4.18 | 4.19 | ~0 |
| 16 · `voxelStage1` | 12.03 | 10.91 | −1.12 |
| 16 · `voxelStage2` | 6.61 | 6.63 | ~0 (feeder win retained) |

The zoom8 regression (+0.34 net vs master under v1) becomes a **net win**, and the
zoom16 win is retained (and `voxelStage1` additionally improves, the tax being
paid there too). Byte-identity holds (`shape_debug` voxel shots `cmp`-identical;
the visible variant is semantically master's kernel).

**Future kernels with a mode branch on the hot path should specialize the same
way** — one shared body, N compiled programs, `#if`-fenced mode code — rather
than branching on a uniform.
