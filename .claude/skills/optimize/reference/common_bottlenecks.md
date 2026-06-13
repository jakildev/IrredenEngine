# common bottlenecks — curated catalog

Seed library of bottleneck patterns this skill has actually encountered.
Each entry has:

- **Pattern** — the smell to grep for.
- **Where** — file:line example from the engine (kept fresh; if the
  example moves, update this).
- **Symptom** — what shows up in profiler output or matrix data.
- **Fix** — the canonical pattern, with a cross-reference if longer.

The catalog grows via step 7 of the optimize flow. Any new pattern a
session uncovers lands here in the same PR as the fix.

---

## CPU bottlenecks (system tick paths)

### 1. `getComponent` on the iterating entity inside a tick

- **Pattern**: `IREntity::getComponent<C_Foo>(entity)` or
  `getComponentOptional<C_Foo>(entity)` inside a per-entity `tick(...)`.
- **Where**: `engine/prefabs/irreden/update/systems/system_spring_platform.hpp:54-55`
  (active known violation in the codebase).
- **Symptom**: That system shows up high on `compare_perf_runs.py`'s
  "top CPU systems" with avg ms scaling linearly with archetype size.
  Profiler trace shows a hash-map probe per entity.
- **Fix**: Add `C_Foo` to the system's template parameters so it
  iterates as a dense archetype column. Full rule:
  `.claude/rules/cpp-ecs.md` §"The ECS footgun".

### 2. Allocation in a per-entity tick

- **Pattern**: `std::vector::push_back` on a non-pre-sized vector,
  `std::string` concatenation, `std::make_unique`, `std::map::operator[]`
  insertion — anywhere inside `tick(...)`.
- **Where**: search for `m_X.push_back` or `m_X[key] = ...` in tick
  bodies under `engine/prefabs/irreden/`.
- **Symptom**: System's `max_ms` spikes occasionally above its `avg_ms`
  by 5–10× — the spike is reallocs on growth boundaries. Easy_profiler
  trace shows a `__malloc` block inside the tick.
- **Fix**: Pre-size in `beginTick` with a high-water-mark `reserve`,
  reuse across frames, `clear()` without `shrink_to_fit()`. Full rule:
  `.claude/rules/cpp-ecs.md` §"Allocations in hot tick paths".

### 3. Function-local `static` for system-owned mutable state

- **Pattern**: `static std::vector<T>& getX() { static std::vector<T> x; return x; }`
  or any function-local non-`constexpr` static inside a system file.
- **Where**: `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp:41-43`
  (active known violation).
- **Symptom**: Not directly a hotspot, but blocks future SIMD batching
  and breaks `SystemParams` lifecycle. Often surfaces only when the
  system gets recreated mid-run.
- **Fix**: Move to `SystemParams` field. Full rule:
  `.claude/rules/cpp-systems.md` §"Canonical SystemParams pattern".

### 4. Per-frame buffer upload without push-at-mutation

- **Pattern**: A `Buffer::subData(0, size, data)` call inside a
  per-frame tick that uploads CPU-mirror state regardless of whether
  it actually changed.
- **Where**: `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
  (positions migrated to push-at-mutation in T-289; color + entityId
  still upload per-frame as of this writing — audit and migrate).
- **Symptom**: GPU stage timing shows a `voxelStage1` baseline cost
  proportional to live voxel count even when nothing changed. CPU
  shows up in the easy_profiler trace under the upload path.
- **Fix**: Push at mutation time in the setter that touches the field
  (see `C_GPUParticlePool::writeSlot` for the canonical pending-list
  flush). Never add a `bool dirty_` flag — full rule:
  `.claude/rules/cpp-ecs.md` §"No dirty flags on components".

### 12. Pending-range queue accumulates across fixed-timestep catch-up ticks

- **Pattern**: A per-frame "pending ranges" / dirty list populated by a
  system in the **UPDATE** pipeline and drained by a system in the
  **RENDER** pipeline. UPDATE runs N times per render frame (fixed-timestep
  catch-up); a moving entity re-queues its range every UPDATE tick, so the
  list grows to `N × (queuing entities)` and the RENDER-side drain (often a
  `std::sort` + coalesce) blows up super-linearly. Worse: a slow frame
  raises N, which slows the frame further — a feedback spiral.
- **Where**: `C_VoxelPool::queuePositionRange` / `flushPendingPositionRanges`
  in `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
  (fixed in the PR that added this entry).
- **Symptom**: A RENDER system's CPU avg scales with *frame time* rather
  than with scene content — `SingleVoxelToCanvasFirst` measured 14 ms at a
  46 ms frame but 62 ms at a 181 ms frame on the *same* scene. Sub-tick
  `IR_PROFILE_SCOPE` blocks localize it to the queue-drain region.
- **Fix**: Cap the queue. Once it saturates the queued ranges no longer
  describe a small moved subset — fall back to one whole-buffer upload and
  drop further queue calls (the full upload covers them). Caps both the
  drain CPU cost and the queue's memory / `push_back` cost. The deeper fix
  is dedupe-at-queue-time or drain-per-UPDATE-tick; the saturation cap is
  the contained, provably-correct version.

