# engine/render/ — trixel render pipeline

The biggest and most performance-critical module. Owns the voxel → trixel →
framebuffer → screen pipeline, all GPU resources, the camera entity, and the
canvas registry.

## Entry point

`engine/render/include/irreden/ir_render.hpp` — exposes `IRRender::` free
functions. Creations and other modules should **only** include this header,
never internal render headers.


## Two managers

**`RenderManager`** (`render/render_manager.hpp`) — stateful, per-frame:

- Owns the main framebuffer, main canvas (voxel pool), background canvas,
  GUI canvas, camera entity, and the `RenderImpl` backend.
- Stores render mode, subdivisions, camera pos/zoom, viewport.
- Drives `beginFrame()` → per-pipeline systems → `presentFrame()`.
- Maps canvas names → entity ids (`m_canvasMap`).

**`RenderingResourceManager`** (`render/rendering_rm.hpp`) — generic GPU
resource pool. Type-indexed storage (`typeid<T>.name()`), id reuse queue,
named lookup. Holds shaders, buffers, textures, VAOs, etc.

## The pipeline, one frame

```
┌──────────────────────────────────────────────────────────────────┐
│  INPUT pipeline: CAMERA_MOUSE_PAN + input systems                │
├──────────────────────────────────────────────────────────────────┤
│  UPDATE pipeline: game logic, voxel mutation                     │
├──────────────────────────────────────────────────────────────────┤
│  RENDER pipeline (systems in order):                             │
│    VOXEL_TO_TRIXEL_STAGE_1  (one per-canvas tick does all of:)   │
│      • upload voxel pos/col/ids to SSBOs                         │
│      • clear distance texture to kTrixelDistanceMaxDistance      │
│      • c_voxel_visibility_compact.glsl → visible index list      │
│      • c_voxel_to_trixel_stage_1.glsl  → distance writes         │
│      • c_voxel_to_trixel_stage_2.glsl  → color + entity id       │
│        (a pool whose storeTiesPossible_ flag is set — displaced  │
│        voxels sharing a rounded cell, #2346 — inserts the        │
│        c_voxel_to_trixel_stage_1_winner_resolve election between │
│        the stages and swaps stage 2 for the winner-guarded       │
│        variant; lattice pools run exactly the two kernels above) │
│    SHAPES_TO_TRIXEL / TEXT_TO_TRIXEL  (optional overlays)        │
│    COMPUTE_VOXEL_AO                                              │
│      • c_compute_voxel_ao.glsl → per-pixel AO factor             │
│    BAKE_SUN_SHADOW_MAP                                           │
│      • c_clear_sun_shadow_map.glsl + c_bake_sun_shadow_map.glsl  │
│      • atomicMin-projects iso pixels into a sun-aligned depth    │
│        map at slot 28 (kBufferIndex_LightOcclusionGrid alias)    │
│    COMPUTE_SUN_SHADOW                                            │
│      • c_compute_sun_shadow.glsl → single-texel lookup against   │
│        the baked sun depth map → per-pixel shadow brightness     │
│    COMPUTE_LIGHT_VOLUME                                          │
│      • c_clear_light_volume + c_seed_light_volume +              │
│        c_propagate_light_volume (×32) — GPU distance-tracked     │
│        dilation chain over a ping-pong pair of 128³ RGBA8 3D     │
│        textures, seeded from a per-frame LightSourceBuffer SSBO. │
│        No CPU upload. The volume is camera-anchored each frame   │
│        (Phase 1c, #360): worldOriginVoxel_ comes from the iso    │
│        camera and lives in LightVolumeParams (UBO @ slot 23);    │
│        the light-occlusion grid carries its own origin in a      │
│        16-byte header at the start of LightOcclusionGridBuffer   │
│        (SSBO @ 28). Lights whose rounded world origin falls      │
│        outside the ±64-voxel window around the camera anchor     │
│        seed the clamped window edge at a distance-discounted     │
│        residual alpha (exact under the Manhattan metric), so     │
│        contribution fades continuously instead of popping; only  │
│        lights whose residual can't reach the window are skipped  │
│        (seeded/eligible counts on the perf HUD). SDF blockers    │
│        (`C_ShapeDescriptor + C_LightBlocker(blocksLOS_=true)`    │
│        entities) are CPU-rasterized into a second bitfield in    │
│        the same SSBO; the propagate shader OR's both bitfields   │
│        per neighbor so SDFs occlude point/spot light without     │
│        affecting AO. See system_build_light_occlusion_grid.hpp.  │
│      • Per-canvas scope (#363): each canvas's light volume is    │
│        seeded only from lights with no CHILD_OF parent (world-   │
│        scope, the back-compat default) plus lights parented to   │
│        that canvas. Parent a light via                           │
│        `IREntity::setParent(light, canvas)` to confine it.       │
│    LIGHTING_TO_TRIXEL                                            │
│      • c_lighting_to_trixel.glsl → modulates canvas colors       │
│        by (AO × sun-shadow), then adds the light-volume          │
│        contribution sampled at the trixel's recovered pos3D      │
│    FOG_TO_TRIXEL                                                 │
│      • c_fog_to_trixel.glsl → masks per-pixel by fog state       │
│        (visible/explored/unexplored) from C_CanvasFogOfWar       │
│    TRIXEL_TO_TRIXEL  (compositing/post)                          │
│    TRIXEL_TO_FRAMEBUFFER                                         │
│      • v_/f_trixel_to_framebuffer.glsl                           │
│      • reads canvas color/dist/id textures → writes framebuffer  │
│    FRAMEBUFFER_TO_SCREEN                                         │
│      • v_/f_framebuffer_to_screen.glsl                           │
│      • + f_debug_overlay.glsl if enabled                         │
│    SPRITE_TO_SCREEN  (optional; no-op when zero sprites)         │
│      • v_/f_sprites_to_screen.glsl + metal/sprites_to_screen     │
│      • CPU iso depth-sort grouped by atlas; one                  │
│        drawArraysInstanced call per atlas via SSBO @ slot 25     │
└──────────────────────────────────────────────────────────────────┘
```

Every system in that list is a normal prefab under
`engine/prefabs/irreden/render/systems/`. Each must have its name in
`SystemName` enum before the specialization will link.


## Shaders

Location: `engine/render/src/shaders/` (GLSL) and
`engine/render/src/shaders/metal/` (Metal).

