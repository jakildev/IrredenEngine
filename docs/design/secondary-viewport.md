# Secondary viewports — a GUI-pinned view through a camera of its own

A creation declares a viewport: a GUI rectangle, a camera (zoom, yaw, focus)
independent of the world camera, and the entities it shows. The engine
renders those entities into the rectangle every frame with the world canvas
untouched. The first consumer is a portrait of the selected entity at its
finest detail while the same entity stands coarse in the world.

Surface: `engine/prefabs/irreden/render/viewport.hpp` (`IRPrefab::Viewport::`),
the `IRRender.createViewport` family (`LuaScript::bindLuaViewport()`), the
`SYNC_VIEWPORT_SUBJECTS` and `VIEWPORT_TO_FRAMEBUFFER` systems.

## Decisions

### D1 — A viewport is a private voxel-pool canvas plus a camera entity

The canvas is the same `kVoxelPoolCanvas` bundle a detached entity canvas
uses, so `VOXEL_TO_TRIXEL_STAGE_1`, `COMPUTE_VOXEL_AO` and
`LIGHTING_TO_TRIXEL` raster and shade it with no new stage. What makes it a
viewport is `C_CanvasCamera` on the canvas: the stages read that camera
(zoom-derived raster density, view-to-world rotation, model-space pan)
instead of the world camera's, and `BUILD_LIGHT_OCCLUSION_GRID` excludes the
canvas because its pool is not world geometry.

The camera entity (`C_ViewportCamera` + `C_ZoomLevel` + yaw in
`C_LocalTransform::rotation_`) is the viewport id. It is what Lua holds and
what the setters address; the canvas is an implementation detail reachable
through `canvasOf`.

Rejected: a detached canvas given a fake camera transform. Its composite is
placed at the owner's iso position and scaled by the world zoom, so pinning it
to a GUI rectangle and giving it another zoom would special-case the
composite for one canvas.

### D2 — Subjects are tagged and resampled, not re-parented

`C_ViewportSubject{ viewport }` on an entity makes `SYNC_VIEWPORT_SUBJECTS`
copy that entity's `C_VoxelSetNew` authored records into the viewport's
pool each frame; the set keeps rendering wherever it already does. Several
tagged entities merge into one centered box at their relative world cells
(lowest entity id is the reference and the id the canvas carries).

The pool is re-derived from the records every frame — no dirty flag, no
notification from edits, retargets or destroyed subjects. A subject's own
span is never touched, and nothing in the viewport pool refers back into it:
bone slots, per-trixel priority tiers and the rotated-emit bit describe the
source span and are dropped; face-occlusion bits are re-derived by the raster.

Rejected: cloning the subject entity — two sources of truth for one thing.

### D3 — The canvas is a window, not a thumbnail

