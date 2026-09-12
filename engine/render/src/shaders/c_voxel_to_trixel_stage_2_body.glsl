/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_2_body.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Shared stage-2 compute BODY. This is an include-FRAGMENT, not a standalone
// shader: the thin wrappers supply the `#version`, the
// `#define IR_STORE_WINNER_ELECTION {0|1}`, and the prerequisite includes, then
// `#include` this body. The body lists NO `#include`s of its own because its
// prerequisite chain is macro-PARAMETERIZED: ir_voxel_face_select.glsl reads the
// wrapper's IR_VOXEL_FOG_GRID_BINDING and this body reads
// IR_STORE_WINNER_ELECTION, so a self-include could resolve above the wrapper's
// `#define`. Each wrapper MUST include, with each macro defined before the first
// include that reads it:
//   #define IR_STORE_WINNER_ELECTION {0|1}
//   #define IR_VOXEL_FOG_GRID_BINDING 3
//   #include "ir_iso_common.glsl"
//   #include "ir_constants.glsl"
//   #include "ir_voxel_face_select.glsl"
//   #include "c_voxel_to_trixel_stage_2_body.glsl"
// The two wrappers:
//   c_voxel_to_trixel_stage_2.glsl        → ELECTION 0 = the default dispatch
//     (the election guard is textually absent, no runtime predication cost)
//   c_voxel_to_trixel_stage_2_winner.glsl → ELECTION 1 = the cardinal
//     winner-guarded dispatch: every cardinal colour/entity-id tap
//     additionally requires `perAxisWinnerIds[cell] == voxelIndex`, admitting
//     exactly one of the faces that tie the settled distance key. Dispatched in
//     place of the default ONLY when the ticking pool's storeTiesPossible_
//     flag is set (displaced-voxel scenes); lattice scenes keep the default.

// local_size_z MUST equal kStageMicroSlicesPerGroup (ir_constants.glsl). Kept a
// literal here because a compute-shader layout qualifier needs a literal on
// every GL driver; the shared constant drives the slice math + guard.
layout(local_size_x = 2, local_size_y = 3, local_size_z = 8) in;


layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Per-axis route selector: 0 = single-canvas route, else 1 + the face axis
    // this dispatch stores.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas.
    uniform float isDetachedCanvas;
    // Per-slot 2x2 deformation matrix packed column-major: `.xy` = column 0,
    // `.zw` = column 1.
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5); `.w` != 0 marks a re-voxelize dispatch.
    uniform ivec4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric. Stage 2 MUST read the same axis stage 1 wrote the distance tap
    // on, or the depth re-test below rejects every detached color tap off a
    // snap. (1,1,1) for world / identity, so the GRID path matches the fixed
    // iso axis. Offset 144, matching the CPU struct + stage 1's binding-7 layout.
    uniform vec4 voxelDepthAxis;
    // detachedWorldReceive_ (offset 160): `.xyz` = the world cell origin of a
    // world-placed detached re-voxelize solid (`roundVec3HalfUp(translation)`),
    // `.w` = 1.0 when world-placed, else 0.0. Stage 2 reads it to recover each
    // detached voxel's WORLD column for the fog cut-face predicate, mirroring
    // stage 1 + the c_lighting_to_trixel recovery. All-zero on the world /
    // per-axis / screen-locked canvas.
    uniform vec4 detachedWorldReceive;
    // Un-widened iso cull viewport for the depth-only feeder path:
    // .xy = floor(min), .zw = ceil(max). A voxel inside [cullIsoMin, cullIsoMax]
    // but OUTSIDE this box is an off-screen shadow feeder — stage 2 skips its
    // colour/entity-id taps (stage 1 still wrote its full-res depth). Matches
    // FrameDataVoxelToCanvas::visibleIsoBounds_ (offset 176).
    uniform ivec4 visibleIsoBounds;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

layout(std430, binding = 13) readonly buffer EntityIdBuffer {
    uvec2 entityIds[];
};

