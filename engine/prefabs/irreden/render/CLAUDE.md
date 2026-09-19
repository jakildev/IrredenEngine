# engine/prefabs/irreden/render/ — canvases, framebuffers, cameras, text

The ECS surface the trixel pipeline reads and writes. Engine-side state and
device drivers: [`engine/render/CLAUDE.md`](../../../render/CLAUDE.md) (§"The
pipeline, one frame" is the diagram). Prefab-wide rules: [`engine/prefabs/CLAUDE.md`](../../CLAUDE.md).
Rationale: [`docs/design/prefab-render-surface.md`](../../../../docs/design/prefab-render-surface.md).

## Validators

- `python3 scripts/gui-verify.py IRShapeDebug -- --gui-test` — headless GUI
  assertion table (help overlay, settings menu, picking).
- `python3 scripts/pivot-verify.py --blocks cursor-latch` — cursor pivot;
  `IRShapeDebug --pivot-verify cursor-latch --cursor-pivot-indicator` captures the marker.
- `scripts/depth-tier-verify.py --only orbitswap --tier 1` — foreground depth
  band (`canvas_stress --only orbitswap` / `--only interpenetrate`).

## GPU resource ownership

- `C_TriangleCanvasTextures` (color / distance / entity-id + Hi-Z chain),
  `C_TrixelCanvasFramebuffer`, and `C_SpriteSheet`'s atlas are created in the ctor
  and freed only in `onDestroy()`. Never stack-construct one, never
  `destroyResource` by hand, never destroy a canvas entity mid-frame while a
  system still holds a reference.
- New canvas textures allocate through `detail::makeCanvas*Texture` in
  `component_triangle_canvas_textures.hpp`; the format triple has one owner. A
  canvas with no explicit parent renders to the engine's main framebuffer.
- `C_PerAxisTrixelCanvases` rides every voxel-pool canvas inert;
  `VOXEL_TO_TRIXEL_STAGE_1::beginTick` allocates it at non-cardinal camera yaw
  and frees it at the cardinal. Camera-only: detached entities rotate through
  the re-voxelize path. The store is base-resolution with the sub-cell frac in
  the distance; every absolute-position reader decodes it via
  `perAxisSubCellFrac` (the frac obligation in `engine/render/CLAUDE.md`).
- Seed a single-byte GPU sentinel with `IRRender::device()->fillBuffer(...)`;
  a multi-byte one (`kTrixelDistanceMaxDistance`) reuses an owned
  self-resetting kernel or a clear dispatch — never a resource-sized CPU
  staging vector + `subData`. Live deviation to migrate when next touched:
  `system_resolve_per_axis_screen_depth.hpp` seeds its scratch with `subData`.

## Ordering contracts

| System(s) | Must sit |
|---|---|
| `LOD_UPDATE` | UPDATE, before `PROPAGATE_TRANSFORM` |
| `FOG_REVEAL_EVAL` | after `PROPAGATE_TRANSFORM`, before `UPDATE_VOXEL_SET_CHILDREN` |
| `UPDATE_JOINT_MATRICES` | after `PROPAGATE_TRANSFORM`, before `UPDATE_VOXEL_POSITIONS_GPU`; a creation with skeletons registers the prepass too |
| `UPDATE_VOXEL_POSITIONS_GPU` | before `VOXEL_TO_TRIXEL_STAGE_1` |
| `VOXEL_PICKING` | RENDER, after the camera systems, before `VOXEL_TO_TRIXEL_STAGE_1` |
| `GIZMO_HOVER` → `GIZMO_DRAG` | INPUT, after `INPUT_KEY_MOUSE` |
| `CAMERA_MOUSE_ROTATE` and the other camera controls | RENDER, before `SHAPES_TO_TRIXEL`, in singleton groups (`MainThread`) |
| `SHAPES_TO_TRIXEL` | RENDER, after `VOXEL_TO_TRIXEL_STAGE_1` when an entity canvas mixes voxels and shapes; the shape rasters in the owner's model frame at the canvas's rendered density ([contract](../../../../docs/design/mixed-private-canvas-lifecycle.md)) |
| `HITBOX_MOUSE_TEST_GUI` → `WIDGET_INPUT` → `WIDGET_APPLY_*` | INPUT; `WIDGET_LUA_DISPATCH` immediately after `WIDGET_INPUT` |
| `TEXT_TO_TRIXEL` → `LAYOUT_COMPUTE` → `WIDGET_RENDER_*` | RENDER, before `TRIXEL_TO_FRAMEBUFFER`; `WIDGET_RENDER_DROPDOWN` last among the renderers |
| `HelpOverlay::systems()`, `SettingsMenu::renderSystems()` / `inputSystems()` | RENDER after `TEXT_TO_TRIXEL`, before the composite / INPUT after `INPUT_KEY_MOUSE` |
| `SPRITE_TO_SCREEN` | after the main canvas's `FRAMEBUFFER_TO_SCREEN` |

`VOXEL_TO_TRIXEL_STAGE_1` runs compact + stage 1 + stage 2 per canvas in one
tick; do not split them. `TEXT_TO_TRIXEL` clears the GUI canvas in
`beginTick` and caps glyphs at `kMaxGlyphCommands` (`gui_text_batch.hpp`);
widget renderers overpaint overlay text, so keep widgets clear of the
perf-stats overlay region (top-right by default).

## Component contracts

- Camera rotation is `C_LocalTransform.rotation_` = `qZ(yaw) × qX(pitch)`
  (`camera.hpp`), pitch clamped to ±(π/2 − ε); GRID reads only the Z-yaw
  (`engine/render/CLAUDE.md` §"Iso-depth-axis invariant"), DETACHED canvases
  the full quaternion via `PROPAGATE_CANVAS_ROTATION`.
- Hitbox / hover systems cache the camera at `beginTick`; a mid-frame camera
  move is seen next frame. Per-canvas behaviour (zoom tracking, hover,
  subdivisions) is a `C_TrixelCanvasRenderBehavior` flag, never a branch.
- `C_EntityCanvas` owns `screenLocked_` (overlay opt-out) and `depthPriority_`
  (foreground band; meaningful only when `!screenLocked_`), read by
  `ENTITY_CANVAS_TO_FRAMEBUFFER` with no foreign `getComponent`. Per-voxel
  tiers: `C_VoxelSetNew::changeVoxelPriority`; every id read goes through
  `IRRender::decodeCarrierEntityId`. A per-trixel override arbitrates only
  across canvases — use separate detached units.
- `C_ActiveLodLevel` is the singleton `LOD_UPDATE` writes; `SHAPES_TO_TRIXEL`
  draws a `C_ShapeDescriptor` only when the active tier is inside its inclusive
  `[lodMax_ .. lodMin_]` band (`lod_utils.hpp`); disjoint bands on co-located
  variants swap, overlapping bands stack. Shapes only.
- Sprites bypass the trixel pipeline ([`docs/design/sprites.md`](../../../../docs/design/sprites.md));
  `C_Sprite::screenPixelSmooth_` (no game-pixel snap) is for the avatar or a camera-locked entity only.
- `C_FogRevealed` opts a grid-canvas voxel entity into one reveal verdict at
  its ground anchor; `C_FogRevealSettings` owns hysteresis and stagger;
  `FOG_REVEAL_EVAL` is active-canvas-only. The voxel reserved bit exempts a
  governed body from the compact fog reject and the stage-1 own-column z drop,
  but `FOG_TO_TRIXEL`'s per-pixel height clip has no entity channel and can
  cut a governed body at a hard ceiling (tracked: #3156).
- GPU transforms: a voxel set opts in with `C_VoxelSetNew::gpuTransformSlot_
  != kVoxelTransformStatic` (the default is CPU-direct, dispatch-free). Joints
  share binding 18 — set slots grow up from 0, joint blocks are carved down from
  `kMaxGpuVoxelTransforms`; skeleton and skinned set share one rig-root entity;
  re-painting `C_Voxel::bone_id_` re-stamps via `IRPrefab::JointTransform::seedVoxelBoneSlots`.

## Exposing system public API from the prefab layer

- **Pattern A — direct component access:** the caller holds the entity id and
  reads or writes the component.
- **Pattern B — prefab-scoped namespace:** a header in this directory exposes
  `IRPrefab::<Feature>::` free functions that own the entity lookup
  (`fog_of_war.hpp`, `cursor_pivot.hpp`, `help_overlay.hpp`); name it anything
  that does not collide with `IRRender::`.
- Never add a feature setter/getter to `IRRender::` or a field to
  `RenderManager` (`engine/render/CLAUDE.md` §"What belongs in engine/render/
  vs engine/prefabs/irreden/render/"). Feature visibility is a singleton
  component (`C_HelpOverlayState`), per `.claude/rules/cpp-globals.md`.
  Registration self-wires (`IRSystem::findSystem`); never ask the creation to
  call a `setFooSystem(id)` after `create()` (§Deprecated).

## Editor interaction

- `IRPrefab::Gizmo::` builders spawn SDF handles (`C_LocalTransform +
  C_WorldTransform + C_ShapeDescriptor + C_GizmoHandle + C_Name`) under a group
  root; `GIZMO_SCREEN_SPACE_SIZE` (UPDATE) holds constant pixel size;
  `SHAPE_FLAG_XRAY_OCCLUDED` is generic for any see-through overlay.
- The parent passed to `spawnHandle` is the drag anchor: grouped builders move
  the gizmo as a unit, `create*GizmoForAnchor(anchor)` mutates the anchor's own
  `C_LocalTransform`, `kNullEntity` makes a marker hoverable but drag-inert.
  `GIZMO_DRAG` locks its iso-depth plane at press.
- Cursor pivot: `IRPrefab::CursorPivot::resolveFocusWorld(exclude)` picks at
  true surface depth and falls back to `IRRender::getDefaultRotationPivotFocus()`;
  pass the indicator as `exclude`; never add a picking-side anchor compensation.
  The indicator spawns lazily on the first drag, is re-hidden not destroyed,
  and sets `canvasEntity_ = kNullEntity`. A marker spawned from a hook after
  `SHAPES_TO_TRIXEL` never renders that frame.

## Help overlay and settings menu

- Adopt: splice `IRPrefab::HelpOverlay::systems()` / `SettingsMenu::renderSystems()`
  after `TEXT_TO_TRIXEL`, `SettingsMenu::inputSystems()` after `INPUT_KEY_MOUSE`,
  then `registerToggleCommand()` (F1 / Escape). Nothing auto-prepends or
  probes for its dependency. Content is `CommandManager`'s registry; the
  overlay names no key. Never default-visible in a reference-gated creation.
- Escape opens the menu, so an adopting demo drops `IRCommand::CLOSE_WINDOW`
  via `registerStandardKeyboardCommands({.omit_ = {...}})` and gets a QUIT
  button. Register settings (`IRPrefab::Settings::register{Bool,Enum,Float}`)
  during init; the menu snapshots them at open.
- Headless readers: `HelpOverlay::builtText()` / `lastGlyphCommandCount()`,
  `SettingsMenu::liveRowCount()` / `*ScreenPx(...)`. A QUIT assertion
  evaluates when the close is observed, not on the capture frame.
  `systemOrNull()` reports absent as `IRSystem::kNullSystemId`, never
  `kNullEntity` (`engine/system/CLAUDE.md` §`findSystem`;
  `test/render/prefab_system_probe_test.cpp`).

## Widget framework

- `IRPrefab::Widget::make<Kind>` builds one entity: `C_Widget` +
  `C_GuiPosition` + `C_Widget<Kind>`, plus `C_WidgetState` + `C_HitBox2DGui`
  for interactive kinds. Theme is the `C_WidgetTheme` singleton
  (`widget_theme.hpp::defaultTheme()`): mutate it once at init, before
  building widgets. Hover publication is opt-in: `makeGuiHoverState()` creates
  the `C_GuiHoverState` singleton, `hoveredWidget()` reads it.
- Dropdown strip geometry (`C_WidgetDropdown::rowHeight` / `expandedHeight` /
  `itemCenterOffsetY` / `itemAtOffsetY`) has one owner; authored `zOrder_` stays
  below `kWidgetDropdownOpenZBias`; two open dropdowns are unordered.
- Radio exclusion (`makeRadio(..., groupId, value)`) runs in
  `WIDGET_APPLY_RADIO::endTick`. Text input edits only while `focused_` (owned
  by `WIDGET_INPUT`). `C_WidgetScroll` is track + thumb only — the owner
  positions content from `scrollPos_`.
- `IRPrefab::GuiTest::` (`gui_test_assertions.hpp`): `hovers` / `clickFires` /
  `sliderValue` / `checkbox` / `picksVoxel` / `picksIsoColumn` / `predicate`,
  one `GUI-ASSERT …` line each plus one `GUI-ASSERT-COVERAGE …` per shot.
  Reference wiring: `creations/editors/voxel_editor/main.cpp`. Lua `onClick`:
  `engine/script/CLAUDE.md` §"Engine service bindings".

## Rotation modes

- `GRID` for world-integrated rotation with exact cell aliasing or shadows from
  thin detail; `DETACHED_REVOXELIZE` for cheap smooth SO(3) that still sorts,
  casts and receives; any `DETACHED` mode with `screenLocked_ = true` for a
  HUD / billboard overlay
  ([`docs/design/detached-canvas-depth-default.md`](../../../../docs/design/detached-canvas-depth-default.md)).
- `IRPrefab::EntityCanvas::createWithVoxelPool` is the detached-canvas
  chokepoint; it attaches `C_CanvasAOTexture` + `C_TrixelCanvasRenderBehavior`
  unless `screenLocked` — without them the canvas composites raw albedo.
- Composite depth is `pos3DtoDistance(roundVec3HalfUp(translation)) × effSub × 8`
  with `rawDist` rescaled by `effSub / renderedSubdivisions_`; on-screen size
  and gather density depend on camera zoom × world extent only
  ([`docs/design/detached-canvas-density-compensation.md`](../../../../docs/design/detached-canvas-density-compensation.md)).
- The sun-shadow bake reads main-canvas-layout depth sources only; a foreign
  model-frame canvas texture is never a bake input.

## GPU stage timing

- Read [`docs/design/gpu-stage-timing-cost-model.md`](../../../../docs/design/gpu-stage-timing-cost-model.md)
  before quoting the overlay. `VOXEL_TO_TRIXEL_STAGE_1` is untagged; its GPU
  rows come from `GpuSubStageScope` (`gpu_substage_timing.hpp`).
- Untagging a system from the observer is a paired edit: add
  `IR_PROFILE_SCOPE("<stageName>")` in the same change or its CPU row reads 0
  forever (`IR_PROFILE_FUNCTION` does not feed the histogram).

## Deprecated

| Surface | Replacement |
|---|---|
| `IRPrefab::JointTransform::setSystem(SystemId)` | none — `system()` resolves via `IRSystem::findSystem(UPDATE_JOINT_MATRICES)` |
| `IRPrefab::VoxelTransform::setAllocatorSystem(SystemId)` | none — `allocator()` resolves via `IRSystem::findSystem(UPDATE_VOXEL_POSITIONS_GPU)` |

No-ops kept for out-of-tree creations (engine API removal rule); the pattern
is banned by `.claude/rules/cpp-ecs.md` §"System-owned invariants".
