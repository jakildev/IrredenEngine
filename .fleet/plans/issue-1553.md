# Epic plan — detached GPU re-voxelize (true-3D asymmetric detached rotation)

**Umbrella issue:** #1553
**Target repo:** jakildev/IrredenEngine
**Model:** opus (all children — core render/ECS invariant work)
**Slug:** detached-gpu-revoxelize

## Context

The detached per-axis forward-scatter is a 2D image-warp of stored faces:
it skews the octahedral-snap orientation's faces by the residual and never
re-rasterizes the rotated solid, so voxel centers are never reorganized
(source of truth: `docs/design/per-axis-trixel-canvas-rotation.md`
§"Detached forward-scatter is terminal for asymmetric solids (#1551)"; root
cause PR #1552). It is correct for a cube (silhouette-invariant under the
snap) but wrong by construction for an asymmetric solid. The DoD's
"asymmetric reads as true 3D" test is unreachable on that path.

This epic builds the path that does meet it: the **detached analogue of the
attached GRID re-voxelize model** (`voxel-face-rasterization.md`
§"Per-entity SO(3) on the main canvas — RETIRED (#1443)"). Fill the
detached entity's **private per-entity voxel pool** with its voxels at their
**full-rotation cell positions**, render that pool with the **cardinal**
(non-rotated) face/projection path — the rotation lives in the cell
positions, not in a deform — and composite the canvas screen-locked. The
end state moves the per-frame cell-fill to a GPU scatter (the "#1396 ever
needed → GPU re-voxelize" path) and **retires the detached forward-scatter**.

### Why this is tractable (verified against current master)

- The **private per-detached-entity pool already exists**:
  `IRPrefab::EntityCanvas::createWithVoxelPool` attaches a `C_VoxelPool` to
  the detached canvas entity; multiple sets target it. No new pool model.
- **`VOXEL_TO_TRIXEL_STAGE_1` is already multi-canvas** and ticks every
  canvas entity with a pool + textures, with per-canvas frame data.
- The **screen-locked composite TRS** in
  `system_entity_canvas_to_framebuffer.hpp` is independent of pool contents
  (no rotation in the TRS — rotation is baked into the canvas content) and
  is reused unchanged.
- The CPU cell-fill math already exists for attached GRID:
  `IRPrefab::GridRotation::worldCellForGridVoxel` (scale → rotateVectorByQuat
  → translate → round-to-cell), reusable for a private pool.
- `#1396`'s GPU prepass (`UPDATE_VOXEL_POSITIONS_GPU`) transforms positions
  of existing voxels but does **not** fill cells — the GPU scatter (P2) is a
  new compute dispatch, not an extension of the prepass.

### Architecture decisions (made here; do not re-litigate)

1. **The rotation lives in cell positions, not a deform.** A re-voxelize
   detached canvas renders its private pool through the **cardinal / static**
   raster frame data (`visualYaw_ = 0`, no residual, no per-entity skew, no
   per-entity `visibleTriplet` baked as a deform) — exactly like the main
   GRID canvas, where "the camera alone drives faces; the entity's rotation
   only changes which cells fill." This is the key departure from today's
   detached frame data (which bakes rotation as a skew). Getting this wrong
   re-introduces the 2D-warp.
2. **CPU-first to prove the model, then GPU.** P1 reuses the CPU
   `worldCellForGridVoxel` to fill the private pool and prove the model
   cheaply; P2 replaces the fill with a GPU scatter for O(visible)-on-GPU
   cost. P1 is a shippable correctness baseline (same cost model as attached
   GRID, an accepted tradeoff), not a throwaway spike.
3. **Per-pool resident GPU locals (P2 resource model — decided on PR #1562).**
   Each re-voxelize pool owns a resident SSBO of its authored locals (GPU-RAII
   on `C_VoxelPool`), seeded once; per frame only the canvas quat uploads
   (O(entities)). The compute writes binding 5 per-canvas in place of
   `flushStaticPositionRanges`. This supersedes the original "reuse #1396
   binding-17/18" note — those are single-instance and break the demo's
   two-canvas case. See `.fleet/plans/issue-1556.md`.
4. **Both cubes and asymmetric solids go through re-voxelize.** Once the path
   exists, all detached SO(3) content uses it; the forward-scatter is retired
   wholesale (P6).

### Reconciliation with #1551 (closed — folded into P1)

#1551 / PR #1552 (the forward-scatter cube stopgap) were **closed as
superseded** by this epic. The cube goal is **folded into P1**: P1 migrates
the existing detached cube fringe to re-voxelize, so the visible cube crumple
+ #1539 pop are fixed correctly (a cube re-voxelizes cleanly) rather than
band-aided on the terminal forward-scatter path. P6 retires the forward-scatter
itself; there is no separate stopgap code to remove.

### No current consumer — P1 must add the validating content

There is no asymmetric detached *rotating* solid in current content (only
cubes spin). P1 therefore **adds an asymmetric detached rotating test solid
to `canvas_stress`** (a public demo) so the headline acceptance criterion is
actually exercisable. Without it there is nothing to verify against.

---

## P1 — Re-voxelize model proof: CPU fill into the private pool + asymmetric demo solid

**Model:** opus
**Blocked by:** (none)

### Scope
Stand up the detached re-voxelize render path end-to-end with a CPU cell-fill,
and prove it meets the true-3D criterion on an asymmetric solid. This is the
headline-proving phase.

### Approach
- Add a detached re-voxelize mode/opt-in (a new `RotationMode` value or a
  detached sub-flag — implementer picks the cleanest plumbing; reuse GRID
  semantics where possible). Opting in routes the entity through re-voxelize
  instead of the octahedral-snap + forward-scatter / per-axis path.
- Each frame, for a re-voxelize detached entity, fill its **private pool's**
  global cell positions via the reused `GridRotation::worldCellForGridVoxel`
  math under the entity's **full** rotation (CPU; same shape as
  `SYSTEM_REBUILD_GRID_VOXELS`, but writing the detached canvas's private
  pool, not the shared world pool). Extend that system to handle the new mode
  for detached canvases, or add a sibling — do not duplicate the transform
  math.
- Route the re-voxelize detached canvas through **cardinal/static raster
  frame data** (decision 1 above): `visualYaw_ = 0`, no residual, no
  per-entity skew — the canvas renders its pool exactly like a static canvas;
  the rotation is already in the cell positions.
- Bypass the off-snap octahedral single-canvas emit + the per-axis
  forward-scatter for re-voxelize-opted entities (no double-draw).
- Composite screen-locked via the **existing gather blit TRS** (unchanged).
- **Add an asymmetric (non-cube) detached rotating test solid to
  `canvas_stress`** (e.g. an L-tromino / slab / stepped solid, 3-DOF spin)
  so the discriminating shot exists.

### Acceptance criteria
- The asymmetric detached solid at ~45° between octahedral snaps reads as a
  **true 3D-rotated solid (voxel centers reorganized)**, not a 2D-skewed
  cardinal arrangement — the criterion the forward-scatter cannot meet.
- Clean, cohesive solid at **every** residual pose across a full SO(3) spin;
  smooth through snap intervals, **no pop** (absorbs #1539 for these
  entities).
- A detached **cube** via re-voxelize is also clean at every pose (validates
  the cube case the epic will use to supersede #1551).
- Non-opted entities (static, and forward-scatter cubes) **byte-identical** to
  master.
- GLSL only this phase; Metal parity deferred to P5.

### Gotchas
- The cardinal-frame-data routing is the crux. If the re-voxelize canvas
  inherits the per-entity skew frame data, the rotation gets applied twice (in
  cells AND as a deform) — wrong. Verify the canvas renders its pool with the
  same frame data a static canvas would.
- Private-pool sizing: the pool must be large enough to hold the rotated
  solid's bounding cells (a rotated solid's AABB is larger than its
  axis-aligned one by up to √3). Size `poolSize` to the rotated worst case.
- Round-to-cell aliasing (gaps / double-occupancy) is expected and is
  refined in P3 — P1 only needs the solid to read as true-3D, not be
  aliasing-perfect.

---

## P2 — GPU scatter fill (replace the CPU fill)

**Model:** opus
**Blocked by:** #P1

### Scope
Move the per-frame private-pool cell-fill from CPU to a GPU scatter compute
dispatch, so a horde of rotating detached entities costs O(entities) CPU
upload + GPU-parallel fill instead of O(authored voxels) CPU per frame.

### Approach
- Reuse `#1396`'s transform-slot indirection (`UPDATE_VOXEL_POSITIONS_GPU`,
  binding-17 local + binding-18 transforms) to get each voxel's world
  position on the GPU.