layout(std430, binding = 25) readonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) readonly buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;
layout(rg32ui, binding = 2) writeonly uniform uimage2D triangleCanvasEntityIds;

// Per-cell deterministic-winner scratch for the per-axis store, resolved
// between the stages by stage 1's winner-resolve dispatch: the minimum
// run-stable voxel pool index among the faces that tie the settled per-cell
// distance key. Read by the per-axis taps and, in the IR_STORE_WINNER_ELECTION
// variant, by the cardinal winner guard — there it holds the winners the
// c_voxel_to_trixel_stage_1_winner_resolve dispatch elected for the ticking
// single canvas. The default compile's cardinal / single-canvas / detached
// paths never touch it. Transiently reuses the resolve-scratch binding
// (kBufferIndex_PerAxisResolveScratch), free during both windows.
layout(std430, binding = 28) readonly buffer PerAxisWinnerScratch {
    uint perAxisWinnerIds[];
};

// Image slots 0/1/2 are the colour, distance, and entity-id outputs, so the
// wrappers route ir_voxel_face_select.glsl's fog grid to image slot 3 via
// IR_VOXEL_FOG_GRID_BINDING.

void writeColorTap(
    const ivec2 canvasPixel,
    const int voxelDistance,
    const vec4 voxelColor,
    const uvec2 packedEntityId
) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;
    if (voxelDistance == canvasDistance) {
        imageStore(triangleCanvasColors, canvasPixel, voxelColor);
        imageStore(triangleCanvasEntityIds, canvasPixel,
                   uvec4(packedEntityId, 0u, 0u));
    }
}

// Per-axis color tap with the deterministic-winner guard. The depth re-test
// alone admits EVERY face whose encoded key ties the settled winner (equal
// keys arise from the 4-bit frac quantization on the per-axis store), and the
// non-atomic imageStore then makes the color/entity-id planes
// last-writer-wins — GPU-scheduling-dependent, so they would vary run-to-run
// at a fixed non-cardinal pose while the distance plane stays identical.
// Requiring `voxelIndex == winner` admits exactly one writer. The cardinal
// paths of the default compile use the plain writeColorTap — their integer
// iso key is a bijection of CELLS, ties arise only when displaced voxels round
// into one cell, and those scenes dispatch the ELECTION variant
// (writeColorTapCardinalWinner) instead of carrying a dead guard here.
void writeColorTapPerAxis(
    const ivec2 canvasPixel,
    const int voxelDistance,
    const vec4 voxelColor,
    const uvec2 packedEntityId,
    const uint voxelIndex
) {
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);
    if (!isInsideCanvas(canvasPixel, canvasSize)) return;
    const uint cell = uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    if (perAxisWinnerIds[cell] != voxelIndex) return;
    writeColorTap(canvasPixel, voxelDistance, voxelColor, packedEntityId);
}

#if IR_STORE_WINNER_ELECTION
// Cardinal colour tap with the deterministic-winner guard — the single-canvas
// counterpart of writeColorTapPerAxis. Once independently displaced voxels
// round into the same integer cell, the cardinal (iso pixel, encoded depth)
// key stops being a bijection of live voxels, the depth re-test in
// writeColorTap admits every tied face, and the colour/entity-id planes go
// last-writer-wins. The winner scratch — elected between the stages by the
// c_voxel_to_trixel_stage_1_winner_resolve dispatch over the identical
// cardinal geometry — holds the minimum run-stable voxel pool index among the
// tied faces; requiring `voxelIndex == winner` admits exactly one writer.
// Same-voxel multi-taps of one (pixel, key) — a slot's 2x3 block, the
// re-voxelize dilation, the dual emit — all carry identical colour/entity-id
// values, so the surviving writes stay order-independent.
void writeColorTapCardinalWinner(
    const ivec2 canvasPixel,
    const int voxelDistance,
    const vec4 voxelColor,
    const uvec2 packedEntityId,
    const uint voxelIndex
) {
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);
    if (!isInsideCanvas(canvasPixel, canvasSize)) return;
    const uint cell = uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    if (perAxisWinnerIds[cell] != voxelIndex) return;
    writeColorTap(canvasPixel, voxelDistance, voxelColor, packedEntityId);
}
#endif

