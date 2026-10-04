# engine/render/ — trixel render pipeline

This module owns graphics primitives: the voxel-to-trixel pipeline, GPU
resources, camera and viewport state, canvases, framebuffers, and the OpenGL
and Metal backends.

## Working agreements

- Creations include the public entry point
  `engine/render/include/irreden/ir_render.hpp`, never internal render headers.

### What belongs in engine/render/ vs engine/prefabs/irreden/render/

- `engine/render/` owns device and pipeline primitives every creation needs.
  Opt-in feature state lives in `engine/prefabs/irreden/render/` behind a
  prefab-scoped API.

### Name identifiers after the rendering effect, not the caller

- Names in this module (C++ types, flags, shader identifiers, binding names,
  comments) describe rendering effects, never the first feature or caller.
- System registration, ordering, and tick contracts live in
  [`engine/system/CLAUDE.md`](../system/CLAUDE.md).
- Current feature-API exceptions are tracked in
  [`.fleet/status/render-api-relocations.md`](../../.fleet/status/render-api-relocations.md);
  feature PRs do not edit that register.

## Commands and validators

### Verifying render changes

Canonical commands: the [validation index](../../docs/agents/VALIDATION.md)
(per-frame jitter: its probe plus the camera contracts below). Render changes
commonly need `header-checks`, `render-debug-loop`, `render-verify`,
`backend-parity`, `cull-verify`, and the relevant `scripts/*-verify.py`
metric; Codex image evidence: [`CODEX.md` § Rendering conversations](../../docs/agents/CODEX.md#rendering-conversations).
Changes with no visual effect (docs, tests, mechanical, build-only) need no
captures.

## Pipeline contracts

### The pipeline, one frame

- Pipeline order is INPUT, UPDATE, then RENDER. Within RENDER, geometry writes
  canvas depth/color/id before AO, shadow, and lighting consume them;
  compositing precedes framebuffer output, which precedes screen output.
- Multiple canvases render in registration order. A canvas's geometry stage
  must finish before another canvas reads its textures.
- `C_TriangleCanvasTextures::onDestroy()` frees GPU textures. Never destroy a
  canvas while a registered system can still reference its entity.
- `QuadVAO` is registered once during initialization and is required by every
  `*_to_framebuffer` system; scripts must not destroy it.
- Changing subdivision mode, subdivision count, or zoom mid-frame can desync
  visibility and raster scale. Apply such changes at a frame boundary.

## Shaders and backend parity

- GLSL lives in `engine/render/src/shaders/`; Metal sources live in its
  `metal/` child. Register added or renamed shader paths in
  `render/shader_names.hpp`. Naming follows the
  [baseline](../../docs/agents/CLAUDE-BASELINE.md#naming).
- A shader fragment may self-include only a macro-free prerequisite.
  Macro-parameterized fragments remain in each wrapper's explicit ordered
  include list after the wrapper's `#define`s.
- `cmake/run_glsl_reserved_word_check.cmake` rejects GLSL reserved words as
  `.glsl` identifiers (NVIDIA GL fails, Metal doesn't); rename the `.metal` twin.
- Derive large SSBO word indices at runtime; constant ones cost NVIDIA minutes per cold link.
- NVIDIA GL defers a self-fed indirect dispatch; what releases it: `detail::forEachOverflowSortStep`.

### Metal compute kernel threadgroup registry

- Every dispatchable `c_*.metal` kernel, excluding `*_body.metal` fragments,
  needs its real threadgroup size in `threadgroupSizeForFunctionName`.
  `functionUsesImageAtomicScratch` must exactly match kernels whose resolved
  source declares an atomic parameter at `kMetalImageAtomicScratchSlot`.
  Header conventions validate both registries.
- Metal AOT compilation treats top-level kernel wrappers as translation units.
  Include fragments must use the excluded `*_body.metal` or `ir_*.metal`
  naming forms. The opt-in metallib has no runtime consumer.

### Metal negates clip `position.y`; GL does not

- Every Metal full-screen or quad vertex stage negates clip-space
  `position.y`; its GLSL twin does not. This is the backend origin adapter.

### Trixel→framebuffer hover: raw texel, no parity shift

- Normal voxel display preserves voxel-face footprints; raw trixel texels are
  a debugging view. Revoxelized private canvases use undilated
  `LOCAL_TRIANGLES` with local parity and row-corrected queries; plain
  detached canvases keep `SOURCE_FACES` via continuous quad drawing. The
  compositor follows the effective producer layout, never depth scaling or
  world position ([local-triangle contract](../../docs/design/detached-local-triangles.md)).
  Lattice agreement cannot certify connected source faces; use the
  [source-face gate](../../docs/design/trixel-face-reconstruction-validation.md).
  General canvas producers keep their rectangular storage contract.
- **Hover identity follows display identity:** the gather gates on
  `floor(displayOrigin) == IRRender::mouseCanvasTexelWorld()` and every read,
  hover entity id included, samples that raw texel; the triangle-lattice
  shift stays out of the hover path. Read the
  [parity-shift design](../../docs/design/trixel-parity-shift-442-investigation.md)
  before changing it; the executor is `IRShapeDebug --gui-test`'s `hover_parity_*` shots.
- Gather interpolates [centered texel units](../../docs/design/trixel-gather-sampling.md);
  normalized UV rescaling can select the wrong half at integer boundaries.
- CPU frame-data structs and shader blocks must agree on field order,
  `std140` padding, and binding index. Every hard-coded binding has a matching
  `kBufferIndex_*` constant.
- Metal buffer indices 0–30 are occupied. A pass needing another buffer must
  bind an existing slot transiently and restore the prior binding itself.

## GPU resource contracts

- `getNamedResource` asserts on a miss; use `getNamedResourceOrNull` only
  when absence is a supported pipeline configuration.
- `ResourceId`s are pooled and reused FIFO: never cache one past its owner's destroy hook.
- Fresh GPU allocations are undefined: prime persistent or coherent
  prior-frame readbacks (statistics rings included) before sampling them.
- Clear trixel distance textures to `kTrixelDistanceMaxDistance`; the separate
  SDF miss sentinel is `kInvalidDepth`. Skipping the clear exposes stale depth.
- On Metal, sampler and image binds share one texture-slot namespace: the
  latest bind of either kind wins and stays resident across dispatches, so
  account for both tables. A resource type in a sticky table untracks itself
  on destruction; destroyed attachments fall back to the default render target.
- Metal R32I image atomics land in scratch storage. For a canvas's own
  texture, call `resolveImageAtomicScratch` after atomic passes and before its
  first reader, having cleared it through `clearTexImage`. A later dispatch
  consuming a foreign canvas's atomic depth resolves it into a
  main-canvas-layout texture first.
- Metal `Texture2D::clear()` / `subImage2D()` writes are ordered through the
  frame command buffer; a same-frame CPU `getBytes` read still needs an
  explicit commit and wait.

## Camera and raster contracts

### Iso-depth-axis invariant (world-camera Z-yaw-only for GRID)

- GRID rendering assumes world `(1,1,1)` is the iso-depth axis and supports
  camera Z-yaw only. Pitch and roll invalidate integer raster, picking,
  hitbox, drag, and SDF-cull shortcuts; DETACHED rendering is axis-agnostic.
  See the [consumer map](../../docs/design/iso-depth-axis-invariant.md).
- World-content placement, world-anchored sprites and debug overlays read
  `getEffectiveCameraIso()`; lighting-grid anchoring deliberately uses the raw
  camera offset. The default pivot depth latches once in `beginFrame`; its
  focus point follows the current camera ([camera-pivot contract](../../docs/design/camera-yaw-pivot.md)).

### Voxel face rasterization (which faces a voxel emits)

- Voxel faces follow `visible-face triplet × exposed-face mask`. Only marked
  rotated GRID cells on the non-revoxelized legacy cardinal raster may emit
  opposite-polarity risers and dual faces. Continuous per-axis quads and
  revoxelized detached canvases use the strict triplet; see the
  [face-rasterization model](../../docs/design/voxel-face-rasterization.md)
  and the [detached-face-normal contract](../../docs/design/detached-face-normals.md).
- Continuous-yaw GRID rendering uses three face-local canvases plus a
  forward-scatter composite. The camera-yaw path rasterizes exact finite
  face quads; its overflow lane retains every exposed cardinal-store loser
  for framebuffer depth arbitration, with no face-origin visibility mask.
  Its ordering and capacity constraints live in the
  [per-axis design](../../docs/design/per-axis-trixel-canvas-rotation.md).
- A per-axis consumer recovering an absolute world position applies the
  encoded sub-cell fraction: receivers use `perAxisCellToWorld3DSubCell`; the
  sun-shadow cast bridge quantizes in the face-local frame before composing
  the rotated basis. Lattice recovery suits only relative math that cancels
  the in-plane offset.
- SDF and voxel-pool silhouettes are bit-identical only at effective
  subdivision one; above it SDFs are analytically smooth and voxel pools keep
  the carved lattice silhouette, intentionally. Representation choice, shared
  geometry expectations and profiling workloads:
  [voxel and SDF rendering](../../docs/design/voxel-and-sdf-rendering.md).

## Lighting contracts

### Lighting culling invariants

- Light-occlusion-grid construction iterates the full voxel pool and never
  applies `visibleIsoViewport`; a visibility-freeze check is not a viewport cull.
- Light seeds include off-screen sources whose radius reaches the visible or
  camera-anchored domain. `light-verify` validates boundary clamping and fade.
- Chunk streaming must include the sun-direction shadow ring. Any world-space
  neighbor sampler also requires a one-chunk resident guard band.

### Sun shadow bake AABB sweep

- With sun shadows on, visible geometry bounds include the shadow-feeder
  sweep, and the sun-map bake and receiver share `kSunShadowMaxDistance`. Read
  the [coverage design](../../docs/design/sun-shadow-bake-coverage.md) before
  changing the bake kernel or splat controls.

## Performance and lifecycle pitfalls

### Gotchas

- **`WindowMode::OFFSCREEN` replaces the swapchain, not the pipeline.** Each
  backend keeps an engine-owned screen target (Metal: a BGRA8 texture handed
  to the runtime via `setMetalOffscreenColorTexture`, read through
  `metalDefaultColorTexture()`; GL: an RGBA8 + depth-stencil FBO bound wherever
  `0` was). `bindDefaultFramebuffer`, `clearDefaultFramebuffer`,
  `readDefaultFramebuffer` and `present` are the only seams; nothing above the
  device knows. Reach the frame's colour target only through
  `metalDefaultColorTexture()`: `metalDrawable()` is nullptr all run long in
  this mode. Mode semantics: `engine/window/CLAUDE.md` "Window modes".
- Compute grids cap X at `kMaxDispatchGroupsX` and spill into Y; consumers
  flatten both group dimensions consistently.
- Hot compute-kernel mode branches use compile-time specializations of a
  shared source body, not uniform runtime branches. Multi-dispatch cost models
  include backend fixed dispatch cost ([GPU stage timing](../../docs/design/gpu-stage-timing-cost-model.md)).
- GUI canvas size defaults to `mainCanvasSize / guiScale`;
  `setGuiCanvasFullResolution()` switches it (and the creation's coordinate
  space) to native framebuffer resolution.
