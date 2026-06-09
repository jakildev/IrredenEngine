# Detached re-voxelize — world sun-shadow + light-volume + depth (P4b)

**Issue:** #1576 (epic #1553). Subsumes #1582 **Option A** (world-depth composite + cast).
**Status:** 🟢 **design-unblocked** — the four decisions in
[§ Open decisions](#open-decisions) are resolved (architect, in-line **Decision**
blocks) and the work is decomposed into the 3-PR stack in
[§ Phasing](#phasing-decided--lands-as-a-stack-of-3-prs). Summary: **opt-in**
per-entity world-placement (screen-locked overlay stays the default; #1576
**stacks on** #1583), **shared** world maps for receive, **second bake dispatch**
for cast (phased last), depth offset = world iso depth on the GRID
`trixelDistances` convention. **P4b-1 (depth composite) + P4b-2 (receive) landed;
P4b-3 (cast) is the remaining phase.**

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

> **Decision (architect): (b) opt-in per-canvas mode; #1576 STACKS on #1583.**
> World-placement is a per-entity **opt-in capability**, not the new default —
> the screen-locked overlay (#1583 / #1553 decision-1) stays the default.
> Reasons: (1) **non-breaking** — existing detached usages (HUD props, floating
> showcases) keep cheap screen-locked behavior and don't suddenly depth-sort or
> get shadowed; (2) **cost** — the world-placed path pays an extra bake dispatch
> + world-pos plumbing + shared-map sampling, which overlay-only entities
> shouldn't carry; (3) **both paths are legitimate** — GRID already gives
> "world-integrated but round-to-cell aliased"; opt-in world-placed re-voxelize
> gives "world-integrated AND smooth", a genuinely better grounded rotating
> solid for scenes that want it. So #1583 merges first (documents the default
> contract); #1576 stacks on it and adds the opt-in. Representation: a
> per-entity opt-in flag defaulting **off** — most naturally a bool on the
> canvas render behavior (`C_TrixelCanvasRenderBehavior`, alongside
> `useCameraPositionIso_`) or a small tag component; worker's choice per the
> component conventions. Do NOT add a new `RotationMode` value — world-integration
> is orthogonal to how rotation is baked, and the enum shouldn't multiply.

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

> **Decision (architect): (a) second bake dispatch — but PHASED LAST.**
> Reject (b): merging detached cells into the main world pool dissolves the
> private-pool architecture that the entire re-voxelize epic is built on (epic
> decision-1: rotation lives in private-pool cells) — non-starter. (a) is the
> right mechanism: a second bake dispatch per **opt-in** detached canvas into
> the **shared** sun-shadow map, with the world-iso-depth offset + entity-world-
> translation recovery, mirroring the existing per-axis resolve bake
> (`system_bake_sun_shadow_map.hpp:173-186`) — that precedent already proves
> "bake a second source texture into the shared map." Sequencing: this is the
> **last** phase (P4b-3), shipped after depth-composite + receive, because it's
> the only piece that doesn't fall out of world-placement and it has the most
> surface (GL + Metal bake changes). Until P4b-3 lands, opt-in detached solids
> depth-sort + receive but cast no shadow — an honest, documented intermediate
> state, not a regression (today they do none of the three).

### Q3 — Receive mechanism
- **(a) Sample the SHARED world sun-shadow map + 128³ light volume** directly at
  each detached voxel's recovered **world** position. Requires plumbing the
  entity world transform into the detached lighting voxel-frame so the shader
  recovers world (not model) pos, then flipping the `isDetachedCanvas`
  shadow=1.0 / light-volume-disabled branch. (Implied by the architect note.)
- **(b) Per-canvas private `C_CanvasSunShadow` / `C_CanvasLightVolume`** (the
  issue's original "per-canvas private infra" option).

> **Decision (architect): (a) sample the SHARED world maps at recovered world pos.**
> The world sun-shadow map + 128³ light volume ARE the world's lighting state;
> a detached solid that opts into world-placement should sample those same
> shared resources at its world position — that is exactly what makes it consistent
> with GRID solids and lets it be shadowed BY world geometry (the floor, other
> solids). Reject (b): per-canvas private `C_CanvasSunShadow` /
> `C_CanvasLightVolume` is a parallel lighting world per entity — expensive (a
> shadow map + light volume per detached canvas) and, worse, it can only
> self-shadow; it would NOT receive shadow from world geometry, defeating the
> point. Implementation: plumb the entity world transform into the detached
> lighting voxel-frame so each detached voxel recovers world (not model) pos,
> then flip the `isDetachedCanvas` branch (`c_lighting_to_trixel.glsl:144-200`
> + `.metal`) to sample the shared maps at that world pos **when the opt-in flag
> is set** (gate on the flag so the default screen-locked path is byte-identical).
> This is phase P4b-2.

### Q4 — Depth-range constant (likely implementable once Q1 lands)
Per the note, share the GRID `trixelDistances` convention: detached
`distanceOffset_` = the entity's **world iso depth** in `rawDist` units, so
`model rawDist + distanceOffset = world depth`. Confirm the offset is the
iso-depth the voxel rasterizer writes for `worldTransform.translation_`, and
that the detached canvas's model-frame `rawDist` is pool-centered so the sum
lands in world range. Coupled to Q1 (only applies on the world-placed path).

> **Decision / confirmation (architect): CONFIRMED.** `distanceOffset_` = the
> entity's world iso depth in `rawDist` units — the SAME value the world voxel
> rasterizer writes into `trixelDistances` for a voxel at
> `worldTransform.translation_` — so `model rawDist + distanceOffset = world
> depth` and opt-in detached solids sort against GRID solids + the floor on one
> shared convention. Two implementation constraints to lock down in P4b-1:
> (1) the detached canvas's model-frame `rawDist` must be **pool-centered** (the
> re-voxelize pool rotates about its origin), so the offset is purely the
> entity's world iso depth and the pool's own center contributes 0; (2) land a
> CPU↔GPU **equivalence check** — a voxel placed at a known world cell via the
> opt-in detached path must composite to the same framebuffer depth as the same
> world cell rendered through GRID. If those two hold, depth-sort is exact, not
> approximate. Only applies on the opt-in world-placed path (Q1); the default
> overlay keeps `distanceOffset_ = 0`.

## Phasing (DECIDED — lands as a stack of 3 PRs)

Each phase is its own PR, stacks on the previous, and is independently verifiable.
**P4b-1 stacks on #1583** (which establishes the documented default contract).

1. **P4b-1 — opt-in flag + world-depth composite (Q1 + Q4). The keystone.** ✅ **Implemented.**
   Add the per-entity opt-in flag (default off → #1583's screen-locked default is
   byte-identical). On opt-in, set `distanceOffset_` = world iso depth + assert the
   GRID-equivalence check. Opt-in detached solids now depth-sort against world
   geometry, the floor, and each other. (Resolves the "floats vs floor" half of #1582.)
   *Landed as:* `C_EntityCanvas::worldPlaced_` (default off); `ENTITY_CANVAS_TO_FRAMEBUFFER`
   sets `distanceOffset_ = pos3DtoDistance(roundVec3HalfUp(translation))` when the flag is
   set (else 0). Because re-voxelize keeps `voxelDepthAxis = (1,1,1)` (the rotation lives in
   the cells) and `pos3DtoDistance` is linear, the composite depth is **exact** against GRID
   at any rotation for integer entity translations — proved by the CPU↔GPU GRID-equivalence
   gtest `test/render/detached_world_depth_test.cpp`. Runnable demo: `canvas_stress
   --screen-lock-revox` to revert to screen-locked overlays).
2. **P4b-2 — receive (Q3a). ✅ Implemented.** Plumb the world transform into the
   detached lighting voxel-frame; flip the `isDetachedCanvas` shadow/light-volume
   branch to sample the shared maps at world pos, gated on the opt-in flag. GL + Metal.
   Opt-in solids now darken in world shadow and pick up light-volume bleed.
   *Landed as:* PROPAGATE_CANVAS_ROTATION propagates `worldPlaced_` +
   `worldCellOffset_ = roundVec3HalfUp(translation)` onto `C_CanvasLocalRotation`;
   `buildVoxelFrameData` publishes them as `FrameDataVoxelToCanvas::detachedWorldReceive_`
   (`.xyz` offset, `.w` enable). `c_lighting_to_trixel` (GL + Metal) recovers
   `worldPos = trixelCanvasPixelToWorld3D(...) + .xyz` and, when `.w != 0`, samples the
   shared 128³ light volume there AND re-runs the sun-shadow cascade lookup at that pos
   via the new shared `ir_sun_shadow_sample.{glsl,metal}` include (extracted from
   COMPUTE_SUN_SHADOW so both passes share one cascade sampler + FrameDataSun layout;
   the lighting system binds the sun-depth SSBO at slot 28). With the flag off the
   shader takes the identical pre-#1576 branch, so the default screen-locked references
   stay byte-identical (verified: the only default-path drift is the pre-existing
   CPU↔GPU sub-cell speckle that master exhibits run-to-run). Receive correctness was
   confirmed with a forced-shadow-factor isolation pass (the world-placed solid darkens
   on the opt-in path; the path + plumbing reach the shader). Note: a *camera-visible*
   cast-shadow-on-a-detached-solid is awkward to frame in `canvas_stress` because its
   sun direction ≈ the iso camera direction, so cast shadows fall away from the camera;
   the end-to-end visual lands naturally with P4b-3 on the #1587 shadow floor.
3. **P4b-3 — cast (Q2a).** Second bake dispatch per opt-in detached canvas into the
   shared sun-shadow map (world-depth offset + translation recovery, per the per-axis
   resolve-bake precedent). GL + Metal. Opt-in solids now cast onto the floor + world.
   **Natural visual verification target: the #1587 shadow floor** — an opt-in detached
   solid dropping a shadow on it is the end-to-end proof.

Each PR carries its own render-verify references (GL + Metal). Default-path
(screen-locked) references must stay byte-identical through all three phases —
that is the regression guard that the opt-in gate is wired correctly.