---

## GPU bottlenecks (render pipeline)

### 5. Wrong compute dispatch grid (hand-rolled instead of helper)

- **Pattern**: `dispatchCompute((n + 63) / 64, 1, 1)` or similar
  hand-rolled math.
- **Where**: search `engine/render/` and `engine/prefabs/irreden/render/`
  for `dispatchCompute` without `voxelDispatchGridForCount` upstream.
- **Symptom**: Stage runs but produces wrong output, or wastes ~2×
  workgroups, or hits the GL `MAX_COMPUTE_WORK_GROUP_COUNT_X` limit at
  scale.
- **Fix**: Use `voxelDispatchGridForCount()` which packs into 2D when
  X exceeds 1024.

### 6. CPU→GPU sync stall (synchronous readback in hot path)

- **Pattern**: `glReadPixels`, `glGetBufferSubData`, `Buffer::getSubData`
  called inline in a system tick or render-stage execution.
- **Where**: shipping code generally avoids this. Exception: cull-stats
  readback in `system_voxel_to_trixel.hpp` (gated on
  `gpuStageTiming().enabled_`, sync-free in shipping).
- **Symptom**: Frame time ~16ms on what should be a fast frame.
  GPU-side timing shows the readback's preceding pass at "full GPU
  utilization" because the CPU is stalled waiting.
- **Fix**: Use a fence + multi-frame ringbuffer, or read prior-frame
  data (the cull-stats pattern). For genuine inline reads, use
  persistent mapped buffers (`PERSISTENT | COHERENT`).

### 7. Shadow-feeder sweep inflates cull bounds at high zoom

- **Pattern**: `IRMath::shadowFeederIsoBounds(visible, sunDir,
  sweepDistance)` expands the cull AABB by ~64 voxels in the sun
  direction. At zoom 4–8 this expansion **dominates** the visible
  AABB, so the cull lets through far more voxels than the viewport
  would naively suggest.
- **Where**: `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp:253-257`,
  math in `engine/math/include/irreden/ir_math.hpp:601`.
- **Symptom**: `compare_perf_runs.py` cull-effectiveness table shows a
  ratio 10–30× looser than `1/zoom²` ideal at high zoom. Frame time
  scales with `sub² × visible_count` and `visible_count` stays high.
- **Fix (partial, landed)**: the **canvas-coverage cull clamp** in
  `system_voxel_to_trixel.hpp` (`clampToCanvasCoverage`): a subdivided
  raster writes taps at `offset + isoPos*scale`, so the fixed-size
  canvas only holds iso positions within `canvasSize/(2*scale)` of the
  view center — swept feeders beyond that have EVERY tap clip-rejected
  and culling them is byte-identical (IRPerfGrid zoom8 FULL cardinal:
  48.5 → 25.7 ms, cull 0.47 → 0.23). The clamp applies only where the
  canvas physically can't hold the sweep; feeders the canvas CAN hold
  still raster (shadow correctness preserved).
