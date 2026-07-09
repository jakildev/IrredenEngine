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
Shared includes: `ir_iso_common.glsl`, `ir_constants.glsl`.
Shader file paths are stored in `render/shader_names.hpp`. Update that
header when you add or rename a shader.

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
the following to the PR body:

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
wrong-winner problem, not a missing write.) The probe lives in
`IRPrefab::DepthProbe::` (a prefab-scoped
Pattern-B namespace over the `Texture2D` /
`PixelDataFormat::DEPTH_COMPONENT` readback primitive). Pure readback:
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
   # pan jitter (voxel box, yaw 45, zoom 4):
   IRShapeDebug --spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 \
       --zoom 4 --auto-screenshot 6
   # rotation jitter (voxel cylinder, Z-yaw-invariant probe):
   IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep \
       --zoom 4 --auto-screenshot 6
   # then score the captured sequence (in order):
   build/tools/jitter_probe/jitter_probe <save_files>/screenshots/screenshot_0*.png
   ```

3. **Read the verdict.** `jitter_probe` tracks the shape's centroid across the
   sequence and reports `SMOOTH` (0 direction-reversals, sub-pixel residual off
   the smooth-motion line, exit 0) vs `JITTER` (sign-reversing deltas + multi-px
   residual, exit 1). A correct fix flips JITTER → SMOOTH; re-run at multiple
   zooms (the per-axis class worsens with zoom/subdivision). For a multi-shape
   scene with no isolation, pass `jitter_probe --color R,G,B,T` to lock onto one
   shape. See `tools/jitter_probe/README.md`.

**Jitter is NOT the same as cardinal byte-identity.** Confirm yaw-0 / static
frames stay byte-identical (`img_diff`) *and* that motion is jitter-free
(`jitter_probe`) — a change can pass one and fail the other.

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
1024² depth map is fixed) and softer shadow boundaries.

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

**Accepted near-cardinal corner drift (coarse cubes) — #1883, root fix in
epic #1933.** The forward-scatter composite grows each cell to a conservative
quad (`scatterConservativeDilation`, `ir_iso_common.glsl`): the per-edge margin
`C ≈ 0.5·|n|` carries foreshortened-silhouette coverage (anti-dashing) and a
miter limit `kScatterMiterLimit` (`M = 2.0`) caps the corner extension. At
near-cardinal poses (≈84° per-axis) on **coarse** cubes this leaves visible
convex-corner **spikes** — a silhouette-tip cell whose dilation quad
over-extends into background by ≈ `M·C`. There is **no local (non-neighbour)
scalar fix**: crisp corners need `M·C ≲ 2px`, but `C` is forced to ≈ half the
on-screen cell pitch to bridge the inter-cell coverage gap on foreshortened
faces, and those two bounds are mutually exclusive — no `(margin-ceiling,
miter-limit)` pair satisfies both (every scalar tried either re-dashes the
silhouette or keeps the spikes). The defect scales **inversely with cell
density** — negligible on normal/fine cubes — so it is accepted as intentional
drift. The neighbour-aware dilation that *would* fix it geometrically was
rejected on cost (2–4 extra `triangleColors` texel fetches per scatter vertex
on the hot per-cell path). The principled root fix — hardware **conservative
rasterization / MSAA** on the scatter pass, which removes the sub-pixel
rasterization dropout at the source so no manual dilation/margin is needed
(killing spikes and silhouette dashing together) — is deferred to epic
**#1933** (fits #935/#937).

Per-axis voxels **cast** sun shadows under continuous yaw via
`RESOLVE_PER_AXIS_SCREEN_DEPTH` (#1435), which collapses the three face-local
per-axis canvases into one screen-space cardinal-layout depth texture so
`BAKE_SUN_SHADOW_MAP` casts them through its existing cardinal recovery — the
per-screen-pixel flattening the raw face-local store lacks (which is what
caused #1380's cross-face self-occlusion). Invariant + the cast/receive
agreement + the Metal `threadgroupSizeForFunctionName` requirement:
[`docs/design/per-axis-sun-shadow-resolve.md`](../../docs/design/per-axis-sun-shadow-resolve.md).

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
  byte-identical. Reading the raw offset at a new producer site silently
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
- **A compute kernel's texture read is silently shadowed by a stale IMAGE bind
  at the same unit on Metal (#1812).** `bindComputeResources`
  (`metal_render_impl.cpp`) flushes the sticky image-binding table AFTER the
  sampler table at the *same* encoder texture index, and the image table is
  never cleared per-frame. So if you bind a texture as a **sampler**
  (`Texture2D::bind`) at unit N to read it, but a prior dispatch left a
  **different** texture bound as an **image** (`bindAsImage`) at unit N, your
  read gets that stale image, not your texture — no error, no warning. The
  #1812 per-voxel cull read `getHiZMip(0)` at unit 1 via `bind()`, but
  `VOXEL_TO_TRIXEL_STAGE_1`/`STAGE_2` leave `trixelDistances` imaged at unit 1,
  so the compact read the (just-cleared, all-65535) distance texture and the
  cull captured zero. **Fix: bind compute texture reads as IMAGES**
  (`bindAsImage(unit, READ_ONLY, <fmt>)` + `access::read` on Metal / `imageLoad`
  on GL) so they occupy — and win — the image slot, rather than a sampler bind
  that a stale image shadows. GL is immune (separate image/texture-unit
  namespaces), so a GL-only smoke will not catch it. Any compute kernel that
  mixes a sampler read with images bound elsewhere in the pipeline is exposed;
  the #1294 chunk cull's fine Hi-Z levels are the known other victim.
