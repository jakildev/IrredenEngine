# Prefab render surface — rationale

Why the rules in [`engine/prefabs/irreden/render/CLAUDE.md`](../../engine/prefabs/irreden/render/CLAUDE.md)
are shaped the way they are. The rules themselves live there; this document
holds only rationale that is still true of the code and is not owned by
another design doc. Feature-level designs are their own documents:
[`voxel-occlusion-culling.md`](voxel-occlusion-culling.md),
[`per-axis-trixel-canvas-rotation.md`](per-axis-trixel-canvas-rotation.md),
[`sprites.md`](sprites.md), [`lod-strategy.md`](lod-strategy.md),
[`detached-canvas-depth-default.md`](detached-canvas-depth-default.md),
[`detached-canvas-density-compensation.md`](detached-canvas-density-compensation.md),
[`gpu-stage-timing-cost-model.md`](gpu-stage-timing-cost-model.md),
[`depth-unification-1884-investigation.md`](depth-unification-1884-investigation.md).

## Canvas components and the Hi-Z chain

`C_TriangleCanvasTextures` centralizes the color / distance / entity-id
format triple in its `detail::makeCanvas*Texture` factories so every other
canvas component allocates through them; a second copy of the formats would
drift. The Hi-Z max-depth mip chain over the distance texture is produced
every frame by `COMPUTE_DISTANCE_HIZ` whether or not the chunk-occlusion
pre-pass consumes it, so enabling the cull
(`IRRender::setVoxelOcclusionCullEnabled`) changes no texture lifetime. The
cull's lag source is last frame's chain, which is stale across a camera cut;
`VOXEL_TO_TRIXEL_STAGE_1::beginTick` therefore keeps every chunk for one
frame after a discontinuous camera move. The cull is bit-identical on and off
because a fully occluded voxel writes nothing either way.

