# Plan: render: per-axis path is run-to-run non-deterministic at fixed pose

- **Issue:** #2255
- **Model:** fable — novel render-pipeline algorithm design across both backends (GLSL + Metal), on the hot per-cell voxel path, gated on a 64-bit-atomic feasibility question. This is stage-design work, not a bounded mechanical fix.
- **Date:** 2026-07-05

### Verified current state + confirmed repro

**Mechanism — confirmed by source trace (both backends), root cause differs from the issue body's first guess.** The distance store is NOT the bug: `writeDistanceTap` is a true `imageAtomicMin` (GL `c_voxel_to_trixel_stage_1.glsl:136-139`) / `atomic_fetch_min_explicit` (Metal `metal/c_voxel_to_trixel_stage_1.metal:20-36`), so `min(k,k)==k` — the distance plane is order-independent and byte-identical. The nondeterminism lives entirely in the **stage-2 color tap**:

`writeColorTap` (`c_voxel_to_trixel_stage_2.glsl:129-142`, Metal `metal/c_voxel_to_trixel_stage_2.metal:21-46`) is a **non-atomic read-compare-write**:
```glsl
int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;
if (voxelDistance == canvasDistance) {          // <-- guard is true for EVERY equal-key face
    imageStore(triangleCanvasColors, canvasPixel, voxelColor);
    imageStore(triangleCanvasEntityIds, canvasPixel, uvec4(packedEntityId, 0u, 0u));
}
```
When two distinct faces produce an **identical encoded key**, both pass the `==` guard and both `imageStore` — last-writer-wins, and that order is GPU-scheduling-dependent → the color and entity-id planes drift run-to-run while the distance plane stays identical. That is exactly the observed symptom (rotated color drift, cardinals identical).

**Why keys collide (per-axis only).** `encodeDepthWithFaceFrac` (`ir_iso_common.glsl:200-202`) packs a **full** 32-bit int: `rawDepth[31:10] (22b) | uFrac4[9:6] (4b) | vFrac4[5:2] (4b) | slot[1:0] (2b)` — **no spare bits**. On the subdivided per-axis path (`stage_1.glsl:531-538`), the store snaps `facePos_sub = round(worldAligned)` for **both** the iso pixel and `rawDepth`, so distinct sub-cell faces share the same pixel + rawDepth + slot and are disambiguated only by 8 frac bits. `fracToFrac4` (`ir_iso_common.glsl:206-217`) quantizes a continuous sub-cell offset to 4 bits with a hard clamp, so two offsets in the same 1/16 bucket (or both saturating at the ±0.5 clamp edges) encode byte-identically → tie. The cardinal path stays deterministic because it keys `encodeDepthWithFace` at the **integer** iso pixel, where (iso-pixel, iso-depth) is a bijection of integer (x,y,z) — unique key per cell, guard true for exactly one face.

**Empirical repro — the drift is an intermittent race and is contingent on the #2254 stack.** Ran the issue's exact repro on macOS/Metal against master (`b611d314`): `fleet-run IRPerfGrid --auto-screenshot --no-overlay --wave-amplitude 0`, **7 runs**. `zoom4_rot`/`zoom1_rot` were **byte-identical every run**, cardinals identical too — the race did **not** fire on master at plan time. #2254 (now merged) makes interior voxels report **all six faces exposed**, populating far more colliding faces on the per-axis path and surfacing the latent race. So the bug is real by construction on master, but the observable byte-drift needs the #2254 stack's denser face emission (or heavier GPU-scheduling variance than the plan host produced).

### Scope

Make the per-axis stage-2 color/entity-id tap select a **single deterministic winner** among faces that tie on the encoded distance key, so the color + entity-id planes are byte-identical run-to-run at a fixed pose — matching the distance plane and the cardinal path — without regressing the #1458 frac anti-jitter or cardinal byte-identity.

### Approach

Root cause: two faces with an equal key both satisfy the stage-2 `==` guard. Determinism requires **exactly one** face to pass. Because a single 32-bit atomicMin is the only cross-thread ordering primitive here and its key is full, the fix promotes the winner selection to a **single 64-bit atomicMin whose key carries a deterministic secondary tiebreak** below the depth field — resolving occlusion AND the tie in one atomic, so stage-2's guard is true for one face by construction. A second, independent atomic (a separate winner-id buffer) is explicitly rejected: "pick the deterministic winner among the depth-winners" cannot be done with two independent atomics without re-introducing the race.

To bound blast radius (keep the existing 32-bit `triangleCanvasDistances` channel that lighting / AO / the framebuffer gather read), add a **separate per-cell 64-bit winner-key scratch** used only by the per-axis store/tap, in parallel with the existing distance store:

1. **Stage 1 (`c_voxel_to_trixel_stage_1.glsl` + `.metal`, per-axis store sites `stage_1.glsl:522-524` and `:536-538`):** keep the existing `writeDistanceTap` for downstream depth, and additionally
   `atomicMin(winnerScratch64[cell], (int64(encodedDistance) << 32) | uint32(ordinal))`
   where `ordinal` is a **run-stable, per-face-unique** value (see gotcha — it must be the source **voxel pool index** folded with the face slot, NOT the compacted-list append index, which is itself run-unstable).
