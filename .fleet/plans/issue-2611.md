# Plan — render(metal): Metal API validation aborts at startup — stencil attachment vs pipeline-state pixelFormat mismatch

- **Issue:** #2611
- **Model:** opus — render-backend internals; the fixes below are measured and near-mechanical, but a surprise (a new assert on a moved master, a render-verify diff) needs render judgment
- **Date:** 2026-08-07

## Verified current state (measured 2026-08-07, macOS/Metal, master `12b8a49c8`)

The repro was confirmed live and the full candidate fix was **probed end-to-end
on the authoring host at plan time** — the phase-0 measurement is already done;
nothing below rests on an asserted mechanism.

- Issue repro command → `RESULT=CRASH exit=134` (SIGABRT), assert verbatim:
  `-[MTLDebugRenderCommandEncoder setRenderPipelineState:]:1639 … For stencil
  attachment, the renderPipelineState pixelFormat must be MTLPixelFormatInvalid,
  as no texture is set.`
- Control without the env vars → `RESULT=CLEAN exit=0`.
- The filed abort is **not the only validation defect** — the process dies at
  the first assert, so each one masks the next. Iterating
  fix → rebuild → re-run enumerated **four distinct defects** across the two AC
  demos; with the four minimal fixes below, **both demos run fully clean** under
  `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER_ERROR_MODE=assert`:
  `IRPerfGrid` (the issue's exact command) → `RESULT=CLEAN exit=0`;
  `IRShapeDebug --auto-screenshot` → `RESULT=CLEAN exit=0`.

**Defect 1 — pipeline declares a stencil format no pass attaches (the filed
defect).** `MetalShaderPipelineImpl::getRenderPipelineState`
(`engine/render/src/metal/metal_pipeline.cpp`) sets BOTH
`setDepthAttachmentPixelFormat` and `setStencilAttachmentPixelFormat` to the
depth format whenever depth is present; `createRenderEncoder`
(`metal_render_impl.cpp`) attaches color + (optionally) depth, never a stencil
attachment. Contract audit, each leg checked: **stencil is unused engine-wide**
— zero `glStencil*` / `GL_STENCIL*` calls in first-party code (generated
`gl_wrap/` excluded), the Metal `DepthStencilDescriptor`s configure depth
compare/write only, and no `stencilAttachment` is ever set on any render pass.
The intended contract is **depth-only**, so of the issue's two AC options the
correct one is: the pipeline stops declaring stencil. (Attaching the stencil
aspect instead would add pass-side stencil plumbing — load/store actions, clear
values — for a feature nothing uses.) Also measured: attaching a
combined-format texture as depth-only is validation-legal — the encoder creates
fine; only the pipeline-state declaration trips.

**Defect 2 — `FrameDataFramebuffer` CPU sizeof 72 vs MSL constant-buffer
size 80.** Assert: `Vertex Function(v_framebuffer_to_screen): argument
frameData[0] from Buffer(2) with offset(0) and length(72) has space for 72
bytes, but argument has a length(80).` The CPU struct
(`ir_render_types.hpp`) is `mat4 + vec2` = 72 bytes (glm, no tail padding);
MSL rounds the struct to its 16-byte alignment → 80. GL never enforces UBO
sizes at draw, so only the layer sees it — exactly the #2350/#2360/#1812
CPU↔GPU struct-mismatch class the issue's "Why it matters" cites. Consumers:
exactly two files (`system_framebuffer_to_screen.hpp` + the types header,
grepped); allocation and upload both use `sizeof`
(`kFramebufferFrameDataUniformBufferSize`), so a tail pad propagates
automatically. std140 offsets are unchanged (pad is tail-only); GL allocates 8
more bytes it never reads.

**Defect 3 — CPU readback of the combined depth-stencil format is
disallowed.** Assert: `_validateGetBytes … CPU access for this texture with
pixel format MTLPixelFormatDepth32Float_Stencil8 is disallowed.` The
default-pivot composite-depth readback in `RenderManager::beginFrame` (#2547)
calls `getBytes` on the framebuffer depth texture every frame; Metal
categorically forbids `getBytes` on combined depth-stencil formats. The
readback already assumes 4 bytes/px — i.e. it was written for pure 32F depth
(the `bytesPerRow(4)` in the assert). Fix: remap
`TextureFormat::DEPTH24_STENCIL8 → MTL::PixelFormatDepth32Float` in
`toMetalTextureFormat` (`metal_texture.cpp`). Justification, each leg measured:

- stencil aspect unused engine-wide (defect 1's audit);
- the combined format's depth aspect is already 32-bit float → bit-identical
  depth precision, zero behavioral delta;
- Metal depth textures here are `TextureUsageRenderTarget`-only +
  `StorageModeShared` — nothing samples them, and `getBytes` on
  Depth32Float+Shared is legal (measured green end-to-end);
- the pipeline's depth declaration follows automatically — the recorded native
  format flows from the texture itself through
  `bindMetalFramebufferRenderTarget`;
- after the remap the tree has **zero** remaining `Depth32Float_Stencil8`
  references (grepped);
- the `metal_texture.cpp` special-cases key on the `TextureFormat` enum value,
  not the MTL format — unaffected.

Rejected alternative: keep the combined format and add a blit-the-depth-aspect
readback path — more code and a second readback mechanism, all to preserve a
stencil aspect nothing uses. GL is untouched (`GL_DEPTH24_STENCIL8` stays; the
enum name was already approximate on Metal — 32F+8, not 24+8). Do **not**
rename the enum in this task (cross-backend cosmetic churn, out of scope).

**Defect 4 — `SHAPES_TO_TRIXEL` pass 0 dispatches with a stale texture at a
declared kernel slot.** Assert: `Compute Function(c_shapes_to_trixel): The
pixel format (MTLPixelFormatR32Sint) of the Texture bound at index 0 is
incompatible with the data type (MTLDataTypeFloat) of the Texture parameter
(triangleCanvasColors [[Texture(0)]]).` The system's two-pass tick
(`system_shapes_to_trixel.hpp`) binds only distances→image 1 before the pass-0
(depth) dispatch and binds colors→0 / entityIds→2 only before pass 1. Metal
validates **every declared kernel argument** at dispatch — including ones the
`passIndex == 0` branch never reads — and unit 0 still holds an R32I distance
texture from an earlier pass (`COMPUTE_SUN_SHADOW`, `BAKE_SUN_SHADOW_MAP`, and
`BUILD_DISTANCE_HIZ` all bind R32I at image 0; sticky-table residency per
`engine/render/CLAUDE.md` §"Sampler and image binds…"). GL never type-checks
this, so it is layer-only. Fires in `IRShapeDebug` (SDF shapes dispatch the
kernel); `IRPerfGrid` never does, which is why its run is clean without this
fix. Fix: move the colors + entityIds binds above the pass-0 dispatch so all
three declared slots are bound for both passes — semantics-neutral on both
backends (same binds, earlier; pass 0's shader branch touches neither), cost is
two bind-table writes.

## Approach

One PR, five steps:

1. Commit this plan as `.fleet/plans/issue-2611.md` (first commit, #1932).
2. Apply the four fixes. Exact probe diff, verified green end-to-end
   2026-08-07 (line numbers as of `12b8a49c8`; locate by symbol):

   ```diff
   --- a/engine/render/src/metal/metal_pipeline.cpp
   +++ b/engine/render/src/metal/metal_pipeline.cpp
   @@ -350,7 +350,6 @@   (in getRenderPipelineState)
            if (depthPixelFormat != MTL::PixelFormatInvalid) {
                descriptor->setDepthAttachmentPixelFormat(depthPixelFormat);
   -            descriptor->setStencilAttachmentPixelFormat(depthPixelFormat);
            }
   --- a/engine/render/include/irreden/render/ir_render_types.hpp
   +++ b/engine/render/include/irreden/render/ir_render_types.hpp
   @@ struct FrameDataFramebuffer {
        mat4 mvpMatrix;
        vec2 textureOffset; // TODO: Update in texture scroll system and make
        // a frame data component as well or add as field for shader program
   +    vec2 metalPad_;
    };
   --- a/engine/render/src/metal/metal_texture.cpp
   +++ b/engine/render/src/metal/metal_texture.cpp
   @@ (toMetalTextureFormat)
            case TextureFormat::DEPTH24_STENCIL8:
   -            return MTL::PixelFormatDepth32Float_Stencil8;
   +            return MTL::PixelFormatDepth32Float;
   --- a/engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp
   +++ b/engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp
   @@ (tick, before the pass-0 dispatch)
                canvasTextures.getTextureDistances()
                    ->bindAsImage(1, TextureAccess::READ_WRITE, TextureFormat::R32I);
   +            canvasTextures.getTextureColors()
   +                ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
   +            canvasTextures.getTextureEntityIds()
   +                ->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);
   ```

   Write constraint-stating comments at the two non-obvious sites (state the
   invariant, not the change): on `metalPad_` — MSL rounds this struct to its
   16-byte alignment (80 bytes) and Metal validates the bound buffer length
   against it, so `sizeof(FrameDataFramebuffer)` must stay a multiple of 16; on
   the pass-0 binds — Metal validates every declared kernel argument at
   dispatch, so pass 0 must bind colors/ids it never reads or a stale R32I at
   unit 0 aborts under `MTL_DEBUG_LAYER=1`. Also delete the note at pass 1 if
   it becomes redundant once the binds move.
3. Build both demos (`fleet-build --target IRPerfGrid`, `--target
   IRShapeDebug`) and run the acceptance commands below.
4. Render-PR duties: `render-verify` (expect zero diffs) + screenshot pair via
   `attach-screenshots` (expect byte-identity; say so in the PR body). No GLSL
   or `.metal` shader edits, so no `backend-parity` follow-up and no new
   kernel-registry entries.
5. Master will have moved past `12b8a49c8`. If a **fifth** assert surfaces:
   same validation-surface class (short `constant` buffer, partial bind set,
   attachment-format mismatch) → fix in-PR with the same minimal-fix
   discipline; different mechanism (e.g. a genuine shader-validation
   out-of-bounds inside a kernel) → stop, comment the measured assert on this
   issue, file it through the agent-approved lane, and land these four fixes
   with the AC re-measured. Never satisfy the AC by weakening the layer —
   `MTL_DEBUG_LAYER_ERROR_MODE=ignore` disables API validation entirely
   (measured in the issue body), so it is not a report-but-continue mode.

## Affected files

- `engine/render/src/metal/metal_pipeline.cpp` — delete the stencil
  declaration in `getRenderPipelineState` (defect 1)
- `engine/render/include/irreden/render/ir_render_types.hpp` —
  `FrameDataFramebuffer` tail pad + constraint comment (defect 2)
- `engine/render/src/metal/metal_texture.cpp` — depth-format remap in
  `toMetalTextureFormat` (defect 3)
- `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp` —
  pass-0 binds (defect 4)
- `.fleet/plans/issue-2611.md` — new (this plan)

No shader edits, no GL backend edits, no public `ir_*.hpp` surface change.

## Acceptance criteria

All measured on a macOS/Metal host. **Positive-fire note:** the two banner
lines `Metal API Validation Enabled` and `Metal GPU Validation Enabled` are
REQUIRED in each validated run's log — without the env vars the same command
passes trivially, so the banner is the proof the layer was armed.

1. `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER_ERROR_MODE=assert
   fleet-run --timeout 180 IRPerfGrid --mode voxel_set --no-overlay
   --auto-screenshot --subdivision-mode none --wave-amplitude 0
   --occlusion-cull` → both banner lines present, zero `failed assertion`
   lines, ends `ir-run: RESULT=CLEAN … exit=0`.
2. Same env: `fleet-run --timeout 240 IRShapeDebug --auto-screenshot` → same
   three checks. (Exercises the shapes/SDF, sun-shadow, and text paths — the
   defect-4 fixture.)
3. Both commands **without** the env vars → `RESULT=CLEAN exit=0` (no
   normal-path regression).
4. `render-verify` on the standard shape_debug manifest → all shots pass
   against committed references. The four fixes are validation-surface-only;
   any diff is a defect in the change, not a new baseline.
5. PR body carries a before/after screenshot pair (`attach-screenshots`) with
   the expected byte-identity called out.

## Gotchas

- **macOS/Metal host required.** No host-routing label exists for Metal-only
  work (checked `gh label list`; precedent: #2488 / #2923 queued unlabeled and
  routed to macOS panes). A non-macOS worker cannot build or verify this —
  skip it rather than claim it.
- `fleet-run`: `--timeout` goes BEFORE the target; judge by the `RESULT=`
  line, never the exit code alone (a truncated run exits 0).
- **Open PR #2850** (`fleet:design-blocked`,
  `claude/2479-peraxis-overflow-determinism`) also touches
  `ir_render_types.hpp` and `metal_pipeline.cpp` — different regions (per-axis
  overflow determinism vs pipeline-state creation), so no textual overlap
  today; whichever lands second rebases mechanically. No other open PR touches
  the four files (all open PRs checked 2026-08-07).
- The probe enumerated the **two AC demos'** full shot suites. Other demos
  (fog, particles, canvas-stress, text-heavy creations) may hide siblings of
  defect classes 2/4 (short `constant` buffers; multi-pass kernels with
  partial bind sets). That wider sweep is out of scope here — a layer run over
  another demo that hits one files it separately (agent-approved lane) rather
  than growing this PR.

One task, no subtasks. No design escalation expected — every mechanism above
is measured, not inferred.

---

## Binding constraints from plan review

Plan review verdict **CLEAR with two binding constraints** (opus-reviewer,
pool-5, 2026-08-07 — full text on issue #2611). Both are implementation-time
obligations, not a replan:

1. **Defect 3's "zero behavioral delta" is unmeasured in the direction that
   matters.** `MetalTexture2DImpl::readSubImage2D` calls `getBytes` on the
   combined depth-stencil format, which Metal categorically disallows — so on
   the un-validated path its returned values are *unspecified*, and that
   readback feeds `decodeCompositeDepth` → `isoDepth` → the #2547 default
   camera pivot. Before treating AC 4's "any diff is a defect in the change"
   as operative, measure the decoded `isoDepth` for one frame pre-fix and
   post-fix. Identical ⇒ AC 4 stands. Different ⇒ defect 3 is a behavioral
   fix, a `render-verify` diff is a consequence to escalate rather than a
   defect to revert, and per-backend reference re-bless stays out of this PR
   (#1969's territory). State the measured result in the PR body.
2. **Re-run the in-flight file enumeration at claim time** rather than
   inheriting the plan's list — GitHub's files API was 500ing for several
   open PRs during review, so neither sweep was exhaustive.

Non-binding, adopted: express the `FrameDataFramebuffer` 16-byte-multiple
invariant as a `static_assert` beside the struct (compile-time checkable, and
it survives the next member being appended) rather than a comment alone.
