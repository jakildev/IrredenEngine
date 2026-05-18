# SDF runtime audit (D1)

**Status:** complete (no code changes)
**Issue:** [#945](https://github.com/jakildev/IrredenEngine/issues/945)
**Parent epic:** [#937 — SDF runtime restriction (effects-only)](https://github.com/jakildev/IrredenEngine/issues/937)
**Companion docs:**
- [`entity-editor-epic.md`](entity-editor-epic.md) §"Architectural decisions (locked)" — D2 will amend this with the restriction shape.
- [`engine/prefabs/irreden/render/CLAUDE.md`](../../engine/prefabs/irreden/render/CLAUDE.md) — system inventory.
- [`engine/prefabs/irreden/voxel/CLAUDE.md`](../../engine/prefabs/irreden/voxel/CLAUDE.md) — `C_ShapeDescriptor` vs `C_VoxelSetNew` choice.

## Purpose

Inform D2 (the decision deliverable that locks the SDF runtime restriction shape) by enumerating every site that uses `C_ShapeDescriptor` and classifying each by role. Output is a categorized list, a count, and a concrete feasibility recommendation for the planned **"SDF for effects only"** direction.

No code is changed in this task. D2 will reference this audit when proposing the language amendment to `entity-editor-epic.md` and any deprecations.

## Method

`grep -rn "C_ShapeDescriptor"` across the engine + creations tree (excluding `.claude/worktrees/` clones). Each surviving site is classified by reading the surrounding code:

- **Primary entity** — the entity's visible geometry comes from the SDF, with no voxel-pool counterpart. The SHAPES_TO_TRIXEL pass is the only producer of pixels for this entity.
- **Lighting blocker** — entity carries `C_LightBlocker(blocksLOS_=true)` and the SDF is used to populate the camera-anchored blocker bitfield in `BUILD_LIGHT_OCCLUSION_GRID`. May or may not also be visible.
- **Editor chrome / UI affordance** — geometry exists to support the editor or developer tooling, not for gameplay rendering (gizmo handles, selection highlights, axis indicators, debug minimap entries).
- **Asset-loader synthesis** — code emits `C_ShapeDescriptor` from a deserialized `.vxs` `SHAPES` / `HYBRID` shape record. The shape is then a primary entity of the spawned prefab. This is the format-level enabler for the SHAPES authoring path.
- **System read** — an engine system *reads* `C_ShapeDescriptor` (LOD filter, picking ray, debug minimap, gizmo color modulation, light-blocker rasterizer). These are consumers; classifying them clarifies what hard-deletes if the SHAPES path is removed.

The `C_ShapeDescriptor` shape parameter set (`shapeType_`, `params_`, `flags_`, `lodMin_`, `canvasEntity_`) is documented in [`engine/prefabs/irreden/voxel/components/component_shape_descriptor.hpp`](../../engine/prefabs/irreden/voxel/components/component_shape_descriptor.hpp).

## Site enumeration

### Asset / loader paths (synthesize C_ShapeDescriptor)

| Site | File:line | Role | Notes |
|---|---|---|---|
| `Prefab::spawn` shape children | `engine/script/src/prefab_api.cpp:227-243` | Asset-loader synthesis | Iterates `loadedVoxels->shapeRecords_` for `SHAPES` / `HYBRID` modes, emits one CHILD_OF entity per record with `C_Position3D + C_ShapeDescriptor`. This is the runtime entry point for the `.vxs` SHAPES authoring path. |
| `.vxs` SHAPES / HYBRID mode | `engine/asset/include/irreden/asset/voxel_set_format.hpp:141-160` | Asset format | `VoxelSetMode::{SHAPES, HYBRID}` and the `SREF` shape-record table. Removing the SHAPES runtime path leaves these consuming code on the loader side; the loader can still parse legacy `.vxs` SHAPES assets but the spawn path would need a migration to voxelize on load (see §"Migration cost" below). |

### Creation / demo entry points (primary entity)

| Site | File:line | Role | Notes |
|---|---|---|---|
| `shape_debug` parity row | `creations/demos/shape_debug/main.cpp:342-364, 380-505` | Primary entity | Standalone test fixture: each shape case spawns a voxel-pool entity *and* an SDF entity side-by-side for cross-pipeline visual comparison. The SDF side is the **whole reason this demo exists** — it's the canonical SHAPES_TO_TRIXEL regression harness (see [`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) "Verifying render changes"). LOD fixtures (lines 475-481) add SDF-only spheres to exercise `lodMin_` filtering. |
| `perf_grid` shape mode | `creations/demos/perf_grid/main.cpp:296-300` | Primary entity | Optional render mode (`--render-mode shape`) used to benchmark SHAPES_TO_TRIXEL against the voxel pool at scale. Counterpart to the voxel-pool mode. |
| `modifier_demo` scene | `creations/demos/modifier_demo/main.cpp:321-340, 583, 591, 621, 643, 651, 673` | Primary entity | The whole visible scene (cubes, floor slab, pillars, bumps) is built from SDF boxes — no voxel-pool entities. The system `ModDemo_Consume` matches `<C_Position3D, C_ResolvedFields, C_Modifiers, C_ShapeDescriptor>` and mutates the shape's `params_` to animate "stretchiness" per frame. |
| `z_yaw_rotation` cardinal ring (static) | `creations/demos/z_yaw_rotation/main_static.cpp:118-138` | Primary entity | SDF sphere + box at two ring positions; the other two ring positions are voxel-pool entities. The demo's purpose is to verify yaw-rotation parity between the two pipelines. |
| `z_yaw_rotation` cardinal ring (mouse) | `creations/demos/z_yaw_rotation/main_mouse.cpp:163-184` | Primary entity (+ picking target) | Same ring as the static variant; SDF entities double as picking targets to confirm GPU entity-id readback works for SDF canvases. |
| `lighting` standard scene SDF row | `creations/demos/lighting/common/lighting_demo_scene.hpp:177-183, 240-254` | Primary entity (+ asymmetric blocker opt-out on floor) | `createSdfShape(...)` is the shared helper used by every `creations/demos/lighting/main_*.cpp` variant for the SDF half of the parity row (BOX/SPHERE/CONE/TORUS at `kSdfRowY = 12`). The floor (`createSdfShape(...)` at line 248) gets `C_LightBlocker{false, false, 0.0f}` — visible primary entity that explicitly **opts out** of LOS, shadow, and opacity. |
| `lighting/main_sdf_cascade.cpp` | `creations/demos/lighting/main_sdf_cascade.cpp` (uses common helper) | Primary entity | Acceptance scene for cascaded SDF blockers / shadows; uses `lighting_demo_scene.hpp` helpers. |
| `unit_movement` game demo | `creations/game/irreden/demos/unit_movement/{lua_bindings.cpp:316, lua_component_pack.hpp:31, scripts/units.lua:111}` | Primary entity (game-side) | Lua-driven game demo: each unit entity is built with `C_ShapeDescriptor.new(variant.shape_type, variant.params, col)` from one of several shape variants. Units are the visible primary entities. The C++ side exposes `createEntityBatchShapeUnits` to Lua via `lua_bindings.cpp`. |

### Lighting-blocker callsites (`C_ShapeDescriptor + C_LightBlocker(blocksLOS_=true)`)

| Site | File:line | Role | Notes |
|---|---|---|---|
| `lighting/main_sdf_blocker` wall | `creations/demos/lighting/main_sdf_blocker.cpp:42-48` | Lighting blocker (also visible) | The wall has visible color (`Color{210, 100, 110, 255}`) **and** `C_LightBlocker{true, false, 1.0f}` (`blocksLOS_=true`, `castsShadow_=false`). Acceptance scene for T-117 / [#364](https://github.com/jakildev/IrredenEngine/issues/364): the SDF's blocker bitfield occludes the warm point light, producing a visible warm/cold floor transition. |

These are the **only** in-tree call sites where `C_ShapeDescriptor` is consumed by `BUILD_LIGHT_OCCLUSION_GRID`'s blocker pass (the system's per-frame archetype query is `<C_ShapeDescriptor, C_LightBlocker, C_PositionGlobal3D>`; see `system_build_light_occlusion_grid.hpp:196-213`). Every other `C_ShapeDescriptor` entity in the tree lacks `C_LightBlocker` and so does not enter the blocker bitfield.

### Editor chrome / UI affordance

| Site | File:line | Role | Notes |
|---|---|---|---|
| Voxel editor reference grid | `creations/editors/voxel_editor/main.cpp:251-289` | Editor chrome (visual) | Ground plane, X-axis red bar, Y-axis green bar, origin marker. Pure visual reference for the editor viewport — no gameplay role. |
| Voxel editor selection highlight | `creations/editors/voxel_editor/main.cpp:319-330` | Editor chrome (UI) | The `highlight` entity carries `C_Position3D + C_ShapeDescriptor + C_VoxelSelection + C_VoxelSelectionHighlight`. `system_voxel_picking` toggles its `SHAPE_FLAG_VISIBLE` on left-click hits. The 1.4× sizing makes it read as a halo around the picked voxel. |
| Gizmo handles | `engine/prefabs/irreden/render/gizmo.hpp:7-9, 59, 110-113` + builders for translate/rotate/scale/joint/bind-point/IK | Editor chrome (UI) | Each gizmo handle is `C_PositionGlobal3D + C_PositionOffset3D + C_Position3D + C_ShapeDescriptor + C_GizmoHandle + C_Name`. Geometry from `IRMath::SDF::ShapeType` (`CYLINDER`, `CONE`, `TORUS`, `SPHERE`, `BOX`). Hover and screen-space sizing systems mutate `C_ShapeDescriptor::color_` and `params_` each frame (see `system_gizmo_hover.hpp:30-48`, `system_gizmo_screen_space_size.hpp:60-77`). |
| Debug culling minimap | `engine/prefabs/irreden/render/systems/system_debug_culling_minimap.hpp:154-262` | Editor / debug chrome | Reads `<C_ShapeDescriptor, C_PositionGlobal3D>` to render an iso minimap of culled vs. visible shape AABBs. Diagnostic-only. |

### Engine system reads (consumers)

| System | File:line | Role | Notes |
|---|---|---|---|
| `SHAPES_TO_TRIXEL` | `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp:38-336` | Render system (primary entity) | The render-side core of the SHAPES authoring path. Per-tick LOD filter + cull + shape-tile populate. Removing the SHAPES primary-entity path retires this system. The shape-tile dispatch infrastructure could be re-purposed for effects-only use (smaller `kMaxShapeDescriptors`, narrower archetype query). |
| `BUILD_LIGHT_OCCLUSION_GRID` blocker pass | `engine/prefabs/irreden/render/systems/system_build_light_occlusion_grid.hpp:120-213` | Render system (lighting blocker) | `rasterizeShapeBlocker` + `rasterizeAllBlockers` walk `<C_ShapeDescriptor, C_LightBlocker, C_PositionGlobal3D>` and populate the camera-anchored blocker bitfield. Unaffected by removing the primary-entity path as long as `C_ShapeDescriptor` remains the blocker shape representation. |
| `VOXEL_PICKING` (highlight entity) | `engine/prefabs/irreden/render/systems/system_voxel_picking.hpp:36-74` | Editor chrome read | Iterates the highlight entity (`C_VoxelSelection + C_VoxelSelectionHighlight + C_Position3D + C_PositionGlobal3D + C_ShapeDescriptor`). Toggles the highlight's `SHAPE_FLAG_VISIBLE` and rewrites `pos_` on each click. |
| `GIZMO_HOVER`, `GIZMO_SCREEN_SPACE_SIZE` | `engine/prefabs/irreden/render/systems/system_gizmo_hover.hpp`, `system_gizmo_screen_space_size.hpp` | Editor chrome write | Each frame: hover writes brightened tint onto `C_ShapeDescriptor::color_`, screen-space-size writes scaled `params_` (and child handle position). Restores from `C_GizmoHandle::baseColor_` when hover clears. |
| `picking.hpp::gatherVisibleShapes` | `engine/prefabs/irreden/render/picking.hpp:70-100` | Editor support read | Snapshot all visible `C_ShapeDescriptor` rows for the editor's SDF-aware ray-cast picker. |
| `lod_utils.hpp` | `engine/prefabs/irreden/render/lod_utils.hpp:4-8` | Render filter | `SHAPES_TO_TRIXEL` calls `shouldSkipAtLod(shape.lodMin_, activeLod_)` to gate per-shape rasterization on the active LOD tier. Same fate as `SHAPES_TO_TRIXEL`. |

### Wiring / metadata

| Site | File:line | Role |
|---|---|---|
| `C_ShapeDescriptor` definition | `engine/prefabs/irreden/voxel/components/component_shape_descriptor.hpp:31-47` | Component definition |
| Lua binding | `engine/prefabs/irreden/voxel/components/component_shape_descriptor_lua.hpp:1-29` | Lua surface (used by `unit_movement` etc.) |
| `C_CanvasTarget` (WIP, unused) | `engine/prefabs/irreden/render/components/component_canvas_target.hpp:5-14` | Future refactor target — intended to absorb the `canvasEntity_` field that currently sits on `C_ShapeDescriptor`. Orthogonal to the D1/D2 decision. |
| `prefab_api_test.cpp` | `test/script/prefab_api_test.cpp` | Test asserting the SHAPES-mode `.vxs` spawn path. Migration would update or retire these cases. |

## Categorized counts

| Category | Distinct in-tree sites |
|---|---|
| Primary entity (gameplay / demo geometry) | 8 (shape_debug, perf_grid, modifier_demo, z_yaw_rotation × 2, lighting common helper, lighting/main_sdf_cascade, unit_movement) |
| Lighting blocker (visible) | 1 (lighting/main_sdf_blocker wall) |
| Editor chrome / UI affordance | 4 (voxel_editor grid + highlight, gizmo system, debug minimap) |
| Asset-loader synthesis | 1 (`Prefab::spawn` SHAPES/HYBRID branch) |
| Engine system reads (consumers) | 7 (SHAPES_TO_TRIXEL, BUILD_LIGHT_OCCLUSION_GRID blockers, VOXEL_PICKING, GIZMO_HOVER, GIZMO_SCREEN_SPACE_SIZE, picking.hpp, DEBUG_CULLING_MINIMAP) |

The **engine consumes `C_ShapeDescriptor` in five distinct roles** even though only one (primary entity) is the subject of the proposed restriction. A naive "delete the component" path is not viable; the audit's job is to identify what is intrinsic to each role.

## Cross-cutting observations

1. **Editor chrome is the dominant in-engine consumer.** Gizmos, the selection highlight, axis indicators, and the debug minimap all depend on `C_ShapeDescriptor + SHAPES_TO_TRIXEL` rendering. These cannot move to voxel pools without a separate authoring story for editor primitives — voxelizing a translate-arrow handle each frame at varying screen-space sizes (the `GIZMO_SCREEN_SPACE_SIZE` system rewrites `params_` per tick) is mechanically possible but produces visibly worse output than the SDF path at high zoom levels.

2. **Lighting blockers depend on `C_ShapeDescriptor` for the SDF, not the SHAPES_TO_TRIXEL render pass.** `BUILD_LIGHT_OCCLUSION_GRID` reads `shape.shapeType_` and `shape.params_` to evaluate the SDF against integer cells in the blocker bitfield. The blocker pass continues to work even if `SHAPES_TO_TRIXEL` is retired — provided `C_ShapeDescriptor` (or an equivalent SDF descriptor) stays around as the shape representation. This is the **strongest argument** for keeping the SDF runtime in some restricted form: the engine still needs an SDF-shaped blocker descriptor regardless of the primary-entity decision.

3. **Effects use is forward-looking, not present.** No current site uses `C_ShapeDescriptor` for the kind of "effects-only" usage the epic gestures at (auras, fields, soft glows, gameplay-specific shaders). The restriction direction is therefore not about *narrowing existing effects use* — it's about *preventing the SHAPES authoring path from expanding* while preserving the infrastructure for future effects.

4. **`shape_debug` is the regression harness for the SDF pipeline itself.** Removing SDF primary-entity rendering would either retire this demo or convert it to a pure-voxel parity harness — which loses its purpose, since the demo's entire value is the side-by-side voxel-vs-SDF comparison. If SHAPES_TO_TRIXEL stays in any form (for editor chrome or effects), `shape_debug` should stay as its visual smoke test.

5. **`modifier_demo` is the most SDF-coupled creation.** It builds its entire scene from SDF shapes and uses a system that animates `params_` per frame. Migrating it to the voxel pool would require either per-frame voxel-set reshape (allocation-heavy in a hot path — see [`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) §"Allocations in hot tick paths") or a fundamentally different approach to the modifier-system demonstration. Likely re-scope: keep the demo on the SDF runtime as the canonical "effects" example.

6. **`unit_movement` (game) is the live game-side consumer.** Game-side units are rendered as SDF shape variants. Migrating to voxel-pool units is feasible and arguably correct under the proposed direction (units = gameplay primary entities), but it's game-team work, not engine work — it should be sequenced after D2 lands and before any engine-side removal.

7. **The `.vxs` SHAPES / HYBRID mode is more entrenched than any single creation.** The asset format spec itself encodes `MODE`-tagged SHAPES files, the loader emits a tested `SREF` shape-record table, `Prefab::spawn` materializes them as `C_ShapeDescriptor` children, and existing game / demo `.vxs` assets in `assets/` may already use the SHAPES mode. The migration cost here is the highest of any site — it touches the on-disk format guarantees in [`entity-editor-epic.md`](entity-editor-epic.md) §"Save format extensibility rules".

## Migration cost summary (informs D2)

| Role | If SDF primary-entity rendering is removed | Effort |
|---|---|---|
| **Asset-loader synthesis** (`Prefab::spawn` SHAPES/HYBRID branch) | Choose: (a) deprecate SHAPES/HYBRID modes in `.vxs` writers, (b) auto-voxelize on load (CPU SDF rasterize → C_VoxelSetNew), or (c) keep current spawn behavior but document SHAPES as legacy-only. (a) breaks existing assets; (b) adds runtime cost on every spawn; (c) is the minimum-friction path. | (c) low; (b) medium; (a) high |
| **`shape_debug` demo** | Retire or convert to SDF-effects-only harness. Currently the canonical render regression smoke target — needs to stay as either an SDF demo or as a pure-voxel parity demo against some other reference. | low (retire) to medium (rework) |
| **`perf_grid` shape mode** | Remove the `--render-mode shape` branch. The voxel-pool benchmark stays as the primary perf reference. | low |
| **`modifier_demo`** | Re-scope as an SDF-effects demo (matches the "effects-only" direction) or rebuild with voxel-pool entities + a different animation hook. SDF-effects re-scope is the natural home. | low (if kept as effects) to high (if rebuilt) |
| **`z_yaw_rotation`** | The SDF half of each ring is the entire reason for the demo's two pipelines. Demo retires or migrates to a single-pipeline yaw harness. | low (retire) to medium (rework) |
| **`lighting` parity row** | The SDF row in `lighting_demo_scene.hpp` is one half of every lighting demo's parity comparison. Demos still need SDF blockers; keeping the SDF row makes the parity test trivially supportable. Recommend keeping. | low (no change required) |
| **`lighting/main_sdf_blocker`** | The wall *is* the demo. If SDF lighting blockers remain (recommended — see Observation 2), this stays. | none if blockers remain |
| **`voxel_editor` chrome** | Gizmos, axis indicators, selection highlight all depend on SHAPES_TO_TRIXEL. Either: (a) keep SDF rendering for editor chrome (effectively "SDF for effects + editor"), or (b) re-author every editor primitive as voxel sets (handles 200+ voxels each, animated per frame by `GIZMO_SCREEN_SPACE_SIZE`). (a) is the only practical path. | (a) low; (b) very high — likely epic-scale work |
| **`unit_movement` (game)** | Game-team migration to voxel-pool units. Feasible but not engine-side work; sequence after D2. | medium (game-team) |

## Recommendation (concrete, for D2)

**Adopt a tri-mode restriction shape** for the SDF runtime, not a binary "SDF stays / SDF goes":

1. **Keep SDF primary-entity rendering enabled** for editor chrome and explicit "effects" entities. The audit shows these are the only intrinsic SDF consumers in-engine, and they cannot move to voxel pools without either visible quality loss (gizmos at zoom) or epic-scale rework (voxelizing every editor primitive).

2. **Deprecate the SHAPES authoring path in `.vxs`** at the writer level — the editor never exposes SDF authoring as the editor epic already states. Keep `VoxelSetMode::SHAPES` / `HYBRID` as **legacy-readable** modes so existing assets don't break, but no new authoring tooling emits them. `Prefab::spawn` continues to materialize SHAPES children as `C_ShapeDescriptor` for those legacy assets. This satisfies the epic's "editor never exposes SDF authoring" rule without rewriting every existing demo or game asset.

3. **Retain `C_ShapeDescriptor` + `BUILD_LIGHT_OCCLUSION_GRID` blocker pass** as the canonical SDF-shaped lighting blocker representation. This is independent of the primary-entity decision and supports the lighting demos' acceptance scenes.

4. **Re-scope demos** along the new boundary:
   - `modifier_demo` → SDF-effects demo (canonical example of the restricted use)
   - `lighting/main_sdf_blocker` → unchanged (lives in the blocker role)
   - `shape_debug` → unchanged as the SDF render-regression harness; the demo's existence is justified by editor-chrome consumers of SHAPES_TO_TRIXEL
   - `perf_grid` shape mode → keep (still benchmarks the same code SHAPES_TO_TRIXEL uses for chrome)
   - `z_yaw_rotation` → unchanged (parity harness for yaw rotation across both pipelines)
5. **File a follow-up** for the `unit_movement` game-side migration once D2 lands. Not in scope for this audit or for D2 itself; tracked under epic D4 (T-189 / T-190 disposition).

**Bottom line:** the planning-stated "effects-only" direction is more usefully framed as **"editor + effects + lighting-blocker"** — three roles, all retained, with the SHAPES authoring writer path being the only thing actually deprecated. The audit found no in-tree primary-entity usage that survives a clean "remove SDF rendering" delete; the proposed restriction is therefore best understood as a *writer-side authoring policy* layered on top of a *retained reader-side SDF runtime*.

## Out of scope (for D1 and D2)

- The `C_CanvasTarget` migration (`engine/prefabs/irreden/render/components/component_canvas_target.hpp` is WIP for moving the `canvasEntity_` field off `C_ShapeDescriptor` and onto a dedicated component). Orthogonal to the SDF restriction decision.
- The `C_VoxelPool::C_VoxelPool` `m_voxelPoolSize3D` initialization gap noted in the PR [#975](https://github.com/jakildev/IrredenEngine/pull/975) review. Separate follow-up.
- T-189 / T-190 (SDF→trixel half-voxel discrepancy investigations). D4 covers their disposition under the new restriction shape.