A detached entity canvas caps its raster density so the whole pool fits the
texture. A viewport does the opposite: the canvas is sized to the GUI
rectangle at the camera's zoom (`canvasSizeForRect`), the density follows the
zoom, and content past the edge clips. The focus (`C_ViewportCamera::focus_`,
in voxels from the subject box's center) is brought to the rectangle's center
by a model-space pan (`C_CanvasCamera::panIso_`), floored to a whole texel
with an even sum so the triangle lattice's parity is preserved.

### D4 — Rotation goes through the re-voxelize path

The viewport camera's yaw is published as the canvas's
`C_CanvasLocalRotation::rotation_ = quatInverse(viewToWorld)` with
`reVoxelize_` set, so the pool is resampled into the camera's frame by the
same GPU inverse resample a `DETACHED_REVOXELIZE` entity uses, and rasters
with cardinal frame data. The lighting pass rotates the resampled faces'
normals back to world by the canvas camera's rotation, so the world sun lights
the portrait as it lights the entity.

Consequences: the subject box must be origin-centered (it is, by
construction) and must fit the shared voxel buffers both at rest and in the
rotated cube (`boxFitsVoxelBuffers`); an oversize subject is not drawn and the
camera's `subjectOversize_` says so. A rotated canvas re-seeds its resample
grid every frame (the records can change every frame); an unrotated one takes
the identity path and uploads nothing extra.

### D5 — Composite after the GUI, as a depth-less overlay

`VIEWPORT_TO_FRAMEBUFFER` runs after `TRIXEL_TO_FRAMEBUFFER` (world + GUI
composite) and before `FRAMEBUFFER_TO_SCREEN`. It draws each visible
viewport's canvas into its rectangle with the ordinary canvas-to-framebuffer
program, depth test and depth write off, hover far off-canvas. The world's
depth attachment and hovered-entity id are untouched, so hovering the portrait
never picks its subject's world entity.

### D6 — Lighting: the world's directional light; no world shadow, AO or fog

The canvas carries `C_CanvasAOTexture` + `C_TrixelCanvasRenderBehavior`, so
AO and the directional sun + sky terms apply. It is never world-placed
(`worldPlaced_`, `castsWorldShadow_` false), so it neither receives nor casts
the world's sun shadow and the fog pass never sees it.

### D7 — LOD

The viewport rasters at its own zoom's subdivision density
(`IRRender::getVoxelRenderEffectiveSubdivisionsForZoom`) and resolves its own
tier from that zoom (`IRRender::computeLodLevel`). `SYNC_VIEWPORT_SUBJECTS`
drops every tagged part whose `[lodMax_ .. lodMin_]` band excludes that tier
(`IRRender::shouldSkipAtLod`) before laying out the box, so a subject authored
as co-located variants on disjoint bands, every variant tagged, shows the world
its coarse variant and a close-up portrait its fine one.

`C_LodTierOverride` is world tier policy and the viewport does not read it: a
subject pinned coarse in the world still renders at the portrait's own tier,
which is what a close-up is for. The world-side gate (`GATE_VOXEL_SETS_BY_LOD`
setting `lodCulled_`) does not hide a part from the portrait either; the
portrait copies authored records, which a culled set keeps.

## Lua

```lua
local portrait = IRRender.createViewport({ x = 50, y = 370, width = 180, height = 260, zoom = 16 })
IRRender.setViewportSubject(portrait, hero)      -- a LuaEntity or an entity id; nil clears
IRRender.setViewportRect(portrait, 60, 380, 160, 240)
IRRender.setViewportCamera(portrait, 8, math.pi / 2)
IRRender.setViewportFocus(portrait, 0, 0, -2)
IRRender.setViewportVisible(portrait, false)
local drawn = IRRender.getViewportSubject(portrait)  -- last frame's subject, or nil
IRRender.destroyViewport(portrait)
```

Rectangle coordinates are GUI-canvas trixels, origin top-left. A retarget
clears the previous subject's tag in the same structural drain it sets the
new one, so two entities never share a viewport for a frame; the calls are
legal mid-iteration.

## Validators

- `python3 scripts/render-verify.py --target IRShapeDebug` — the
  `viewport_portrait` pass (`IRShapeDebug --viewport-portrait`): the world at
  zoom 1 and the portrait at zoom 16 in one frame, and the same with the world
  camera yawed a quarter turn (the portrait must not move).
- `python3 scripts/render-detached-lighting-metric.py <capture> --region
  x,y,w,h [--quarter-turns N]` — the portrait's face colours against the world
  sun at the viewport camera's yaw.
- `python3 scripts/gui-verify.py IRLuaWidgetsDemo` — a Lua-built portrait,
  retargeted, moved, resized and re-zoomed from a Lua `onClick`; the canvas's
  entity-id texture names the drawn entity.
- `ctest -R 'ViewportTest|LuaViewportBindings'` — the layout math, the
  subject sync and the Lua seam, headless.

## Costs and limits

- A subject rasterizes twice (world + portrait); keep a portrait to one
  subject. The pool copy is O(subject voxels) per frame on the CPU.
- A rotated viewport pays a GPU resample seed per frame (D4).
- The viewport pool shares the engine's voxel buffers with every canvas; a
  subject box larger than `VoxelPoolConfig::getMaxAllocationSizeTotal()` at
  rest or rotated is not drawn.
- Shapes (`C_ShapeDescriptor`) are not viewport subjects; only voxel sets.
- A resized rectangle or re-zoomed camera reallocates the canvas textures at
  the next sync (a frame-boundary operation).