`renderedSubdivisions_` on the canvas records the subdivision the canvas
actually rastered at, which is below the global effective subdivision when
the `subdivisionCap` fires. The detached composite needs it to rescale
model-frame depth into shared framebuffer units (see "World-placed composite
depth" below); a canvas that rastered no voxels this frame stamps 0.

## Per-axis canvases are camera-only

`C_PerAxisTrixelCanvases` exists to smooth the main world canvas between
cardinal yaws. It is bundled on every voxel-pool canvas default-constructed
and inert; the once-per-frame gate in `VOXEL_TO_TRIXEL_STAGE_1::beginTick`
(`syncAllocationToCameraYaw`) allocates the main canvas's textures only while
the camera sits at a non-cardinal residual yaw and frees them at the
cardinal, so a static scene stays byte-identical. A rotating detached entity
never uses per-axis canvases: its SO(3) goes through the re-voxelize path,
which rotates and rounds the entity's voxel cells into a private pool that
rasterizes through the ordinary single-canvas cardinal path.

The per-axis store writes at base (world-unit) resolution — one cell per
voxel face regardless of the active subdivision — and packs the fractional
sub-cell offset into the distance encoding. That is what lets the forward
scatter shift each face quad onto its true plane without overflowing a
base-resolution canvas. The consequence is the obligation
`engine/render/CLAUDE.md` states: every absolute-position reader of the store
(lighting receive, the sun-shadow cast bridge) owes the frac through the
shared `perAxisSubCellFrac` decode; the bit layout lives only in the
`ir_iso_common.{glsl,metal}` encode/decode helpers. The per-axis dispatch
still launches the capped density from
`IRPrefab::PerAxisCanvas::subdivisionDensity()`, but only the z=0 invocation
writes.

## Camera rotation on the SQT quaternion

The camera's yaw and pitch are the ordinary `C_LocalTransform.rotation_`
composed as `qZ(yaw) × qX(pitch)`, with `camera.hpp` exposing both halves.
The GRID rasterizer reads only the Z-yaw through the cardinal/residual split
(the iso-depth-axis invariant in `engine/render/CLAUDE.md`), while DETACHED
canvases consume the full quaternion through `PROPAGATE_CANVAS_ROTATION`.
Pitch is clamped to ±(π/2 − ε) to avoid gimbal lock.

## GPU transform slots: one buffer, two allocators

`UPDATE_VOXEL_POSITIONS_GPU` computes `world = modelToWorld × localPos` only
for voxel sets that opt into transform indirection
(`C_VoxelSetNew::gpuTransformSlot_ != kVoxelTransformStatic`). Voxels keep the
sentinel by default so the CPU-direct `UPDATE_VOXEL_SET_CHILDREN` flush still
owns their slots and a scene with no GPU-transformed sets pays no dispatch.
The slot rides the local-position `.w` lane because Metal has no free buffer
index past 30. Per-frame upload is one `mat4` per dynamic set, not O(voxels).

`UPDATE_JOINT_MATRICES` writes each skeleton's skin matrices
(`jointWorld × bindInverse`) into a contiguous block of the same binding-18
`EntityTransformBuffer`, so a skinned voxel's `.w` set to `slotBase + bone_id`
skins through the existing prepass with no new shader. The 4096-slot budget
is partitioned — dynamic voxel-set slots grow up from 0 (capped at
`kJointTransformSlotBase`), joint blocks are carved down from
`kMaxGpuVoxelTransforms` — so the prepass's contiguous `[0, maxSlotUsed_]`
re-upload can never clobber a joint slot. The association is same-entity: the
skeleton and the skinned voxel set live on one rig-root entity, and a bone id
outside the joint list falls back to the entity's own slot (rigid follow).
The rest pose and target slot per joint are gathered once in `beginTick`, so
the tick iterates `<C_Joint, C_WorldTransform>` densely.

## One tick for compact + stage 1 + stage 2

`VOXEL_TO_TRIXEL_STAGE_1` runs the compact, stage-1 and stage-2 dispatches
per canvas inside one tick because the three share the voxel SSBOs: a
stage-2 system that runs after every canvas's stage 1 reads SSBOs the next
canvas has already overwritten, so multi-canvas scenes require all three
dispatches to complete per canvas before the next canvas's compact.

## LOD bands

`LodLevel` indexes go down as detail goes up (`LOD_0` = highest detail at
zoom ≥ 16, `LOD_4` = the silhouette tier, always drawn). A shape's inclusive
band `[lodMax_ .. lodMin_]` defaults to the whole range so an unmarked shape
renders at every zoom. Co-located variants with disjoint bands render
exclusively — one per zoom, swapping in place — because additive co-location
z-fights. The filter is CPU-side in `SHAPES_TO_TRIXEL::beginTick`, ahead of
the yaw and cull-bounds math, so a culled shape costs no GPU staging.

## Gizmo interaction

`GIZMO_DRAG` captures, at press, the anchor's local position, the cursor's
world point projected onto an iso-depth plane through the anchor, and the
cursor's canvas-iso angle around the anchor. The plane is fixed at press time
because a plane that followed the anchor's changing iso depth would make the
gizmo run away from the cursor mid-drag. Kind-specific math: TRANSLATE_ARROW
projects the cursor world delta onto the handle axis; ROTATE_RING tracks the
canvas-iso angle change with Shift snapping to π/12; SCALE_STICK projects onto
the axis against `kScaleStickRefWorld`; SCALE_CENTER reads diagonal cursor
screen displacement against `kScaleCenterRefPixels`.

`spawnHandle` records the parent passed into the builder as the handle's
`anchorEntity_`. The grouped builders (`createTranslateGizmo` /
`createRotateGizmo` / `createScaleGizmo`) pass their own group root, so a drag
moves the gizmo as a unit; the `*ForAnchor` builders pass the anchored entity
deliberately, so a drag mutates that entity's own `C_LocalTransform` — the
per-joint placement and FK-posing handles. A marker built with `kNullEntity`
stays hoverable but no-ops on drag.

`SHAPE_FLAG_XRAY_OCCLUDED` is a generic `ShapeFlags` bit: the shader blends a
flagged shape at reduced alpha wherever it loses the depth contest and knows
nothing about gizmos. Any prefab that needs a see-through overlay opts in the
same way without touching `engine/render/`.

## Help overlay and settings menu

Both adopt the registry-driven command list: `CommandManager` records each
`IRCommand::createCommand<NAME>` display name and description from
`kCommandInfo`, so a creation that calls `registerStandardKeyboardCommands()`
gets the camera bundle described with no per-demo strings, and its own keys
appear as soon as they register through a named path. The overlay's header
names no key: the toggle registers through the same path and appears as its
own row, so the overlay documents whichever key the creation chose.

Neither `systems()` nor `inputSystems()` / `renderSystems()` auto-prepends the
system it depends on (`TEXT_TO_TRIXEL`, `INPUT_KEY_MOUSE`). The probes answer
existence soundly, but the lists are built while the adopter is still
assembling its pipeline, so "absent" and "about to be spliced in" are
indistinguishable and "present" says nothing about order. A wrong guess
double-creates named GPU resources (overlay) or double-fires every click
(menu).

The overlay's visibility is the `C_HelpOverlayState` singleton, not a
`RenderManager` field (`.claude/rules/cpp-globals.md`; `m_guiVisible` is a
pre-existing deviation, not a precedent). Hidden, the system iterates one
singleton row and returns early — no string build, no rect, no glyphs — and
text rebuilds only when `getRegistrationGeneration()` changes.

The menu owns no entities until it opens, so the widget systems iterate empty
archetypes and existing captures stay byte-identical. Its widgets are created
in `endTick` and released with `IREntity::destroyEntity`, which marks rather
than destroys; because INPUT runs before `destroyMarkedEntities()`, which runs
before RENDER, a menu opened this frame draws this frame and one closed this
frame never does. Settings are snapshotted at open, which is why a setting
registered after the menu is open appears only at the next open. The
screen-pixel accessors exist because the menu centers itself, so a scripted
shot fills its MOVE target at run time via `IRRender::guiTrixelToScreenPx`.
A QUIT click ends the run before the harness's post-settle capture frame, so
a creation asserting it evaluates at the moment the close is observed.

## Cursor-latched rotation pivot

`resolveFocusWorld` returns the world point under the cursor at its true
surface depth through `IRPrefab::Picking::castVoxelRay`; CPU picking never
applied the raster's anchor shift, so it already agrees with the raster at
every cardinal and a picking-side compensation would be wrong. A background
click falls back to `IRRender::getDefaultRotationPivotFocus()`, so the mode
degrades to the default pivot.

The indicator is spawned lazily on the first cursor-pivot drag so a creation
that never uses the mode keeps its entity-id layout and captures; it is then
re-hidden, never destroyed. It sets `canvasEntity_ = kNullEntity` so
`SHAPES_TO_TRIXEL` resolves the canvas at RENDER time: `C_ShapeDescriptor`'s
constructor snapshot of the active canvas is right for a scene-setup spawn,
but an entity built mid-frame from a per-frame system would snapshot
whichever canvas the last pass left active.

A marker spawned from a hook that runs after `SHAPES_TO_TRIXEL` never
renders — the pass is done for the frame and the new entity reaches no
archetype it reads. What decides this is the spawner's order relative to
`SHAPES_TO_TRIXEL`, not which pipeline it lives in: `CAMERA_MOUSE_ROTATE`
(spliced by `standardControlSystems()`) is a RENDER-pipeline system ahead of
it, while the capture-frame hooks are past it, which is why the
`--pivot-verify cursor-latch` harness spawns its marker at scene setup.
`CAMERA_MOUSE_ROTATE` carries the `IRSystem::MainThread` tag because the lazy
spawn is an eager `createEntity` followed by `getComponent`, both
main-thread-only; a multi-system pipeline group dispatches every member onto
a worker, and the tag turns that wiring mistake into a boot-time FATAL in
`validateAllPipelineGroups`.

## Widget framework

Every `WIDGET_RENDER_*` system's `create()` calls `ensureThemeSingleton()` so
the `C_WidgetTheme` singleton exists before frame 1: `singleton<T>()`'s first
call must be main-thread, and a non-main-thread `createEntity` defers the row
insertion until `flushStructuralChanges`, so the touch belongs at
registration time, not in `beginTick`.

`WIDGET_INPUT` writes only the generic state machine (`C_WidgetState`); the
per-kind `WIDGET_APPLY_*` followers exist so the input system never calls
`getComponent` on kind-specific data — each follower's archetype already
includes its data component. `WIDGET_RENDER_*` is split per kind for the same
reason. The list and dropdown followers reuse `C_WidgetState::dragValue_`
(otherwise written only by the slider) to carry the hovered row index to the
renderer; −1 means outside any row.

An expanded dropdown grows its `C_HitBox2DGui::size_` so later frames' hover
routing still reaches the item strip, and biases `C_Widget::zOrder_` by
`kWidgetDropdownOpenZBias`. The bias is the input-side counterpart to
registering `WIDGET_RENDER_DROPDOWN` last: without it the strip paints over
its neighbours but loses the equal-`zOrder_` hover tie-break to them, so an
item row covering another widget is unclickable. Both revert on close. The
bias does not order two simultaneously expanded dropdowns against each other
(nothing closes one dropdown when another opens); that needs an open-order
rank, not a flat bias. `IRGui.makePanel` clamps rather than inheriting the
`C_Widget` constructor's z-order assert because script data should not throw
and the assert compiles out under `IR_RELEASE`.

The expanded strip's geometry (`rowHeight` / `expandedHeight` /
`itemCenterOffsetY` / `itemAtOffsetY` on `C_WidgetDropdown`) has one owner
because a re-derived formula drifts silently — the only symptom is a scripted
click landing on the wrong row.

