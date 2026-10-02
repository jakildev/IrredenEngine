# LOD strategy — artist-driven detail tiers, prefab-manifest composition

**Status:** Phase 1 (#708, #1467, #3966) and Phase 2 (#3975) shipped.
Phase 3 (cross-tier interpolation) is deferred to its own design pass.

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
│      [LOD_0] = "flower_stamen.vxs",     -- only at highest zoom  │
│      [LOD_2] = "flower_petals.vxs",     -- medium+ zoom          │
│      [LOD_4] = "flower_silhouette.vxs", -- always visible        │
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
`docs/design/entity-editor-epic.md` §"Save format extensibility rules":

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
`computeLodLevel(float zoomLevel)` and
`shouldSkipAtLod(entityLodMin, currentLod)`. `C_ShapeDescriptor` already
has a `lodLevel_` field that ships to the GPU. None of this needs a
format change to become real — it just needs the system that reads
camera zoom and writes a singleton plus the filter in
`SHAPES_TO_TRIXEL`. That's Phase 1.

---

## Phase 1 — runtime selector (in-flight, #708)

Scope: turn the existing scaffolding into a working pipeline where a
single entity can carry shapes with different `lodMin_` thresholds,
and the renderer skips ones whose threshold tier is **finer than**
(smaller index than) the current zoom-derived LOD tier — i.e. shapes
that wanted more detail than the camera is providing.

Concrete pieces:

- **`LodLevel` enum.** Already in `lod_utils.hpp`. Five tiers
  (LOD_0 = highest detail at high zoom, LOD_4 = lowest at low zoom).
  The index goes **lower as detail goes up**, so `activeLod` decreases
  as the camera zooms in. The runtime filter below reads in this
  direction.
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
- **Per-shape `lodMin_` field.** Rename
  `C_ShapeDescriptor::lodLevel_` from `std::uint32_t` to `LodLevel` and
  treat it as `lodMin_` — "the minimum-detail LOD tier at which this
  shape is still rendered." Because `LodLevel` indexes go down as
  detail goes up, `lodMin_` is the **smallest index** the shape is
  visible at. A shape with `lodMin_ = LOD_0` renders only when
  `activeLod == LOD_0` (highest zoom). A shape with `lodMin_ = LOD_4`
  (the default — coarsest tier) is always visible because every
  `activeLod` satisfies `activeLod <= LOD_4`.
- **CPU-side filter in `SHAPES_TO_TRIXEL`.** At `beginTick`, read the
  singleton. In the per-entity tick, skip shapes whose `lodMin_` is
  **strictly below** the active level (i.e. render only when
  `activeLod <= lodMin_`). Don't push the culled shapes into the GPU
  bucket. No shader change. The existing
  `lod_utils.hpp::shouldSkipAtLod(entityLodMin, currentLod)` stub
  (with its `+2` grace window) is from a different filter shape and
  gets **replaced** by this strict comparison — Phase 1 either updates
  the helper in place or deletes it; the doc's intent is the strict
  form, not the `+2` window.

Verified by a render-verify shot list at zoom 1× / 2× / 4× / 8× of a
test entity with shapes at multiple `lodMin_` thresholds.

### Phase 1b — exclusive (swap, not stack) LOD via a band (#1467)

The single-sided `lodMin_` filter above is **additive**: every co-located
variant whose `lodMin_` admits the active tier renders, so at high zoom all
tiers draw at once. When the variants share a position (the natural authoring
for "this entity, at different detail levels") that means they stack — and
identical-or-near SDFs at the same depth z-fight, which read as the glitch
reported in #1467 ("multiple ones layered over each other … glitchy").

The fix gives each shape an inclusive LOD **band** `[lodMax_ .. lodMin_]`:

- `lodMin_` — coarsest tier (largest index) the shape draws at (existing).
- `lodMax_` — finest tier (smallest index) the shape draws at (**new field**).

`shouldSkipAtLod` draws iff `lodMax_ <= activeLod <= lodMin_`. The defaults
(`lodMin_ = LOD_4`, `lodMax_ = LOD_0`) span the whole tier range, so an
unmarked shape is never culled — **byte-identical to the pre-band filter**;
no existing content changes. Additive composition is still expressible
(overlapping bands), so the Phase 2 lean toward additive `.vxs` loading is
unaffected.

Authoring co-located variants with **disjoint** bands yields exclusive LOD:
exactly one variant renders per zoom, so they *swap* in place rather than
stack. The conventions that give the behavior #1467 describes:

- The coarsest variant keeps `lodMin_ = LOD_4` so it persists at min zoom.
- The finest variant keeps `lodMax_ = LOD_0` so it persists past its
  threshold (zoom 32×/64× clamp to `LOD_0`).
- Interior variants tile the remaining tiers with no gaps or overlaps.

This is the **discrete** form of the per-shape `lodMin`/`lodMax` ramp model
sketched for Phase 3 below (hard pop at the band edge instead of an alpha
ramp). Phase 3 can later make `activeLod` continuous and turn the same two
fields into a cross-fade without a data-model change. The `shape_debug`
demo's LOD fixture (cube → cone → sphere swap + a single-LOD control) is the
reference; render-verify covers it at zoom 1×–16×.

### Phase 1c — the tier surface for creations and DENSE sets (#3966)

A creation runs a *structural* LOD policy — which child parts of a composite
entity exist at a tier — on top of the zoom-derived tier. Three pieces make
that possible without the creation re-deriving anything:

- **The tier is readable from Lua.** `IRRender.getActiveLodTier()` returns the
  `C_ActiveLodLevel` singleton as an integer (0 finest .. 4 coarsest, mirrored
  as `IRRender.LodLevel.LOD_n`), bound in the shared render glue. It is a
  function rather than a component column because the tier is a singleton: a
  Lua system declaring it would iterate a one-row archetype to read one value.
  A CODEGEN system reads it the same way — `IRRender.getActiveLodTier()` is a
  whitelisted intrinsic lowered to `IRRender::getActiveLodTier()` in
  `lod_utils.hpp`, the function the EVAL binding calls.
- **An entity can pin its tier.** `C_LodTierOverride { tier_ }` makes the
  entity resolve to that tier at every zoom; removing it returns the entity to
  the camera's tier. Every consumer resolves through
  `IRRender::resolveEntityLod(active, override)`, fed by an
  `IRPrefab::Lod::TierSnapshot` captured once per tick, so a pin means the same
  thing to the shape filter, the DENSE gate, and any later consumer (a part
  spawner, a portrait canvas). A pin is a tier, not a bias: the policies that
  want this want "draw this entity at full detail", not "one tier finer".
- **DENSE voxel sets carry the band.** `C_VoxelSetNew` gains `lodMin_` /
  `lodMax_` with the same defaults and semantics as `C_ShapeDescriptor`.
  `GATE_VOXEL_SETS_BY_LOD` (UPDATE, after `LOD_UPDATE`, before
  `UPDATE_VOXEL_SET_CHILDREN`) holds each set to its band through the set's
  render gate: an out-of-band set has its pool active mask cleared and is
  skipped by the per-frame update arms, exactly like a set hidden by
  `visible_`. The band is per set, so nothing new is uploaded and no shader
  changes. The gate never touches the pool allocation — the span, colors and
  authored alpha survive — so a swap is free and reversible.

The LOD gate is a second flag (`lodCulled_`) beside `visible_`, not a second
writer of it: fog's whole-body reveal owns `visible_`, and two owners of one
flag would overwrite each other's verdict. A set draws only while
`renders()` — both gates open.

Zoom snaps to powers of two, so tier changes are discrete and the engine adds
no hysteresis; a creation's policy owns any debounce. `computeLodLevel`'s
thresholds are unchanged. `lodVoxelScale` is removed: it had no callers, and
sub-world voxel pitch is ruled out.

`C_VoxelSetNew` persists its authored `lodMin_` / `lodMax_` band in snapshot
format version 3, so a reloaded set retains its inclusive LOD range.
`C_LodTierOverride` stays save-opted-out because it is a policy output that
the policy reapplies, and `lodCulled_` stays transient because the LOD gate
recomputes it.

The `shape_debug --lod-dense-swap` fixture (two co-located DENSE sets on
disjoint bands) is the reference; render-verify covers it at zoom 2× / 8×, and
`LodTierLua` covers the read, the pin and the gate headlessly.

What Phase 1 explicitly does **not** do:

- Multi-tier `.vxs` composition. A Phase 1 entity carries all its shapes
  in a single `C_ShapeDescriptor` set; LOD is per-shape or per-set, not
  per-file.
- Rig LOD. See "Rigs and LOD" below — likely never.
- Cross-tier blending. Hard pop at threshold for Phase 1.

---

## Phase 2 — prefab-manifest composition (#3975)

A composite entity is a prefab root plus **parts**. Schema v2
(`prefab_version = 2`) adds a `parts` list; v1 manifests load unchanged (a
v1 `parts` key is ignored with a warning). The root keeps its own `voxel_ref`
(the flower's stem), drawn at every tier.

```lua
-- assets/prefabs/flower.prefab.lua (abridged)
local L = IRRender.LodLevel
return {
    prefab_version = 2,
    voxel_ref = "assets/prefabs/flower_stem.vxs",
    parts = {
        { id = "silhouette", voxel_ref = "assets/prefabs/flower_silhouette.vxs",
          transform = { translation = { 0, 0, -7 } },
          lod = { fine = L.LOD_3, coarse = L.LOD_4 } },
        { id = "petals", voxel_ref = "assets/prefabs/flower_petals.vxs",
          transform = { translation = { 0, 0, -7 } },
          lod = { fine = L.LOD_0, coarse = L.LOD_2 } },
        { id = "stamen",
          shape = { type = IRShape.SPHERE, params = { 1.5, 1.5, 1.5, 0 },
                    color = { r = 250, g = 210, b = 60 } },
          transform = { translation = { 0, 0, -8 } },
          lod = { fine = L.LOD_0, coarse = L.LOD_0 } },
    },
}
```

A part takes:

| Field | Meaning |
|---|---|
| `id` | Required, unique within the manifest. |
| `voxel_ref` **or** `shape` | At most one. `voxel_ref` attaches like the root's (SHAPES records as children of the part, DENSE data on the part); `shape = { type = IRShape.*, params, color, flags }` is one `C_ShapeDescriptor` on the part. Neither = a bare node. |
| `transform` | `{ translation, rotation (quat), scale }`, relative to the root. |
| `rotation_mode` / `canvas_size` | As on the root; a canvas-owning mode allocates the part's own canvas. |
| `lod = { fine, coarse }` | Inclusive band, indexed the engine way: `fine` is the finest tier (smallest index), `coarse` the coarsest. Defaults `LOD_0` / `LOD_4`. `{ fine = LOD_0, coarse = LOD_2 }` means "zoom 4× and up". |
| `resident` | `true` keeps the part alive (hidden) outside its band instead of destroying it. |
| `components` | The root's `components = {}` form, applied to the part. |

The original sketch's root-level `lod = { [LOD_n] = ... }` table is
**rejected**: the band lives on the part, so there is one way to say it, and
it reads the same as a shape's or a DENSE set's band.

### Locked decisions

- **Layout: parent with children.** Each live part is a `CHILD_OF` child of
  the root, so the root's cascade destroy (`destroyTree`) removes it with its
  own children. A part is rebuilt from the same manifest slot every time it
  re-enters its band.
- **Composition: additive by band.** Overlapping bands stack (the petals stay
  while the stamen appears); disjoint bands replace (the silhouette leaves when
  the petals arrive). The Phase 1 shape filter composes the same way.
- **Load: lazy.** A part's `.vxs` is loaded on the part's first spawn and kept
  by the root's parsed manifest; every live root of one prefab shares the copy.
  The file's existence is checked at `Prefab.spawn`, so a bad path fails the
  spawn rather than a later zoom.
- **Existence is engine-owned.** `Prefab.spawn` creates the parts whose band
  holds the root's resolved tier (`resolveEntityLod`, so a declared
  `C_LodTierOverride` pin decides it). `PREFAB_LOD_PARTS` (UPDATE, after
  `LOD_UPDATE`) then keeps the live set equal to the in-band set: a tier must
  hold for `kPrefabPartsTierSettleTicks` before the parts follow, at most
  `kPrefabPartSpawnBudgetPerTick` parts spawn per tick across all roots (the
  rest on later ticks), and a part leaving its band is destroyed. A creation
  that wants its own policy leaves the system out; parts then stay as spawned.
- **Resident parts** stay alive out of band. Their content (the part and any
  SHAPES children) takes the part's band and a `C_LodTierOverride` mirroring
  the root's settled tier, so the Phase 1 shape filter and
  `GATE_VOXEL_SETS_BY_LOD` hide them at exactly the tier the other parts swap.

Not persisted: `C_PrefabParts` is save-opted-out (it holds live ids and the
manifest's Lua tables); a creation re-spawns the prefab after a load.

The `shape_debug --load-prefab assets/prefabs/flower.prefab.lua` pass is the
reference: render-verify captures it at zoom 1× / 4× / 16× (stem +
silhouette, stem + petals, stem + petals + stamen). `PrefabParts` covers the
schema, the tier-driven spawn / destroy, the settle window, the spawn budget,
resident parts and v1 compatibility headlessly.

---

## Phase 3 — cross-tier interpolation (deferred)

The hard pop at tier transitions is jarring. Phase 3 would blend.

Two strategies are worth considering when the time comes:

- **Alpha cross-fade.** Render both tiers for a short range around the
  threshold (e.g. zoom 3.5× to 4.5× renders both LOD_1 and LOD_2 with
  alpha ramps). Requires the trixel pipeline to support partial alpha
  on shape rasterization. The existing `SHAPE_FLAG_XRAY_OCCLUDED` path
  (gizmos) proves the renderer can already do alpha blending per shape.
- **Per-shape ramp.** Each `ShapeRecord` carries a `lodMin`
  (coarsest-tier appearance bound, largest index) and a `lodMax`
  (finest-tier disappearance bound, smallest index). With `activeLod`
  as a continuous float where smaller = higher detail, the shape's
  alpha ramps from 0 at `lodMin + 0.5` (below detail floor) up to 1
  at `lodMin`, holds across the visible interval, and ramps back
  toward 0 as `activeLod` falls past `lodMax`. Continuous detail
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

- **Secondary viewports** (`docs/design/secondary-viewport.md`). A viewport
  rasters its subject at its own camera's zoom-derived density
  (`getVoxelRenderEffectiveSubdivisionsForZoom`), so a portrait at zoom 16 is
  the finest tier while the world draws the same entity coarse. Its subjects'
  DENSE bands are filtered at the viewport's own zoom-derived tier;
  `C_LodTierOverride` pins the world's tier only and is not read there.
- **Subdivision-count scaling** (`render_manager.cpp:240-253`). Existing
  per-zoom behavior: subdivision passes per voxel scale with
  `max(zoom.x, zoom.y)` in `SubdivisionMode::FULL`. This is orthogonal
  to LOD — it gives finer-grained voxel rasterization within the
  already-chosen tier. Keep as-is; LOD does not replace it.
- **Gizmo screen-space sizing** (T-164). Already scales gizmo
  `C_ShapeDescriptor::params_` inversely with zoom to keep gizmos at
  constant pixel size. Gizmos opt out of LOD by setting
  `lodMin_ = LOD_4` (always visible — every `activeLod` value
  satisfies `activeLod <= LOD_4`) — they're UI affordances, not
  artistic content.
- **Hover / picking** (T-153, T-165). The entity-id texture readback
  used for picking sees whatever shapes the renderer drew. If a shape
  is culled by the LOD filter, it's also un-pickable, which is
  arguably correct (the user can't click on what they can't see).
  The same holds for a Phase 2 part outside its band: destroyed or, when
  resident, culled, so it is un-pickable too.
- **Singleton-component infrastructure** (T-162). The `C_ActiveLodLevel`
  singleton uses the same pattern as `C_LayoutState` (T-174).

---
