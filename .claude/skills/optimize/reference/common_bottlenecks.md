# common bottlenecks — curated catalog

Patterns this skill has measured, each with the smell, a location, the
profiler symptom, and the canonical fix. Append in the same PR as the fix
that found a new one; never add a pattern that was only suspected.

## CPU (system tick paths)

### 1. `getComponent` on the iterating entity inside a tick

- **Pattern**: `IREntity::getComponent<C_Foo>(entity)` /
  `getComponentOptional<C_Foo>(entity)` inside a per-entity `tick(...)`.
- **Where**: `.claude/rules/cpp-ecs-smells.md` §"Per-entity tick violations".
- **Symptom**: the system ranks high in the comparator's "top CPU systems"
  with avg ms linear in archetype size; the trace shows a hash-map probe per
  entity.
- **Fix**: add `C_Foo` to the system's template parameters so it iterates as
  a dense column — `.claude/rules/cpp-ecs.md` §"The ECS footgun".

### 2. Allocation in a per-entity tick

- **Pattern**: `push_back` on an un-reserved vector, `std::string`
  concatenation, `std::make_unique`, `std::map::operator[]` insertion inside
  `tick(...)`.
- **Where**: grep `m_X.push_back` / `m_X[key] =` in tick bodies under
  `engine/prefabs/irreden/`.
- **Symptom**: `max_ms` spikes 5–10× above `avg_ms` on growth boundaries; a
  `__malloc` block inside the tick in the trace.
- **Fix**: high-water-mark `reserve` in `beginTick`, reuse across frames,
  `clear()` without `shrink_to_fit()` — `.claude/rules/cpp-ecs.md`
  §"Allocations in hot tick paths".

### 3. Function-local `static` for system-owned mutable state

- **Pattern**: `static std::vector<T>& getX() { static std::vector<T> x; return x; }`
  or any non-`constexpr` function-local static in a system file.
- **Where**: the live register in `.claude/rules/cpp-systems.md` §"Live
  deviations" — cite it, not a remembered file:line.
- **Symptom**: not a hotspot directly; blocks SIMD batching and breaks the
  `SystemParams` lifecycle, surfacing when a system is recreated mid-run.
- **Fix**: a member on the `System<N>` specialisation (`registerSystem`) or a
  `SystemParams` field — `.claude/rules/cpp-systems.md`.
- **Perf note**: a static accessor pays an acquire-load guard per call.
  Replacing it with a singleton-component read wins only when the read sits
  behind an early-out; an unconditional `singletonOrNull` per frame trades one
  guard load for two hash probes and regresses.

### 4. Per-frame buffer upload without push-at-mutation

- **Pattern**: `Buffer::subData(0, size, data)` in a per-frame tick uploading
  CPU-mirror state whether or not it changed.
- **Where**: `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
  (positions push at mutation; colour and entityId still upload per frame).
- **Symptom**: `voxelStage1` GPU cost proportional to live voxel count with
  nothing changing; the upload path in the CPU trace.
- **Fix**: push in the setter that touches the field
  (`C_GPUParticlePool::writeSlot` is the pending-list flush); never a
  `bool dirty_` — `.claude/rules/cpp-ecs.md` §"No dirty flags on components".

### 5. Pending-range queue accumulating across fixed-timestep catch-up ticks

- **Pattern**: a dirty list filled by an UPDATE system and drained by a RENDER
  system. UPDATE runs N times per render frame, so a moving entity re-queues
  every tick, the list grows to `N × entities`, the drain (`std::sort` +
  coalesce) blows up super-linearly, and a slow frame raises N — a feedback
  spiral.
- **Where**: `C_VoxelPool::queuePositionRange` / `flushPendingPositionRanges`
  in `system_voxel_to_trixel.hpp`.
- **Symptom**: a RENDER system's CPU avg scales with frame time rather than
  scene content; `IR_PROFILE_SCOPE` sub-blocks localise it to the drain.
- **Fix**: cap the queue; once saturated, fall back to one whole-buffer upload
  and drop further queue calls. Dedupe-at-queue-time or drain-per-UPDATE-tick
  is the deeper fix.

### 6. `sol::table::size()` in a loop condition at the Lua boundary

- **Pattern**: `for (std::size_t i = 1; i <= t.size(); ++i)` over a Lua array
  argument. `sol::table::size()` is a Lua `#` call across the VM boundary,
  re-paid per element; it reads like a free `std::vector::size()`.
- **Where**: grep `engine/script/include/irreden/script/lua_*_bindings.hpp`
  for `<= *\w+\.size()` and `.size()` inside a `for`.
- **Symptom**: binding cost grows faster with element count than the
  per-element work justifies. Invisible to the perf_grid matrix (no Lua
  bindings driven) — needs an in-process A/B timed from Lua.