Distance bands in `trixel_rect.hpp` govern only the framebuffer composite,
not the per-canvas write order; within one widget tick the system draws
background → border → label in painter order so overlaps overwrite
consistently. Widget renderers run after `TEXT_TO_TRIXEL` so its canvas clear
has already happened, which also means widget pixels overpaint unrelated GUI
overlay text where they overlap.

`WIDGET_LUA_DISPATCH` keys `onClick` handlers per widget `EntityId` on its own
state and runs SERIAL because LuaJIT is single-threaded; it sits immediately
after `WIDGET_INPUT` so `fireAction_` is fresh.

`GuiTest` evaluation lives in the prefab layer because it needs widget and
picking access `engine/video` lacks; the scripted-shot harness drives it
through the type-erased `IRVideo::GuiTestConfig::onAssertFrame_` callback
with a caller-owned `LatchState` that carries the one-frame `fireAction_`
pulse across the settle window. `picksIsoColumn` asserts only that the ray
hit a voxel on the target's iso column, because which voxel along the column
is front-most is scene state, not a mapping fact.

## Rotation modes

`GRID` re-rasterizes the entity's rotated voxels into the shared world pool
every frame, so they write world `trixelDistances` at their true iso depth
and depth-sort against, cast onto, and receive from world geometry; the cost
is per-frame world re-rasterization and round-to-cell aliasing. `DETACHED` /
`DETACHED_REVOXELIZE` render on a private camera-yaw-zeroed canvas that
`ENTITY_CANVAS_TO_FRAMEBUFFER` composites at the iso screen position. Plain
`DETACHED` (octahedral-snap forward scatter) depth-sorts only, because its
per-face deform has no faithful world-position recovery for lighting; it is
on the retirement path. The world-placed default and the `screenLocked_`
overlay opt-out are [`detached-canvas-depth-default.md`](detached-canvas-depth-default.md).

