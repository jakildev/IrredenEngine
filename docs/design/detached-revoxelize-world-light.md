# Detached re-voxelize — world sun-shadow + light-volume + depth (P4b)

**Issue:** #1576 (epic #1553). Subsumes #1582 **Option A** (world-depth composite + cast).
**Status:** 🔴 **design-blocked** — current-state analysis recorded here; the
four decisions in [§ Open decisions](#open-decisions) need an architect call
before implementation. This doc is the `docs/design/` artifact the #1576
acceptance criteria require; the architect fills the **Decision** blocks.

## Goal

Give detached re-voxelize solids (the #1555–#1560 path: a rotating detached
entity's voxel **cells** are rotated+rounded into a private pool and rasterized
through the cardinal single-canvas path, then blit to the framebuffer) full
**world-light participation**: depth-sort against world geometry, **cast**
world sun-shadow, and **receive** world sun-shadow + the 128³ light volume —
equivalently to an attached GRID solid.

Architect note on #1576 (the refinement of the issue's "architect to refine"
approach): *"world-place the detached pool once, then depth-test / shadow-cast /
light-receive all fall out … share the GRID `trixelDistances` depth convention
rather than a separate detached band."*

## Current state (verified against master @ this branch's base)

The detached re-voxelize canvas is a **separate canvas, rasterized in the
entity's own MODEL frame** (camera yaw + pan zeroed). Three subsystems
currently exclude it from world light:

### 1. Composite — fixed depth (screen-locked overlay)
`engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp`
- `tick()` builds the model TRS with **Z = 0** (`translate(..., vec3(entityFbCenter, 0.0f))`, line 129) and sets `fd.distanceOffset_ = 0` (line 144).
- The framebuffer depth write is `gl_FragDepth = normalizeDistance(rawDist + distanceOffset)` (`f_trixel_to_framebuffer.glsl`), `normalizeDistance` mapping `[kMinTriangleDistance, kMaxTriangleDistance] → [0,1]`.
- So the detached canvas writes **model-frame** trixel depths near 0 → it does **not** depth-sort against world geometry (this is exactly #1582's complaint). PR **#1583** (#1582 **Option B**, currently open/unmerged) keeps this screen-locked-overlay behavior and documents it.

### 2. Receive — forced fully-lit
`engine/render/src/shaders/c_lighting_to_trixel.glsl:144-200` (+ `.metal` parity)
- Comment, verbatim: *"World cast/receive shadow + light-volume bleed for detached solids are deferred to P4b (#1576)."*
- `const float shadow = detachedCanvas ? 1.0 : imageLoad(canvasSunShadow, pixel).r;` — detached forced to **1.0** (no shadow received).
- `if (lightVolumeEnabled != 0 && !detachedCanvas)` — light volume **disabled** for detached.
- `system_lighting_to_trixel.hpp`: the detached canvas carries `C_CanvasAOTexture` but **no** `C_CanvasSunShadow` / `C_CanvasLightVolume`; the main canvas's are bound as inert placeholders (lines 100-106, 135-140). Lit by AO + directional-sun face-shading + sky only (P4 / #1558 Option A).

### 3. Cast — invisible to the bake
`engine/prefabs/irreden/render/systems/system_bake_sun_shadow_map.hpp`
- The bake iterates `C_TriangleCanvasTextures + C_CanvasSunShadow + C_TrixelCanvasRenderBehavior` but **early-returns unless `behavior.useCameraPositionIso_`** (line 122) — i.e. it bakes **only the main world canvas's** distance texture, recovering world position via the **main canvas's** voxel frame data (`SingleVoxelFrameData`).
- The detached canvas is model-frame and not iterated → detached solids **cast no world shadow**. The existing per-axis resolve bake (lines 173-186) is the closest precedent for "bake a second source texture into the shared map."

## Why "cast falls out" is not literally true

Depth-sort (composite Z) and receive (sample shared maps at world pos) are both
reachable from world-placement. **Cast is not**: the bake reads the canvas
**distance texture** in world-frame iso coords and never sees the separate
model-frame detached canvas. Making detached solids cast requires a deliberate
choice about how model-frame voxels enter the world-frame bake — see Q2.

## Open decisions

### Q1 — Default vs opt-in; relationship to in-flight PR #1583
#1582 **Option B** (PR #1583, open) keeps detached canvases **screen-locked
overlays**; #1576 **Option A** **world-places** them. Same composite, opposite
behavior. Decision needed: is world-placement (a) the **new default** that
supersedes #1583 (then #1576 closes/replaces #1583, or stacks on it and flips
the default), or (b) an **opt-in** per-canvas mode (a `C_DetachedCanvas` flag /
new `RotationMode`) with screen-locked overlay remaining the default? And is
#1576 **sequenced after #1583** (stack) or does it **replace** it?

> **Decision:** _(architect)_

### Q2 — Cast mechanism
How do model-frame detached voxels enter the world sun-shadow bake?
- **(a) Second bake dispatch per detached canvas** into the shared map, with a
  world-iso-depth offset + entity-world-translation recovery (mirrors the
  per-axis resolve bake, `system_bake_sun_shadow_map.hpp:173-186`). Self-
  contained; needs the bake's world-pos recovery authored per detached entity.
- **(b) Merge detached re-voxelize cells into the main world canvas/pool** so
  they rasterize + bake as world voxels. Heavier; changes the re-voxelize
  architecture (epic decision #1: rotation lives in private-pool cells).
- **(c) Scope cast OUT for now** (receive-only), document, defer cast.

> **Decision:** _(architect)_

### Q3 — Receive mechanism
- **(a) Sample the SHARED world sun-shadow map + 128³ light volume** directly at
  each detached voxel's recovered **world** position. Requires plumbing the
  entity world transform into the detached lighting voxel-frame so the shader
  recovers world (not model) pos, then flipping the `isDetachedCanvas`
  shadow=1.0 / light-volume-disabled branch. (Implied by the architect note.)
- **(b) Per-canvas private `C_CanvasSunShadow` / `C_CanvasLightVolume`** (the
  issue's original "per-canvas private infra" option).

> **Decision:** _(architect)_

### Q4 — Depth-range constant (likely implementable once Q1 lands)
Per the note, share the GRID `trixelDistances` convention: detached
`distanceOffset_` = the entity's **world iso depth** in `rawDist` units, so
`model rawDist + distanceOffset = world depth`. Confirm the offset is the
iso-depth the voxel rasterizer writes for `worldTransform.translation_`, and
that the detached canvas's model-frame `rawDist` is pool-centered so the sum
lands in world range. Coupled to Q1 (only applies on the world-placed path).

> **Decision / confirmation:** _(architect)_

## Suggested phasing (once decided)
1. **Depth-composite** (Q1 + Q4) — world-place + depth-sort. The keystone.
2. **Receive** (Q3) — sample shared world maps at world pos; flip the shader branch; GL + Metal.
3. **Cast** (Q2) — bake integration; GL + Metal.
4. Visual verification (`canvas_stress` / a dedicated scene) + render-verify references.

Each is plausibly its own PR; #1576 likely decomposes into a short stack.
