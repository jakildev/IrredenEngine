# Plan: render — close the remaining yaw-vs-cardinal frame gap (per-axis lighting + scatter stages)

- **Issue:** #2256
- **Model:** opus — heavy render-pipeline work, but it *applies an established indirect-compute-over-compacted-cells pattern* (already shipped for the voxel raster in `system_voxel_to_trixel.hpp`) to four more stages, rather than inventing a new algorithm. Multi-shader + GL/Metal parity + byte-identity/ordering judgment put it above sonnet; the existing reference impl keeps it below fable.
- **Date:** 2026-07-05
- **Blocked by:** PR #2254 (stack #2249 → #2251 → #2254). The settled baselines in this issue were measured on that stack; implement on top of it. Plan is complete now; start once the stack merges. (Merged as of implementation — commit `5b2079b3`.)

## Verified current state (source-checked, not assumed)

The per-axis (split-mode, rotating) path runs **four** GPU compute stages, each dispatched over the **full worst-case per-axis grid** — never clipped to what is actually populated — with empty cells skipped only *in-shader* by a `0x7FFFFFFF` sentinel test:

| stage | dispatch site | grid |
|---|---|---|
| `COMPUTE_VOXEL_AO` | `system_compute_voxel_ao.hpp:120-168` (`dispatchPerAxisAO`, grid @137-138, 3-axis loop @139-144) | `divCeil(axes.size_, 16)` × 3 |
| `COMPUTE_SUN_SHADOW` | `system_compute_sun_shadow.hpp:85-132` (grid @102-103, loop @104-109) | `divCeil(axes.size_, 16)` × 3 |
| `LIGHTING_TO_TRIXEL` | `system_lighting_to_trixel.hpp:224-280` (grid @242-243, loop @244-251) | `divCeil(axes.size_, 16)` × 3 |
| `RESOLVE_PER_AXIS_SCREEN_DEPTH` pass 1 (scatter) | `system_resolve_per_axis_screen_depth.hpp:138-149` | `divCeil(axisSize, 16)` × 3 |

`axes.size_` is the worst-case rotated footprint `≈ (2W, W+H)` (`per_axis_canvas.hpp:41`, stored `component_per_axis_trixel_canvases.hpp:67`). At zoom 8 + yaw the canvas is large, so this is `~3 × 4 = 12` full-`(2W)(W+H)` compute sweeps whose invocations mostly hit the sentinel and early-return — the un-scoped GPU work that the issue's profiling attributes the ~6 ms yaw delta to (raster stage-1 is already at/under cardinal parity there).

**The occupied-cell list needed to fix this already exists** but only feeds the *raster scatter*, not the compute stages:
- `c_per_axis_cell_compact.glsl` (`engine/render/src/shaders/`) scans one per-axis distance canvas and, per occupied cell (`rawDist < 0x7FFFFFFF`), `atomicAdd`s a count and appends the cell's linear index into a per-axis SSBO region (slots 25/26).
- Driven by `compactPerAxisCells()` (`system_trixel_to_framebuffer.hpp:438-480`), run in that system's **`beginTick`** (line 509) — i.e. at the **end** of the render pipeline, *after* the four compute stages above. It fills a `PerAxisCellDrawCommand` (draw-indirect) consumed by `drawElementsInstancedIndirect` in `drawPerAxisScatter` (`:337-354`).

**Indirect compute is already a first-class primitive on both backends** — `RenderDevice::dispatchComputeIndirect(buffer, offset)` (`render_device.hpp:30`), implemented at `opengl_render_impl.cpp:36` and `metal_render_impl.cpp:422`. A **direct reference implementation** of "GPU-count a per-axis compacted cell set → indirect-compute one workgroup per N cells indexing the list" already ships for the voxel raster at `system_voxel_to_trixel.hpp:564,577`, using a `VoxelIndirectDispatchParams` buffer (`ir_render_types.hpp:1193`, `numGroupsX/Y/Z + visibleCount`).