2. **Stage 2 (`writeColorTap`, `stage_2.glsl:129-142` + `metal/…:21-46`):** replace the `voxelDistance == canvasDistance` guard with `myKey64 == winnerScratch64[cell]` (recomputing `myKey64` from this face's `encodedDistance` + `ordinal`). Exactly one face matches the resolved min → deterministic color/id write. The stage-1→stage-2 dispatch barrier already exists, so the read sees a fully-resolved value.
3. **Binding:** the GPU bind-point budget (0–30) is full, so `winnerScratch64` must **transiently reuse** an existing binding for the per-axis store/tap dispatches (`Buffer::bindRange`/`bindBase` then restore — the sanctioned pattern in `engine/render/CLAUDE.md` §Gotchas), not claim a 31st index.
4. **Both backends line-for-line:** the store, encode, and tap are mirrored GLSL/Metal twins; port identically and verify each side.

**Documented fallback (only if step-0 feasibility fails):** if a target backend lacks 64-bit buffer atomics, resolve the winner in a **dedicated intermediate compute pass** — pass 2a does the deterministic `atomicMin`/`atomicMax` of the ordinal into a 32-bit per-cell winner-id scratch (a full separate dispatch, so the resolve completes before the color write), pass 2b writes color/id only where this face's ordinal == the resolved winner. Costs one extra dispatch per axis canvas; same determinism guarantee. This is a strict fallback, not a co-equal choice — take it only if the 64-bit-atomic check in step 0 fails on GL or Metal.

### Affected files

- `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` — add the 64-bit winner-key `atomicMin` beside `writeDistanceTap` at both per-axis store sites.
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1.metal` — Metal twin.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` — change `writeColorTap`'s guard to the 64-bit winner-key match.
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_2.metal` — Metal twin.
- `engine/render/src/shaders/ir_iso_common.glsl` — helper to build/split the 64-bit `(encodedDistance, ordinal)` key (keep `encodeDepthWithFaceFrac` intact for the 32-bit distance channel).
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` (+ any per-axis scratch owner) — allocate + clear the 64-bit winner scratch and wire the transient binding for the per-axis dispatches; GL & Metal resource paths.
- `engine/render/src/metal/metal_pipeline.cpp` — Metal threadgroup/binding wiring if a new buffer arg is added to the stage kernels.

### Acceptance criteria

- On current master (which now contains #2254), `fleet-run IRPerfGrid --auto-screenshot --no-overlay --wave-amplitude 0` run ≥10× produces **byte-identical `zoom1_rot` and `zoom4_rot`** across all runs (`shasum`/`cmp`). This is the positive test — verifying determinism, not just non-crash.
- Cardinal shots (yaw 0: `fit_grid`, `zoom1_origin`, `zoom4_pan`) remain **byte-identical to pre-change master** (the per-axis path isn't taken at cardinals; the 64-bit scratch must be a no-op there).
- `jitter_probe` on the per-axis rotation sweep stays `SMOOTH` — the fix must not regress #1458 anti-jitter.
- Both backends build + smoke clean (this PR will carry `fleet:needs-linux-smoke` + `fleet:needs-macos-smoke`).

### Gotchas

- **Step 0 — verify 64-bit buffer-atomic support on BOTH backends first.** GL needs `GL_ARB_gpu_shader_int64` + a 64-bit atomic extension (`GL_NV_shader_atomic_int64` / `GL_EXT_shader_atomic_int64`); Metal needs `atomic_min` on `atomic_ulong` in a `device` buffer (Apple-GPU / Metal-version gated). If either is missing, take the documented two-pass fallback — do not ship a backend-divergent primary.
- **The ordinal must be run-STABLE.** Do NOT use the compacted-visibility-list index — `c_voxel_visibility_compact` appends via atomics, so that index varies per run. Use the source **voxel pool index** (fixed at pool build) folded with the face slot; that is stable and unique per face.
- **Keep `encodeDepthWithFaceFrac` and the 32-bit distance channel untouched.** Downstream depth (framebuffer gather `normalizeDistance`, lighting/AO recovery) reads `triangleCanvasDistances` as R32I; widening THAT channel has a large blast radius. The 64-bit key is a separate winner-selection scratch.
- **Bind-point budget is full (0–30).** Transiently reuse an existing binding for the per-axis dispatch; never claim a 31st index (`engine/render/CLAUDE.md` §Gotchas).
- **Metal foreign-canvas R32I read caveat (#1640)** and the Metal `threadgroupSizeForFunctionName` requirement apply to any new per-axis kernel/buffer — mirror the existing per-axis wiring in `metal_pipeline.cpp`.

### Sibling / in-flight reconciliation

- **#2254** (merged) is the surface that exposes this race and touches the same per-axis path. The #2255 fix is **independent and complementary** (it hardens `writeColorTap`; #2254 changed face emission in `c_voxel_visibility_compact`). No code overlap; verification now possible on master since #2254 landed.
- No open PR modifies `writeColorTap` or `encodeDepthWithFaceFrac` — the fix surface is uncontended.

### One task or subtasks

One task. The change is a single coherent stage-design edit (store + tap + scratch, both backends). It does not decompose into a stack; the fallback is a branch within the same task, not a separate ticket.
