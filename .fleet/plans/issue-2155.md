## Plan: render: no-priority perf fast-path — skip triangleEntityIds read in f_trixel_to_framebuffer (#2122 item 3)

- **Issue:** #2155
- **Model:** opus
- **Date:** 2026-07-01

### Scope
`f_trixel_to_framebuffer` reads the `triangleEntityIds` channel **unconditionally**
at finalization (GLSL line 89 / Metal ~line 126) to decode the per-trixel priority
tier (`tier = max(depthPriorityMode, decodePriority(rawEntityId))`) — one extra
texture fetch per finalization fragment on the DEFAULT (no-priority) path,
pre-existing since #1989. Add a scene-derived `anyPerTrixelPriority` flag so the
shader can skip that fetch when no voxel in the drawn canvas carries a non-zero
per-trixel priority, restoring **perf-identity** (not just byte-identity) on the
default path, on BOTH backends. Single task, no stack.

### Verified current state
- GLSL `engine/render/src/shaders/f_trixel_to_framebuffer.glsl:89` reads
  `rawEntityId` once and uses it for **two** things: the priority tier (line 93,
  every fragment) and hover-picking (line 133, only inside `if (isMouseHovered)`).
  So the read cannot be dropped outright — the hover path still needs it for
  hovered fragments. Gate must be `(anyPerTrixelPriority || isMouseHovered)`.
- Metal twin `engine/render/src/shaders/metal/trixel_to_framebuffer.metal` mirrors
  the same unconditional read + `max(depthPriorityMode, decodePriority(...))`.
- The shared UBO is `FrameDataTrixelToFramebuffer` (`ir_render_types.hpp:84`,
  `sizeof == 208`, GLSL block `FrameDataIsoTriangles`). It has a **free scalar pad**
  `float _depthColorPad0_` at **offset 200** (declared `float _depthColorPad0;` in
  the GLSL block), sitting right before `int depthPriorityMode_` at offset 204.
  This is the same kind of slot #1958 repurposed (`_depthColorPad1_` -> `depthPriorityMode_`).
- Confirmed repro of the priority path: `canvas_stress --only interpenetrate` calls
  `C_VoxelSetNew::changeVoxelPriorityAll(priority)` (`creations/demos/canvas_stress/main.cpp:794`).
  The #2122 ENABLED-path gate `scripts/depth-tier-verify.py`
  (`--depth-probe-assert 639,362,tier=2`) already covers the priority-ON decode.
