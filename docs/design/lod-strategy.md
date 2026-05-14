# LOD strategy — artist-driven detail tiers, prefab-manifest composition

**Status:** Phase 1 in queue (#708). Phase 2 design sketched here, no
implementation ticket yet. Phase 3 (cross-tier interpolation) deferred to
its own design pass when Phase 2 ships.

**Decision:** LOD lives at two layers — runtime selection (Phase 1) and
prefab-manifest composition (Phase 2). It deliberately does **not** live
inside the binary asset formats (`.vxs`, `.rig`). The format-extensibility
escape hatch remains open if that decision later proves wrong.

**Problem owner:** render + prefab —
`engine/prefabs/irreden/render/lod_utils.hpp`,
`engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp`,
future `.prefab.lua` loader under `engine/script/`.

---

## The use case driving this

An artist authors an entity — a flower, say — and wants progressively
more detail as the camera zooms in:

- At zoom 1×, the flower is a simple silhouette: stem, leaves, blob for
  the head.
- At zoom 2×, the head resolves into individual petals.
- At zoom 4×, each petal has shape detail; a stamen appears in the center.
- At zoom 8× and above, fine veins, pollen specks, the works.

The trigger is camera zoom, but the **content of each tier is
artist-authored**, not procedurally generated from a single base mesh.
This is the key difference from automatic mesh-decimation LOD: the
artist explicitly designs each detail level. The engine's job is to
pick which one to draw.

Eventually we'll want to interpolate between tiers (blend the LOD_1
flower toward the LOD_2 flower over a zoom range so the transition is
not a hard pop). That's a Phase 3 concern.

---

## Three-layer split

```
┌──────────────────────────────────────────────────────────────────┐
│  Layer 3 — Prefab manifest (.prefab.lua, future T-173)           │
│  Composes multiple .vxs files into one entity, keyed by LOD tier │
│    lod = {                                                       │
│      [LOD_0] = "flower_silhouette.vxs",                          │
│      [LOD_2] = "flower_petals.vxs",                              │
│      [LOD_4] = "flower_stamen.vxs",                              │
│    }                                                             │
└──────────────────────────────────────────────────────────────────┘
                              ▲
                              │ composes references to
                              │
┌──────────────────────────────────────────────────────────────────┐
│  Layer 2 — Runtime selector (Phase 1, issue #708)                │
│  - LOD_UPDATE system writes C_ActiveLodLevel singleton each frame│
│  - SHAPES_TO_TRIXEL filters C_ShapeDescriptor by lodMin_         │
│  - Filter is pre-GPU; shader unchanged                           │
└──────────────────────────────────────────────────────────────────┘
                              ▲
                              │ reads
                              │
┌──────────────────────────────────────────────────────────────────┐
│  Layer 1 — Asset binary (.vxs, .rig) — UNCHANGED by LOD work     │
│  One .vxs = one detail level. No LOD tier slot in the format.    │
└──────────────────────────────────────────────────────────────────┘
```

The architecturally important property: **Layer 1 doesn't know LOD
exists.** Each `.vxs` is an opaque "render this content at this detail."
The decision of which content to load lives in Layer 3 (manifest); the
decision of which shapes to draw from already-loaded content lives in
Layer 2 (runtime filter). Neither requires a format change.

---

## Why not bake LOD into `.vxs`

The tempting alternative is to add an `LODG` chunk to `.vxs` so a single
file carries all tiers, with the per-shape `recordVersion_` field
extended to include a `lodMin` byte. Rejected for five reasons:

### 1. One `.vxs` = one editable thing

The editor (entity-editor epic, #213) opens a `.vxs` and lets the artist
draw shapes. If a single file held all five LOD tiers, the editor needs
a "which tier am I editing?" mode, a "show me the next tier overlaid"
mode, and conflict resolution when a shape exists in multiple tiers.
That's a lot of UX complexity to push onto Phase 1 of the editor.

By keeping each `.vxs` single-tier, the editor stays mode-less. Open
`flower_petals.vxs`, draw petals, save. Open `flower_stamen.vxs`, draw
the stamen, save. Each file is independently authorable, previewable,
and version-controllable.

### 2. Manifest composition is already required

The prefab format (T-173) has to exist regardless of LOD — it's the
layer that pulls a `.vxs` plus a `.rig` plus a component pack into a
single spawnable entity. Once that layer exists, **adding LOD to it is
one extra optional table**. Compare to extending `.vxs`: new chunk type,
new migration story, every consumer of `.vxs` (loader, editor, sidecar
emitter, `IRShapeDebug`, future tools) has to understand it.

The marginal cost of "LOD in the manifest" is much lower than the
marginal cost of "LOD in every binary asset format."

### 3. Storage efficiency is not a real constraint here

The classic argument for "LOD tiers in one file" is shared geometry:
LOD_2 and LOD_3 reuse most of LOD_1's vertices. For our SDF-primitive
SHAPES mode this doesn't apply — each shape is 80 bytes (per
`ShapeRecord`); a flower at five tiers is maybe 500 KB of shape records
in the worst case. Storage is not the bottleneck. For DENSE mode it
**could** apply (a coarser voxel grid is genuinely a subset of a finer
one), but Phase 2 punts on DENSE LOD; if we ever need it, we can add
the chunk then with full information about the actual access pattern.

