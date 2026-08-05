## Plan: render: Metal feeder-ring depth never reaches trixelDistances — ring Hi-Z is sentinel, #2298 per-voxel cull no-ops on Metal

- **Issue:** #2488
- **Model:** opus — the approach below is fully committed and bounded, but verification is judgment-bearing (shadow-delta interpretation, reference re-bakes) and the work must run on a macOS/Metal host end to end.
- **Date:** 2026-08-04

### Scope

Materialize the Metal image-atomic scratch buffer into the `trixelDistances` texture after stage 1, restoring GL parity of the distance channel — feeder ring included — so every texture-reading consumer sees feeder depth on Metal: the sun bake (`c_bake_sun_shadow_map`), `COMPUTE_VOXEL_AO`, and `COMPUTE_DISTANCE_HIZ` (which is what un-no-ops the #2298 per-voxel cull's Metal leg).

### Verified current state

Code-pinned (origin/master @ 34c7f7f46):

- Stage 1 (visible + feeder dispatches) atomic-mins depth into the per-texture scratch **buffer** only (`writeDistanceTap`, `c_voxel_to_trixel_stage_1_body.metal:44-58`; scratch contract `metal_runtime.hpp:141-160`).
- The only writer of the distance **texture** on Metal is stage 2's winner tap (`writeColorTap`, `c_voxel_to_trixel_stage_2_body.metal:37-62`), and it early-returns for shadow feeders (the #1740 skip, same file ~391-421). Ring texels therefore stay at the 65535 clear sentinel in the texture.
- `c_build_distance_hiz.metal` reads the texture (`access::read`), is not a scratch consumer (`functionUsesImageAtomicScratch`, `metal_pipeline.cpp:120-129`), so the ring Hi-Z is sentinel — measured by the issue's staged probe (98.4% of feeders at sentinel Hi-Z, PR #2475 branch @ 5d2b5833) and the capture-0 table.
- The GLSL backend has no gap: stage 1's `imageAtomicMin` writes the texture directly, ring included.

**Correction to the issue's bake claim.** `c_bake_sun_shadow_map.metal` reads `trixelDistances` as a plain `texture(0)` — there is no scratch path into the bake. So deep-ring feeder depth does **not** reach the bake on Metal today either; the force-cull A/B's interior-shadow delta is explained by (a) margin-band feeders (inside the +4-iso-px `visibleIsoBounds` margin, which stage 2 fully writes) and (b) feeders winning depth elections at edge texels whose tap is then skipped — the issue's own secondary observation. The stage-2 #1740 comment's premise "Stage 1 wrote its full-res depth (the bake + AO read only trixelDistances)" is GL-authored and false on Metal. The gap is therefore wider than the Hi-Z: Metal currently loses sun shadows from off-screen casters beyond the margin band. The approach below fixes both consumers and does not depend on this correction being the whole story — wherever depth already flows, the resolve writes identical values (no-op by construction).

This also settles the issue's fix-direction choice: direction 2 (a scratch-reading `c_build_distance_hiz` variant) repairs only the Hi-Z, leaves the bake's ring gap in place, and adds a scratch-consumer registration plus a diverging shader surface. Direction 1, generalized to the device layer, is committed below.

### Approach

One approach, no alternatives left open: a backend-neutral device primitive plus one guarded call site. No new shaders, no `functionUsesImageAtomicScratch` entry, no slot-16 alias exposure (#1619).

1. **`render_device.hpp`** — add a defaulted no-op virtual (precedent: the `GpuTimestamp` family defaults in the same interface):
   `virtual void resolveImageAtomicScratch(const Texture2D *texture) {}`
   Semantics documented at the declaration: "make the texture's contents reflect its image-atomic state — no-op on backends whose image atomics write the texture directly (GL); a scratch→texture materialization on Metal." The OpenGL impl is untouched.
2. **`metal_render_impl.cpp`** — override: look up the sibling scratch (`lookupImageAtomicScratchBuffer`), return if null, else encode one full-texture `copyFromBuffer(scratch, 0, width*4, totalBytes, (w,h,1), texture, 0, 0, origin(0,0))` through `createBlitEncoder` (so an active `GpuSubStageScope` samples it, #2280) — byte-for-byte the geometry the `clearTexImage` mirror blit already uses at `metal_render_impl.cpp:761-780`, in the reverse direction.
3. **`system_voxel_to_trixel.hpp`** — call `IRRender::device()->resolveImageAtomicScratch(triangleCanvasTextures.getTextureDistances())` inside the per-canvas `voxelStage1` sub-scope, immediately after the feeder dispatch's `memoryBarrier` (~line 1745). Guard: the non-detached single-canvas cardinal branch, and ring non-empty — spelled as the direct comparison `gpuVp != visibleVp` on the two viewports the tick already computes at ~1336-1364 (the #1740 comment pins that with sun shadows off the sweep is zero and the two are equal). That guard makes the shadows-off, detached, GUI, and per-axis paths structurally resolve-free.
4. **Whole-canvas resolve, not ring-only rects.** After stage 1, the scratch is exactly the distance state GL's texture holds at the same pipeline point: visible-domain winner texels are rewritten with identical values (stage 2's winner test IS scratch equality), never-written texels resolve sentinel→sentinel (the clear mirrors into the scratch, `metal_render_impl.cpp:777`), and the only actually-changed texels are those GL has and Metal lacks. This removes all iso→canvas-pixel rect math from the design. Encoder order is the engine-wide sequencing guarantee (the #1436 contract); stage 2 is order-independent of the blit since it re-tests depth from the scratch, not the texture.
5. **Comment/doc maintenance in the same PR:** amend the #1740 comment in `c_voxel_to_trixel_stage_2_body.metal` (its premise becomes true on Metal via the resolve — say so); update `engine/render/CLAUDE.md`'s #1640 bullet to name the primitive as the sanctioned own-canvas materialization (distinct from the foreign-canvas resolve-then-bake rule, which stands).

### Affected files

- `engine/render/include/irreden/render/render_device.hpp` — new defaulted virtual
- `engine/render/src/metal/metal_render_impl.cpp` — Metal override (blit encoder copy)
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — guarded call after the feeder dispatch
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_2_body.metal` — comment amendment only
- `engine/render/CLAUDE.md` — #1640 note
- `test/render/gpu_compute_dispatch_test.cpp` — new headless resolve test (registered in `test/CMakeLists.txt:62` already; no build wiring needed)

### Acceptance criteria

1. **Headless GPU unit test (positive-fire, deterministic, macOS-runnable).** In `gpu_compute_dispatch_test.cpp` (vehicle A, `bootstrapHeadlessRenderDevice`): create an R32I texture, `bindAsImage` (pairs the scratch), seed a known pattern into the scratch via `lookupImageAtomicScratchBuffer(...)->contents()`, clear the texture to sentinel, call `resolveImageAtomicScratch`, read back, assert the pattern landed. Negative control in the same test: an identical texture without the resolve call reads back all-sentinel (proves the assert can fail). Skips cleanly when no Metal device, like the existing cases.
2. **Scene-level positive fire (Metal, fixture exists on master).** `IRPerfGrid --mode voxel_set --no-overlay --subdivision-mode none --wave-freeze --wave-amplitude 5` at zoom 8 — the issue's own dense harness (47,835 feeders at that zoom, shadows on by default): capture master-vs-branch screenshots, `tools/img_diff` reports a non-zero pixel delta concentrated in shadow regions (off-screen casters now cast). **Byte-identity control:** the same A/B with `--no-sun-shadows` (`perf_grid/main.cpp:624`, #1812) reports **zero** differing pixels — the guard never dispatches the resolve with an empty ring.
3. **Suite control:** `IrredenEngineTest` at 1493/1494 — `SaveTrait.InventoryIsComplete` is the pre-existing #2834 failure, not this PR's.
4. **GL untouched:** no `opengl/` diff; the default virtual is a no-op. Normal reviewer-side `fleet:needs-linux-smoke` covers the cross-host build+run.
5. **macOS references:** run `render-verify` on the macOS reference set; any shot whose scene has sun shadows plus off-screen casters may legitimately shift toward GL — re-bake exactly those references in the same PR, with the before/after pairs and the shadows-off control cited in the PR body.
6. **Perf note in the PR:** `--auto-profile` pre/post on the same scene — the blit's bandwidth matches the existing every-frame `clearTexImage` blit (~4 bytes/texel), so the `voxelStage1`/`canvasClear` GPU rows are expected unchanged within noise; cite the measured rows.

### Gotchas

- **The resolve imports GL's depth-without-color edge texels to Metal.** Texels where a feeder won the election and stage 2 skipped its tap currently read sentinel on Metal but carry depth on GL; post-fix both backends agree. Any visible artifact there is the pre-existing, backend-agnostic #1740 margin-adequacy question — the issue routes it to the #2298 GL gate run; do not scope-creep it here.
- **In-flight collision: PR #2475 (#2298)** touches the same tick region. It is currently parked (blocked on #2385); whichever lands second takes a trivial rebase. Behavioral interplay to note on its thread when this merges: its "byte-identical-safe on Metal, captures nothing" claim stops holding — the widened per-voxel cull will actually capture on Metal, and its Metal-side gate numbers must be re-measured on the resolved baseline. Master's chunk-occlusion cull (off by default) likewise starts seeing real ring Hi-Z on Metal — same data GL already feeds it.
- **Use `createBlitEncoder`, never a raw `blitCommandEncoder`** — sub-stage timing attribution (#2280) rides on it.
- **Do not add any kernel to `functionUsesImageAtomicScratch`** — there is no kernel in this design; that list and the slot-16 alias stay untouched.
- **Guard placement:** the feeder dispatch encodes unconditionally (empty when zero feeders), so the resolve's guard must come from the CPU-side viewport comparison, not from any per-dispatch feeder count (that count is GPU-side in indirect struct 1).
- Engine-public plan: no game-repo references anywhere in the PR (baseline isolation rule).

One task, no stack — the device primitive, call site, test, and reference re-bakes land as a single PR (`Closes #2488`).