- Add a **new compute dispatch** that scatters each visible voxel into the
  detached pool's private grid cells: compute `floor(worldPos + 0.5)`,
  scatter into the pool's binding-5 cell (atomicMin on depth to resolve
  multiple source voxels landing in one cell). Run it **before**
  `VOXEL_TO_TRIXEL_STAGE_1` ticks that canvas so binding 5 holds the
  scattered cells.
- Extract the `worldCellForGridVoxel` transform into a shared GLSL/Metal
  helper (mirror the CPU math, CPU↔GPU bit-identical per the IRMath rule) —
  reuse the math, not the CPU system.

### Acceptance criteria
- Visually **byte-identical to P1** for the asymmetric + cube test solids
  across a full spin.
- Per-frame CPU cost for a rotating detached entity is O(entities) upload,
  not O(authored voxels) re-rasterize; GPU dispatch cost measured (run
  `optimize`).
- Binding-5 single-instance multi-canvas constraint respected: the scatter
  populates the detached pool's positions correctly even with multiple
  canvases (coordinate with `lastUploadedCanvas_` / the per-canvas re-seed).
- GLSL only; Metal in P5.

### Gotchas
- Binding 5 / 17 / 18 are single-instance and shared across canvases; the
  scatter for canvas A must complete and be consumed by A's STAGE_1 before
  canvas B overwrites them. Sequence per-canvas.