- **Priority always originates on `C_VoxelSetNew`.** `changeVoxelPriority` /
  `changeVoxelPriorityAll` (`component_voxel_set.hpp:336,342`) write the low 2 bits
  of `C_Voxel::reserved_`; `c_voxel_to_trixel_stage_2` packs them into the id
  carrier. The rotating `DETACHED_REVOXELIZE` path (#2023) copies the SAME
  `reserved` lane from the source set into its grid — so an aggregate maintained
  on `C_VoxelSetNew` captures every priority-carrying path (world pool, static
  detached, re-voxelize). No separate GPU-side scan is needed.

### Approach
Byte-identity-preserving on both the flag-ON and flag-OFF paths. Order:

1. **`ir_render_types.hpp`** — repurpose the offset-200 pad: rename
   `float _depthColorPad0_ = 0.0f;` -> `int anyPerTrixelPriority_ = 0;`. Update the
   doc comment (mirror the `depthPriorityMode_` comment style: repurposes the former
   `_depthColorPad0_` std140 slot at offset 200, no size/offset change). `sizeof`
   stays 208; every existing `static_assert` (128/144/160/176/192/208) is unchanged.

2. **`f_trixel_to_framebuffer.glsl`** — in the `FrameDataIsoTriangles` block rename
   `float _depthColorPad0;` -> `int anyPerTrixelPriority;` (same offset 200, 4-byte
   scalar — layout-identical). Restructure `main()`:
   - Move the hover-position computation (current lines 120-127: `subdivisions`,
     `hoveredPosition`, `originIndex`, `hoveredIndex`, `isMouseHovered`) **above**
     the entity-id read. It depends only on `origin`, `canvasOffset`, `textureSize`,
     `effectiveSubdivisionsForHover` — no texture read — so the reorder is inert.
   - Replace the unconditional read with:
     ```glsl
     int tier = depthPriorityMode;
     uvec2 rawEntityId = uvec2(0u);
     if (anyPerTrixelPriority != 0 || isMouseHovered) {
         rawEntityId = textureLod(triangleEntityIds, origin / vec2(textureSize), 0).rg;
         tier = max(depthPriorityMode, int(decodePriority(rawEntityId)));
     }
     ```
   - The `enc`/`depth` block (lines 94-116) and the hover block (lines 128-143) are
     otherwise unchanged; the hover block already runs only under `isMouseHovered`,
     where `rawEntityId` is now guaranteed read. When `anyPerTrixelPriority == 0`,
     `decodePriority` would be 0 for every fragment, so `tier == depthPriorityMode`
     — byte-identical.

3. **`metal/trixel_to_framebuffer.metal`** — mirror step 2: rename the pad field,
   compute `isMouseHovered` before the read, gate the `triangleEntityIds.read(...)`
   on `(frameData.anyPerTrixelPriority != 0 || isMouseHovered)`, default
   `tier = frameData.depthPriorityMode`. Keep the sampleCoord/hoverCoord distinction
   the Metal path already uses.

4. **`C_VoxelSetNew`** (`component_voxel_set.hpp`) — add author-time aggregate:
   `std::uint32_t perTrixelPriorityVoxelCount_ = 0;` and
   `bool hasPerTrixelPriority() const { return perTrixelPriorityVoxelCount_ > 0; }`.
   Maintain it precisely in the two authoring methods (compare old vs new low-2 bits
   in `changeVoxelPriority`; in `changeVoxelPriorityAll` set to
   `priority != 0 ? liveVoxelCount : 0`) and zero it in `clear()`. This is the
   sanctioned "push at mutation time" pattern (not a dirty flag — it gates nothing).

5. **`C_TriangleCanvasTextures`** (`component_triangle_canvas_textures.hpp`) — add a
   per-canvas stamp `int anyPerTrixelPriority_ = 0;` next to `renderedSubdivisions_`,
   with the **same lifecycle** (0 = no priority voxel rastered this frame -> fast path).

6. **`VOXEL_TO_TRIXEL_STAGE_1`** (`system_voxel_to_trixel.hpp`) — where it already
   visits each `C_VoxelSetNew` rastered into the canvas, OR
   `set.hasPerTrixelPriority()` into that canvas's `anyPerTrixelPriority_` (reset to
   0 at the start of the canvas's raster, exactly like `renderedSubdivisions_` is
   stamped). O(sets per canvas) — no per-voxel scan, no per-fragment cost.

7. **`system_trixel_to_framebuffer.hpp`** (main gather) — set
   `frameData.frameData_.anyPerTrixelPriority_ = triangleCanvasTextures.anyPerTrixelPriority_;`
   in `tick()` (the `const C_TriangleCanvasTextures &` is already a tick param), next
   to the existing `depthPriorityMode_ = 0;` line (~117).

8. **`system_entity_canvas_to_framebuffer.hpp`** (detached composite) — set
   `fd.anyPerTrixelPriority_` from the child canvas's `C_TriangleCanvasTextures`
   stamp (it already reads that canvas's textures for the `renderedSubdivisions_`
   depth rescale), next to the existing `fd.depthPriorityMode_ = ...` line (~335).

9. **Scatter shaders** (`v_/f_peraxis_scatter.glsl` + `.metal`) — they declare the
   shared UBO tail. Grep for `_depthColorPad0` and either rename to
   `int anyPerTrixelPriority` for consistency or leave as an unread pad; layout is
   identical either way (they never read this slot). Verify they still compile.