A detached re-voxelize canvas participates in world lighting only when it
carries `C_CanvasAOTexture` and `C_TrixelCanvasRenderBehavior`, which put it
in the `COMPUTE_VOXEL_AO` and `LIGHTING_TO_TRIXEL` archetypes.
`IRPrefab::EntityCanvas::createWithVoxelPool` — the detached-canvas
chokepoint, distinct from the `kVoxelPoolCanvas` builder that also builds the
main canvas — attaches both unless `screenLocked`, because a spawn site that
forgot the pair composited raw albedo regardless of every other flag.

### World-placed composite depth

The shared framebuffer depth runs at `worldDepth × effSub × 8` (the
`encodeDepthWithFace` flip-and-face shift over the subdivision-scaled depth),
and a detached canvas rasters its pool at its own possibly-capped
subdivision. The composite therefore scales the entity's iso depth
(`pos3DtoDistance(roundVec3HalfUp(translation))`) by `effSub × 8` into
`distanceOffset_`, and rescales the canvas's model-frame `rawDist` by
`effSub / renderedSubdivisions_` (carried in
`effectiveSubdivisionsForHover_.y`, applied in `f_trixel_to_framebuffer`).
An offset left in raw world units is under-scaled by `effSub × 8` and sinks
world-placed solids behind the floor as zoom grows. `screenLocked_` keeps the
offset at 0 and the scale at 1. `detached_world_depth_test` pins the
equivalence with the same GRID cell.

