# engine/render/ — trixel render pipeline

This module owns graphics primitives: the voxel-to-trixel pipeline, GPU
resources, camera and viewport state, canvases, framebuffers, and the OpenGL
and Metal backends.

## Working agreements

- `engine/render/include/irreden/ir_render.hpp` is the public entry point.
  Creations include it rather than internal render headers.
- `engine/render/` owns device and pipeline primitives needed by every
  creation. Opt-in feature state belongs under
  `engine/prefabs/irreden/render/`; expose it through a prefab-scoped API.
- Names in this module describe rendering effects, never the first feature or
  caller that uses them. This applies to C++ types, flags, shader identifiers,
  binding names, and comments.
- System registration, ordering, and tick contracts live in
  [`engine/system/CLAUDE.md`](../system/CLAUDE.md).
- Current feature-API exceptions are tracked in
  [`.fleet/status/render-api-relocations.md`](../../.fleet/status/render-api-relocations.md);
  feature PRs do not edit that register.

## Commands and validators

Use the [validation index](../../docs/agents/VALIDATION.md) for the canonical
commands. Render changes commonly need `header-checks`, `render-debug-loop`,
`render-verify`, `backend-parity`, `cull-verify`, and the relevant
`scripts/*-verify.py` metric. Codex image inspection and evidence requirements
are in [`CODEX.md` § Rendering conversations](../../docs/agents/CODEX.md#rendering-conversations).
Pure documentation, tests, mechanical refactors, and build-only changes with
no visual effect do not require render captures.

## Pipeline contracts

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
- Every dispatchable `c_*.metal` kernel, excluding `*_body.metal` fragments,
  needs its real threadgroup size in `threadgroupSizeForFunctionName`.
  `functionUsesImageAtomicScratch` must exactly match kernels whose resolved
  source declares an atomic parameter at `kMetalImageAtomicScratchSlot`.
  Header conventions validate both registries.
- Metal AOT compilation treats top-level kernel wrappers as translation units.
  Include fragments must use the excluded `*_body.metal` or `ir_*.metal`
  naming forms. The opt-in metallib has no runtime consumer.
- Every Metal full-screen or quad vertex stage negates clip-space
  `position.y`; its GLSL twin does not. This is the backend origin adapter.
- Trixel-to-framebuffer gathering uses the raw sample coordinate for
  color/depth/tier and the parity-shifted coordinate only for hover/picking.
  Read the [parity-shift design](../../docs/design/trixel-parity-shift-442-investigation.md)
  before changing either coordinate path.
- CPU frame-data structs and shader blocks must agree on field order,
  `std140` padding, and binding index. Every hard-coded binding has a matching
  `kBufferIndex_*` constant.
- Metal buffer indices 0–30 are occupied. A pass needing another buffer must
  bind an existing slot transiently and restore the prior binding itself.

## GPU resource contracts

- `getNamedResource` asserts on a miss and never implements optional
  behavior. Use `getNamedResourceOrNull` only when absence is a supported
  pipeline configuration.
- Fresh GPU allocation contents are undefined. Prime persistent or coherent
  prior-frame readbacks before sampling them, including statistics rings.
- Clear trixel distance textures to `kTrixelDistanceMaxDistance`; the separate
  SDF miss sentinel is `kInvalidDepth`. Skipping the clear exposes stale depth.
- On Metal, sampler and image binds share one texture-slot namespace: the most
  recent bind of either kind wins and remains resident across dispatches.
  Before relying on a resident bind, account for both tables. Any resource
  type stored in a sticky table must untrack itself on destruction; destroyed
  framebuffer attachments fall back to the default render target.
- Metal R32I image atomics land in scratch storage. For a canvas's own texture,
  call `resolveImageAtomicScratch` after atomic passes and before its first
  texture reader; textures that will be resolved must first be cleared through
  `clearTexImage`. To consume a foreign canvas's atomic depth in a later
  compute dispatch, resolve it into a main-canvas-layout texture first.
- Metal `Texture2D::clear()` and `subImage2D()` writes are ordered through the
  frame command buffer. A same-frame CPU `getBytes` read still requires an
  explicit commit and wait.

## Camera and raster contracts

- GRID rendering assumes world `(1,1,1)` is the iso-depth axis and supports
  camera Z-yaw only. Pitch and roll invalidate integer raster, picking,
  hitbox, drag, and SDF-cull shortcuts; DETACHED rendering is axis-agnostic.
  See the [consumer map](../../docs/design/iso-depth-axis-invariant.md).
- World-content placement reads `getEffectiveCameraIso()`. Lighting-grid
  anchoring, screen-space sprites, and debug overlays intentionally use the
  raw camera offset. The default pivot depth is latched once in `beginFrame`,
  while its focus point is derived from the current camera position; see the
  [camera-pivot contract](../../docs/design/camera-yaw-pivot.md).
- Voxel faces follow `visible-face triplet × exposed-face mask`. Rotated voxel
  staircases may substitute the exposed opposite-polarity face only when the
  rotated-content marker is set. The canonical model and affected shaders are
  in [voxel face rasterization](../../docs/design/voxel-face-rasterization.md).
- Continuous-yaw GRID rendering uses three face-local canvases plus a
  forward-scatter composite. Its overflow lane, analytic edge coverage,
  ordering, and fixed-cost constraints live in the
  [per-axis design](../../docs/design/per-axis-trixel-canvas-rotation.md).
- Any per-axis consumer recovering an absolute world position applies the
  encoded sub-cell fraction. Receivers use `perAxisCellToWorld3DSubCell`; the
  sun-shadow cast bridge quantizes in the face-local frame before composing
  the rotated basis. Relative consumers may use lattice recovery only when
  their math cancels the in-plane offset.
- SDF and voxel-pool silhouettes are bit-identical only when effective
  subdivision is one. At higher subdivision, SDFs are analytically smooth and
  voxel pools retain the carved lattice silhouette; this difference is
  intentional.

## Lighting contracts

- When sun shadows are enabled, visible geometry bounds include the
  shadow-feeder sweep. The sun-map bake and receiver share
  `kSunShadowMaxDistance`; read the
  [coverage design](../../docs/design/sun-shadow-bake-coverage.md) before
  changing the bake kernel or splat controls.
- Light-occlusion-grid construction iterates the full voxel pool and never
  applies `visibleIsoViewport`. A visibility-freeze check is not a viewport
  cull.
- Light seeds include off-screen sources whose radius reaches the visible or
  camera-anchored domain. `light-verify` validates boundary clamping and fade.
- Chunk streaming must include the sun-direction shadow ring. Any world-space
  neighbor sampler also requires a one-chunk resident guard band.

## Performance and lifecycle pitfalls

- Compute grids cap X at `kMaxDispatchGroupsX` and spill into Y; consumers
  flatten both group dimensions consistently.
- Hot compute-kernel mode branches use compile-time specializations from a
  shared source body, not uniform runtime branches. Multi-dispatch cost models
  must include backend fixed dispatch cost; see
  [GPU stage timing](../../docs/design/gpu-stage-timing-cost-model.md).
- GUI canvas size defaults to `mainCanvasSize / guiScale`.
  `setGuiCanvasFullResolution()` changes it to native framebuffer resolution,
  and the creation must use that coordinate space.