### Affected files
- `engine/render/include/irreden/render/ir_render_types.hpp` — pad -> `anyPerTrixelPriority_` (offset 200), doc comment.
- `engine/render/src/shaders/f_trixel_to_framebuffer.glsl` — UBO field rename + conditional read.
- `engine/render/src/shaders/metal/trixel_to_framebuffer.metal` — mirror.
- `engine/prefabs/irreden/voxel/components/component_voxel_set.hpp` — author-time count + `hasPerTrixelPriority()`.
- `engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp` — `anyPerTrixelPriority_` stamp.
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — OR-reduce the stamp during raster.
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` — publish stamp into UBO (main gather).
- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp` — publish stamp into UBO (detached composite).
- `engine/render/src/shaders/v_peraxis_scatter.glsl` / `f_peraxis_scatter.glsl` (+ `.metal`) — sync the shared UBO tail declaration.

### Acceptance criteria
- `anyPerTrixelPriority` plumbed into `f_trixel_to_framebuffer` frame data on BOTH
  backends; when false, the finalization path skips the `triangleEntityIds` decode
  read (still reads it for hovered fragments so picking is unaffected).
- **Byte-identical, flag ON:** priority-scene output unchanged.
  `scripts/depth-tier-verify.py` still exits 0; `render-verify`/`--depth-probe` on
  `canvas_stress --only interpenetrate` matches its committed reference.
- **Byte-identical, flag OFF:** a no-priority scene is unchanged (the skipped read
  would decode tier 0 anyway). Verify via `render-verify` on a no-priority
  `canvas_stress`/`shape_debug` shot set + a `--depth-probe` reading.
- **Picking still works, flag OFF:** confirm hover/entity-id readback in a
  no-priority scene (the `isMouseHovered` branch still reads the channel).
- **Perf-identity:** on a no-priority scene, GPU-stage timing for
  `TRIXEL_TO_FRAMEBUFFER` shows the extra fetch is gone (spot-check both backends;
  this is the whole point of the item).
- Both `fleet:needs-linux-smoke` + `fleet:needs-macos-smoke` cleared (GL + Metal
  touched); render-debug-loop screenshot + ROI-crop pair attached.

### Gotchas
- **Do not grow the UBO.** Use the offset-200 pad; a fresh appended int would
  bump `sizeof` to 224 and break the std140 asserts + every consumer of the shared
  block. Renaming the pad keeps all offsets and asserts intact.
- **The read feeds hover too.** Skipping it wholesale breaks picking; the
  `|| isMouseHovered` disjunct is mandatory. Compute `isMouseHovered` before the
  read (it needs no texture fetch).
- **Conservative-TRUE is always safe.** A false-positive flag only costs the fast
  path, never correctness. So the author-time count needs to be reliable only for
  the "no priority anywhere" -> FALSE case; mutations that could leave it
  stale-HIGH (e.g. overwriting a priority voxel via a generic `setVoxel`) are
  acceptable. Maintain it precisely in `changeVoxelPriority*`/`clear`; don't chase
  every incidental mutation.
- **Stamp lifecycle matches `renderedSubdivisions_`.** A canvas not rastered this
  frame keeps stamp 0 -> fast path, which is correct (no priority voxel reached the
  composite). Reset per-raster, don't accumulate across frames.
- **Same-canvas caveat still holds (#1960).** Per-trixel priority only arbitrates
  ACROSS canvases at finalization; this change doesn't alter that — it only gates
  when the decode read runs.
- **Cross-host:** GL + Metal edited in lockstep; keep the field name/type identical
  across `ir_render_types.hpp`, both `.glsl`, and both `.metal`. Metal has no free
  buffer index past 30 — this reuses an existing UBO slot, so no new binding.

