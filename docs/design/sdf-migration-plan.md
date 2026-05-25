# SHAPES authoring deprecation migration plan (D3)

**Status:** complete
**Issue:** [#961](https://github.com/jakildev/IrredenEngine/issues/961)
**Parent epic:** [#937 — SDF runtime restriction (effects-only)](https://github.com/jakildev/IrredenEngine/issues/937)
**Companion docs:**
- [`sdf-runtime-audit.md`](sdf-runtime-audit.md) — D1 audit (usage enumeration)
- [`entity-editor-epic.md`](entity-editor-epic.md) §"Architectural decisions (locked)" — D2 decision
- [`engine/prefabs/irreden/voxel/CLAUDE.md`](../../engine/prefabs/irreden/voxel/CLAUDE.md) — D2 restriction note for `Prefab.spawn`

---

## D2 decision summary

The D2 deliverable ([#960](https://github.com/jakildev/IrredenEngine/issues/960)) adopted
the D1 audit's tri-mode recommendation. The restriction shape is recorded in
[`entity-editor-epic.md`](entity-editor-epic.md) §"Architectural decisions (locked)":

- **SHAPES / HYBRID writer path deprecated.** The editor never exposes SDF
  authoring; the parametric-shape bake (Epic A A3) always emits DENSE.
  `saveShapeGroup` and the `C_ShapeDescriptor`-shaped `saveVoxelSet` wrapper in
  `engine/prefabs/irreden/asset/voxel_set_io.hpp` are deprecated for new callers.
  No new `.vxs` assets should be authored in SHAPES or HYBRID mode.
- **SDF runtime retained** for three roles: editor chrome (gizmos, axis
  indicators, selection highlight, debug culling minimap), forward-looking
  effects (auras, fields, soft glows, gameplay shaders), and the
  `BUILD_LIGHT_OCCLUSION_GRID` blocker pass (entities carrying `C_LightBlocker`).
- **`VoxelSetMode::SHAPES` / `HYBRID` remain legacy-readable.** Existing `.vxs`
  assets continue to `Prefab.spawn` as `C_ShapeDescriptor` children without
  migration.

---

## Per-site disposition

Each `C_ShapeDescriptor` site from the D1 audit is classified below.
"Retained" means no code change is required; the site falls within one of the
three permitted roles.

### Primary-entity demos

| Site | Disposition | Rationale |
|---|---|---|
| `creations/demos/shape_debug/` | **Retained — regression harness** | Canonical SHAPES_TO_TRIXEL smoke target. The demo exists to verify SDF rasterization correctness via side-by-side voxel-vs-SDF comparison; it is required as long as any in-engine consumer (editor chrome, effects) uses the SDF render path. |
| `creations/demos/perf_grid/` `--render-mode shape` | **Retained — benchmark** | Benchmarks the same SHAPES_TO_TRIXEL dispatch path that editor chrome uses. Removing the mode loses the only dedicated throughput reference for the SDF rasterization pipeline. |
| `creations/demos/modifier_demo/` | **Retained — canonical effects demo** | Re-scoped as the canonical "SDF for effects" demonstration. The whole-scene SDF composition and per-frame `params_` animation via `ModDemo_Consume` illustrate the intended pattern for future effects-only `C_ShapeDescriptor` callers. |
| `creations/demos/z_yaw_rotation/` (both variants) | **Retained — parity harness** | SDF + voxel-pool ring tests yaw-rotation and GPU entity-id readback parity between pipelines. Both pipelines must appear in the same demo to catch one-sided regressions. |
| `creations/demos/lighting/` (SDF parity row, `main_sdf_cascade.cpp`) | **Retained — lighting parity + effects** | The SDF row in `lighting_demo_scene.hpp` verifies `C_ShapeDescriptor` entities compose correctly with the lighting pipeline. `main_sdf_cascade.cpp` is the cascaded SDF blocker acceptance scene. |

### Lighting-blocker sites

| Site | Disposition | Rationale |
|---|---|---|
| `creations/demos/lighting/main_sdf_blocker.cpp` wall | **Retained — blocker demo** | The only in-tree site exercising the `C_LightBlocker + C_ShapeDescriptor` blocker path. `BUILD_LIGHT_OCCLUSION_GRID`'s blocker pass is retained per D2; this demo is its acceptance scene (T-117 / [#364](https://github.com/jakildev/IrredenEngine/issues/364)). |

### Editor chrome

| Site | Disposition | Rationale |
|---|---|---|
| Voxel editor reference grid, axis bars, origin marker | **Retained — editor chrome** | Visual reference for the editor viewport. Migrating to voxel pools would require per-frame voxelization at varying screen-space sizes — epic-scale work with visible quality loss at zoom. Editor chrome is one of the three permitted roles. |
| Voxel editor selection highlight | **Retained — editor chrome** | `C_VoxelSelectionHighlight` halo; toggled per-click by `VOXEL_PICKING`. Editor chrome role. |
| Gizmo handles (translate, rotate, scale, joint, bind-point, IK) | **Retained — editor chrome** | `GIZMO_SCREEN_SPACE_SIZE` rewrites `params_` per tick; voxel-pool handles would lose sub-voxel precision at zoom. Editor chrome is a permitted role. |
| Debug culling minimap | **Retained — editor chrome** | Diagnostic-only; reads `<C_ShapeDescriptor, C_PositionGlobal3D>` to render culled AABB overlays. Editor chrome role. |

### Game-side

| Site | Disposition | Notes |
|---|---|---|
| `unit_movement` Lua creation (game repo) | **Deferred — game team** | Game-side unit entities rendered via `C_ShapeDescriptor`. Migrating to voxel-pool units is correct under the tri-mode restriction but is game-team scope, not engine scope. Exact file paths omitted per cross-repo isolation policy. |

---

## Editor scaffold audit

**Finding: no SDF-primitive authoring UI exists in the engine editor.**

The voxel editor (`creations/editors/voxel_editor/`) contains a **shape bake
tool** (`applyFillSDF` / `EditorBake` system, added in T-286) that uses CPU-side
SDF evaluation to fill voxels in a `C_VoxelSetNew` and commit them as DENSE
geometry. This is the correct design (always emits DENSE; SDF math is a fill
tool, not a runtime entity author) and is **not** affected by the D2 restriction.

The ghost entity used as a visual preview of the bake region
(`g_fillTool.ghostEntity_`, a `C_ShapeDescriptor`) is editor chrome — retained
per D2 (editor chrome role).

No inspector panel for creating or editing runtime `C_ShapeDescriptor` entities
as game-world assets was found in the engine editor. D3 acceptance criterion
(2) is satisfied without any code removal.

---

## Writer-side deprecation notices

The SHAPES / HYBRID write paths carry `@deprecated` doc comments as of this PR.
Reader-side functions are not deprecated — legacy `.vxs` SHAPES and HYBRID
assets continue to load via backward-compat.

| Function | Location | Note |
|---|---|---|
| `saveShapeGroup(path, span<ShapeRecord>)` | `engine/asset/include/irreden/asset/voxel_set_format.hpp` | SHAPES write path; no new callers |
| `saveVoxelSet(path, span<ShapeRecord>, DenseVoxelSet)` | `engine/asset/include/irreden/asset/voxel_set_format.hpp` | HYBRID write path; no new callers |
| `IRAsset::saveVoxelSet(path, span<C_ShapeDescriptor>, ...)` | `engine/prefabs/irreden/asset/voxel_set_io.hpp` | `C_ShapeDescriptor`-shaped SHAPES write adapter; no new callers |

Tests in `test/asset/voxel_set_shapes_test.cpp` and `voxel_set_hybrid_test.cpp`
are retained as regression coverage for the reader path. They call the deprecated
write functions to construct in-memory fixtures — this is expected and correct.

---

## Open action items

The only open item from the D1 audit is the game-side `unit_movement` migration.
That is game-team scope; this plan documents the engine-side decision only.

No engine code changes are required for the SHAPES_TO_TRIXEL system,
`C_ShapeDescriptor`, or the reader path. If a future decision eliminates the SDF
render path entirely (after replacing editor chrome with an alternative), that
would be a separate epic downstream of Epic D.
