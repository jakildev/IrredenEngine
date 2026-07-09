## Plan: render: per-axis view-visibility overflow lane — view mask + overflow append + scatter draw (C1)

- **Issue:** #2333
- **Model:** fable
- **Date:** 2026-07-08
- **Epic:** #2331 — see `~/.fleet/plans/issue-2331.md` for full context
- **Blocked by:** #2332

### Verified current state

- `c_voxel_to_trixel_stage_1.{glsl,metal}` per-axis branch (`perAxisRoute != 0`): store key `perAxisBase + pos3DtoPos2DIso(facePos)`, value `encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, …)`, `imageAtomicMin` into the distance image (binding 1). `resolveMode == 1` (#2255) is an existing second dispatch over the IDENTICAL face geometry that elects a deterministic winner into `PerAxisWinnerScratch` (SSBO binding 28, transiently reused — `kBufferIndex_PerAxisResolveScratch`).
- `v_peraxis_scatter.glsl` / `metal` twin: one instanced quad per canvas cell; origin recovered by `isoPixelToPos3D(cell − perAxisBase, rawDepth)`; composite depth is the yawed planar metric (#1457/#1370); conservative dilation margins (#1883).
- Bindings 5/6 (positions/colors) are re-uploaded per canvas — by `TRIXEL_TO_FRAMEBUFFER` time they hold whichever canvas dispatched last (the documented reason the scatter reads canvas textures, not SSBOs). **Overflow entries therefore must carry payload, not voxel indices.**
- GPU bind-point budget 0–30 is full (`engine/render/CLAUDE.md` gotcha); binding 28 is the sanctioned transient slot during the per-axis window.
- Root-cause math in the epic plan: the store keeps only the cardinal-visible set; coset pairs `t·(1,1,1)` separate on screen by `t·(−2sinθ, 2−2cosθ)` under residual θ, and the `x+y+z` election sign-inverts past 120° full yaw.

### Affected files

- `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` + `metal/c_voxel_to_trixel_stage_1.metal` — `resolveMode` 2 (view-mask write) and 3 (overflow test + append).
- `engine/render/src/shaders/v_peraxis_scatter.glsl`, `f_peraxis_scatter.glsl` + Metal twins — overflow-instance branch.
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — dispatch sequencing, scratch sizing/lifecycle, finalize dispatch.
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` — indirect overflow draw.
- `engine/render/include/irreden/render/ir_render_types.hpp` — scratch layout constants (no new binding index).

### Approach

1. **First implementation step: map the per-axis-window dead-binding set** (audit every `bindBase`/`bindRange` between the per-axis store and the scatter). Target layout: ONE enlarged scratch buffer bound at 28, with 256-B-aligned `bindRange` windows per role: `[winnerIds: cells·4 B][viewMask: cells·4 B][counter+indirect args: 32 B][overflow entries: cap·12 B]`. Cap = canvas cells / 4, floor 65536 entries.
2. **Mode 2 (view mask):** per face, `atomicMin(viewMask[yawedCell], quantYawedDepth)` where `yawedCell = roundHalfUp(pos3DtoPos2DIsoYawed(facePos, visualYaw)) − viewMaskBase` and `quantYawedDepth` uses the SAME function+quantization mode 3 compares with (exact-equality safe). Runs for all three axis routes into the shared mask (view visibility competes across axes).
3. **Mode 3 (overflow append):** a face appends iff `quantYawedDepth ≤ viewMask[yawedCell] + ε` (ε = one half-voxel quantization step; over-emit safe — framebuffer depth test cleans up) AND its encoded key ≠ its cardinal cell's settled distance value (the same match test `resolveWinnerTap` uses, negated). Entry: `uint0` = camera-anchored world cell, 3×10-bit signed (anchor uniform shared with the scatter; out-of-range → count-and-drop); `uint1` = colorPacked; `uint2` = faceId(3) | fracU(4) | fracV(4) | slot(2). Atomic counter; at cap, count drops and raise a one-shot CPU warn (never silent).
4. **Dispatch order per rotating frame:** stores (3 routes) → barrier → mode 2 (3 routes) → barrier → mode 3 (3 routes) → barrier → existing mode 1 election (3 routes) → stage 2. Mode 1 only reads the settled distance image, so moving it after mode 3 cannot change its winners.
5. **Finalize:** one-thread dispatch writes `instanceCount = min(counter, cap)` into an indirect-draw args region (GL: `GL_DRAW_INDIRECT_BUFFER` target — not an SSBO slot, no budget cost; Metal: indirect buffer passed at draw time).
6. **Scatter:** after the three per-cell draws, one indirect instanced draw with a `uOverflowMode` uniform: vertex fetches the entry by instance id, unpacks anchor-relative world cell → facePos, builds the same `faceSpanCorner` deformed-quad footprint and yawed planar depth as the cell path; fragment takes the vertex color (albedo this child), writes background entity-id (documented drift: hover on revealed slivers).
7. **Fog parity:** modes 2/3 must replicate the per-axis branch's fog early-outs exactly, or the view mask admits fog-hidden faces.
8. **Lifecycle:** scratch allocated lazily with the per-axis canvases and freed at the cardinal (mirror `syncAllocationToCameraYaw`), so a non-rotating scene pays nothing.

### Acceptance criteria

- #2332 wave sweep green at quadrant-0 residuals (coverage ≥ 0.99 / hole parity vs cardinal), Metal + OpenGL.
- #1880 dense sweep unregressed; cardinal / yaw-0 md5 A/B byte-identical.
- Overflow cap warn counter = 0 on demo scenes; synthetic cap test logs the dropped count once.
- render-debug-loop before/after + ROI crops attached.

### Gotchas

- Memory barriers between every dispatch pair above; Metal has no second image-atomic slot — the view mask MUST be SSBO device atomics, not a texture image (same rationale as the #2255 scratch). The #1640 foreign-R32I caveat does not apply (main-canvas in-tick pattern).
- ε too small re-opens holes at quantization boundaries; ε too large over-emits (cost, not correctness).
- Do not touch the single-canvas path or the mode-0/1 semantics; #2255 determinism must keep passing.
- The 30-bit position pack assumes content within ±511 cells of the anchor — the compact cull already bounds survivors to the widened viewport; assert the bound in the pack and drop-with-count otherwise.

### Sibling / in-flight reconciliation

- Assumes #2332 landed (the acceptance harness).
- **PR #2296** (#2207 riser-polarity carrier) edits the same per-axis encode in stage 1/2 — coordinate merge order; no semantic interaction (overflow entries carry explicit `faceId`, unaffected by the polarity bit), but expect textual conflicts in stage-1.
- **PR #2325** (#2258 Step B feeder dispatch partition) touches stage dispatch sizing — same file, coordinate merge order.
- #2334 (C2) lights the entries this child leaves albedo; keep the entry layout's color lane rewritable in place.

### Verification

`fleet-build --target IRPerfGrid` + `IRShapeDebug`; run #2332's tier (quadrant 0) both backends (backend-parity skill for the lagging host); md5 A/B at yaw 0; `--checkerboard` ROI crops at 10°/30°; #1880 sweep; gpu sub-stage rows before/after (#2281) recorded in the PR body.

