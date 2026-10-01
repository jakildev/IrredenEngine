# Composite entity canvases — shared parts, detach, residency

**Issue:** #3972. Builds on the runtime rotation-mode switch (#3967) and the
detached re-voxelize path ([`detached-revoxelize-world-light.md`](detached-revoxelize-world-light.md)).

A composite entity — a body whose parts turn independently — previously
needed one private canvas per part. One canvas now hosts several voxel sets,
each posed by its own transform; a part can leave its host and continue as an
independent entity; and entity canvases are a budgeted residency resource
across a large world instead of a per-entity allocation.

## Cell groups

A `DETACHED_REVOXELIZE` pool is resampled by the inverse-resample kernel
(`c_revoxelize_detached.{glsl,metal}`) one **cell group** at a time. A group is
a contiguous pool span (one hosted `C_VoxelSetNew`) with its own rotation and
translation in the canvas's model frame (`VoxelCellGroup`, posted on
`C_VoxelPool`). The seed (`IRPrefab::DetachedRevoxelize::seedResidentLocals`)
scans each span separately — its own source grid, half-cell anchor and
rotation-independent dest cube — and lays the groups' dest-slot ranges out
contiguously; the fill dispatches up to `kRevoxelizeGroupsPerDispatch` groups
per call, each thread resolving its group from its slot.

The pool's lattice phase is the first group's half-cell anchor; every group
resamples onto that lattice through its own translation, with the dest window
`destWindowBase` sized to hold the group under any rotation at that
translation. Two groups may author the same dest cell from different slots;
the raster resolves it like any other equal-depth pair, so a pool with more
than one group sets `storeTiesPossible_` and runs the winner election.

A pool that has never been posted to is one implicit group over its live
prefix, posed by the canvas rotation — the single-set path, byte-identical to
before. Once a group has been posted the pool resamples exactly the posted
spans (`hostsCellGroups()`), so a host whose last part left draws nothing
rather than the freed slots.

## Parts

`C_CanvasPart{host}` marks an entity's voxel set as a part of `host`'s
canvas. `PROPAGATE_CANVAS_PARTS` (UPDATE, after `PROPAGATE_CANVAS_ROTATION`)
rebuilds every re-voxelize pool's group list each frame from the parts whose
set lives in it: rotation is the part's world rotation with the camera basis
removed (`IRPrefab::CanvasPose::canvasRotation`), translation is its world
offset from the host in that frame. Parenting a part to its host is how the
part rides the host's motion; the pose is read from `C_WorldTransform` either
way. `PROPAGATE_CANVAS_ROTATION` itself now bakes the owner's **world**
rotation, so a detached child under a rotating parent turns with it.

A hosted part carries `C_RotationMode{DETACHED_REVOXELIZE}` (keeps the GRID
rebuild off its span) and no `C_EntityCanvas` (keeps the composite from
drawing it twice). Membership outlives residency: when the host leaves
re-voxelize mode its parts fall back to GRID through
`IRPrefab::CanvasPart::releaseSets`, and `adoptParts` moves them back when a
re-voxelize canvas returns.

### Detach

`IRPrefab::RotationMode::setMode(part, mode)` on a part first calls
`CanvasPart::leaveHost`: the span is freed, and `C_VoxelPool::deallocateVoxels`
drops the matching cell group in the same call, so the host cannot resample a
span whose set now lives elsewhere. The set is then re-homed (a new private
canvas, or the active canvas for GRID) before the call returns. Every set a
switch moves is resident in its destination on return, and an allocated
canvas is posed by `stampCanvasPose`; requested ahead of the UPDATE transform
chain (the switch is a staged structural change), the entity is drawn exactly
once on every frame across the switch. The `canvas_stress --only detach
--probe-assert` census asserts that per frame; its late-request arm (a part
leaving after the frame's groups were posted) is the double-draw that the
deallocate-time drop closes.

## Residency

`C_CanvasResidency` opts an entity into `CANVAS_RESIDENCY` (UPDATE, first).
Each frame the system classifies managed entities against the camera's
interest region — last frame's cull viewport plus a margin: inside the
promote margin a GRID entity gains a canvas, outside the demote margin a
resident one releases it, and the band between is hysteresis. Promotions run
nearest-first within `promotionsPerFrame_` and the live-canvas budget
(`C_CanvasResidencySettings`, defaults in `IRConstants`); the composite's
instance capacity is that same budget, so a world under the policy never hits
the composite's drop. An entity that finds no room stays GRID and is still
drawn. The `--only residency --probe-assert` census holds live canvases to the
budget on every frame of a pan and requires every on-screen entity to be
drawn by exactly one path.

## Measured (Phase 0)

See the PR for the `--parts-count N` / `--separate-canvases` A/B on
`canvas_stress` (one shared canvas vs N canvases, same solids and poses).
