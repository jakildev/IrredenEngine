# Plan: render: per-axis path is run-to-run non-deterministic at fixed pose

- **Issue:** #2255
- **Model:** fable — novel render-pipeline algorithm design across both backends (GLSL + Metal), on the hot per-cell voxel path. Stage-design work, not a bounded mechanical fix.
- **Date:** 2026-07-05 (planned) / 2026-07-06 (step-0 resolution absorbed, see addendum)

## Verified current state + confirmed repro

**Mechanism — confirmed by source trace (both backends); root cause differs from the issue body's first guess.** The distance store is NOT the bug: `writeDistanceTap` is a true `imageAtomicMin` (GL `c_voxel_to_trixel_stage_1.glsl`) / `atomic_fetch_min_explicit` (Metal), so `min(k,k)==k` — the distance plane is order-independent and byte-identical. The nondeterminism lives entirely in the **stage-2 color tap**:

`writeColorTap` (`c_voxel_to_trixel_stage_2.glsl`, Metal twin) is a **non-atomic read-compare-write**: every face whose encoded distance equals the resolved canvas distance passes the `==` guard and issues `imageStore` — last-writer-wins, GPU-scheduling-dependent → the color and entity-id planes drift run-to-run while the distance plane stays identical (rotated color drift, cardinals identical).

**Why keys collide (per-axis only).** `encodeDepthWithFaceFrac` packs a full 32-bit int: `rawDepth[31:10] | uFrac4[9:6] | vFrac4[5:2] | slot[1:0]` — no spare bits. On the subdivided per-axis path the store snaps `facePos_sub = round(worldAligned)` for both the iso pixel and `rawDepth`, so distinct sub-cell faces share pixel + rawDepth + slot and are disambiguated only by 8 frac bits; `fracToFrac4` quantizes to 4 bits with a hard clamp, so two offsets in the same 1/16 bucket (or both saturating at the clamp edges) encode byte-identically → tie. The cardinal path keys `encodeDepthWithFace` at the integer iso pixel, where (iso-pixel, iso-depth) is a bijection of integer (x,y,z) — unique key per cell, deterministic.

**Empirical repro:** intermittent race, contingent on the #2254 stack's denser face emission — now merged into master, so the race fires on current master.

## Scope

Make the per-axis stage-2 color/entity-id tap select a **single deterministic winner** among faces that tie on the encoded distance key, so the color + entity-id planes are byte-identical run-to-run at a fixed pose — matching the distance plane and the cardinal path — without regressing the #1458 frac anti-jitter or cardinal byte-identity.

## Approach (as resolved by the opus step-0 feasibility pass, 2026-07-06)

**Step 0 (feasibility) — RESOLVED: no 64-bit atomics available → the two-pass 32-bit fallback is REQUIRED.** The GLSL shaders use zero `#extension` directives and no int64 anywhere (all `#version 450 core`); introducing `GL_ARB_gpu_shader_int64` + a 64-bit atomic extension would be a new GPU-gated dependency — off the table. The two-pass design uses only primitives already proven on BOTH backends.