**Why byte-identity is the correctness gate:** downstream consumers of the per-axis lighting/depth only ever *read occupied cells* — the scatter draw draws only compacted cells (#1961), and the resolve scatter contributes only sentinel-guarded cells via order-independent `imageAtomicMin`. So dispatching a compute stage over *exactly* the occupied cells (instead of the full grid) removes only invocations that wrote nothing that is ever read ⇒ the final framebuffer is bit-for-bit unchanged. This is the load-bearing invariant the implementer must re-confirm (below), and it doubles as the test.

## Approach (single, committed)

Feed the four per-axis **compute** stages from the same occupied-cell compaction that already feeds the scatter, via indirect compute — so each stage processes only occupied cells rather than the full `(2W)(W+H)` grid.

1. **Move the per-axis compaction earlier in the pipeline.** It must run *after* per-axis routing (T2, inside `VOXEL_TO_TRIXEL_STAGE_1`) and *before* `COMPUTE_VOXEL_AO`, so the compacted list is available to the compute stages this frame. Preferred: run `compactPerAxisCells()` in `VOXEL_TO_TRIXEL_STAGE_1::endTick` (no new `SystemName`, no per-creation pipeline reorder). **Verify first** that the per-axis distance canvas is fully written by the end of STAGE_1's tick (STAGE_1 runs compact+stage1+stage2 in one tick per `render/CLAUDE.md`); if T2's routing barrier isn't complete there, fall back to a dedicated tiny system registered between `VOXEL_TO_TRIXEL_STAGE_1` and `COMPUTE_VOXEL_AO` (add its `SystemName` first, and insert it in every render pipeline that lists the per-axis stages — canonical list `creations/demos/shape_debug/main.cpp:511-533`). Remove the now-redundant late build in `TRIXEL_TO_FRAMEBUFFER::beginTick`; the scatter consumes the earlier-built list unchanged.
2. **Extend the compaction to also emit compute-indirect params.** It already writes a `PerAxisCellDrawCommand` (draw-indirect) per axis. Add a sibling `VoxelIndirectDispatchParams`-shaped write per axis: `numGroupsX = divCeil(occupiedCount, tile)`, `numGroupsY = numGroupsZ = 1`, plus `visibleCount = occupiedCount` for the in-shader bound guard. Reuse the slot-26 indirect-params region (`kBufferIndex_PerAxisCellIndirect = kBufferIndex_IndirectDispatchParams = 26`) with the same per-axis stride discipline the draw command already uses; if draw-args and dispatch-args can't share the exact bytes, append a second aligned per-axis block (mirror `kPerAxisSsboAlignBytes`).
3. **Convert the four compute dispatches to indirect over the compacted list.** For each of AO / sun-shadow / lighting / resolve-scatter, replace the `dispatchCompute(divCeil(axes.size_,16))` inside the 3-axis loop with `dispatchComputeIndirect(perAxisIndirectBuf, axisOffset)` (template: `system_voxel_to_trixel.hpp:564`). Each kernel gains a 1-D global invocation → `idx`; guard `if (idx >= visibleCount) return;`; read `cell = compactedList[axisBase + idx]`; decode `(x,y)` from the linear cell index; then run the **unchanged** per-cell body (`perAxisRoute` handling, `perAxisCellToWorld3D` recovery — no lighting-math change). Bind the per-axis compacted-list + params SSBOs before each loop. Kernels touched: `c_compute_voxel_ao.glsl`, `c_compute_sun_shadow.glsl`, `c_lighting_to_trixel.glsl`, `c_resolve_per_axis_screen_depth.glsl` — **and their `.metal` twins** (backend parity is mandatory; see Gotchas).
4. **Preserve the main-canvas image-rebind restore** after each 3-axis loop (`system_lighting_to_trixel.hpp:274-279` and the AO/sun-shadow siblings) — Metal's image-binding table persists across frames and dangles at the next cardinal `release()` (#1311). The restore must stay; converting the loop body must not drop it.

Leaves untouched: the scatter composite (already tight via #1961), the resolve **pass 2** blit (main-canvas-sized, not per-axis, not the target), all lighting/AO/shadow *math*, and the cardinal fast path (per-axis textures freed at cardinals ⇒ every guard false ⇒ byte-identical).

## Affected files
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` — relocate/relet `compactPerAxisCells()`; emit compute-indirect params alongside the draw command; drop the late build.
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel_stage_1.hpp` (or a new interstitial system) — host the early compaction call; if new system, add its `SystemName`.
- `engine/prefabs/irreden/render/systems/system_compute_voxel_ao.hpp`, `system_compute_sun_shadow.hpp`, `system_lighting_to_trixel.hpp`, `system_resolve_per_axis_screen_depth.hpp` — indirect-dispatch the per-axis loops; bind the list/params SSBOs.
- `engine/render/src/shaders/c_compute_voxel_ao.glsl`, `c_compute_sun_shadow.glsl`, `c_lighting_to_trixel.glsl`, `c_resolve_per_axis_screen_depth.glsl` — 1-D invocation → list index → cell decode; bound guard.
- The matching `engine/render/src/metal/…/*.metal` kernels — same transform (register threadgroup sizes, see Gotchas).
- `engine/render/include/irreden/render/ir_render_types.hpp` — a per-axis compute-indirect params struct/region if not shareable with `PerAxisCellDrawCommand`.
- `engine/system/include/irreden/system/ir_system_types.hpp` + the render pipeline lists — only if a new interstitial `SystemName` is chosen in step 1.
- `engine/render/src/shaders/c_per_axis_cell_compact.glsl` — write the dispatch-params if the compaction kernel emits them (vs a CPU-side derive).

## Cross-system audit (dispatch-iteration change to a shared per-axis surface)

Consumers of the per-axis compacted-cell list / indirect-params region (slots 25/26) and the per-axis compute outputs:
- **`drawPerAxisScatter`** (`system_trixel_to_framebuffer.hpp:337-354`) — already consumes the draw-command; must keep reading the *same* list after it's built earlier. No behavior change intended; verify the earlier build produces byte-identical scatter.
- **AO / sun-shadow / lighting** per-axis outputs (`ao_`, `sunShadow_`, per-axis color) — read only by the scatter at occupied cells; safe under the invariant.
- **`RESOLVE_PER_AXIS_SCREEN_DEPTH` → `BAKE_SUN_SHADOW_MAP`** (`system_bake_sun_shadow_map.hpp:292-314` reads `resolveDepth_`) — resolve pass 1 scatters occupied cells via `imageAtomicMin` into the slot-28 scratch; occupied-only dispatch is byte-identical (atomicMin is order-independent, empties contribute nothing). Pass 2 blit unchanged.
- **Slot budget (0–30 is full).** Slots 25/26 (compacted list / indirect params) and 28 (resolve scratch, aliased to `LightOcclusionGrid`/sun depth map) are already in use with a transient-rebind discipline. Any new params block must fit the existing slots or reuse transiently — do **not** claim a new permanent binding (`render/CLAUDE.md` "GPU buffer bind-point budget is full").

## Sibling / in-flight reconciliation
- **Two different "compact" passes — do not confuse them.** #2251/#2254 (the blocker stack) change the **stage-1 voxel compact** (`roundHalfUp` cell conversion; fully-interior voxel drop). This task changes the **#1961 per-axis cell compact** (`c_per_axis_cell_compact.glsl`) — a distinct pass. The issue's baselines already include #2254's stage-1 changes.
- **Sun-shadow bake PRs #2140 (analytic edge-aware coverage, fable, design-blocked) and #2204 (cardinal footprint-splat, design-blocked)** touch `BAKE_SUN_SHADOW_MAP` / shadow *quality*. This task changes only per-axis compute *dispatch iteration*, not shadow-sampling math, and is byte-identity-preserving — so it composes with whatever the bake produces regardless of those PRs' resolution. Coordinate ordering with the merger if both land near each other, but there is no design conflict.

## Acceptance criteria
- **Cardinal poses byte-identical** before/after (per-axis path is off at cardinals — confirms no fast-path regression). Use `--auto-screenshot` cardinal shots, byte-compared.
- **Yaw poses: no visual regression** — but **not** an exact byte-compare (see #2255 in Gotchas). Use a thresholded render-verify at `--yaw 0.35`, zoom 1/8, plus opus visual inspection for missing lit/shadowed regions (a dropped occupied cell shows as an unlit hole).
- **Measured GPU-time reduction** at the target pose: `fleet-run IRPerfGrid --auto-profile 300 --zoom 8 --yaw 0.35` shows a lower frame time and lower per-axis-stage GPU time than the #2254-stack baseline (13.8 ms / 81 FPS today). Report the before/after table in the PR.
- No new nondeterminism introduced (occupied-only dispatch must not change atomicMin tie outcomes — it can't, but confirm run-to-run yaw noise is no worse than the #2255 floor).

## Gotchas
- **#2255: per-axis output is already run-to-run nondeterministic at a fixed yaw.** Do **not** gate on exact yaw byte-identity — it will flake independent of this change. Byte-compare **cardinal** shots only; use thresholded compare + inspection for yaw (per the shader-A/B staged-swap workflow).
- **GL + Metal parity is mandatory** — every `.glsl` kernel edited here has a `.metal` twin that must get the identical index-through-list transform. New/changed Metal compute kernels default to a 1×1×1 threadgroup unless registered in the threadgroup-size map (`metal_pipeline.cpp`), which silently under-dispatches — register them.
- **Metal image-binding restore** after each per-axis loop (#1311) must be preserved verbatim; the loop-body rewrite is the exact place it's easy to drop.
- **Resolve scratch seeding** (`system_resolve_per_axis_screen_depth.hpp:96-99`) currently uses a CPU-sized staging vector + `subData` (a flagged live deviation). Not required by this task, but if the resolve pass is touched, migrate it to a GPU-side clear per `render/CLAUDE.md` ("Seed GPU-buffer sentinels GPU-side").
- **Ordering barrier:** the early compaction reads the per-axis distance canvas written by T2 — ensure a memory/image barrier between T2's write and the compaction read (STAGE_1 already barriers between its sub-dispatches; confirm the compaction sits after that barrier).
- **Empty-scene / zero-occupied guard:** `numGroups = 0` must issue a no-op indirect dispatch cleanly on both backends (the voxel reference at `system_voxel_to_trixel.hpp:564` already handles this — mirror it).

*(Note: `RESOLVE_PER_AXIS_SCREEN_DEPTH` runs GL-and-Metal-portable compute — no image atomics — so this is verifiable on the macOS/Metal host the baselines were taken on.)*
