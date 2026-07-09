# Detached SO(3) shadow projection — design spike (D3, #2323)

- **Status:** investigation spike. Deliverable is *this doc* + a filed follow-on
  ticket stack. No engine code changes ship from #2323 (throwaway probes only).
- **Author:** worker (opus), 2026-07-08. Static-analysis pass on master
  `cb54b319`. GL-runtime confirmation of the §4 camera-pose hypothesis is the
  first follow-on (this pass was authored on a macOS/Metal host, where the cast
  path is #1640-blocked and cannot be observed end-to-end — see §6).
- **Epic:** #2314 (lighting/shadow domain culling correctness). Sibling of D1
  #2322 (detached world-lighting membership defaults) and the camera-Z-yaw epic
  #1881 (this spike is the *entity*-rotation counterpart — see §8).
- **Precedents inherited:** `docs/design/per-axis-sun-shadow-resolve.md`
  (resolve-then-bake), `docs/design/detached-revoxelize-world-light.md`
  (Q2 REVISED resolve-then-bake ruling), `docs/design/detached-canvas-depth-default.md`
  (#1624 world-placed default), `docs/design/screen-space-sun-shadow-map.md`
  (the sun-space bake).

---

## 0. TL;DR / recommendation

A freely SO(3)-rotated detached entity already casts *and* receives a
sun shadow through the **re-voxelize** path (`DETACHED_REVOXELIZE`) — the
entity's full quaternion is baked into a cardinal private voxel pool each
frame, and that cardinal pool flows through the existing resolve-then-bake
cast and the existing world-receive. **The entity's own rotation is not the
hard part; it is already handled** (to within the accepted round-to-cell
quantization). Two things are actually open:

1. **Cast is Metal-blocked, not design-blocked.** The resolve-then-bake cast
   is correct by construction for a re-voxelized SO(3) entity; it produces no
   visible output *on Metal* only because of the #1640 foreign-R32I gap
   (#2091). No new cast *mechanism* is needed. **Recommendation: adopt
   re-voxelize as the one sanctioned rotating-cast path and formally retire the
   ambition of a plain-`DETACHED` (octahedral-snap) cast** (continuing the
   #1589 trajectory). Do *not* build a bespoke SO(3) resolve target.

2. **Receive has a camera-pose gap that cast does not.** The cast recovery
   compensates the camera's cardinal yaw; the receive recovery forces
   `rasterYaw = 0` and applies **no** camera-yaw compensation (§3-B, §4). Static
   analysis says the two recoveries agree only at camera cardinal index 0, so a
   rotated detached entity **receives** the wrong world shadow at camera yaw
   90/180/270 (and drifts under residual yaw), even though it **casts**
   correctly there. **Recommendation: make the receive recover its world
   position the same way the cast does** (thread the camera raster-yaw + the
   cardinal recovery into the world-receive branch, and rotate the surface
   normal likewise), generalizing to residual yaw via the per-axis precedent.
   This is the "receive path at rotated surface points" work #2323 asked to
   scope.

The **hard constraint** every option must respect (verbatim from three docs +
`engine/render/CLAUDE.md`): *the sun-shadow bake only ever reads
main-canvas-layout depth sources; a foreign/rotated-frame canvas R32I texture is
never a bake input* (#1640 Metal gap). Resolve-then-bake is mandatory.

---

## 1. What "SO(3)-rotated detached shadow" means here

Two distinct rotations compose in a detached-canvas frame, and keeping them
separate is the whole game:

- **`R_entity`** — the entity's own world orientation (`C_LocalTransform.rotation_`),
  a full SO(3) quaternion. This is the "freely rotated entity" of the title.
- **`R_camera`** — the world camera's orientation, `qZ(yaw) · qX(pitch)`
  (`engine/prefabs/irreden/render/camera.hpp`, `getRotationQuat`). For world
  content the supported regime is Z-yaw only; pitch/roll break the iso-depth-axis
  shortcuts (`engine/render/CLAUDE.md` §"Iso-depth-axis invariant"). Yaw splits
  into a **cardinal** snap (0/90/180/270) + a continuous **residual**.

`PROPAGATE_CANVAS_ROTATION` composes the two into the canvas carrier:

> `C_CanvasLocalRotation.rotation_ = quatMul(cameraRotationInverse_, localTransform.rotation_)`
> = `R_camera⁻¹ · R_entity`
> — `engine/prefabs/irreden/render/systems/system_propagate_canvas_rotation.hpp:75-76`

That composed quaternion is baked into the private pool's **integer cell
positions** each frame by `REBUILD_DETACHED_VOXELS` / `c_revoxelize_detached`
(`system_rebuild_detached_voxels.hpp:118-131`, `c_revoxelize_detached.glsl`
`RevoxelizeParams.canvasRotation_`). So a re-voxelized detached solid is, by
the time any shadow stage sees it, a **cardinal** (axis-aligned) voxel pool
whose cells sit at `round(R_camera⁻¹ · R_entity · local)` — the SO(3)
orientation is in the *geometry*, not in a transform the shadow stages read.

This is why the header comment on the receive branch says the recovery is a
pure translation (`the rotation is baked into the integer cells, so model-frame
== world-frame` — `c_voxel_to_trixel_stage_1.glsl` ~L447). It is true **only
when `R_camera = I`** — see §4.

---

## 2. The pipeline as it stands (mechanism map)

```
UPDATE  PROPAGATE_CANVAS_ROTATION
          rotation_ = R_camera⁻¹·R_entity ; worldPlaced_=!screenLocked_ ;
          worldCellOffset_ = round(translation)          [translation only]
        REBUILD_DETACHED_VOXELS / c_revoxelize_detached
          bake rotation_ into cardinal private-pool CELL positions

RENDER  VOXEL_TO_TRIXEL_STAGE_1 (per canvas)
          buildVoxelFrameData → reVoxelize branch:
            visualYaw_=rasterYaw_=residualYaw_=0 (CARDINAL by fiat)
            detachedWorldReceive_ = (worldCellOffset_, worldPlaced_?1:0)
          dispatchReVoxelize uploads rotation_ (the ONE downstream reader)
        COMPUTE_VOXEL_AO
        BAKE_SUN_SHADOW_MAP
          base bake (main canvas, always)
          per-axis resolve bake (only at residual yaw != 0)
          world-placed CAST bake (only when opt-in casters exist):
            gatherWorldPlacedCasters → {textures_, worldCellOffset_}   [no rotation]
            patchFrameYawSplit(cameraRasterYaw, 0)                     [camera cardinal!]
            Pass1 c_resolve_world_placed_depth  (scatter each caster → shared scratch)
            Pass2 blit scratch → imageStore R32I resolve texture
            Pass3 ONE bake dispatch, cardinal recovery
        COMPUTE_SUN_SHADOW           (main-canvas pixels; rasterYaw = cameraRasterYaw)
        LIGHTING_TO_TRIXEL
          detached world-RECEIVE branch:
            worldReceivePos = trixelCanvasPixelToWorld3D(..., rasterYaw=0)   [forced 0!]
                              + detachedWorldReceive.xyz
            worldSunShadowFactor(worldReceivePos, worldNormal, isoDepth)     [needs TRUE world]
```

Key facts established by reading the code:

- **The bake never reads a foreign canvas's R32I depth texture.** All three bake
  dispatches read an `imageStore`-written, real-texture-memory resolve
  (`system_bake_sun_shadow_map.hpp` `binding=0` is the iterating canvas's own /
  per-axis `resolveDepth_` / world-placed `worldPlacedResolveDepth_`). The
  caster's own model-frame distance texture is bound only as the *scatter*
  read (`c_resolve_world_placed_depth.glsl:57`), never as a bake input. This is
  the #1640-mandated invariant (`engine/render/CLAUDE.md` §"Foreign-canvas R32I
  image reads … return empty on Metal").

- **`N` casters ⇒ one extra bake** (not `N` bakes) — one shared scratch, one
  blit, one bake dispatch (`system_bake_sun_shadow_map.hpp:326-424`).

- **The sun-shadow map is keyed in TRUE world space.** `worldSunShadowFactor`
  projects its position onto a **world-space** sun basis built from
  `getSunDirection()` (`ir_sun_shadow_sample.glsl:101-110`;
  `system_bake_sun_shadow_map.hpp:509-526`). The bake and the main-canvas
  receive both reconstruct true-world positions (cardinal recovery undoes the
  camera cardinal yaw; the per-axis resolve handles residual). Any receiver must
  present a TRUE-world position, or it samples the wrong texel.

---

## 3. Cardinal-assumption inventory (grep-verifiable, master `cb54b319`)

Every site that assumes an unrotated / camera-yaw-zeroed model frame. Grouped
cast / receive / carrier. Line numbers are on `cb54b319`; each row quotes a
grep-stable token so it re-locates if lines drift.

### 3-A. Cast (resolve-then-bake)

| # | Site | What it assumes | Break under SO(3) / camera pose |
|---|------|-----------------|------------------------------|
| A1 | `system_bake_sun_shadow_map.hpp` `struct WorldPlacedCaster { … vec3 worldCellOffset_; }` (~:136-140) | caster record carries a **translation only** — no rotation field | structurally cannot re-orient model-frame data; only a translate is ever possible downstream |
| A2 | `system_bake_sun_shadow_map.hpp` `gatherWorldPlacedCasters` `push_back({textures.value(), rot.worldCellOffset_})` (~:463-470) | `rot.rotation_` consulted only via `isDetached()` boolean | the SO(3) quaternion is dropped at gather time |
| A3 | `c_resolve_world_placed_depth.glsl:89` `modelPos = isoPixelToPos3D(isoRel.x, isoRel.y, rawDepth)` | plain cardinal iso decode; no quaternion available in-shader | `modelPos` carries `R_camera⁻¹·R_entity` baked in; taken as-is |
| A4 | `c_resolve_world_placed_depth.glsl:94-96` `worldPos = roundHalfUp(modelPos + detachedWorldReceive.xyz * scale)` | "lift to world" is a **pure translation** | valid world pos only if `modelPos` is already world-oriented |
| A5 | `c_resolve_world_placed_depth.glsl:105-110` `cardinalIndex = rasterYawCardinalIndex(rasterYaw); … rotateCardinalZ(worldPos, cardinalIndex)` | only re-orientation is a **Z-axis 90°-multiple** keyed on the **camera** cardinal yaw | compensates camera *cardinal* yaw; cannot express residual yaw, pitch, or a general `R_entity` |
| A6 | `c_resolve_world_placed_depth.glsl:124-127` `viewNormal = rotateCardinalZ(faceOutwardNormal6I(slot<<1), cardinalIndex)` | face axis re-oriented by the same Z-cardinal step only | region/normal correct only insofar as the pool faces are already axis-aligned |
| A7 | `system_bake_sun_shadow_map.hpp:345-349` `patchFrameYawSplit(cameraRasterYaw, 0.0f)` | cast "cardinal recovery" defined purely by the **camera's** yaw split, residual zeroed | residual camera yaw is dropped ⇒ accepted ~1-cell-loose cast silhouette (per-axis precedent) |
| A8 | `c_bake_sun_shadow_map.glsl` `else { pos3D = trixelCanvasPixelToWorld3D(…, rasterYaw); }` (~:117-120) | world-placed resolve baked through the same cardinal-only inverse as GRID | no "this pixel came from a rotated frame" branch |

### 3-B. Receive (`LIGHTING_TO_TRIXEL` world-receive)

| # | Site | What it assumes | Break under SO(3) / camera pose |
|---|------|-----------------|------------------------------|
| B1 | `voxel_frame_data.hpp:84-96` reVoxelize branch: `visualYaw_=rasterYaw_=residualYaw_=0` | the canvas's own frame data is **cardinal by fiat**, independent of `rotation_` | forces the receive's `rasterYaw` to 0 (see B2) |
| B2 | `c_lighting_to_trixel.glsl:196-200` `worldReceivePos = trixelCanvasPixelToWorld3D(…, rasterYaw) + detachedWorldReceive.xyz`, with `rasterYaw==0` | recovery is a **pure translation** — `cardinalIndex==0` skips `rotateCardinalZInv` entirely | **no camera-cardinal compensation at all** (contrast A5/A7); off by the full `R_camera⁻¹` for any camera pose ≠ cardinal index 0 |
| B3 | `c_lighting_to_trixel.glsl:186-188` `worldNormal = faceOutwardNormal6(visibleFaceIds[slot])` from the cardinal triplet | receive normal is a fixed cardinal axis | normal is in the `R_camera⁻¹`-baked frame; not rotated to true world, so Lambert + slope-bias are off at non-cardinal camera pose |
| B4 | `c_lighting_to_trixel.glsl:210-217` `isoDepth = rawDepth + detachedWorldReceive.x+y+z` | canonical `(1,1,1)` iso-depth sum on a translation-only offset | cascade selection keyed to the uncompensated depth |
| B5 | `voxel_frame_data.hpp:70` `voxelDepthAxis_ = (1,1,1,0)` (reVoxelize branch never overrides) | fixed iso depth axis; the sibling octahedral branch *does* set `isoDepthAxisModel(rotation)` at ~:176 | rotation-derived depth axis available but unused on the world-receive branch |

**The asymmetry (A5/A7 vs B2) is the headline finding.** Cast recovery uses
`cameraRasterYaw`; receive recovery uses `0`. Agent-verified and re-read:
`system_bake_sun_shadow_map.hpp:345-349`, `c_resolve_world_placed_depth.glsl:105-110`,
`c_lighting_to_trixel.glsl:196-200`, `voxel_frame_data.hpp:84-96`. The two
world-position recoveries — commented as recovering "the same world position by
construction" (`c_resolve_world_placed_depth.glsl:84`) — coincide **only when
the camera sits at cardinal index 0**.

### 3-C. Rotation carrier / plumbing

| # | Site | Fact |
|---|------|------|
| C1 | `component_canvas_local_rotation.hpp:33` `IRMath::vec4 rotation_` | full SO(3) **quaternion** `(qx,qy,qz,qw)`; `worldCellOffset_` (~:66) is a translation only |
| C2 | `system_voxel_to_trixel.hpp` `dispatchReVoxelize` → `RevoxelizeDetachedParams.canvasRotation_` (~:397) | **the only** downstream consumer of the quaternion; binding-16 UBO, disjoint from the binding-7 `FrameDataVoxelToCanvas` that the shadow shaders read |
| C3 | `voxel_frame_data.hpp:134-135` `detachedWorldReceive_ = vec4(worldCellOffset_, worldPlaced_?1:0)` | the only rotation-adjacent field crossing into binding-7 is a translation + flag; `rotation_` never crosses |

Net: the quaternion exists and is genuinely SO(3), but after
`dispatchReVoxelize` bakes it into cells, **no shadow stage reads it** — cast
and receive both re-derive world pos/normal with at most a camera-cardinal Z
step (cast) or nothing (receive).

---

## 4. What actually breaks — two regimes

Separating `R_entity` from `R_camera` (§1) resolves the apparent contradiction
that "receive was resolved (#2080)" yet "receive is untracked for SO(3)" (epic
#2314 ground truth).

### 4.1 Entity rotation `R_entity` — already handled (both cast + receive)

With the camera at reference (`R_camera = I`), the pool cells sit at
`round(R_entity · local)` — the true world-rotated positions. Then:
- **Cast:** the cardinal pool resolves-then-bakes through the existing cardinal
  recovery ⇒ shadow lands at the entity's true world footprint. Correct (modulo
  Metal delivery + round-to-cell looseness).
- **Receive:** `worldReceivePos = R_entity·local + translation` = true world ⇒
  samples the map correctly. Correct (modulo cardinal-cell normal staircasing).

The round-to-cell staircasing of a rotated solid into cardinal cells is the
**accepted quantization** (the per-axis doc already ruled out threading sub-cell
rotation fraction, choosing cast==receive exactness over silhouette fidelity —
`per-axis-sun-shadow-resolve.md` §"Accepted residual", #2082). This is why
`R_entity` is *not* the hard part despite the human flag: the re-voxelize path
is a working SO(3) shadow mechanism for the entity's own rotation.

### 4.2 Camera pose `R_camera` — the real gap, and it is asymmetric

The pool bakes `R_camera⁻¹` (§1). The **cast** undoes the camera *cardinal*
part (A5/A7: `rotateCardinalZ(cameraCardinal)` forward on scatter, cardinal
recovery inverse on bake) and accepts residual looseness — so **cast is correct
at every camera cardinal (0/90/180/270)** and ~1-cell loose under residual yaw.
The **receive** applies *no* compensation (B2: `rasterYaw = 0`) — so:

- **Camera cardinal 90/180/270:** receive samples the map at a `qZ(-90k)`-rotated
  position and with a `qZ(-90k)`-rotated normal ⇒ **wrong shadow lands on the
  rotated detached entity**, while the *same* entity casts correctly onto the
  floor. Concrete, reproducible discrepancy.
- **Residual camera yaw (~30°, 45°):** additionally off by the residual (receive
  has no residual path at all, where cast at least drops to cardinal).
- **Camera pitch:** both paths carry uncompensated pitch baked into the pool
  (unsupported for world content anyway — iso-depth-axis invariant).

Why this was not caught: the #2080/#2091 scenes spin the **entity** with a
**camera fixed at yaw 0** (`R_camera = I`), the one pose where B2's
translation-only recovery is exact. The epic's cross-cutting acceptance
requirement — *verify at cardinal AND ~30° AND 45° yaw, both backends* (#2314)
— is precisely the matrix that exposes this. **This is the design content #2323
was chartered to surface.**

> **Confidence.** §3 rows are verified code facts (grep-stable citations).
> §4.2's "receive is wrong at camera yaw 90/180/270" is a **static-analysis
> inference** from those facts (the map is true-world-keyed; receive omits the
> compensation cast applies). It should be **confirmed by a GL probe** before an
> implementation ticket commits — see §7-T0. Authoring host here is macOS/Metal,
> where the cast is #1640-blocked and cannot be observed, so the probe is GL.

---

## 5. Candidate mechanisms, costed

### 5-A. Cast

| Option | Sketch | Cost | Verdict |
|---|---|---|---|
| **Ca. Re-voxelize-only (status quo mechanism)** | Keep the resolve-then-bake cast; the cardinal pool already encodes SO(3). Do nothing new for cast except unblock Metal (#1640/#2091, out of scope) and document the residual looseness. | ~0 engine work for the mechanism | **RECOMMENDED.** No foreign-texture read, one extra bake, already shipped on the GL path. |
| **Cb. Bespoke SO(3) resolve target** | A resolve that keeps a non-cardinal (rotated) target layout so the bake reads rotated distances. | High: needs a new bake recovery that is not the cardinal inverse; breaks the "bake reads main-canvas cardinal layout" invariant; no precedent. | **REJECT.** Re-voxelize already reduces SO(3) → cardinal; a second rotated target is redundant and fights the shared-bake invariant. |
| **Cc. Per-caster foreign-texture bake read** | Bind each caster's own model-frame R32I as a bake read. | The literal PR #1626 mechanism. | **REJECT (hard).** Returns empty on Metal (#1640). This is the exact anti-pattern the resolve-then-bake invariant exists to forbid. |
| **Cd. Retire plain-`DETACHED` cast** | Formally state plain `DETACHED` (octahedral forward-scatter) never casts; only `DETACHED_REVOXELIZE` casts. | 0 (documentation + a guard/assert). | **ADOPT alongside Ca.** Plain `DETACHED`'s per-face deform has no faithful world-pos recovery (`detached-canvas-depth-default.md`; #1589 retirement path). |

### 5-B. Receive

| Option | Sketch | Cost | Verdict |
|---|---|---|---|
| **Ra. Camera-compensate the receive recovery (mirror the cast)** | In the world-receive branch, recover world pos with the **camera** raster-yaw + cardinal recovery (the A5/A7 mechanism), and rotate the cardinal normal by the same `rotateCardinalZ(cameraCardinal)`. Generalize residual yaw via the per-axis-lighting precedent (T4 #1311 already lights per-axis canvases). | Medium: thread `cameraRasterYaw` (not 0) into the detached receive's frame data + a normal rotate; residual generalization is a follow-on tier. | **RECOMMENDED.** Makes receive frame-consistent with cast + the map; reuses proven cardinal recovery. |
| **Rb. Accept a cardinal-camera-only receive** | Document that detached world-receive is correct only at camera cardinal poses; gate it off (raw-albedo fallback) at residual/non-zero-cardinal. | Low, but a real capability regression | **FALLBACK only** if Ra proves too costly; must be an explicit, asserted limitation, not silent wrong shadow. |
| **Rc. Thread the SO(3) quaternion into the receive** | Cross `rotation_` into binding-7 and apply a general inverse per receiver pixel. | High + a scarce binding slot (bind budget is full, 0-30); redundant with the cells already encoding `R_entity`. | **REJECT.** The entity rotation is already in the cells; only the *camera* term is missing, and that is a cardinal Z step (Ra), not a general per-pixel quaternion. |

---

## 6. Recommendation

- **Cast:** adopt **Ca + Cd** — re-voxelize is the one sanctioned rotating-cast
  path; plain-`DETACHED` cast is formally retired. No new cast mechanism.
  The only cast work is external: unblock Metal delivery via #1640/#2091
  (out of #2323 scope) and document the accepted residual-yaw looseness.
- **Receive:** adopt **Ra** — recover the detached world-receive position and
  normal through the **camera cardinal recovery** the cast already uses (retire
  the forced `rasterYaw = 0` at B1/B2), then generalize to residual yaw via the
  per-axis-lighting precedent as a second tier. Rb is the documented fallback.

**Metal delivery constraint (must be stated in every follow-on):** the bake
never reads a foreign/rotated-frame R32I texture; resolve-then-bake is
mandatory (#1640). The cast follow-on is *blocked on #2091 → #1640* for Metal
visibility but is verifiable on GL today. The receive follow-on (Ra) is **not**
blocked on #1640 — receive re-runs the cascade lookup at a recovered world pos,
it does not do a foreign-texture bake read (`detached-revoxelize-world-light.md`
Q3) — so Ra can land and be verified on both backends independently of the cast
blocker.

---

## 7. Follow-on ticket stack (to file per TASK-FILING; file-epic if the human approves the stack)

- **T0 — GL probe: confirm the §4.2 receive camera-pose gap (spike, opus).**
  On GL, place a `DETACHED_REVOXELIZE` receiver where a world caster's shadow
  should fall on it; sweep camera yaw through 0 / 90 / ~30° / 45° and capture
  (`canvas_stress`, `--debug-overlay shadow`). Confirm the received shadow
  drifts off the entity at non-zero camera yaw while the cast onto the floor
  stays put. Gate the implementation tickets on this observation. *(Not
  blocked; GL-runnable.)*
- **T1 — Receive: camera-cardinal-compensated world recovery (Ra, cast parity)
  (opus).** Thread `cameraRasterYaw` + cardinal recovery + normal rotate into
  the `LIGHTING_TO_TRIXEL` detached world-receive branch (retire B1/B2's forced
  0). Acceptance: received shadow correct at all four camera cardinals, both
  backends; cardinal-0 byte-identical. *(Not blocked on #1640.)*
- **T2 — Receive: residual-yaw generalization (opus).** Extend T1 to residual
  camera yaw via the per-axis-lighting precedent (T4 #1311). Blocked by T1.
- **T3 — Cast: retire plain-`DETACHED` cast + document re-voxelize-only (sonnet).**
  Assert/guard that only `DETACHED_REVOXELIZE` gathers as a caster
  (`gatherWorldPlacedCasters` already filters `reVoxelize_`); add a CLAUDE.md
  callout and a headless guard so a future plain-`DETACHED` cast attempt fails
  loudly rather than silently. Continues #1589.
- **T4 — Cast: SO(3) verification harness on GL (sonnet).** A `depth-probe-assert`
  / render-verify shot proving a re-voxelized SO(3) caster's floor shadow is
  correct at cardinal + residual yaw on GL (the ENABLED-path positive test the
  render CLAUDE.md demands; byte-identity at cardinal 0 is not enough). The
  Metal counterpart waits on #2091/#1640.

*(Worker note: #2323 does not file these itself as queued work — they go up as
GitHub issues with no labels per TASK-FILING; the human stamps `human:approved`.
This section is the sketch the follow-on filing draws from.)*

---

## 8. Sibling reconciliation

- **#1881 (camera-Z-yaw rotation epic)** owns *camera* rotation of world
  content; **#2323** owns *entity* rotation of detached content. They meet at
  §4.2: the receive gap is precisely a *camera*-yaw term missing from the
  *entity*-canvas receive. T1 should be scoped as "make detached receive
  camera-cardinal-consistent," aligned with #1881's cardinal/residual split, not
  as a bespoke detached-only rotation system.
- **#2091 (Metal world-placed cast) / #1640 (foreign R32I gap)** deliver the
  *existing* cardinal resolve on Metal; #2323 designs the *rotated
  generalization* on top and concludes the generalization needs **no new cast
  mechanism** — so #2091/#1640 remain the only cast blocker, and this spike does
  not add to that critical path.
- **D1 #2322 (detached world-lighting-by-default membership)** is orthogonal:
  it fixes *whether* a detached solid participates (the
  `C_CanvasAOTexture` + `C_TrixelCanvasRenderBehavior` prerequisite,
  raw-albedo-on-miss). #2323 assumes participation and fixes *correctness under
  rotation*. T1 should land after D1 so the receiver is actually in the lighting
  archetype when tested.
- **Out of scope (recorded):** plain-`DETACHED` cast (retired, T3); sub-cell
  rotation-fraction exactness (rejected precedent #2082); camera pitch/roll for
  world content (iso-depth-axis invariant).

---

## 9. Verification (spike acceptance)

- **§3 inventory is grep-verifiable.** Each row quotes a token that re-locates
  the cited site on master `cb54b319`; the file:line pairs were read directly
  (cast recovery, receive recovery, carrier composition, frame-data cardinal
  fiat) or agent-mapped and spot-verified.
- **§4.2 is an inference, explicitly flagged**, with the confirming GL probe
  filed as the first follow-on (T0) — the spike buys the design; T0 buys the
  empirical proof before any implementation commits.
- **No engine code ships from #2323.** This doc + the filed follow-on stack are
  the deliverable; #2323 closes with pointers, not code.