- Atomic cell-aliasing resolution overlaps P3 — keep P2 to the fill mechanism;
  P3 owns aliasing/occlusion correctness.

---

## P3 — Round-to-cell occlusion + aliasing correctness

**Model:** opus
**Blocked by:** #P2

### Scope
Make the re-voxelized solid's coverage and occlusion correct at every pose —
no aliasing holes/specks from the round-to-cell fill, no double-occupancy or
dropped cells, back faces correctly hidden.

### Approach
- Resolve multiple source voxels mapping to one destination cell (keep the
  nearest / correct color) and fill gaps where the rotated lattice
  under-samples a cell column (the standard re-voxelize aliasing problem; the
  attached GRID path's accepted round-to-cell tradeoff is the reference for
  what is acceptable vs. what must be fixed).
- Verify per-voxel occlusion through the normal STAGE_1 `atomicMin` depth is
  correct on the re-voxelized pool.

### Acceptance criteria
- No aliasing holes / specks / dropped cells across a full SO(3) spin of the
  asymmetric solid.
- Occlusion correct: back faces hidden, exposed-face mask correct on the
  re-voxelized cells.
- Coverage quality at least matches the attached GRID re-voxelize path.

### Gotchas
- Don't reintroduce a CPU per-voxel loop to fix aliasing — keep the fix on the
  GPU scatter (atomic resolution / a coverage pass), consistent with P2.

---

## P4 — AO / sun / light integration

**Model:** opus
**Blocked by:** #P3

### Scope
Light the re-voxelized detached canvas consistently with the rest of the
engine (AO + sun-shadow + light-volume + Lambert), at parity with how an
attached GRID solid or the per-axis path it replaces is lit.

### Approach
- Drive the existing per-canvas lighting passes over the re-voxelized detached
  pool (the canvas renders cardinal cells, so the standard lighting path
  applies — the rotation is in the cells, the normals are cardinal).
- Confirm the detached lighting contract (cast/receive) from the per-axis
  path (`per_axis_sun_shadow_resolve`, the P4/#1465 detached lighting) is
  preserved or correctly superseded.

### Acceptance criteria
- Re-voxelized detached solid is lit equivalently to an attached GRID solid.
- Shadows cast/receive per the existing detached lighting contract.
- Identity / static byte-identical.

---

## P5 — Metal parity

**Model:** opus
**Blocked by:** #P4

### Scope
Port the new GPU scatter compute shader (+ any GLSL changes from P1–P4) to
the Metal backend; build and run on macOS/Metal.

### Approach
- Mirror the scatter compute + any modified raster/lighting shaders to
  `.metal`; keep the CPU↔GPU transform math bit-identical across backends.
- Build `macos-debug`, run the `canvas_stress` re-voxelize demo, verify the
  asymmetric solid renders identically to OpenGL.

### Acceptance criteria
- Metal output matches OpenGL for the re-voxelized detached path (full-frame +
  ROI).
- Backend parity verified per the `backend-parity` skill.

---

## P6 — Render-verify baselines + retire the detached forward-scatter

**Model:** opus
**Blocked by:** #P5

### Scope
Commit render-verify baselines for the re-voxelize detached path and retire
the now-superseded detached per-axis forward-scatter (and the #1551 stopgap),
making re-voxelize the sole detached SO(3) path.

### Approach
- Add render-verify reference images for the asymmetric + cube re-voxelize
  detached shots.
- Remove the detached forward-scatter (`v_peraxis_scatter_detached.{glsl,metal}`
  + the `PerAxisScatterDetachedProgram` path) and the per-axis canvas
  allocation for detached (`syncAllocationToDetachedEntities`,
  `kMaxDetachedRotatingCanvases`) now that re-voxelize covers all detached
  SO(3). Keep the per-axis machinery the **camera/main-canvas** path still
  uses (only the detached consumer is retired).
- Remove the #1551 cube-smoothness stopgap code on the forward-scatter.
- Update `docs/design/per-axis-trixel-canvas-rotation.md`: flip the
  "Detached forward-scatter is terminal" section from "deferred" to "shipped —
  superseded by detached re-voxelize," and update
  `voxel-face-rasterization.md` + the render `CLAUDE.md` to point at the new
  path.

### Acceptance criteria
- Detached forward-scatter removed; re-voxelize is the sole detached SO(3)
  path; cube + asymmetric both clean.
- The camera/main-canvas per-axis path is untouched and byte-identical.
- Cardinal / identity byte-identical; render-verify green; docs updated.

### Gotchas
- Scope the removal precisely: the per-axis canvas machinery is **shared**
  with the camera smooth-yaw path — retire only the **detached** consumer, not
  the camera path. Cross-check every `isDetached()`-gated branch.

---

## Dependency chain

P1 (none) → P2 → P3 → P4 → P5 → P6. Strictly linear; each phase leaves the
tree green and the non-re-voxelize paths byte-identical.