// Emit a face's 2x3 trixel block through the deformation matrix D; only a
// detached canvas super-samples (n up to 6).
// Under IR_STORE_WINNER_ELECTION every tap routes through the winner guard.
// The guarded tap set must stay a SUBSET of stage 1's election footprint (it
// is: both stages emit `base + roundHalfUp(D * src)` over the same lattice,
// and stage 1's n never undercuts stage 2's) — a tap outside it would read the
// 0xFFFFFFFF no-winner sentinel and reject every writer.
void emitDeformedFace(
    const ivec2 base,
    const mat2 D,
    const int voxelDistance,
    const vec4 voxelColor,
    const uvec2 packedEntityId,
    const int faceId,
    const bool reVoxelize
#if IR_STORE_WINNER_ELECTION
    , const uint voxelIndex
#endif
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 1;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage: mirror stage 1's re-voxelize footprint dilation
    // so the colour/entity tap reaches the same gap pixels the distance tap
    // claimed. writeColorTap's depth re-test then paints only
    // the occlusion-winning face per pixel, so gaps get the correct colour.
    ivec2 su = ivec2(0);
    ivec2 sv = ivec2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            ivec2 p = base + roundHalfUp(D * src);
#if IR_STORE_WINNER_ELECTION
            writeColorTapCardinalWinner(p, voxelDistance, voxelColor, packedEntityId, voxelIndex);
            if (reVoxelize) {
                writeColorTapCardinalWinner(
                    p + su, voxelDistance, voxelColor, packedEntityId, voxelIndex
                );
                writeColorTapCardinalWinner(
                    p - su, voxelDistance, voxelColor, packedEntityId, voxelIndex
                );
                writeColorTapCardinalWinner(
                    p + sv, voxelDistance, voxelColor, packedEntityId, voxelIndex
                );
                writeColorTapCardinalWinner(
                    p - sv, voxelDistance, voxelColor, packedEntityId, voxelIndex
                );
            }
#else
            writeColorTap(p, voxelDistance, voxelColor, packedEntityId);
            if (reVoxelize) {
                writeColorTap(p + su, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p - su, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p + sv, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p - sv, voxelDistance, voxelColor, packedEntityId);
            }
#endif
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    // Micro-slice packing — MUST mirror stage 1's zIdx recovery + guard so the
    // color/entity-id tap runs on exactly the micro-slices stage 1 wrote
    // distances for.
    const int zIdx = int(gl_WorkGroupID.z) * kStageMicroSlicesPerGroup + int(gl_LocalInvocationID.z);
    const int microSliceCount =
        (voxelRenderOptions.x != 0) ? (max(voxelRenderOptions.y, 1) * max(voxelRenderOptions.y, 1)) : 1;
    if (zIdx >= microSliceCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];
    vec4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    // Pack the per-voxel priority tier (low 2 bits of Voxel.reserved) into
    // the top 2 bits of the stored entity id via the shared carrier chokepoint.
    // Default reserved == 0 ⇒ id unchanged. f_trixel_to_framebuffer reads it as the
    // per-trixel tier; every id reader masks it off (decodeEntityId).
    uvec2 packedEntityId =
        encodeEntityIdWithPriority(entityIds[voxelIndex], voxels[voxelIndex].reserved & 0x3u);
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    int faceId = visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    const bool reVoxelize = visibleFaceIds.w != 0;

    // Stage 2 applies stage 1's exposed-face gate (for re-voxelize content, the
    // rotated-frame mask c_revoxelize_detached authors) so it doesn't waste an
    // `imageLoad` + depth compare on faces stage 1 skipped — same face set as
    // stage 1, so the colour tap matches the distance tap.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Face selection — the visible-triplet × exposed-mask gate, the
    // silhouette-riser flip + dual-emit predicate, and the fog cut-face
    // widening — is shared with stage 1 via ir_voxel_face_select.glsl: both
    // stages key their taps off ONE definition, so the colour tap cannot desync
    // from the distance tap. The own-column drop is NOT repeated here — stage 1
    // drops those voxels' distances on every route, so the depth re-test in
    // writeColorTap rejects their colour taps.
    const VoxelFaceSelect sel = selectVoxelFace(
        faceId, reVoxelize, voxels[voxelIndex].reserved, flagsByte,
        voxelPosition, perAxisRoute, isDetachedCanvas, detachedWorldReceive
    );
    if (!sel.keepFace) return;
    faceId = sel.faceId;
    const int riserFlip = sel.riserFlip;
    const bool bothPolaritiesExposed = sel.bothPolaritiesExposed;
    // A face kept ONLY by the fog cut rule is the interior cross-section wall —
    // fold the flag into the id (bit 29) AFTER the priority encode (which strips
    // it via kEntityIdHighWordMask) so LIGHTING_TO_TRIXEL force-lights it.
    // Non-cut ⇒ id unchanged.
    packedEntityId = encodeEntityIdCutFace(packedEntityId, sel.isCutFace);

    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Per-axis routing — mirrors stage 1's geometry exactly so the
    // color/entity-id tap lands on the same single center cell the distance tap
    // did. The store holds one cell per face center (not the emitDeformedFace
    // cluster); the framebuffer scatter reconstructs the face quad.
    if (perAxisRoute != 0) {
        const int axis = perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store — mirrors stage 1's re-key so the color /
        // entity-id tap lands on the same cardinal iso cell + depth the distance
        // tap did. The whole-iso base anchor MUST match stage 1's per-axis anchor
        // exactly, or color/id taps land on a different cell than the distance.
        const ivec2 perAxisBase = trixelOriginOffsetZ1(canvasSizePixels) + ivec2(floor(frameCanvasOffset));
        // Store at BASE (world-unit) resolution regardless of effSub —
        // on the subdivided path only the z=0 invocation writes. The cell + key
        // derive through the SAME shared helper stage 1's taps used.
        if (voxelRenderOptions.x != 0 && zIdx != 0) return;
        int voxelDistance;
        const ivec3 facePos =
            perAxisStoreFacePos(voxelPosition, faceId, slot, axis, riserFlip, voxelDistance);
        writeColorTapPerAxis(
            perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance,
            voxelColor, packedEntityId, voxelIndex
        );
        return;
    }

    // Depth-only shadow-feeder path. On the cardinal single-canvas world route
    // (perAxisRoute == 0 here), a voxel whose cardinal iso position lies outside
    // the un-widened visible viewport but inside the shadow-feeder-widened cull
    // is a SHADOW FEEDER: it exists only to cast sun shadows onto on-screen
    // pixels through stage 1's distance bake, and is never displayed, lit, or
    // picked. Stage 1 writes its full-resolution depth (the sun-shadow bake + AO
    // read ONLY trixelDistances), so skipping its colour + entity-id taps leaves
    // rendered output unchanged.
    //
    // Why no on-screen pixel resolves from a feeder (measured — not a proof): on
    // this route stage 1 emits each voxel through emitDeformedFace with identity
    // D and n == 1, so its whole write set is base + {0,1} x {0,1,2} — reach +1
    // texel in x and +2 in y from the classify centre, and 0 toward -x/-y. The
    // +4-iso-px margin baked into visibleIsoBounds (kGpuMargin,
    // system_voxel_to_trixel.hpp) covers that footprint with 2-3 texels of slack
    // on the two sides the reach points into the viewport. KEEP IN SYNC:
    // widening the emit hull past that reach — or narrowing kGpuMargin — reopens
    // the gap, the same obligation the emit hull carries toward
    // voxelOccludedByHiZ's window. scripts/feeder-margin-verify.py is the
    // executed gate: it promotes the band with a classify pad and requires zero
    // changed pixels, with a shrink arm proving the instrument fires.
    // When sun shadows are off visibleIsoBounds == cullIsoMin/Max, so nothing is
    // skipped. Matches the compact cull's cardinal projection
    // (c_voxel_visibility_compact.glsl) so the classification agrees.
    if (residualYaw == 0.0 && isDetachedCanvas < 0.5) {
        ivec3 feederPos = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            // Plain cardinal rotation — no lower-corner shift;
            // mirrors the compact cull and the stage-1 store.
            feederPos = rotateCardinalZ(feederPos, cardinalIndex);
        }
        // One definition with the compact cull's Step-B classify
        // (isShadowFeederIso, ir_iso_common.glsl) so the skip and the
        // classification cannot drift. The predicate re-tests the two route
        // terms — provably true inside this gate; the gate itself stays as the
        // cheap early-out that avoids the cardinal projection above.
        if (isShadowFeederIso(
                pos3DtoPos2DIso(feederPos), visibleIsoBounds, residualYaw, isDetachedCanvas
        )) {
            return;
        }
    }

    if (voxelRenderOptions.x == 0) {
        // roundHalfUp mirrors stage 1's cell resolution exactly — hardware
        // round() ties are implementation-defined, and a mismatch here makes
        // writeColorTap's depth re-test reject the tap at tie positions.
        ivec3 voxelPositionInt = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            // Plain cardinal rotation — no lower-corner shift;
            // MUST mirror stage 1's store cell bit-identically.
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
        }
        // Detached entities project occlusion depth onto the entity-rotated
        // iso axis; world/GRID keeps the fixed (1,1,1) via pos3DtoDistance.
        // MUST mirror stage 1's distance-tap depth or the re-test in
        // writeColorTap rejects the tap.
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot, riserFlip);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(
            base, D, voxelDistance, voxelColor, packedEntityId, faceId, reVoxelize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
#endif
        );
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = zIdx / subdivisions;
    int v = zIdx % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = roundHalfUp(voxelPositionAligned * float(subdivisions));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    // View-space micro position at non-zero cardinals — byte-identical mirror
    // of stage 1's form: rotate the CELL origin and the FACE ID, then run
    // cardinal-0 face math. The colour tap desyncs from the distance if the two
    // stages disagree here.
    ivec3 viewCellFixed = voxelPositionFixed;
    int viewFaceId = faceId;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation — no lower-corner shift; mirrors
        // stage 1's subdivided branch bit-identically.
        viewCellFixed = rotateCardinalZ(voxelPositionFixed, cardinalIndex);
        viewFaceId = rotateFaceIdCardinalZ(faceId, cardinalIndex);
    }
    const ivec3 microPositionFixed =
        faceMicroPositionFixed6(viewFaceId, viewCellFixed, u, v, subdivisions);
    // Detached: mirror stage 1's entity-rotated occlusion axis; depth is in
    // subdivision units on both branches so the encode is the same.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot, riserFlip);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base, D, voxelDistance, voxelColor, packedEntityId, viewFaceId, reVoxelize
#if IR_STORE_WINNER_ELECTION
        , voxelIndex
#endif
    );

    // Both-exposed dual emit — mirror of stage 1's opposite-face emit so the
    // colour tap lands on the riser plane's pixels too.
    // `viewFaceId ^ 1` after rotation == rotating the opposite face, since
    // rotateFaceIdCardinalZ maps opposite-face pairs to opposite-face pairs.
    if (bothPolaritiesExposed) {
        const ivec3 microOpposite =
            faceMicroPositionFixed6(viewFaceId ^ 1, viewCellFixed, u, v, subdivisions);
        const int depthOpposite = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(microOpposite, voxelDepthAxis.xyz)
            : (microOpposite.x + microOpposite.y + microOpposite.z);
        // Mirror of stage 1: the opposite plane is the non-triplet polarity.
        const int distanceOpposite = encodeDepthWithFace(depthOpposite, slot, riserFlip ^ 1);
        const ivec2 baseOpposite = frameOffsetFixed + pos3DtoPos2DIso(microOpposite);
        emitDeformedFace(
            baseOpposite, D, distanceOpposite, voxelColor, packedEntityId, viewFaceId ^ 1, reVoxelize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
#endif
        );
    }
}