- **Fix (remaining, open)**: feeders inside coverage still pay `sub²`.
  Sketch: split shadow-feeder voxels into a depth-only fast path that
  doesn't pay the `sub²` cost in `voxel_to_trixel_stage_2`. Don't break
  the invariant (`engine/render/CLAUDE.md` §"Lighting culling
  invariants") — feeder voxels still need `trixelDistances` writes for
  the sun-shadow bake; they just don't need color / AO / entity ID.

### 8. O(sub²) per-shape tile generation in SHAPES_TO_TRIXEL

- **Pattern**: CPU-side tile descriptor generation expands each shape's
  iso bounds by `sub` and divides by `kShapeTileSize`, so tile count
  per shape grows as `O(sub²)`. Many of those tiles never touch a
  shape pixel.
- **Where**: `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp:412-429`.
- **Symptom**: GPU stage timing shows `shapePass1` growing rapidly with
  zoom in scenes that use SDF shapes (perf_grid `--mode sdf`).
  Profiler shows tile-dispatch count >100 per shape at zoom 4 with
  default subdivisions.
- **Fix**: Pre-cull tiles on CPU against the shape's actual SDF/AABB
  before emitting tile descriptors. Cheap conservative bounding-rect
  test first, then full SDF intersection if needed. Not yet filed;
  measure with `--mode sdf` matrix run to confirm before optimizing.

### 9. `numGroupsZ = subdivisions²` voxel dispatch growth

- **Pattern**: `c_voxel_visibility_compact.glsl` writes
  `numGroupsZ = subdivisions²` for stage 2's indirect dispatch. By
  design (each face needs sub² micro-pixel coverage), but at high zoom
  with FULL mode this can hit `MAX_COMPUTE_WORK_GROUP_COUNT_Z` (≥65535
  on most drivers) before the X×Y limit.
- **Where**: `engine/render/src/shaders/c_voxel_visibility_compact.glsl:92-93`.
- **Symptom**: Stage 2 returns wrong / missing output at sub ≥16 and
  visible counts >65535/sub². No GL error.
- **Fix**: Cap effective subdivisions OR repack Z into X×Y when sub²
  exceeds a threshold. Not yet filed — currently the
  `getVoxelRenderEffectiveSubdivisions` clamp to 16 keeps it under the
  limit, but the headroom is small.

### 12. Reusing an indirect dispatch command whose Z the pass ignores

- **Pattern**: a pass dispatches `dispatchComputeIndirect` from a
  shared indirect-params buffer whose `numGroupsZ = subdivisions²`,
  but the pass's shader early-returns for every `z != 0` (base-
  resolution stores post-#1458). Each wasted z-slice still launches,
  reads its inputs, and exits — `sub²×` overdispatch.
- **Where**: the per-axis smooth-yaw store in
  `system_voxel_to_trixel.hpp::dispatchPerAxisCanvases` (3 axes × 2
  stages per frame) before the single-slice command landed.
- **Symptom**: rotated frame time scales with zoom² while the cardinal
  path doesn't; IRPerfGrid zoom8 FULL rotated sat at 137 ms vs 48 ms
  cardinal. GPU stage timing won't show it if the stage tags don't
  cover the extra dispatches.
- **Fix**: have the producer (compact pass) author a SECOND indirect
  command in the same buffer — same XY, `z = 1`
  (`VoxelIndirectDispatchParams::singleSliceNumGroups*`, byte offset
  20) — and dispatch the z-ignoring pass from that offset.
  137 → 20.6 ms on the worst IRPerfGrid cell.

### 13. Brute-force instanced draw over a mostly-empty grid

- **Pattern**: `drawElementsInstanced(instanceCount = W*H)` over every
  cell of a canvas/grid, with the vertex shader degenerating empties
  off-screen. >90 % of instances are empty on typical frames; the
  vertex stage still runs (texelFetch + branch) for each.
- **Where**: the per-axis forward scatter in
  `system_trixel_to_framebuffer.hpp::drawPerAxisScatter` before the
  compaction pre-pass (worst-case (2W, W+H) canvas ≈ 2M cells × 3
  axes per frame).
- **Symptom**: a flat per-frame cost on the affected path that does
  NOT scale with scene content — IRPerfGrid rotated NONE-mode floor
  sat ~6 ms above cardinal even with a 512-voxel grid.
- **Fix**: compute pre-pass (`c_compact_scatter_cells`) appends
  non-empty cell indices + authors the 5-uint indirect draw command;
  the draw becomes `drawElementsIndirect` over occupied cells only.
  Caveats: (a) GL `DrawElementsIndirectCommand` and Metal
  `MTLDrawIndexedPrimitivesIndirectArguments` share the 5-uint layout,
  so one shader writes both; (b) canvas-texture reads in a COMPUTE
  pass must use `bindAsImage`/`imageLoad`, not sampler `bind()` — the
  Metal compute table only sees image binds; (c) on Metal, any sticky
  image/texture bind of a transient texture (the per-axis canvases
  release at cardinal) must be purged on texture destruction —
  `unbindMetalTexture` in the texture destructor — or the next
  encoder re-encodes a freed `MTL::Texture*` (segfault at the
  rotated→cardinal transition).

---

## Math / shader hot paths

### 10. `glm::*` or `std::*` math primitives outside `engine/math/`

- **Pattern**: `glm::sin(x)`, `std::max(a, b)`, etc. in C++ code
  outside `engine/math/`.
- **Where**: tracked in `<engine-root>/.fleet/status/glm-deviations.md`
  (queue-manager-owned, committed; not `~/.fleet/`) — or will be, once
  the file is introduced. Until then, discover violations with
  `rg 'glm::|std::(sin|cos|sqrt|abs|min|max|clamp)' --type cpp engine/
  -g '!engine/math/**'`.
- **Symptom**: Not a runtime hotspot, but blocks the
  IRMath-implementation-swap path and breaks CPU↔GPU consistency
  (e.g. `glm::round` vs `IRMath::roundHalfUp`).
- **Fix**: Use the `IRMath::` wrapper. If the wrapper doesn't exist,
  add it to `engine/math/` first. Full rule:
  `.claude/rules/cpp-math.md`.

### 11. Repeated math sequences across multiple shaders

- **Pattern**: Same iso-projection or SDF math copy-pasted across two
  or more `.glsl` files.
- **Where**: search `engine/render/src/shaders/` for duplicate
  function bodies.
- **Symptom**: Not a runtime hotspot directly, but a maintenance
  hazard — a fix on one side gets forgotten on the other. Worth
  flagging in code review even when perf isn't the immediate concern.
- **Fix**: Move to a shared include (`ir_iso_common.glsl`,
  `ir_constants.glsl`).

---

## How to extend this catalog

Append to this file in the same PR as the optimization fix that
discovered the pattern. Required fields:

- **Pattern**: the regex or smell.
- **Where**: file:line example.
- **Symptom**: what surfaces in `compare_perf_runs.py` or the
  easy_profiler trace.
- **Fix**: the canonical pattern + a cross-reference to the rule or
  PR that defines it.

Don't add patterns you only suspect — only ones you've actually
encountered and measured.