Naming prefixes follow the convention in
[`CLAUDE-BASELINE.md §Naming`](../../docs/agents/CLAUDE-BASELINE.md#naming).
Shared includes: `ir_iso_common.glsl`, `ir_constants.glsl`,
`ir_world_lighting.glsl` (light-source list layout + SPOT cone +
ACES tonemap, shared by the world-lighting passes),
`ir_voxel_face_select.glsl` (fog reveal + face selection + per-axis
store key, shared by the stage-1/stage-2 kernel family — the fog grid
image slot is wrapper-supplied via `IR_VOXEL_FOG_GRID_BINDING`),
`ir_resolve_cardinal_emit.glsl` (the cardinal-layout micro-cell diamond
emit shared by the two sun-shadow RESOLVE scatter kernels — on the GLSL
side it writes through the wrapper's own `resolveScratch` SSBO, since
GLSL cannot pass a buffer as an argument; the Metal twin takes it as a
parameter).
Shader file paths are stored in `render/shader_names.hpp`. Update that
header when you add or rename a shader.

**A fragment may self-include only a MACRO-FREE prerequisite.** Both
resolvers are recursive with a visited-set cycle guard scoped to the whole
resolution (`resolveShaderIncludes`, `loadAndPreprocessMetalSource`), so a
fragment that includes a prerequisite the wrapper already pulled in resolves
to a suppressed duplicate rather than a second copy. That makes the
self-include free **when the prerequisite reads no wrapper `#define`** —
the GLSL `ir_iso_common` / `ir_sun_projection` carry no preprocessor
directive at all, and their Metal twins carry only a self-contained
include guard plus `#include <metal_stdlib>`; neither reads a wrapper
macro, so where they resolve can never cross one. A macro-parameterized
fragment (`ir_voxel_face_select`, which reads the wrapper's
`IR_VOXEL_FOG_GRID_BINDING`; the `c_voxel_to_trixel_stage_*_body` fragments,
which read `IR_FEEDER_PASS` / `IR_STORE_WINNER_ELECTION`) stays in the
wrapper's explicit ordered include list, so its position relative to the
`#define`s remains something the wrapper states rather than something a
transitive include decides.

### Metal compute kernel threadgroup registry

Every dispatchable Metal compute kernel (`engine/render/src/shaders/metal/c_*.metal`,
excluding `*_body.metal` include-fragments) needs an entry in
`threadgroupSizeForFunctionName` (`metal_pipeline.cpp`) supplying its real
`threadsPerThreadgroup`. A kernel absent from that chain falls through to the
`MTL::Size(1, 1, 1)` fallback with no error — GL is structurally immune (it
reads `local_size` out of the GLSL source), so this is Metal-only. This
registry is **enforced**: the header-checks CI workflow (via
`cmake/run_header_checks_standalone.cmake`) and the `header-checks` / `lint`
CMake targets all run `cmake/run_metal_kernel_registry_check.cmake`, which
fails the run and names the kernel if a new `.metal` file has no matching
entry (#2798). The check is pure text-scanning, so it fires on every PR —
including GL-authored ones from hosts with no Metal toolchain.

### Metal image-atomic scratch consumer list

`metal_pipeline.cpp` carries a second hand-written kernel list,
`functionUsesImageAtomicScratch`. It gates the per-kernel bind of the sticky
R32I image-atomic scratch at `kMetalImageAtomicScratchSlot` (16) — a slot that
doubles as `kBufferIndex_RevoxelizeDetachedParams`, because the Metal 0-30
buffer table has no free index. **Both** directions of drift are silent
correctness bugs: a consumer missing from the list never gets the scratch bound
(its `imageAtomicMin` writes land nowhere), and a non-consumer wrongly on the
list gets the scratch bound over whatever it declared at slot 16 — #1619, where
`c_revoxelize_detached`'s params UBO was clobbered and the fill read
distance-clear words as its params.

Enforced the same way as the threadgroup registry, by
`cmake/run_metal_scratch_consumer_check.cmake` (#2878), on the same three
entry points. The expected set is derived, not declared: a kernel is a consumer
iff its resolved source — its own file plus the transitive
`#include "…"` closure, which is how the `*_body.metal` fragments reach their
wrappers — declares an `atomic_int`/`atomic_uint` parameter at that slot. The
atomic qualifier is what separates a real consumer from `c_revoxelize_detached`'s
`constant RevoxelizeParams&` at the same slot, so **there is no allowlist to
maintain**; the check reads the slot number out of the
`kMetalImageAtomicScratchSlot` constant rather than hardcoding 16.

Practical upshot when adding a Metal kernel: declare the scratch and the check
tells you to add the name; don't declare it and adding the name is what the
check rejects. Either way the answer comes from CI, not from remembering these
two lists exist.

### Metal AOT metallib build (opt-in, no runtime consumer)

`engine/render/CMakeLists.txt` can precompile every `.metal` file into
`default.metallib` via `xcrun metal`/`metallib`, gated behind
`IRREDEN_METAL_AOT_SHADERS` (default `OFF`). **This is a convention, not a
build-time check**: kernel wrapper files (`c_voxel_to_trixel_stage_1.metal`,
`_feeder`, `_winner_resolve`, `c_voxel_to_trixel_stage_2.metal`, `_winner`,
and the other top-level kernel files) are the only standalone compile units
— they `#define` the macros an include-fragment needs and then `#include`
it. `*_body.metal` (the stage bodies) and `ir_*.metal` (shared helpers like
`ir_iso_common.metal`, `ir_per_axis_lighting.metal`) are include-fragments:
they reference wrapper-supplied macros or lack an entry point, so they
cannot compile as standalone translation units. The AOT glob excludes both
patterns by filename convention — a new include-fragment MUST match one of
them or it silently re-breaks the opt-in AOT path.

`default.metallib` currently has **no runtime consumer** — the live path is
always `metal_pipeline.cpp`'s `loadAndPreprocessMetalSource`, which resolves
`#include` recursively (with a visited-set cycle guard) and compiles from
source at runtime. The AOT path exists for a future full-Xcode packaging
step; enabling it costs an `xcrun`-not-found `WARNING` (correct once
opted in) and produces an unloaded `.metallib` today.

## Backends

`render/opengl/` and `render/metal/` each implement the `RenderImpl` /
`RenderDevice` interfaces. `RenderManager` holds one via `unique_ptr`.
Platform selection is compile-time (`IR_GRAPHICS_OPENGL` / `_METAL`).

### Metal negates clip `position.y`; GL does not

Every Metal `*_to_*` vertex stage (`trixel_to_framebuffer`,
`framebuffer_to_screen`, `sprites_to_screen`, `debug_overlay`) computes
`out.position = mpMatrix * aPos` and then `out.position.y = -out.position.y`.
The GLSL twins emit `mpMatrix * aPos` unflipped. This is not a per-shader
quirk — it is the single adapter that lets **one GL-authored projection
matrix** render right-side-up on both backends despite their opposite
framebuffer-Y origins: OpenGL's default framebuffer is **bottom-left** origin
(`gl_FragCoord.y` increases upward), Metal's render target is **top-left**
(`position.y` increases downward). When you add or port a full-screen /
quad pass, mirror this negate on the Metal side or the image renders
upside-down.

### Trixel→framebuffer parity shift (GL-only)

The `TRIXEL_TO_FRAMEBUFFER` gather samples the canvas at
`origin = TexCoords * textureSize`. Each iso texel-cell holds two triangles
split along a diagonal; `trixelFramebufferSamplePosition`
(`ir_iso_common.{glsl,metal}`) resolves which half a fragment covers by
conditionally decrementing **`origin.y`** by one row (parity bit + a sub-pixel
`fract` test, byte-identical to CPU `IRMath::pos2DIsoToTriangleIndex`).

**GL applies that shift to the color/depth/id reads; Metal reads color/depth
from the raw origin.** Both backends build identical per-vertex `TexCoords`, but
per the "Metal negates clip `position.y`" note above they rasterize that quad
under **opposite framebuffer-Y origins**: GL's raw sample lands on the row that
needs the shift, while Metal's flipped raster already lands the raw sample on the
correct row (the equivalent one-row correction, applied implicitly). Both read
the *correct* trixel for their own raster convention — not a latent bug, so the
asymmetry is kept, not reconciled. **Picking is the one shared exception:** both
backends apply the shift to the *hover* coordinate, because it must match CPU
`mouseTrixelPositionWorld()` → `pos2DIsoToTriangleIndex` (computed independently
of GPU raster-Y), even though only GL applies it to the color/depth gather.

Before editing either `f_trixel_to_framebuffer` shader or
`trixelFramebufferSamplePosition`, read
[`docs/design/trixel-parity-shift-442-investigation.md`](../../docs/design/trixel-parity-shift-442-investigation.md)
— it carries the #394/#438/#442 timeline, the ruled-out X-axis/rounding
candidates, and the keep-and-document decision.

## What belongs in engine/render/ vs engine/prefabs/irreden/render/

`engine/render/` is a graphics primitive library. It owns what the pipeline
itself needs regardless of which features a creation enables:

- Device abstraction and context (`RenderManager`, `RenderImpl`, `RenderDevice`).
- GPU resource CRUD (`RenderingResourceManager`).
- Pipeline execution (frame loop, canvas dispatch, framebuffer flip).
- Camera, viewport, subdivision mode — the pipeline reads these every frame.
- Voxel pool allocation (the pool is a device-level concept). When the
  allocation is a slice owned by a streaming chunk
  (`IRWorld::ChunkResidencySlot::poolAllocation_`), any system that writes
  voxels through the slice MUST call
  `IRWorld::ChunkResidencyManager::markChunkDirty(key)` immediately after
  the write — without it, eviction silently drops the save and the chunk
  reverts on re-resident. See
  [`engine/world/CLAUDE.md`](../world/CLAUDE.md#chunk-mutation-must-route-through-markchunkdirty).
  Single-chunk creations never see a residency manager and the rule does
  not apply.

Feature state — anything a creation opts into — belongs in
`engine/prefabs/irreden/render/`. If the renderer can ship without the feature
(fog-of-war is optional per-creation; debug overlay is dev-only), that feature
does not belong in `engine/render/`.

**Rule of thumb.** If you are about to add a field to `RenderManager`, ask:
is this a per-feature concern? If so, it belongs on a component owned by the
feature's system, exposed from a prefab-scoped surface. `RenderManager` should
not grow fields for features that individual creations may not use.

For the two viable patterns for exposing feature API from the prefab layer, see
`engine/prefabs/irreden/render/CLAUDE.md` §"Exposing system public API from
the prefab layer".

### Name identifiers after the rendering effect, not the caller

The same separation extends past C++ APIs into **type identifiers, shader
constants, shader variables, and the comments around them.** Anything that
lives under `engine/render/` — `ir_render_types.hpp`, `engine/render/src/shaders/`
(GLSL), `engine/render/src/shaders/metal/` (Metal) — names what the renderer
*does*, not which feature is asking for it.

| Layer                       | Allowed                                  | Not allowed                              |
|-----------------------------|------------------------------------------|------------------------------------------|
| `ShapeFlags` enum values    | `SHAPE_FLAG_HOLLOW`, `SHAPE_FLAG_CHECKERBOARD`, `SHAPE_FLAG_DEPTH_COLOR`, `SHAPE_FLAG_XRAY_OCCLUDED` | `SHAPE_FLAG_GIZMO`, `SHAPE_FLAG_BUTTON`, `SHAPE_FLAG_ENEMY_HIGHLIGHT` |
| Shader `FLAG_*` constants   | mirrors of the C++ flag names above       | feature-named mirrors                    |
| Shader local variables      | `xrayOccluded`, `isHollow`, `parity`      | `isGizmo`, `isWidget`                    |
| Shader-side tunables        | `kXrayOccludedAlpha`, `kCheckerScale`     | `kGizmoOccludedAlpha`, `kButtonAlpha`    |
| Doc comments in the above   | "shapes flagged X behave like Y"          | "editor gizmos use this for …"           |

Why: the shader is feature-blind by design. It transforms inputs into pixels;
it has no idea whether a shape is a gizmo, a HUD marker, or an enemy
silhouette. A use-case-named identifier in this layer pulls feature concerns
into the rendering primitive — a second caller wanting the same effect either
adds a second redundant flag, or perpetuates the misnomer by riding on the
first feature's name. A behavior-named identifier (HOLLOW, XRAY_OCCLUDED) is
reusable from day one; the gizmo prefab just sets the flag and the next
caller (a selection highlight, a debug marker, a "see through walls" mode)
sets it too. The same principle covers other engine/render/ surfaces — shader
texture/SSBO binding names, `RenderImpl` method names, debug-overlay enum
values, etc. — name the graphics effect, not the first user.

This is a specific application of the wider engine/render/ vs prefab/ split
above: feature names live in the prefab layer (`IRPrefab::Gizmo::`,
`C_GizmoHandle`, `gizmo.hpp`), behavior names live in the primitive layer.

### Current deviations

See `.fleet/status/render-api-relocations.md` (queue-manager-owned;
feature PRs do not edit) for in-flight relocations of feature-specific
API off `IRRender::` and onto feature-scoped prefab namespaces.

## Verifying render changes

Rendering bugs rarely show up in the type checker or the test suite. Any
PR that touches:

- `engine/render/src/shaders/` (GLSL or MSL)
- `engine/prefabs/irreden/render/systems/` (pipeline systems)
- anything affecting pipeline ordering, canvas textures, or the voxel pool

must run the **`render-debug-loop`** skill after the change and attach
the following to the PR body.

Every verification run must also end `ir-run: RESULT=CLEAN` — a
teardown crash after the screenshots saved still fails the run (see
[`docs/agents/FLEET.md`](../../docs/agents/FLEET.md) §"Clean-exit
policy").

Attach:

1. At least one full-frame before/after screenshot pair.
2. **At least one ROI crop pair** (current + baseline) covering a
   cube/voxel silhouette — a 128×128 native crop is small enough to
   embed inline and dense enough to surface 1-pixel drift that
   downscaled full-frames hide. ROI crops come for free with
   `--auto-screenshot` once the demo's `kShots[]` table includes
   `RoiCrop` entries (see `creations/demos/shape_debug/main.cpp` for
   the canonical example).

If the PR intentionally changes silhouettes / lighting / shading
model, call out the intentional drift in the description so reviewers
know the new crop is the new baseline rather than a regression.

**Sun-shadow / lighting / AO shader diffs don't show in the default demo.**
For a diff touching the sun-shadow / lighting / AO shader family the
`attach-screenshots` picker routes to the lighting-capable stress demo with
a `--debug-overlay` mode at a frozen pose (`IRCanvasStress --debug-overlay
shadow|ao|light_level --no-auto-rotate --no-spin`) — the default
`IRShapeDebug` suite renders none of those effects, so its before/after
pairs come out byte-identical and read as "no visual change" (#2343).

**Occlusion diagnostics for rotated voxel content: use `--checkerboard`,
not `--depth-color`.** `--depth-color` quantizes hue in 4/3-world-unit
bands; at any non-cardinal yaw the bands beat against the 1-unit voxel
lattice as staircase moiré that reads as front/back scramble — while an
SDF twin (continuous per-pixel palette) looks smooth, making the
side-by-side structurally misleading. `--depth-color` is only sound at
cardinal poses or against a voxel (not SDF) reference; `--checkerboard`
(alternating per-voxel colors) shows true geometry/occlusion in one
capture. Three fix rounds on #1457 chased this artifact.

The skill drives any creation that supports `--auto-screenshot`
(today: `shape_debug`; reference implementation is
`creations/demos/shape_debug/main.cpp`) and carries topic-indexed
diagnosis tables for trixel / SDF shapes, lighting phases, and
backend-parity symptoms.

For the "show me the drift" case — when two crops look identical at
a glance but a regression actually moved one pixel — pipe them
through **`tools/img_diff`**:

```
build/tools/img_diff/img_diff <baseline.png> <current.png> /tmp/diff.png
```

The output renders drifted pixels solid red against a desaturated
baseline. Useful both for the agent's evaluation step and for
reviewer-facing PR-body screenshots.

For depth-ordering bugs (a near surface clipping behind a far one)
where you need the **exact stored composite depth** rather than a
visual cue, use the `--depth-probe X,Y` flag (#1910;
`canvas_stress` + `perf_grid`). It reads back and logs the real
depth-test value at main-framebuffer texture pixel (X,Y) each frame —
the GL_LESS winner across every render path, since gather, per-axis
scatter, and the detached-canvas composite all write `gl_FragDepth`
into the one depth attachment — decoded to shared trixel-distance
units. (The detached composite writes depth on **both** backends —
#1957 verified Metal `depthWriteEnabled_` and OpenGL `glDepthMask` are
both at their default-enabled state when the composite runs, then made
that write explicit (`setDepthWrite(true)` + restore) so it can't
silently regress. The probe is **not** blind to the detached path on
either backend; the earlier "Metal composite drops depth — #1884/#1950
Finding 1" reading was a misdiagnosis — the composite participates in
depth, and where its `x+y+z` iso-depth ranks behind the floor it loses
the test rather than failing to write, which is the #1958 Bug A
wrong-winner problem, not a missing write.) The readback + `enc` decode
are `IRRender::readbackCompositeDepth` / `decodeCompositeDepth` (over the
`Texture2D` / `PixelDataFormat::DEPTH_COMPONENT` primitive) — engine-side
because the depth-aware camera pivot consumes them too, so the #1960
N-tier decode has one home; the `IRPrefab::DepthProbe::` prefab-scoped
Pattern-B namespace layers the debug log line + assert guards on top.
Pure readback:
no shader or pipeline change, so a flagless run is byte-identical. Use
it when a screenshot can't disambiguate which surface won a pixel.

The sibling `--depth-probe-assert` flag (`canvas_stress`) turns one
readback into a machine-readable `[depth-probe-assert] … result=PASS|FAIL`
line. Two forms:

- `X,Y` (#1957) — composite **depth-write** guard: PASS iff the composite
  stored a non-background depth at (X,Y). Aim it at a texel inside a
  world-placed detached solid (canonical:
  `--only canary --no-spin --no-auto-rotate --depth-probe-assert 321,210`)
  so a future pass that disables the detached-canvas composite depth-write
  fails the run headlessly on either backend.
- `X,Y,tier=N` (#1960; #2122) — per-trixel-priority **tier** guard: PASS
  iff the composite winner at (X,Y) decodes to the #1960 tier N. The
  positive ENABLED-path gate the per-trixel carrier needs (byte-identity at
  default priority 0 can't prove the carrier survives the rotating
  re-voxelize MODE 1 fill). The `scripts/depth-tier-verify.py` harness wraps
  the canonical run
  (`--only interpenetrate --no-spin --no-auto-rotate --depth-probe-assert 639,362,tier=2`)
  into a build → run → parse gate, exiting non-zero if the far priority unit
  decodes `tier=0` (carrier dropped). Both forms are pure readback — a
  flagless run is byte-identical.

**Default-off features need a positive enabled-path test, not just
byte-identity at default.** Render features routinely default OFF (priority
0, a mode flag off, an opt-in branch) precisely to preserve byte-identity —
but byte-identity at default only proves the OFF path is a no-op, never that
the feature works. Author a test that exercises the **ENABLED** path (a
`--depth-probe`/`-assert` reading, a demo shot with the flag ON) and confirms
the effect end-to-end (CPU author → GPU upload → shader output). A
CPU-authored field uploaded only on a *specific* path (e.g. the per-frame
binding-6 voxel upload, not a detached-revoxelize bake) can silently never
reach the shader, and a "compiles + byte-identical at default" merge ships a
feature that doesn't function in its actual use case (#1989 per-trixel
priority caught exactly this on resume).

**The vacuous-failure mirror: a gate no correct implementation can pass is
a gate defect, not a code defect.** Harnesses outlive the contracts they
were written against — an early phase pins a metric, a later phase changes
what the right answer *is*, and the metric keeps scoring the old one.
Before treating a red harness as a bug, check whether its oracle measures
the ratified contract; the tell is cheap: solve the metric for its
parameter, and if no input satisfies the threshold (#2585's ≤1.5px
centroid gate needed a probe radius ≤ ~0.07 world units), re-ground the
oracle rather than bending the mechanism to the metric — and verify the
replacement oracle is itself *reachable* before it ships as the new gate.

**The disabled-direction complement: a "gated-off / byte-identical on path
X" claim that leans on a shared shader predicate is empirical, not
structural.** Multi-dispatch passes (per-axis resolve, world-placed resolve,
resolve-then-bake) read resident shared UBO state (`FrameDataVoxelToCanvas`,
`FrameDataSun`) that the C++ driver patches per dispatch, so a gate over
that state is a decode-path predicate — a sibling dispatch may deliberately
spoof the gated field (the per-axis resolve zeroes `residualYaw` to reuse
the cardinal recovery) and take the gated path spuriously (#2293).
Positively verify which dispatches actually take the gated path, or drive
the intent from a dedicated per-dispatch C++ value so the byte-identity
holds by construction.

**Rebasing a shader-kernel or encoding PR past a carrier/encoding migration
on master:** re-verify against master's *current* source — (a) every
`encode*`/`decode*` helper call's arity and signature, and (b) that any
polarity/priority/flip carrier the migration added is threaded through the
rebased kernel on the ENABLED path. Byte-identity at default does not prove
the carrier survived the rebase: an extracted or open-coded kernel that
forked pre-migration compiles and passes cardinal byte-identity while
silently dropping the carrier on the rotated/enabled path (#2325 vs #2207).

**Per-stage GPU timings quoted as evidence are multi-frame averages +
spread, never single-frame reads** — see
`.claude/skills/optimize/reference/gpu_profiling.md` for the rule (#2255
per-axis nondeterminism makes a single frame ~2× off).

### Verifying temporal stability (per-frame jitter)

**A single screenshot cannot prove a moving scene is jitter-free.** Pan/rotation
jitter is content that *should* translate smoothly but instead oscillates ±1px
(or worse) frame-to-frame — typically when an integer canvas anchor and its
sub-pixel compensation disagree at a cell boundary (the #1944 per-axis class).
Each individual frame looks correct; only the *sequence* reveals it. Any change
to the camera-offset decomposition, the per-axis scatter, the anti-vibration
split, or the framebuffer/screen blit MUST be checked this way, not just by
before/after stills.

Three checks, in order:

1. **Static ("after the camera stops") — must be byte-identical run-to-run.**
   Capture the same pose in two separate runs and `img_diff` them; expect 0
   drift. A non-zero diff here is non-determinism / static jitter.

2. **During pan / during rotation — sweep + `tools/jitter_probe`.** `shape_debug`
   has two fine-step sweep harnesses that hold everything fixed except the swept
   variable, capturing one frame per step:
   - `--pan-sweep` — steps the camera position at a fixed (non-cardinal) yaw.
   - `--yaw-sweep` — steps the camera yaw within ONE cardinal quadrant (constant
     visible-face triplet) at a fixed position.

   Drive them with an **isolated shape on a black field** so the centroid is
   clean — `--spin-shape <shape> --spin-shape-voxel` (voxel → exercises the
   per-axis path; omit `--spin-shape-voxel` for the SDF/cardinal path). For
   rotation use a **vertical cylinder**: its silhouette is Z-yaw-invariant, so
   any centroid wobble is jitter, not a legitimate face-triplet change (a cube's
   silhouette changes under yaw and contaminates the metric).

   ```bash
   # ALWAYS wipe first. The engine never clears this dir and numbers *around*
   # leftovers (VideoManager::reserveNextScreenshotIndex), so without this the
   # glob below scores earlier runs' frames too. End the path at .../screenshots
   # — a find rooted at the demo dir also deletes staged data/images/ assets and
   # the next run dies on a misleading image-load assert. Use -delete, not
   # `rm -f <dir>/*.png`: zsh aborts the whole && chain on an unmatched glob.
   find <save_files>/screenshots -name '*.png' -delete
   # pan jitter (voxel box, yaw 45, zoom 4):
   IRShapeDebug --spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 \
       --zoom 4 --auto-screenshot 6
   # rotation jitter (voxel cylinder, Z-yaw-invariant probe). --pivot-origin is
   # load-bearing, not optional: it selects RotationPivotMode::ORIGIN so the yaw
   # pivots about the world origin, which is exactly where --spin-shape spawns
   # its single fixture — the shape stays screen-centred and the centroid carries
   # no pivot-orbit term. Without it the default CAMERA_CENTER focus contributes
   # an orbit that dominates the metric (see the bar table below).
   IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep \
       --pivot-origin --zoom 4 --auto-screenshot 6
   # then score the captured sequence (in order). --reversal-eps 0.8 retires the
   # reversal criterion on these probes (see "the reversal criterion" below).
   # The 6-digit glob matches full frames ONLY — ROI crops share the prefix and
   # append _<label>__crop_<crop>.png, and a 128x128 crop has no usable
   # foreground. --expect-frames must equal the count the sweep ACTUALLY writes,
   # which is the sweep's own shot-table length, NOT the --auto-screenshot value
   # (that is the per-shot warmup): --yaw-sweep --auto-screenshot 6 emits 24
   # frames, and the engine logs the number ("Yaw-sweep: 24 shots"). The guard is
   # the only thing that catches a widened glob, since scoring the wrong set
   # still yields a confident verdict (measured during #2469 — see
   # tools/jitter_probe/README.md §"Wipe before every capture").
   # --max-excursion-x is the primary rotation-gate assertion (#2606); the bar is
   # per-zoom, from the table below, and applies to the pinned
   # --yaw-sweep --pivot-origin population only — omit it when scoring
   # --pan-sweep, where x translates by design. y is deliberately left
   # unconstrained — on a yaw sweep it legitimately translates.
   build/tools/jitter_probe/jitter_probe --reversal-eps 0.8 --expect-frames 24 \
       --max-excursion-x <bar(zoom)> \
       <save_files>/screenshots/screenshot_[0-9][0-9][0-9][0-9][0-9][0-9].png
   ```

   **Assert the arm from the engine log, never from the argv you think you
   passed.** The run logs `RotationPivotMode: ORIGIN (--pivot-origin) — Z-yaw
   pivots about the world origin`; that line present (and `Spin-shape single
   fixture: Cylinder (voxel-pool)` vs `(SDF)`, and `Yaw-sweep: 24 shots`, and the
   zoom as *received*) is what makes a population what you labelled it. A silently
   unpinned arm reads ~200x higher and looks like a real regression.

3. **Read the verdict.** `jitter_probe` tracks the shape's centroid across the
   sequence and reports `SMOOTH` (0 direction-reversals, sub-pixel residual off
   the smooth-motion line, exit 0) vs `JITTER` (sign-reversing deltas + multi-px
   residual, exit 1). A correct fix flips JITTER → SMOOTH; re-run at multiple
   zooms (the per-axis class worsens with zoom/subdivision). For a multi-shape
   scene with no isolation, pass `jitter_probe --color R,G,B,T` to lock onto one
   shape. See `tools/jitter_probe/README.md`.

   **The reversal criterion, and what this gate does NOT catch (#2469).** On both
   canonical probes one axis is near-**pinned** while the other translates, so
   `reversals == 0` counts sign flips of sub-pixel coverage noise and is
   unsatisfiable on healthy master — measured 2026-07-28 at zoom 2/4/8 on the
   yaw sweep AND on the pan-sweep twin. `--reversal-eps 0.8` zeroes those deltas
   and makes the gate effectively **residual-axis only**; the `--max-residual`
   default (1.50px) is unchanged and is the live assertion. Do not read the eps
   as a calibrated floor — it sits at the top of the observed per-frame delta
   range precisely because the criterion is being retired for these probes.

   Two consequences to know before you lean on this gate:

   - **It does not fire on the `IR_PERAXIS_OVERFLOW_DISABLE=1` runtime control.**
     That lever presented (2026-07-28) as a *smooth* 11.13px x-centroid migration
     scoring `reversals=0, max_residual=0.73px` — clean on both shipped axes. No
     eps separates the populations (the control clears at 0.6, healthy master at
     0.8), which is why the eps was not calibrated against it. The conclusion
     still holds; the 11.13px figure does not — see the re-measure below, where
     a pivot-orbit term now dominates both arms.
   - **The hard check is the residual axis against the analytic floor.** The
     pre-#2427 defect record (residual 2.93px, Δmax 5.37 at zoom 4) fails the
     1.50px bar by ~2×, so the original multi-pixel face-pop class is still
     caught. A *systematic migration* class is not.

   The per-axis assertion that expresses "x stays pinned while y may translate"
   ships as `jitter_probe --max-excursion-x/-y` (#2606), and every default-mode
   run prints `excursion=` per axis, so the by-hand max-min read is gone. **On the
   `--pivot-origin` pinned sweep it is the primary rotation-gate assertion, and
   these are its bars** (macOS/Metal, 2026-08-08, 24-frame sweeps, one session,
   arm identity asserted per run from the engine log):

   | zoom | healthy voxel | SDF control | `IR_PERAXIS_OVERFLOW_DISABLE=1` | **bar** | separation |
   |---|---|---|---|---|---|
   | 2 | 0.62px | — | 6.23px | **2.0px** | 10x |
   | 4 | 0.18px | 0.00px | 10.87px | **0.5px** | 60x |
   | 8 | 0.06px | 0.00px | 21.79px | **0.5px** | 363x |

   Bar rule: the smallest half-integer >= 2.5x the max healthy x excursion at that
   zoom (voxel and SDF both count as healthy), required to also be <= 0.5x the
   defect excursion — so a published bar always has >=2.5x headroom below and
   >=2x above. A zoom with no value satisfying both is omitted rather than
   published thin; all three separate here by 10x or better. The defect arm fires
   at every zoom, and *attributably*: re-running it with `--max-residual 99` still
   exits 1, so the failure is the excursion criterion by construction rather than
   another axis catching it by accident. y excursions for the same populations
   (recorded, not gated): healthy 0.21 / 0.11 / 0.06px, defect 2.64 / 5.27 /
   10.08px at zoom 2/4/8.

   **At a zoom not in the table, re-derive — never interpolate, and never carry a
   neighbouring row across.** z1 is omitted by the clause above, not by oversight:
   healthy reads **0.93px** against **3.53px** for the defect arm, a separation of
   only 3.8x against 10x/60x/363x at the published zooms, so the candidate bar
   (smallest half-integer >= 2.5 x 0.93 = 2.325 → 2.5px) sits *above* its own
   ceiling (0.5 x 3.53 = 1.765px) and no value satisfies the rule. Borrowing the
   adjacent 0.5px there **false-fires** — it exits 1 on a green z1 population
   (0.93 > 0.5), reporting a healthy tree as a rotation regression. Upward is the
   harmless direction: z16 healthy reads **0.04px**, 12x under that same 0.5px.
   The two identical 0.5px rows are a rounding coincidence, not a plateau to read
   off. (z1/z16 measured macOS/Metal 2026-08-08, same fixture and arm-identity
   discipline as the table. The z16 pass is non-vacuous: `--max-excursion-x 0.03`
   on those frames exits 1, so the flag is live on the population it clears.)

   **The bar belongs to the pinned probe only — the unpinned sweep carries no
   excursion bar.** Drop `--pivot-origin` and the same healthy z4 population reads
   **38.18px** instead of 0.18px (212x), because the **default** `CAMERA_CENTER`
   focus (#2547, landed 2026-07-31) contributes a residual orbit with a
   1-iso-unit cap-entry bias — the surface `pivot-verify.py` owns, and it is green
   throughout (explicit-focus blocks pin at 0.94/1.27px). #2641 → PR **#2758**
   (open, in review) root-causes that bias as *inherent* to the derive: the
   composite stores a per-face sort key, not the metric depth at the sampled
   trixel. **The bar above does not rest on that ruling** — ORIGIN removes the
   pivot term outright rather than bounding it (`getEffectiveCameraIso`,
   `engine/render/src/ir_render.cpp:47-49`, tests ORIGIN before the focus branch
   and returns `cameraIso` unmodified), so no pivot term is stricter than any
   future default derive, whichever way #2758 lands.

   Free evidence for **#2907** from the same session: the pinned probe's x
   *residual* reads 0.41 / 0.08 / 0.03px at zoom 2/4/8, while the unpinned z4 arm
   reproduces #2907's 1.78px exactly. The default-focus probe's residual redness
   is therefore orbit-inflated, not a floor of the per-axis path.

**Jitter is NOT the same as cardinal byte-identity.** Confirm yaw-0 / static
frames stay byte-identical (`img_diff`) *and* that motion is jitter-free
(`jitter_probe`) — a change can pass one and fail the other.

**Rotation-pivot invariance is a THIRD independent property** — a sweep can be
SMOOTH and byte-identical at yaw 0 while the pivot pins the wrong point (a
(0.5,0.5,0.5) anchor offset rides the iso depth axis: zero projection at
cardinal 0, a visible orbit under yaw). Any change to `getEffectiveCameraIso`,
`cameraYawPivotOffset`, the kernels' yawed reposition, or the per-axis anchor
must also run `python3 scripts/pivot-verify.py` — it drives
`IRShapeDebug --pivot-verify <block>` (isolated cylinder probe, explicit-focus
+ default-pivot blocks, voxel + SDF twins) and gates each block with the oracle
that is valid for it: `jitter_probe --stationary` whole-silhouette invariance
where the probe rotates about a point on its own axis, and the demo's
`[pivot-focus-assert]` pinned-point check — derived focus vs the analytic
ray/surface intersection — for the default-pivot blocks, whose silhouettes
legitimately orbit the pin. The SDF twin is **reported, not gated** — with no
voxel lattice to land on, its silhouette is quantized by the destination pixel
grid alone, a flat one game-resolution pixel at every zoom — 2.00px on a 2x
(HiDPI) host, 1.00px on a 1x one, both measured — that no
pivot fix can move (#2645). No reference images. Contract + known deviations:
[`docs/design/camera-yaw-pivot.md`](../../docs/design/camera-yaw-pivot.md)
(epic #2544).

For changes that touch only one graphics backend (GLSL without MSL
counterpart, or vice versa), follow up with the **`backend-parity`**
skill on the lagging-side host — the rule is in [`docs/agents/FLEET.md`](../../docs/agents/FLEET.md)
under "Cross-platform parity". `render-debug-loop` captures the
evidence; `backend-parity` drives the port.

Exceptions: pure header-doc edits, string-literal fixes, and internal
refactors with provably no runtime effect can skip the loop. When in
doubt, run it — a missing screenshot pair is a fast reviewer-rejection.

### Cross-host smoke validation

Render PRs are almost always authored on only one host (Linux/OpenGL
via the fleet, or macOS/Metal). The other backend's build and smoke
are not exercised until a fleet agent on that host picks the PR up.
The `fleet:needs-linux-smoke` and `fleet:needs-macos-smoke` labels
tally outstanding cross-host validation so no render PR merges
unvalidated on either backend.

**Tagging.** When a fleet reviewer (sonnet-reviewer or opus-reviewer)
approves an engine PR whose diff touches `engine/render/`,
`engine/prefabs/irreden/render/`, `engine/render/src/shaders/`, or
any `*.glsl` / `*.metal` file, it adds BOTH labels alongside the
verdict label. The reviewer cannot tell which host the PR was
authored on, so it tags both and lets each host's agents clear
their own.

**Validation.** Each host's author agents (opus-worker, sonnet-author)
poll for the label matching their host at the start of each loop
iteration, before picking new work. They check out the PR, run
`fleet-build --target IRShapeDebug`, run
`fleet-run IRShapeDebug --auto-screenshot 10`, and on success remove
their label and post a confirmation comment. On failure they add
`fleet:needs-fix` and leave the smoke label in place.

**Merge gating.** The human holds the merge while either label
persists. Both labels must be gone for the PR to be safe to merge.

Skip the smoke flow for game-repo PRs (the game's render pipeline
uses the engine's backend — cross-host applies at engine level) and
for non-render engine PRs (tooling, docs, non-render modules — these
don't exercise backends and don't benefit from cross-host smoke).

## Iso-depth-axis invariant (world-camera Z-yaw-only for GRID)

The integer trixel raster, picking, hitbox cardinal-snap, gizmo drag,
and the SDF analytic AABB cull all assume the world-space (1,1,1)
direction is the iso depth axis. World-camera pitch or roll silently
breaks those shortcuts — every "sum of components = depth" closed form
and every cardinal-index API loses meaning. DETACHED entities are
exempt (they raster through `faceDeformationMatrixSO3`, which is
axis-agnostic, and the per-canvas SO(3) bake absorbs arbitrary camera
rotation).

Future free-camera work (orbit, perspective preview, cinematics) should
cost itself against the consumer map and "how to break it" table in
[`docs/design/iso-depth-axis-invariant.md`](../../docs/design/iso-depth-axis-invariant.md)
before scoping. Sized similarly to T-054 / T-055 combined; DETACHED-only
pitch/roll is free via issue #1076 + #1075.

## Lighting culling invariants

The render cull (`visibleIsoViewport` → `buildChunkVisibilityMask` in
`system_voxel_to_trixel.hpp`, and the per-shape iso-bounds check in
`system_shapes_to_trixel.hpp`) covers the visible iso AABB **plus the
shadow-feeder sweep** when sun shadows are enabled
(`IRMath::shadowFeederIsoBounds` widens by `kSunShadowMaxDistance` along
`sunDir` (toward the sun); T-131 / PR #576). It governs which voxels/shapes are written
into canvas textures — pixels outside the visible AABB but inside the
swept extent still produce `trixelDistances` writes so the screen-space
sun-shadow bake can project off-screen casters onto on-screen pixels.
Bounds collapse to the visible viewport when `getSunShadowsEnabled()` is
false.

Lighting splits across two sampling spaces:

- **Screen-space**: sun shadows use a sun-aligned depth map baked from
  `trixelDistances`. Off-screen geometry participates because the bake's
  iso-frustum AABB is swept along `-sunDir` by `kSunShadowMaxDistance`
  (64 voxels), so shadow casters within that range project correctly
  even when their iso position is outside the visible rect.
- **World-space**: light-volume propagation reads the world-space
  light-occlusion grid (post-T-091, AO migrated off it). As of
  Phase 1c (#360) the grid is **camera-anchored** — it covers the
  256-voxel cube centered on the iso camera's world voxel rather
  than a fixed `[-128, 128)` window. Off-screen geometry inside that
  cube still participates in lighting by design (a torch a few
  voxels off-screen still floods light into on-screen voxels).
  Geometry farther from the camera than ±128 voxels is outside the
  grid this frame and contributes nothing — the cull is a side
  effect of the anchor, not a separate viewport check (invariant 1
  below still holds: the grid-build iterates the full pool and
  writes whichever voxels land in-range).

**Phased-out producer:** `BUILD_LIGHT_OCCLUSION_GRID` and the
`LightOcclusionGrid` SSBO are scheduled for removal once light-volume
LOS moves off the world-space bitfield. AO already migrated to
screen-space neighbour sampling (T-091), so the bitfield now feeds only
`c_propagate_light_volume`. The single source of truth for "is there
geometry along ray R" in the long run is `trixelDistances` (and the
depth-map bakes derived from it); the world-space bitfield is an
intermediate that survives only until the propagate shader migrates to
a screen-space LOS source.

The four invariants below exist because these are the places easiest to break
silently. Each lighting PR (AO #166, shadows #167, flood-fill #168,
fog-of-war #170) reviewer should run this checklist. See #196 for the
architect review that originated them.

The sun-shadow path reads the screen-space sun depth map (baked from
`trixelDistances`), not the light-occlusion grid; invariants 1, 2, and
4 below apply to the light-volume propagate path only (AO migrated to
screen-space sampling in T-091). The shadow-ring (invariant 2) is
implicitly enforced by the bake AABB sweep — see "Sun shadow bake AABB
sweep" below.

### 1. Grid-build iterates the full voxel pool, not the render-culled subset

`buildChunkVisibilityMask` is a render-pipeline-local mask inside
`system_voxel_to_trixel`. The light-occlusion-grid-build system must use
its own iteration path and must **not** consult that mask. The failure
mode is sharing a helper that accidentally applies the render cull to
the grid build.

**Check:** `System<BUILD_LIGHT_OCCLUSION_GRID>` does not call
`visibleIsoViewport` and applies no viewport filter to the grid build.
The invariant is **behavioral**, not "the header is absent":
`system_build_light_occlusion_grid.hpp` now transitively includes
`cull_viewport_state.hpp` (via `camera_anchor.hpp`, added in #2315 for
`isCullingFrozen()` freeze-state gating) — the freeze check is fine; only a
`visibleIsoViewport`-based cull on the grid path would break the invariant.

**Status (T-010, PR #188; renamed in T-126; include note #2315):** compliant —
`System<BUILD_LIGHT_OCCLUSION_GRID>` iterates `pool.getLiveVoxelCount()`
on the full pool with no viewport filter.

### 2. Shadow-ring extent when chunk streaming activates

T-010's grid is full-world today, so this is not yet triggered. When
per-chunk streaming is introduced (resident chunk set controlled by camera
position), the loaded set must extend past the view frustum in the
sun-projection direction by at least:

```
shadowRingDistance = maxCasterHeight × cot(sunAltitude)
```

For a 256-tall world at 45° sun that is one chunk; at a shallow 20° sun it
is 3+ chunks.

**Check:** whenever chunk streaming lands, the resident-chunk-set calculation
includes this expansion. Document the formula next to the streaming code.

### 3. Light-seed set — off-screen sources must still seed flood-fill

A torch 10 tiles off-screen with radius 15 should still glow the on-screen
tiles nearest it. T-014 seeds BFS from all `C_LightSource` entities, which is
correct as-specced. The failure mode is a later optimizer adding "only seed
lights within the view frustum" without the radius expansion — that silently
drops the overflow case.

**Invariant:** seed from all `C_LightSource` entities within
**view frustum + max(radius) expansion**, not view frustum alone.

**Check:** the flood-fill seed-gather tick does not filter by
`visibleIsoViewport` without expanding by `C_LightSource::radius_`.
Automated: `scripts/light-verify.py` (#2317, V3) drives the lighting demo
family's zoom×yaw×pan-distance shot matrix, parses each shot's
`DOMAIN-STATE` line, and asserts the boundary-clamp state machine
(never `SKIPPED` in-window/band, monotone residual fade out to the
window edge) — see `.claude/skills/render-debug-loop/diagnosis/lighting.md`
§"Automated light/shadow-domain harness".

### 4. AO and shadow neighbor-lookup guard band

T-012 AO reads 3-diagonal neighbors per visible face. Once T-010's chunk
streaming activates, the chunk containing each neighbor must be resident. A
face at the view edge whose neighbor chunk is unloaded produces wrong AO.

**Invariant:** resident chunk set = view-chunk set ∪ 1-chunk guard band (in
all six directions) for AO sampling correctness, in addition to the
shadow-ring from invariant #2.

**Check:** resident chunk set calculation includes this guard band when chunk
streaming is introduced.

## Sun shadow bake AABB sweep

`BAKE_SUN_SHADOW_MAP` derives its sun-space AABB from the iso-frustum
corners and a sweep of `kSunShadowMaxDistance` (64 voxels) along
`-sunDir`. That sweep is what guarantees off-screen casters within
shadow range project into the depth map even when their iso position
is outside the visible rect — same role invariant #2 plays for the
old occupancy march. Bumping `kSunShadowMaxDistance` is the lever for
longer shadows; expect proportionally larger sun-space texels (the
1024² depth map is fixed) and softer shadow boundaries. The receiver's
shadow-throw window reads this same distance (uploaded into
`FrameDataSun.sunMaxShadowThrow_`; #2320), so a caster the sweep bakes is
receivable at its full throw — the two cannot drift.

The AABB sweep governs which off-screen casters *reach* the map; a
separate concern is **in-map coverage** — a screen-space bake projects a
sparse camera-rastered caster set into sun-UV, so a near-overhead sun's
cast shadow shatters into a moth-eaten point scatter (#1717 / #2270). The
settled coverage model (the density-ratio + per-pixel-neighbour + down-ray
extrusion refutations, and the bounded `atomicMin` uniform-box splat that
fixes it, with its two byte-identity regimes + kill switch) is an
engine-level invariant future bake work must not re-derive:
[`docs/design/sun-shadow-bake-coverage.md`](../../docs/design/sun-shadow-bake-coverage.md).
Read it before touching `c_bake_sun_shadow_map.{glsl,metal}` or
`FrameDataSun.sunSplatMaxTexels_`.

## Lighting debug overlay

`IRRender::setDebugOverlay(DebugOverlayMode)` swaps the artistic
composite in `LIGHTING_TO_TRIXEL` for a false-color visualization of
the underlying lighting buffer. Use it when triaging a lighting bug
where the per-pixel input value is suspect — the overlay exposes the
exact scalar that the artistic path would multiply, so you can tell
whether the issue is in the buffer producer (`COMPUTE_VOXEL_AO`,
`COMPUTE_SUN_SHADOW`) or in the composite itself.

Modes:

- `AO` — red→green gradient of the AO factor (red = fully occluded,
  green = fully unoccluded).
- `LIGHT_LEVEL` — combined `ao × shadow` scalar painted blue→white
  (blue = dark, white = bright).
- `SHADOW` — directional sun-shadow occupancy (black = lit, magenta
  = shadowed).

Upstream passes keep running; only the final composite is replaced.
GUI pixels are unaffected because the GUI canvas early-returns out
of the lighting pass. Invoke from a creation via the engine API
(`IRRender::setDebugOverlay`) or in `shape_debug` via
`--debug-overlay <none|ao|light_level|shadow>`.

## Voxel face rasterization (which faces a voxel emits)

The voxel-pool raster's face-selection model — which of a voxel's six
faces get emitted into the canvas at a given camera orientation — is
specified in
[`docs/design/voxel-face-rasterization.md`](../../docs/design/voxel-face-rasterization.md).
The canonical model is **visible-face triplet × exposed-face mask**: the
three camera-facing faces (a pure function of the camera quaternion,
recomputed per frame) intersected with the voxel's exposed faces (the
camera-independent `exposedFaces` mask set at pool build/mutate time). A
voxel emits a face iff it is both camera-visible and exposed.

**Silhouette-riser exception for ROTATED content.** The triplet carries one
polarity per axis — correct for a convex, axis-aligned solid, which only ever
shows one polarity per axis. A *rotated* voxel footprint (GRID re-voxelize cells,
detached re-voxelize) round-to-cells into a STAIRCASE whose camera-side grazing
edge presents the OPPOSITE polarity of an axis (e.g. a +X riser where the
cardinal triplet carries X_NEG): that face is exposed and on the silhouette yet
absent from the triplet, so the strict model never emits it → see-through
"venetian-blind" gaps that appear only at the camera directions where the yaw
chirality turns that edge toward the camera. `c_voxel_to_trixel_stage_{1,2}`
therefore flip a slot's face to the opposite same-axis polarity when the triplet
face is occluded but the opposite is exposed — **gated to rotated content** (the
`visibleFaceIds.w` re-voxelize uniform OR the per-voxel `VoxelReserved::kRotatedEmit`
marker, reserved bit 2, set by `REBUILD_GRID_VOXELS`). Non-rotated content keeps
the strict triplet, so the single-canvas and per-axis fast paths stay
byte-identical. The flipped face is a back face on convex content (it would lose
the depth `atomicMin` anyway, iso (pixel,depth) being a bijection of (x,y,z)) —
the gate just avoids the wasted emit + the per-axis store's sub-pixel drift.

This supersedes the historical "always emit the three lower-coordinate
faces (−X, −Y, −Z)" model, which was correct only at cardinal yaw 0 and
caused the stripe/checkerboard artifact (#1256) at every other cardinal.
Treat the six faces as six distinct enum values, **not** three axes each
with a ± sign — see the design doc for why that distinction is what fixes
the bug, and how the same model generalizes to per-entity SO(3) (#1272)
and camera pitch (PR #1265). Read it before touching
`c_voxel_to_trixel_stage_{1,2}`, `c_voxel_visibility_compact`,
`c_compute_voxel_ao`, `c_lighting_to_trixel`, or the `C_VoxelPool` face
metadata.

**Smooth camera yaw between cardinals** (interpolating voxel-center
positions, not just deforming face shapes) is a separate architecture, now
**in implementation** (T1 #1308 / T2 #1309 merged; T3 #1310 in flight): route
each visible face axis to its own deformed trixel canvas and composite the
three by depth at the framebuffer. Bounded by a minimum on-screen trixel size.
The per-canvas trixel→framebuffer **parity** was the #1 correctness risk; it is
resolved by a **forward-scatter** composite (Option 4) — each non-empty canvas
cell is scattered to its true deformed footprint with no gather inverse, so the
single-global-parity stripe class (#1256) cannot occur. Spec + the rejected
gather/inverse alternatives:
[`docs/design/per-axis-trixel-canvas-rotation.md`](../../docs/design/per-axis-trixel-canvas-rotation.md).

**View-visibility overflow lane (epic #2331).** The cardinal-keyed store
above elects one winner per store cell, which is the **cardinal**-visible
face set — under residual yaw the **view**-visible set can include a coset
member (two faces separated by world `t·(1,1,1)`) the store never wrote. A
bounded, rotating-only overflow lane (the view-mask write folded into the
mode-0 store since #2487 plus one extra `resolveMode` append pass in
`c_voxel_to_trixel_stage_1_body.{glsl,metal}`, a second scatter pass in
`v_peraxis_scatter.glsl`, and a relight pass in
`c_light_overflow_faces.{glsl,metal}` at the tail of `LIGHTING_TO_TRIXEL`)
carries exactly `viewVisible ∖ cardinalWinners`, gated off at cardinal
(byte-identical fast path). See
[`docs/design/per-axis-trixel-canvas-rotation.md`](../../docs/design/per-axis-trixel-canvas-rotation.md)
§"Current contract — view-visibility overflow lane" for the two-set model,
the pass sequence, the entry format, and the measured cap/cost numbers.

**Overflow draw order is canonical on flagged pools only (#2479).** Entry
index IS draw order in the scatter's overflow branch, and the append assigns
indices with `atomicAdd`, so equal-key entries used to resolve their depth
contest by run-variant arrival order (10 distinct rotated-shot hashes / 10
runs at wave amplitude 5). `c_per_axis_overflow_sort.{glsl,metal}` canonically
orders the entries by full record value between the append and the indirect
draw — **dispatched only when the ticking pool's `storeTiesPossible_` flag is
set AND the last completed frame's overflow list was nonempty** (#2479 (ii)).
Both terms are decided CPU-side so the sort semantics never fork by backend;
the count term is the retired-frame `ctrl[1]` readback the cap-drop warn
already performs, so the common interactive case (flagged pool, empty list)
pays zero dispatches with no added sync. Its one-frame lag scopes the
determinism guarantee to **steady-state frames**: an empty→nonempty
transition draws exactly one frame unsorted, self-healing next frame.
Unflagged pools keep order-resolved cross-cell band-code ties as a documented
residual class; a measured repro there widens the flag recompute, it does
**not** un-gate the sort. The sort is a bitonic network whose span is derived
**GPU-side** from `ctrl[1]` (a live read would sync-stall), so its traffic
scales with the live population rather than the entry cap.
Cost + the residual-class revisit trigger are in the design doc's §"Draw order
is canonical" block.

**Near-cardinal corner spikes on coarse cubes (#1883) — fixed by the epic
#1933 analytic coverage, now on both backends.** Historically the
forward-scatter composite let the *dilation* decide coverage: each cell grew
to a conservative quad whose per-edge margin `C ≈ 0.5·|n|` carried
foreshortened-silhouette coverage (anti-dashing), capped by
`kScatterMiterLimit` (`M = 2.0`). That made crisp corners and gap-free
foreshortened faces mutually exclusive — crisp corners need `M·C ≲ 2px` while
bridging the inter-cell gap forces `C` to ≈ half the on-screen cell pitch — so
near-cardinal poses (≈84° per-axis) on **coarse** cubes showed convex-corner
**spikes** over-extending into background by ≈ `M·C`, and no scalar
`(margin-ceiling, miter-limit)` pair fixed both.

The resolution was to **split the two jobs**. `scatterConservativeDilation` is
now only a *visit-bound*: a fixed `kScatterDilateMarginPx` (~1px) per edge,
just wide enough that the rasterizer visits every fragment the true footprint
could touch. Coverage is decided per-fragment in `f_peraxis_scatter` by
`scatterAnalyticEdgeCoverage` (`ir_iso_common.{glsl,metal}`), from the
fragment's position in the true `[0,1]^2` footprint plus a per-edge
interior/boundary classification: interior edges fill solid (closing the
inter-cell seam), boundary edges get exact sub-pixel box coverage (crisp
corners, no dashing). The neighbour taps the old note recorded as
*rejected on cost* are what pay for the classification — but two per vertex,
not 4: each axis's polarity-interior edge is interior unconditionally and
skips its tap.

Landed Metal-first (#1937), then GL (#1938); the two shaders are hand-mirrored,
so keep them in lockstep. Measured on the canonical coarse-cube probe
(`IRShapeDebug --spin-shape box --spin-shape-voxel --yaw-sweep --pivot-origin
--zoom 4`, 24 frames, Windows/GL, 2026-08-21): before **JITTER**, x excursion
1.92px against the 0.50px zoom-4 bar; after **SMOOTH**, 0.01px.

Still open: **#1939 (C3)** retires the residual margin / miter / yield tower
now that neither backend needs it to decide coverage. Hardware conservative
rasterization / MSAA remains unused — the analytic model was chosen precisely
because Metal exposes no conservative-raster API, so do **not** reach for
`GL_NV_conservative_raster` on the GL side (it would fork the two backends).

**Accepted sub-pixel yaw-sweep centroid residual (voxel content) — #2469.**
On the canonical Z-yaw-invariant probe (voxel cylinder, `--yaw-sweep`) the
per-axis path leaves a sub-pixel centroid residual that scales with zoom. It is
**content + sampling, not a positioning defect**, and is accepted as intentional
drift. Measured on macOS/Metal, 2026-07-28 (24-frame sweeps, one quadrant):

| probe | x excursion | x rev | x residual | y excursion | y rev | y residual |
|---|---|---|---|---|---|---|
| voxel cylinder, zoom 2 | 1.68px | 3 | 0.85px | 2.69px | 2 | 0.21px |
| voxel cylinder, zoom 4 | 1.26px | 5 | 0.57px | 5.29px | 0 | 0.19px |
| voxel cylinder, zoom 8 | 2.83px | 5 | 1.25px | 10.79px | 0 | 0.18px |
| **SDF cylinder** (continuous-geometry control), zoom 4 / 8 | 2.00px | 0 | 1.43px | 4.00 / 10.00px | 0 | 0.95px |

> **The excursion columns above are historical — they measure a probe the recipe
> no longer uses (#2606).** This table predates #2547's default pivot focus
> (landed 2026-07-31), whose residual orbit (#2641 → #2758, in review) dominates
> the metric on an *unpinned* sweep: same recipe, same host/backend,
> x excursion re-measured 19.97 / 38.18 / 76.81px at zoom 2/4/8 on 2026-08-07,
> ~30x this table. The canonical rotation gate is now the **`--pivot-origin`
> pinned** sweep, which removes the orbit rather than tolerating it and reads
> 0.62 / 0.18 / 0.06px healthy — the live bar table lives in §"Verifying temporal
> stability" and is the only place to read numbers for a gate. The **residual**
> columns are unaffected by all of this and still reproduce at zoom 2 (0.85px);
> at zoom 4/8 the unpinned probe reads 1.78/2.70px against the 1.50px bar, which
> is **#2907**'s finding, not this one — and the pinned probe's 0.41/0.08/0.03px
> says that redness is orbit-inflated.

Three findings ground the accept, each measured rather than asserted:

1. **The 4-bit face-frac lane is inert on this probe.** The probe's cylinder is
   an odd-size (9³) origin-centred grid, so every active voxel sits on the exact
   integer lattice: `fracInCell ≡ 0` after `snapNearIntegerVoxelPosition`, and
   `fracToFrac4` encodes exactly 8/8/8, for which the scatter's decoded origin
   adjustment (`peraxis_scatter.metal:281-284`, `v_peraxis_scatter.glsl:243-246`)
   evaluates to exactly `0.0`. Staging a diagnostic scatter shader with that
   adjustment deleted produced **`img_diff` = 0 on all 24 frames at both zoom 4
   and zoom 8**, with bit-identical probe metrics. A lane pinned at its zero
   point cannot produce a yaw-varying wobble. (`emitDeformedFace` is likewise
   off-path: the per-axis store writes one cell per face centre.)
2. **The voxel path's residual is LOWER than the defect-free control's.** The
   SDF twin — continuous geometry, no voxel store — reads 1.43px at zoom 4 and 8,
   above the voxel path at every zoom. The residual is a floor the probe itself
   carries, not a per-axis excess.
3. **The scaling fits content anisotropy, not a pixel-domain defect.** A voxelized
   cylinder is only 4-fold symmetric — only the *continuous* cylinder is
   Z-yaw-invariant, so the rotating staircase's true silhouette legitimately
   wobbles. A pixel-domain positioning defect would sit ~constant in px across
   zooms; the pre-#2427 overflow defect measured 2.93px residual / 5.37 Δmax at
   zoom 4, ~2-5x this floor.

There is no local fix: the residual is content plus the sampling floor. Note
this is **not** addressed by the #1933 analytic coverage that fixed the #1883
corner spikes above — that changed which fragments a face claims, not the
content anisotropy or the destination sampling grid. The remaining direction
would be hardware MSAA on the scatter pass, which nothing schedules today.

**The `reversals == 0` criterion misfires on these probes — see the gate recipe
in §"Verifying temporal stability" for the shipped flags and the limitation
they carry.**

Per-axis voxels **cast** sun shadows under continuous yaw via
`RESOLVE_PER_AXIS_SCREEN_DEPTH` (#1435), which collapses the three face-local
per-axis canvases into one screen-space cardinal-layout depth texture so
`BAKE_SUN_SHADOW_MAP` casts them through its existing cardinal recovery — the
per-screen-pixel flattening the raw face-local store lacks (which is what
caused #1380's cross-face self-occlusion). Invariant + the cast/receive
agreement + the Metal `threadgroupSizeForFunctionName` requirement:
[`docs/design/per-axis-sun-shadow-resolve.md`](../../docs/design/per-axis-sun-shadow-resolve.md).

Per-axis lighting **receive** recovery: an absolute-position consumer of the
per-axis store (light-volume sample, sun-shadow receive, overflow relight)
must recover positions via `perAxisCellToWorld3DSubCell`
(`ir_per_axis_lighting.{glsl,metal}`) — the lattice-only
`perAxisCellToWorld3D` drops the encoding's sub-cell frac and lands up to
half a world cell inside the solid for fractional-positioned content (see
#2251). Relative-position consumers whose math provably cancels the in-plane
offset (AO's outward-normal height dot) keep the cheaper lattice form.

## SDF (`SHAPES_TO_TRIXEL`) vs voxel-pool (`VOXEL_TO_TRIXEL_*`) parity

A `C_ShapeDescriptor` (SDF, GPU-evaluated) and a `C_VoxelSetNew` carved
from the same SDF (CPU-quantized at construction) are intentionally NOT
trixel-for-trixel identical at every render configuration. The SDF
shader's `smoothMode` gate (`c_shapes_to_trixel.glsl`:
`smoothMode = (renderMode != 0) && (subdivisions > 1)` — `renderMode`
is the `SubdivisionMode` enum value, 0 = `NONE`, 1 = `POSITION_ONLY`,
2 = `FULL`) selects between two paths:

- **`smoothMode == false`** — `SubdivisionMode::NONE` always, or
  `POSITION_ONLY` / `FULL` with effective `sub == 1`. Bit-identical to
  the voxel pool at the silhouette. The SDF shader skips the analytical
  surface solver and routes through `snapLatticeWalk`. (The
  `subdivisions > 1` half of the gate was added in commit 87d2b681 so
  `sub == 1` falls back to the parity-gated lattice walk instead of the
  analytical 2x3 emit that aliased against the voxel-pool tiling.) The
  walk only evaluates iso pixels with `(isoRel.x + isoRel.y) & 1 == 0`
  — the same even-parity set integer voxels project to — and rounds
  each candidate via `roundHalfUp` to the same integer voxel the CPU
  carve evaluates. The SDF surface check uses `<= 0.5` with no bias,
  matching CPU's `> kSurfaceThreshold` exclusion exactly.
- **`smoothMode == true`** — `POSITION_ONLY` or `FULL` with effective
  `sub > 1`. Silhouette differs by design. The SDF shader runs
  `findSurfaceDepth` analytically: each iso sub-pixel solves for the
  smooth surface depth, producing a continuous (sub-pixel) silhouette.
  The voxel pool runs `faceMicroPositionFixed` over `sub²`
  micro-positions per active voxel, producing a stair-stepped silhouette
  that snaps to the discrete carved voxel set scaled up by `sub`. At any
  given silhouette iso pixel one path may emit where the other doesn't
  — that's the "lone trixel" / "half-extent voxel" effect on a
  side-by-side comparison. It is NOT a bug; it is the entire reason
  the smooth analytical path exists alongside the voxel-pool path.

The `kSdfBiasEpsilon` (1e-3) used in the analytical surface check
(`sdf <= 0.5 + kSdfBiasEpsilon`) is for FMA-noise stability across
frames at integer-edge depths, not a deliberate widening of the surface
shell. It can keep one extra sub-pixel column at borderline analytical
hits where the CPU carve drops; the visual effect is bounded by the
epsilon and only visible at static analysis with a per-pixel diff.

If you need bit-identical voxel-pool output from a `C_ShapeDescriptor`,
the only correct configuration is `SubdivisionMode::NONE` (or any
`sub == 1` configuration) where both paths route through the same
parity-gated lattice walk. At higher subdivisions, choose the path
based on intent: voxel pool for "cubes-of-cubes" stylization, SDF for
smooth analytical silhouettes. The lighting demo's `kShots[]`
zoom8/zoom16 captures showcase the difference for visual reference.

Per D2 (Epic D #937, SDF runtime restriction), `C_ShapeDescriptor` is
effects-only for primary entity authoring. The silhouette delta
documented above is intentional and **not a bug to fix**: effects
entities (auras, shadow occluders, soft glows) do not require trixel
parity with voxel-pool primary shapes.

## Gotchas

- **Hardcoded uniform-buffer bind points.** Indices like
  `kBufferIndex_FrameDataVoxelToCanvas = 7` appear in both C++ and GLSL. A
  mismatch is silent — wrong uniforms, no error.
- **GPU buffer bind-point budget is full (0–30).** Every `kBufferIndex_*`
  (`ir_render_types.hpp`) is occupied and Metal has no free buffer index
  past 30. A change that needs a new SSBO/UBO while the voxel/per-axis path
  is active must **reuse an existing binding transiently** — bind the new
  buffer onto an occupied index via `Buffer::bindRange`/`bindBase` for the
  dispatch, then restore — never claim a 31st index. A plan that adds a GPU
  buffer should check this before settling on a binding, or it designs an
  approach that's impossible to wire mid-implementation.
- **Camera-iso offset pivots about the focus (`getEffectiveCameraIso`).**
  Any producer that positions world content relative to the camera — voxel
  raster, SDF main-canvas placement, per-axis scatter base, trixel→trixel
  composite, particles, the framebuffer pan/blit, cull viewports, the
  detached entity-canvas composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`), and the
  picking/hover inverses — must read `IRRender::getEffectiveCameraIso()`,
  **not** `getCameraPosition2DIso()`. The effective offset applies the
  `RotationPivotMode` correction (#1352) so camera Z-yaw pivots about the
  on-screen focus instead of the world origin; in `ORIGIN` mode and at
  `visualYaw == 0` it returns the raw offset, so the cardinal fast path is
  byte-identical. For the DEFAULT (no explicit `setRotationPivotFocus`) focus,
  **the iso depth is latched and the point is derived live**:
  `RenderManager::beginFrame` re-derives the depth once per frame from a
  composite-depth readback under the viewport center (#2547), so every stage in
  a frame reads one value, while `getDefaultRotationPivotFocus` recomputes the
  point from the current `cameraIso` on every call. Don't move that derive
  into the pipeline — a mid-frame re-derive splits the frame across two
  pivots — and don't "optimize" it into a latched world point:
  `IRMath::cameraMoveRelativeToYaw`'s pan pre-compensation inverts
  `d effCam / d cameraIso`, which only holds while the focus tracks the camera
  (a frozen point makes interactive pan overshoot and pop back at any non-zero
  yaw; `test/render/camera_pan_pivot_test.cpp` guards it). Reading the raw offset at a new producer site silently
  reintroduces the off-origin orbital swing while every other layer pivots
  correctly. The detached composite reads the effective offset only for the
  screen PLACEMENT of the canvas quad; its de-tile gather parity stays keyed
  to the entity's FIXED world iso (`-entityIso`), which is camera-independent.
  Lighting-grid centering (`camera_anchor`), screen-space sprites, and debug
  overlays intentionally stay on the raw offset.
- **Distance texture clear.** Cleared to `kTrixelDistanceMaxDistance`
  (65535, **not** INT32_MAX). Voxels and shapes both write smaller values
  via `imageAtomicMin`; the clear value acts as the "nothing here" background.
  The shape SDF helpers use a separate `kInvalidDepth = 0x7FFFFFFF` (INT32_MAX)
  constant to signal "ray missed" and skip writing — don't confuse the two.
  If a clear is skipped, stale depth causes flicker.
- **Persistent mapped buffers.** `HoveredEntityIdBuffer` is
  `PERSISTENT | COHERENT`. Reading it too early (before GPU write) returns
  garbage from the previous frame.
- **Dispatch limits.** `kMaxDispatchGroupsX = 1024` (≈1M voxels before
  hitting the second dispatch dimension). Very large voxel counts slow.
- **Mode branches in hot compute kernels: compile-time-specialize, don't
  uniform-branch.** A runtime *uniform* branch is predicated, not skipped —
  its instructions decode on every invocation even when never taken, taxing
  the kernel at all inputs. Prefer a `#if` specialization from one shared
  source body. See `docs/design/gpu-stage-timing-cost-model.md` §2.
- **Budgeting a multi-dispatch Metal pass: count ~64 µs fixed cost per
  compute dispatch** — measured on the #2479 overflow sort, invariant under
  kernel fusion, memory traffic, and threadgroup occupancy; see
  `docs/design/per-axis-trixel-canvas-rotation.md` §"Metal per-dispatch fixed
  cost" for the three-arm numbers before trusting an encoder-cost model.
- **Render mode × subdivision × zoom.** `SMOOTH` mode multiplies positions
  by `subdivisions × zoom`. Changing any of these mid-frame is a perf
  cliff and can desync chunk visibility.
- **Canvas destruction mid-frame.** `C_TriangleCanvasTextures::onDestroy()`
  frees GPU textures. If a canvas entity is destroyed while a system still
  references the canvas id, the next frame draws to freed handles.
- **VAO lifetime.** `QuadVAO` is named-resource-registered once at init.
  Do not destroy it from scripts; every `*_to_framebuffer` system depends
  on it.
- **Canvas render order matters.** Multiple canvases write to the same
  framebuffer in registration order. Stage 2 must complete for a canvas
  before the next canvas reads from it.
- **GUI canvas scaling.** GUI canvas is sized `mainCanvasSize / guiScale`
  by default; `IRRender::setGuiCanvasFullResolution()` instead sizes it to
  the native framebuffer resolution (1 GUI trixel == 1 framebuffer pixel) so
  GUI text/widgets render small and crisp — the calling creation owns laying
  its GUI out for the finer coordinate space. Changing `guiScale` without
  resizing the GUI canvas entity breaks coordinate mapping.
- **CPU texture writes order via the command buffer on Metal.**
  `Texture2D::subImage2D` and `clear()` write canvas textures, but on Metal
  the per-frame work is deferred: `clear()` enqueues a GPU blit and the
  `subImage2D` backend stages + blits through the frame's command buffer so a
  CPU `replaceRegion` can't be clobbered by a clear that *executes* later
  (the OpenGL path is already submission-ordered). The upshot: a system that
  writes a canvas via `subImage2D` (the widget render systems, fog-of-war) and
  one that clears it (`TEXT_TO_TRIXEL` clears the GUI canvas each frame) compose
  correctly **only because** both routes land on the command buffer in encoder
  order. If you reintroduce an immediate CPU texture path on Metal — or move a
  clear off the command buffer — CPU writes made earlier in the frame silently
  vanish under the deferred clear (the #1436 invisible-widgets bug). Mixing a
  `subImage2D` write with a same-frame CPU `getBytes` readback of the same
  texture still needs an explicit commit+wait (picking already does this).
- **Foreign-canvas R32I image reads in a second in-tick compute dispatch
  return empty on Metal (#1640).** An R32I distance texture whose contents
  were produced by `imageAtomicMin` (the scratch-buffer path —
  `VOXEL_TO_TRIXEL_STAGE_1` and the other `functionUsesImageAtomicScratch`
  kernels; see `metal_runtime.hpp`) is reliably readable on Metal by the
  canvas's own downstream stages and by later same-frame passes that read it
  as a sampled / `access::read` texture (e.g. `LIGHTING_TO_TRIXEL`). But
  binding a **non-main** canvas's distance texture as the read source of a
  **second** in-tick compute dispatch (the rejected per-caster sun-shadow
  bake reading a detached canvas's own model-frame distances, PR #1626's
  literal Q2-(a)) returns the clear value (65535) for every pixel — that
  canvas's data is not delivered to the read, even though a forced-position
  write proves the dispatch runs. The sanctioned pattern (mirrored from the
  per-axis cast precedent) is to **resolve** foreign distances into a
  main-canvas-layout texture via an `imageStore`-written, real-texture-memory
  resolve pass first, then read THAT — never bind a foreign model-frame R32I
  texture as a bake/compute read input. Invariant: the sun-shadow bake only
  ever reads main-canvas-layout depth sources. The underlying backend gap is
  tracked as #1640; until it lands, resolve-then-bake is mandatory.

  **The own-canvas half has a primitive: `RenderDevice::resolveImageAtomicScratch`
  (#2488).** The rule above governs reading a *foreign* canvas's distances. The
  separate, narrower problem is a canvas's OWN distances that no pass ever
  materialized into the texture — on Metal the atomics land in the scratch
  buffer, so a texel the stage-2 winner tap skips stays at the 65535 clear
  sentinel in the texture even though the scratch holds real depth. Every
  texture-reading consumer (`c_bake_sun_shadow_map`, `COMPUTE_VOXEL_AO`,
  `COMPUTE_DISTANCE_HIZ`) then reads a hole GL does not have. Call
  `IRRender::device()->resolveImageAtomicScratch(texture)` after the atomic
  passes and before the first texture reader; it is a defaulted no-op on GL
  (whose `imageAtomicMin` writes the texture directly) and a whole-texture blit
  on Metal. `VOXEL_TO_TRIXEL_STAGE_1` is the reference call site — after the
  shadow-feeder dispatch, guarded on a non-empty feeder ring
  (`IRPrefab::SunShadow::shadowFeederRingNonEmpty`).

  **What makes the whole-texture blit safe is that `clearTexImage` *ensures*
  the scratch rather than looking it up.** A canvas clears its distance texture
  before it ever image-binds it, so a lookup would find no scratch on the first
  tick and skip the clear's mirror — and a freshly allocated scratch is
  zero-filled, i.e. NEAREST depth, not the 65535 empty sentinel. The resolve
  would then stamp a solid surface over the whole canvas for that frame (and
  every `atomicMin` would lose against the zero, so stage 2's `scratch == depth`
  winner tap matches nothing). Because the clear seeds it unconditionally,
  untouched texels resolve their own clear value back onto themselves. A new
  R32I texture that is resolved must therefore also be *cleared* through
  `clearTexImage`, not merely bound.

  Two boundaries worth keeping straight. It is **not** a substitute for
  resolve-then-bake: that rule is about a foreign model-frame texture reaching a
  bake at all, and it stands unchanged. And it needs **no**
  `functionUsesImageAtomicScratch` entry — the resolve is a blit, not a kernel,
  so the slot-16 alias (#1619) is untouched.
- **Sampler and image binds are mutually exclusive per texture unit on Metal —
  the most recent bind wins (#2350/#2360; supersedes the #1812 workaround).**
  Metal flattens the sampler and image namespaces into ONE `setTexture` slot
  space. The backend keeps two sticky tables, so `bindMetalTexture` /
  `bindMetalImageTexture` (`metal_runtime.cpp`) each **evict the sibling
  entry** at that unit; `bindComputeResources` then flushes whichever survived.
  Bind your reads however the kernel declares them — the last bind at a unit is
  what the dispatch sees.

  The former guidance — "bind compute texture reads as IMAGES so they win the
  slot" — is **retired**; it was a per-kernel workaround for a flush-order bug
  that can no longer occur, so existing image-bound reads are still correct but
  no longer required. GL is structurally immune (separate image/texture-unit
  namespaces), so a GL-only smoke never catches this class (see #1812, #2350).

  **Residency rule:** a bind at unit N stays resident until the next bind of
  *either kind* at unit N — a dispatch boundary does not clear it. Carrying a
  bind across dispatches is therefore supported, and the engine relies on it:
  `LIGHTING_TO_TRIXEL` binds units 6/7 on its main-canvas dispatch and the
  per-axis dispatches ride on them, rebinding only 0/1/2/4. What mutual
  exclusion removes is *cross-kind* survival — a sampler bind at unit N now
  evicts an image another pass left there, and vice versa. So before relying on
  a leftover bind, confirm nothing binds that unit, **in either table**, between
  the producer and the consumer. The widest such range in the frame is
  `VOXEL_TO_TRIXEL`'s chunk-occlusion pass, which sampler-binds units 0–11
  every frame the cull runs.

  The same stickiness makes resource **destruction** a hazard: the tables hold
  non-owning pointers, so destroying a bound resource leaves a dangling entry
  the next dispatch's flush re-binds — `objc_retain` on the freed handle
  segfaults (#2412: a rotation-lifecycle buffer bound at a slot no
  cardinal-path pass re-binds, freed at the yaw→0 per-axis release). The
  backend therefore scrubs its tables on destruction — `untrackMetalTexture` /
  `untrackMetalBuffer` in the Metal texture/buffer destructors
  (`metal_runtime.cpp`) — mirroring GL's delete-unbinds semantics. A new
  resource type that lands in any sticky table must untrack itself the same
  way.

  **The render-target pair is a sticky table too, and it is the one this rule
  keeps getting read as excluding.** `MetalRuntimeState` holds non-owning
  `MTL::Texture *` in four places `untrackMetalTexture` must scrub, not two:
  the `textures_` / `imageTextures_` bind-slot arrays **plus** `colorTexture_`
  and `depthTexture_`, written by `bindMetalFramebufferRenderTarget` and
  consumed by `createRenderEncoder` (`metal_render_impl.cpp`), which both
  `setTexture:`-retains the handle and dereferences it for the viewport. `untrackMetalTexture` scrubs all four
  (#2808); a destroyed attachment falls the runtime back to the **default**
  target rather than nulling in place, because a null colour attachment with
  `useDefaultRenderTarget_` still false makes `createRenderEncoder` return
  `nullptr` for every later pass — rendering nothing, silently.
  `~MetalFramebufferImpl` performs the same scrub as an independent second
  closing path.

  A **fifth** holder exists and is deliberately outside that scrub:
  `imageAtomicScratchBuffers_`'s map key, cleared at
  `releaseImageAtomicScratchBuffer` instead, because the entry owns the mapped
  `MTL::Buffer` and has to release it rather than just drop the reference (the
  reason is stated at the field itself in `metal_runtime.cpp`). Five holders,
  four scrubbed — so a newly added slot is the *sixth*, not the fifth.
  `test/render/metal_sticky_untrack_test.cpp` regression-guards the four
  scrubbed slots **by name**; it has no reflection over `MetalRuntimeState`, so
  a sixth handle-holding field would fail nothing. Scrubbing it and covering it
  stays on whoever adds it — the same "prose read as stronger than what runs"
  gap that hid #2808 for two resource types.