- **Fix**: hoist `const std::size_t n = t.size();` (~8.5% off the binding at
  200 paths × 5 points per frame). Removing the temp `std::vector` was a
  further ~2% — below the 5% bar and it costs all-or-nothing argument
  validation; the boundary crossing dominates.

## GPU (render pipeline)

### 7. Hand-rolled compute dispatch grid

- **Pattern**: `dispatchCompute((n + 63) / 64, 1, 1)` or similar.
- **Where**: `dispatchCompute` in `engine/render/` /
  `engine/prefabs/irreden/render/` without `voxelDispatchGridForCount`
  upstream.
- **Symptom**: wrong output, ~2× wasted workgroups, or the
  `MAX_COMPUTE_WORK_GROUP_COUNT_X` limit at scale.
- **Fix**: `voxelDispatchGridForCount()`, which packs into 2D past 1024.

### 8. CPU→GPU sync stall in the hot path

- **Pattern**: `glReadPixels`, `glGetBufferSubData`, `Buffer::getSubData`
  inline in a tick or render stage.
- **Where**: the cull-stats readback in `system_voxel_to_trixel.hpp` is the
  sanctioned exception (gated on `gpuStageTiming().enabled_`).
- **Symptom**: ~16 ms frames that should be fast; the preceding pass shows
  "full GPU utilisation" because the CPU is stalled.
- **Fix**: fence + multi-frame ring buffer, prior-frame reads (the cull-stats
  pattern), or persistent mapped buffers (`PERSISTENT | COHERENT`).

### 9. Shadow-feeder sweep inflates cull bounds at high zoom

- **Pattern**: `IRMath::shadowFeederIsoBounds(visible, sunDir, sweepDistance)`
  expands the cull AABB ~64 voxels along the sun; at zoom 4–8 the expansion
  dominates the visible AABB.
- **Where**: `system_voxel_to_trixel.hpp` cull-bounds construction;
  `engine/math/include/irreden/ir_math.hpp`.
- **Symptom**: the comparator's cull table 10–30× looser than the `1/zoom²`
  ideal; frame time scales with `sub² × visible_count`.
- **Fix**: keep feeder voxels on a depth-only path that skips the `sub²`
  colour / AO / entity-ID cost in `voxel_to_trixel_stage_2` while preserving
  their `trixelDistances` writes for the sun-shadow bake
  (`engine/render/CLAUDE.md` §"Lighting culling invariants").

### 10. O(sub²) per-shape tile generation in SHAPES_TO_TRIXEL

- **Pattern**: tile descriptors expand each shape's iso bounds by `sub` and
  divide by `kShapeTileSize`; many tiles never touch a shape pixel.
- **Where**: `system_shapes_to_trixel.hpp` tile generation.
- **Symptom**: `shapePass1` grows rapidly with zoom in SDF scenes
  (perf_grid `--mode sdf`); >100 tiles per shape at zoom 4.
- **Fix**: pre-cull tiles on the CPU against the shape's AABB, then full SDF
  intersection. Confirm with a `--mode sdf` matrix run first.

### 11. `numGroupsZ = subdivisions²` dispatch growth

- **Pattern**: `c_voxel_visibility_compact.glsl` writes
  `numGroupsZ = subdivisions²` for stage 2's indirect dispatch; at high zoom in
  FULL mode this can hit `MAX_COMPUTE_WORK_GROUP_COUNT_Z` (≥ 65535) before the
  X×Y limit.
- **Symptom**: wrong or missing stage-2 output at sub ≥ 16 with visible counts
  > 65535/sub², no GL error.
- **Fix**: cap effective subdivisions or repack Z into X×Y past a threshold;
  today the `getVoxelRenderEffectiveSubdivisions` clamp to 16 keeps it under
  the limit with little headroom.

## Math / shader hot paths

### 12. `glm::*` / `std::*` math primitives outside `engine/math/`

- **Where**: `.claude/rules/cpp-math.md` §"Detection" (sweep command and
  allowlist) and §"Live deviations".
- **Symptom**: not a runtime hotspot; blocks the IRMath implementation swap
  and breaks CPU↔GPU consistency (`glm::round` vs `IRMath::roundHalfUp`).
- **Fix**: the `IRMath::` wrapper, added to `engine/math/` first if missing.

### 13. Repeated math across shaders

- **Pattern**: the same iso-projection or SDF math in two or more `.glsl`
  files.
- **Symptom**: a maintenance hazard rather than a hotspot — a fix on one side
  is forgotten on the other.
- **Fix**: move to a shared include (`ir_iso_common.glsl`, `ir_constants.glsl`).