Receive: `PROPAGATE_CANVAS_ROTATION` propagates the entity's world cell
origin onto `C_CanvasLocalRotation` and the frame data publishes it as
`detachedWorldReceive_`; `LIGHTING_TO_TRIXEL` recovers each detached voxel's
world position and samples the shared sun-shadow map and light volume there.
Only the re-voxelize frame-data branch publishes the flag, so forward-scatter
canvases never world-receive. Cast: `BAKE_SUN_SHADOW_MAP` scatters every
world-placed re-voxelize canvas's model-frame distances plus its world cell
origin into one shared main-canvas-layout scratch and bakes that through the
unchanged cardinal recovery — one extra bake regardless of caster count.

## GPU stage timing

`VOXEL_TO_TRIXEL_STAGE_1` is untagged from the per-system observer; its tick
brackets each dispatch group with a `GpuSubStageScope`, so the `canvasClear` /
`voxelCompact` / `voxelStage1` / `voxelStage2` GPU rows are attributed
individually and the old whole-tick number is their sum. A sub-scope reuses
the timestamp machinery at the attachment slot the observer would have used,
which is free only because the system is untagged; the CPU `voxelStage1` row
stays the whole per-canvas tick through an `IR_PROFILE_SCOPE`. Sub-scopes are
single-canvas-exact (last sample wins on multi-canvas) and do not cover the
rotating-only per-axis dispatch.

The per-system observer is what feeds `cpuFrameHistogram`, so removing a
system's observer tag zeroes its CPU overlay row unless the tick gains a
replacement `IR_PROFILE_SCOPE("<stageName>")` in the same change. An
`IR_PROFILE_FUNCTION` already in the tick feeds easy_profiler, not the
histogram the overlay reads; the build stays green and the row silently
zeroes.

## Depth priority tiers

A world-placed canvas with `C_EntityCanvas::depthPriority_ != 0` composites
into a reserved near depth band (`kDepthForegroundCeil`) instead of its world
iso depth, so a floating showcase never clips behind the floor. World content
is clamped out of the band, a no-op for in-budget content. The split is N
disjoint tiers (`kDepthForegroundTierCount`: world / entity-fg /
per-trixel-override); `f_trixel_to_framebuffer` resolves
`tier = max(perEntityTier, perTrixelTier)` per fragment. The per-trixel tier
is authored per voxel (`C_VoxelSetNew::changeVoxelPriority`, the low 2 bits
of `C_Voxel::reserved_`) and rides the top 2 bits of the 64-bit entity id in
the `triangleEntityIds` channel; the single decode chokepoint
(`decodeEntityId` / `decodePriority` in `ir_iso_common.{glsl,metal}`,
`IRRender::decodeCarrierEntityId/Priority` in C++) masks the carrier off
everywhere an id is read, so a non-zero priority cannot corrupt a picked id.
A per-trixel override arbitrates only across canvases at finalization: two
voxel sets on one canvas resolve depth at the canvas raster (`atomicMin`),
upstream of the partition, so an occluded same-canvas voxel's priority never
reaches the composite. A rotating `DETACHED_REVOXELIZE` unit carries the tier
through `c_revoxelize_detached`'s `reserved` lane.

## GPU sentinel seeding

A repeating-single-byte sentinel is seeded with
`IRRender::device()->fillBuffer(buffer, bytes, byteValue)` (GL
`glClearNamedBufferSubData` / Metal blit `fillBuffer` under one primitive).
A multi-byte sentinel such as `kTrixelDistanceMaxDistance` (`0x0000FFFF`) has
no driver-side clear, so it is seeded by an already-owned self-resetting
kernel or a clear dispatch rather than a resource-sized CPU staging vector
plus `subData` on a cold path.