- **Winner-id per-cell scratch (32-bit)**, resolved by `imageAtomicMin(winner, cell, voxelIndex)` among the depth-winning faces (those passing `voxelDistance == canvasDistance` after stage-1's `atomicMin` settles). `voxelIndex` (`= compactedVoxelIndices[compactedIdx]`, the source pool index) is **run-stable and unique per per-axis tap** — exactly one face per voxel emits per axis route (`(faceId>>1)!=axis` filter), and the #2157 dual-emit is NOT taken on the per-axis store. So `voxelIndex` alone is a valid deterministic tiebreak; no ordinal folding needed.
- **Stage-2 per-axis** `writeColorTap` sites (the 2 in the `perAxisRoute != 0` branch) gain `&& voxelIndex == winner[cell]` → exactly one (min-index) depth-winner writes color/id.
- **Resolve dispatch** inserted between stage-1 and stage-2 in `dispatchPerAxisCanvases` (stage1 → barrier → **resolve** → barrier → stage2). Reuse stage-1's minimal per-axis branch under an appended `resolveMode` uniform (do the winner `atomicMin` instead of `writeDistanceTap`), avoiding a third geometry copy. Cost = one extra dispatch per axis (plan-accepted).
- **Metal wrinkle — RESOLVED:** only ONE image-atomic-scratch slot exists (`kMetalImageAtomicScratchSlot = 16`), already used by the distance read. The winner is a **manually-managed `atomic_uint` buffer at a transiently-reused buffer index** (mirror the `kBufferIndex_PerAxisResolveScratch` (= `kBufferIndex_LightOcclusionGrid`, slot 28) precedent; `system_resolve_per_axis_screen_depth.hpp` is the transient-bind pattern). On GL the winner is an r32ui image on a free image unit (4).
- **Both backends line-for-line:** the store, resolve, and tap are mirrored GLSL/Metal twins; port identically and verify each side.

## Affected files

- `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` — resolve-mode winner `atomicMin` beside the per-axis store sites.
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1.metal` — Metal twin.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` — winner guard on the 2 per-axis `writeColorTap` sites.
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_2.metal` — Metal twin.
- FrameData — append `resolveMode` (sanctioned end-append).
- `C_PerAxisTrixelCanvases` — winner resource (alloc + per-axis clear to `0xFFFFFFFF`).
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — resolve dispatch + transient bind.
- `engine/render/src/metal/metal_pipeline.cpp` — only if a new kernel is added; reusing stage-1 avoids it.

## Acceptance criteria

- On current master (contains #2254): `fleet-run IRPerfGrid --auto-screenshot --no-overlay --wave-amplitude 0` run ≥10× produces **byte-identical `zoom1_rot` and `zoom4_rot`** across all runs (`shasum`/`cmp`).
- Cardinal shots (yaw 0: `fit_grid`, `zoom1_origin`, `zoom4_pan`) remain **byte-identical to pre-change master** (per-axis path not taken at cardinals; the winner scratch must be a no-op there).
- `jitter_probe` on the per-axis rotation sweep stays `SMOOTH` — no #1458 anti-jitter regression.
- Both backends build + smoke clean (PR carries `fleet:needs-linux-smoke` on the GL side; authored + verified on macOS/Metal).

## Gotchas

- **The winner id must be run-STABLE.** Do NOT use the compacted-visibility-list index — `c_voxel_visibility_compact` appends via atomics, so that index varies per run. Use the source voxel pool index.
- **Keep `encodeDepthWithFaceFrac` and the 32-bit distance channel untouched.** Downstream depth (framebuffer gather `normalizeDistance`, lighting/AO recovery) reads `triangleCanvasDistances` as R32I; the winner scratch is a separate selection channel only.
- **Bind-point budget is full (0–30).** Transiently reuse an existing binding for the per-axis dispatch; never claim a 31st index (`engine/render/CLAUDE.md` §Gotchas).
- **Metal foreign-canvas R32I read caveat (#1640)** and the Metal `threadgroupSizeForFunctionName` requirement apply to any new per-axis kernel/buffer — mirror the existing per-axis wiring.
- The stage-1/2 source comments document 3 prior incidents where a subtle change reshuffled the Metal per-axis tie-winner and broke byte-identity — cardinal byte-identity A/B is mandatory before push.

## Sibling / in-flight reconciliation

- **#2254 merged** (master `5b2079b3`) — the acceptance repro observes the drift on master directly; no stacking needed.
- No open PR modifies `writeColorTap` or `encodeDepthWithFaceFrac` — the fix surface is uncontended.

## One task or subtasks

One task. Single coherent stage-design edit (store + resolve + tap, both backends); the two-pass design is the resolved primary path, not a fork.