### 4. The escape hatch is free

`.vxs` follows the Save format extensibility rules in
`engine/asset/CLAUDE.md` §"Save format extensibility rules":

- Rule #1 — chunk-table forward compatibility. Adding an `LODG` chunk
  later requires zero version bump.
- Rule #3 — per-record additive versioning. Adding `lodMin` to
  `ShapeRecord` later is a `kShapeRecordVersion` bump plus a default in
  the older-build load path.

So choosing "manifest-layer LOD" today does not foreclose
"format-layer LOD" later. If after Phase 2 ships we discover that
artists want both — one `.vxs` per entity covering all tiers, with the
manifest selecting at coarser granularity — we can add the chunk and
migrate. The reverse migration ("undo a format change because the
manifest layer turned out to be enough") is much harder.

### 5. Runtime selection is already 80% scaffolded

`engine/prefabs/irreden/render/lod_utils.hpp` already defines
`computeLodLevel(float zoomLevel)`, `lodVoxelScale(LodLevel)`,
`shouldSkipAtLod(entityLodMin, currentLod)`. `C_ShapeDescriptor` already
has a `lodLevel_` field that ships to the GPU. None of this needs a
format change to become real — it just needs the system that reads
camera zoom and writes a singleton plus the filter in
`SHAPES_TO_TRIXEL`. That's Phase 1.

---

## Phase 1 — runtime selector (in-flight, #708)

Scope: turn the existing scaffolding into a working pipeline where a
single entity can carry shapes with different LOD-min thresholds, and
the renderer skips ones whose threshold is above the current zoom-derived
LOD tier.

Concrete pieces:

- **`LodLevel` enum.** Already in `lod_utils.hpp`. Five tiers
  (LOD_0 = highest detail at high zoom, LOD_4 = lowest at low zoom).
- **`computeLodLevel(float zoom) -> LodLevel`.** Already stubbed.
  Threshold values may need adjustment — the stub uses
  `< 0.5 / < 1.0 / < 2.0 / < 4.0 / >= 4.0` but actual camera zoom is
  clamped to [1.0, 64.0] (see
  `IRConstants::kTrixelCanvasZoomMin/Max` in
  `engine/common/include/irreden/ir_constants.hpp` and the snap at
  `engine/render/src/render_manager.cpp:261`). Recommended:
  `< 2.0 -> LOD_4 / < 4.0 -> LOD_3 / < 8.0 -> LOD_2 / < 16.0 -> LOD_1 / >= 16.0 -> LOD_0`.
- **`C_ActiveLodLevel` singleton.** New component carrying
  `LodLevel current_`. Uses the singleton-component infrastructure from
  T-162.
- **`LOD_UPDATE` system in UPDATE pipeline.** Reads `C_ZoomLevel` from
  the active camera entity, computes the tier, writes the singleton.
  Runs once per frame, before RENDER.
- **Per-shape `lodMin_` field.** Reinterpret the existing
  `C_ShapeDescriptor::lodLevel_` as "minimum LOD tier at which this
  shape is drawn." A shape with `lodMin_ = LOD_2` is drawn when
  `activeLod >= LOD_2` (zoom is high enough). A shape with
  `lodMin_ = LOD_0` (the default) is always drawn.
- **CPU-side filter in `SHAPES_TO_TRIXEL`.** At `beginTick`, read the
  singleton. In the per-entity tick, skip shapes whose `lodMin_` exceeds
  the active level. Don't push them into the GPU bucket. No shader
  change.

Verified by a render-verify shot list at zoom 1× / 2× / 4× / 8× of a
test entity with shapes at multiple `lodMin_` thresholds.

What Phase 1 explicitly does **not** do:

- Multi-tier `.vxs` composition. A Phase 1 entity carries all its shapes
  in a single `C_ShapeDescriptor` set; LOD is per-shape, not per-file.
- DENSE-mode voxel LOD. The filter applies only to SHAPES-mode.
- Rig LOD. See "Rigs and LOD" below — likely never.
- Cross-tier blending. Hard pop at threshold for Phase 1.

---

## Phase 2 — prefab-manifest composition (sketched, no ticket yet)

Once the prefab format (T-173) lands, extend its DSL with an optional
per-`.vxs` LOD table:

```lua
-- flower.prefab.lua
return {
    vxs = {
        [LOD_0] = "flower_silhouette.vxs",   -- always loaded
        [LOD_2] = "flower_petals.vxs",       -- loaded when activeLod >= LOD_2
        [LOD_4] = "flower_stamen.vxs",       -- loaded when activeLod >= LOD_4
    },
    rig = "flower.rig",                       -- one rig, all tiers share it
    components = { ... },
}
```

Open design questions for Phase 2:

- **Lazy vs eager load.** Load all referenced `.vxs` at prefab spawn, or
  only the active tier? Eager is simpler but uses more memory; lazy
  needs a "swap content on tier change" mechanism. Lean lazy with a
  prefetch hint when a zoom-in animation crosses a threshold.
- **Tier transition.** When `activeLod` increases (zoom in), do we keep
  the lower-tier content drawn alongside the new tier (additive), or
  swap (replace)? Both are valid; "additive composition" makes the
  Phase 1 per-shape filter and Phase 2 per-file selection compatible.
  Lean additive — `flower_petals.vxs` adds petals on top of the
  silhouette; the silhouette stays loaded.
- **Entity layout.** The prefab spawns a parent entity with one child
  per loaded `.vxs` tier? Or a single entity with merged content? Lean
  parent-with-children — it matches the existing `Relation::CHILD_OF`
  pattern (`engine/entity/include/irreden/entity/ir_entity_types.hpp:39`)
  and lets each tier be hidden/shown independently as the tier changes.

These decisions don't need to be locked until Phase 2 starts. They're
listed here so the next pass has a starting point.

---

## Phase 3 — cross-tier interpolation (deferred)

The hard pop at tier transitions is jarring. Phase 3 would blend.

Two strategies are worth considering when the time comes:

- **Alpha cross-fade.** Render both tiers for a short range around the
  threshold (e.g. zoom 3.5× to 4.5× renders both LOD_1 and LOD_2 with
  alpha ramps). Requires the trixel pipeline to support partial alpha
  on shape rasterization. The existing `SHAPE_FLAG_XRAY_OCCLUDED` path
  (gizmos) proves the renderer can already do alpha blending per shape.
- **Per-shape ramp.** Each `ShapeRecord` carries `lodMin` and `lodMax`
  fields; the shape's alpha ramps from 0 at `lodMin - 0.5` to 1 at
  `lodMin` (and similarly fades out at `lodMax`). Continuous detail
  appearance instead of cross-fade. Requires `activeLod` to be a
  continuous float rather than a discrete enum.

Both are runtime-side changes. Neither requires a format extension —
`lodMax` would land via the per-record `kShapeRecordVersion` bump
(Rule #3), not a chunk addition.

---

## Rigs and LOD

The `.rig` format carries joint hierarchies, bind points, and (later)
animation tracks. **It does not get LOD tiers.**

Reasoning:

- A flower's bones are the same at every zoom level. The petals attach
  at the same joint regardless of whether they're rendered as a blob or
  a detailed mesh.
- Animation drives joint motion, not joint **existence**. The number of
  joints stays constant across LOD tiers.
- The reasonable LOD optimization for rigs is "skip the IK solve when
  the entity is at LOD_4" — a runtime decision about what work to do,
  not a file-format question about what to store. That can live in the
  IK system as a check against `C_ActiveLodLevel` when the IK epic
  ships.

If we ever discover a case where some joints should be culled at low
LOD (extremely complex characters with hundreds of joints, where the
skeletal hierarchy itself dominates frame time), the per-joint additive
versioning (Rule #3) lets us add a `lodMin` field to `JointRecord`
without a format break. Same escape hatch as `.vxs`.

---

## How this interacts with other systems

- **Subdivision-count scaling** (`render_manager.cpp:240-253`). Existing
  per-zoom behavior: subdivision passes per voxel scale with
  `max(zoom.x, zoom.y)` in `SubdivisionMode::FULL`. This is orthogonal
  to LOD — it gives finer-grained voxel rasterization within the
  already-chosen tier. Keep as-is; LOD does not replace it.
- **Gizmo screen-space sizing** (T-164). Already scales gizmo
  `C_ShapeDescriptor::params_` inversely with zoom to keep gizmos at
  constant pixel size. Gizmos opt out of LOD by setting
  `lodMin_ = LOD_0` (always visible) — they're UI affordances, not
  artistic content.
- **Hover / picking** (T-153, T-165). The entity-id texture readback
  used for picking sees whatever shapes the renderer drew. If a shape
  is culled by the LOD filter, it's also un-pickable, which is
  arguably correct (the user can't click on what they can't see).
  Verify this assumption holds for the editor before shipping Phase 2.
- **Singleton-component infrastructure** (T-162). The `C_ActiveLodLevel`
  singleton uses the same pattern as `C_LayoutState` (T-174).

---

## What to file when Phase 2 starts

When T-173 (`.prefab.lua`) is ready to absorb the LOD addition, file a
follow-up ticket covering:

1. DSL syntax for the `lod = { ... }` table.
2. Lazy-vs-eager `.vxs` loading decision.
3. Additive-vs-replace tier composition decision.
4. Parent-with-children entity layout.
5. Sample `.prefab.lua` for the flower test case from this doc.
6. Render-verify shots covering tier-transition correctness.

Until then, Phase 1 (#708) is the only LOD work in flight.
