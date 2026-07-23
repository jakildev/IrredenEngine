// Shared stage-2 compute BODY (#2346, mirroring stage 1's #2258 a′ idiom) —
// Metal twin of c_voxel_to_trixel_stage_2_body.glsl. An include-FRAGMENT, not
// a standalone shader: the thin wrappers supply the prerequisite includes, the
// `#define IR_STORE_WINNER_ELECTION {0|1}`, and the
// `#define IR_STAGE2_KERNEL_NAME`, then `#include` this body. The wrappers:
//   c_voxel_to_trixel_stage_2.metal        → ELECTION 0, kernel name
//     c_voxel_to_trixel_stage_2        (the default dispatch — byte-for-byte
//     master's stage-2 kernel, the election guard textually absent)
//   c_voxel_to_trixel_stage_2_winner.metal → ELECTION 1, kernel name
//     c_voxel_to_trixel_stage_2_winner (#2346 cardinal winner-guarded
//     dispatch: every cardinal colour/entity-id tap additionally requires
//     `perAxisWinnerIds[cell] == voxelIndex`, run in place of the default when
//     the ticking pool's storeTiesPossible_ flag is set)
// Metal's loadAndPreprocessMetalSource IS recursive, but the body is kept
// include-free to mirror the (non-recursive) GLSL idiom.

// Stage 2 of the voxel→trixel pipeline: each surviving voxel re-evaluates the
// canvas distance scratch buffer (populated by stage 1) and, on a depth
// match, writes its color/distance/entityId taps into the trixel canvas
// textures.  Reads compacted visible voxel indices produced by
// c_voxel_visibility_compact.metal.
//
// We read the depth via the scratch buffer (atomic_load) rather than the
// R32I texture, mirroring the write path used by stage 1 — see
// engine/render/include/irreden/render/metal/metal_runtime.hpp.

struct IndirectDispatchParamsRO {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

inline void writeColorTap(
    int2 canvasPixel,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    const int canvasDistance =
        atomic_load_explicit(&distanceScratch[linearIndex], memory_order_relaxed);
    if (voxelDistance != canvasDistance) {
        return;
    }
    const uint2 pixel = uint2(canvasPixel);
    triangleCanvasColors.write(voxelColor, pixel);
    triangleCanvasDistances.write(int4(voxelDistance, 0, 0, 0), pixel);
    triangleCanvasEntityIds.write(uint4(packedEntityId, 0u, 0u), pixel);
}

// Per-axis color tap with the deterministic-winner guard (#2255) — GLSL twin
// in c_voxel_to_trixel_stage_2.glsl. The depth re-test alone admits every
// face whose encoded key ties the settled winner (equal keys arise from the
// 4-bit frac quantization on the per-axis store), and the non-atomic texture
// write then makes the color/entity-id planes last-writer-wins —
// GPU-scheduling-dependent, drifting run-to-run at a fixed pose (#2255).
// The winner scratch (resolved between the
// stages by stage 1's resolveMode dispatch) holds the minimum run-stable
// voxel pool index among the tied faces; requiring `voxelIndex == winner`
// admits exactly one writer. In THIS default compile the cardinal paths keep
// the plain writeColorTap above — their integer iso key is a bijection of
// CELLS, and displaced-voxel cell collisions route to the ELECTION variant
// (writeColorTapCardinalWinner below, #2346) instead of a guard here, so the
// lattice-scene byte-identity contract holds.
inline void writeColorTapPerAxis(
    int2 canvasPixel,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    uint voxelIndex,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    device const atomic_uint* perAxisWinnerIds,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    if (atomic_load_explicit(&perAxisWinnerIds[linearIndex], memory_order_relaxed) !=
        voxelIndex) {
        return;
    }
    writeColorTap(
        canvasPixel, voxelDistance, voxelColor, packedEntityId, canvasSize,
        distanceScratch, triangleCanvasColors, triangleCanvasDistances,
        triangleCanvasEntityIds
    );
}

#if IR_STORE_WINNER_ELECTION
// Cardinal colour tap with the deterministic-winner guard (#2346) — GLSL twin
// in c_voxel_to_trixel_stage_2_body.glsl, the single-canvas twin of
// writeColorTapPerAxis above. Once independently displaced voxels round into
// the same integer cell, the cardinal (iso pixel, encoded depth) key stops
// being a bijection of live voxels, the depth re-test admits every tied face,
// and the colour/entity-id planes go last-writer-wins. The winner scratch —
// elected between the stages by c_voxel_to_trixel_stage_1_winner_resolve over
// the identical cardinal geometry — holds the minimum run-stable voxel pool
// index among the tied faces; requiring `voxelIndex == winner` admits exactly
// one writer. Same-voxel multi-taps of one (pixel, key) all carry identical
// values, so the surviving writes stay order-independent.
inline void writeColorTapCardinalWinner(
    int2 canvasPixel,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    uint voxelIndex,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    device const atomic_uint* perAxisWinnerIds,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    if (atomic_load_explicit(&perAxisWinnerIds[linearIndex], memory_order_relaxed) !=
        voxelIndex) {
        return;
    }
    writeColorTap(
        canvasPixel, voxelDistance, voxelColor, packedEntityId, canvasSize,
        distanceScratch, triangleCanvasColors, triangleCanvasDistances,
        triangleCanvasEntityIds
    );
}
#endif

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-sampling gated by isDetached — see c_voxel_to_trixel_stage_1.glsl
// for the full super-sampling contract.
// Under IR_STORE_WINNER_ELECTION (#2346) every tap below routes through the
// winner guard; the guarded tap set stays a SUBSET of stage 1's election
// footprint (both stages emit `base + roundHalfUp(D * src)` over the same
// lattice, and stage 1's n never undercuts stage 2's).
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    float4 voxelColor,
    uint2 packedEntityId,
    uint2 localId,
    bool isDetached,
    int faceId,
    bool reVoxelize,
    int2 canvasSize,
    device const atomic_int* distanceScratch,
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds
#if IR_STORE_WINNER_ELECTION
    , uint voxelIndex
    , device const atomic_uint* perAxisWinnerIds
#endif
) {
    const int maxN = isDetached ? 6 : 1;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B) — mirror of stage 1's footprint
    // dilation so the colour/entity tap reaches the same gap pixels the distance
    // tap claimed; writeColorTap's depth re-test paints only the occlusion winner.
    int2 su = int2(0);
    int2 sv = int2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            const int2 p = base + roundHalfUp(D * src);
#if IR_STORE_WINNER_ELECTION
            writeColorTapCardinalWinner(
                p, voxelDistance, voxelColor, packedEntityId, voxelIndex, canvasSize,
                distanceScratch, perAxisWinnerIds, triangleCanvasColors,
                triangleCanvasDistances, triangleCanvasEntityIds
            );
            if (reVoxelize) {
                writeColorTapCardinalWinner(
                    p + su, voxelDistance, voxelColor, packedEntityId, voxelIndex, canvasSize,
                    distanceScratch, perAxisWinnerIds, triangleCanvasColors,
                    triangleCanvasDistances, triangleCanvasEntityIds
                );
                writeColorTapCardinalWinner(
                    p - su, voxelDistance, voxelColor, packedEntityId, voxelIndex, canvasSize,
                    distanceScratch, perAxisWinnerIds, triangleCanvasColors,
                    triangleCanvasDistances, triangleCanvasEntityIds
                );
                writeColorTapCardinalWinner(
                    p + sv, voxelDistance, voxelColor, packedEntityId, voxelIndex, canvasSize,
                    distanceScratch, perAxisWinnerIds, triangleCanvasColors,
                    triangleCanvasDistances, triangleCanvasEntityIds
                );
                writeColorTapCardinalWinner(
                    p - sv, voxelDistance, voxelColor, packedEntityId, voxelIndex, canvasSize,
                    distanceScratch, perAxisWinnerIds, triangleCanvasColors,
                    triangleCanvasDistances, triangleCanvasEntityIds
                );
            }
#else
            writeColorTap(
                p, voxelDistance, voxelColor, packedEntityId, canvasSize,
                distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                triangleCanvasEntityIds
            );
            if (reVoxelize) {
                writeColorTap(
                    p + su, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p - su, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p + sv, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
                writeColorTap(
                    p - sv, voxelDistance, voxelColor, packedEntityId, canvasSize,
                    distanceScratch, triangleCanvasColors, triangleCanvasDistances,
                    triangleCanvasEntityIds
                );
            }
#endif
        }
    }
}

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Fog constants/struct + fogColumnReveal and the shared face-selection /
// per-axis store-key math live in ir_voxel_face_select.metal (the wrapper
// includes it before this body). STAGE_2's [[texture(0)]] is the colour
// output, so the fog grid binds at [[texture(3)]] here (1/2 are the distance +
// entity-id outputs); Metal passes the texture + observers into the shared
// functions as arguments.

kernel void IR_STAGE2_KERNEL_NAME(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],
    device const uint2* entityIds [[buffer(13)]],
    device const uint* compactedVoxelIndices [[buffer(25)]],
    device const IndirectDispatchParamsRO& indirectParams [[buffer(26)]],
    device const atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    texture2d<uint, access::write> triangleCanvasEntityIds [[texture(2)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(3)]],
    constant FogObserverData& fogObservers [[buffer(27)]],
    device const atomic_uint* perAxisWinnerIds [[buffer(28)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint compactedIdx = groupId.x + groupId.y * indirectParams.numGroupsX;
    if (compactedIdx >= indirectParams.visibleCount) {
        return;
    }

    // #2258 micro-slice packing — MUST mirror stage 1's zIdx recovery + guard so
    // the color/entity-id tap runs on exactly the micro-slices stage 1 wrote
    // distances for. See c_voxel_to_trixel_stage_1.glsl.
    const int zIdx =
        int(groupId.z) * kStageMicroSlicesPerGroup + int(localId3.z);
    const int microSliceCount = (frameData.voxelRenderOptions.x != 0)
        ? (max(frameData.voxelRenderOptions.y, 1) * max(frameData.voxelRenderOptions.y, 1))
        : 1;
    if (zIdx >= microSliceCount) {
        return;
    }

    const uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const float4 voxelPosition = positions[voxelIndex];
    float4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    if (voxelColor.a == 0.0) {
        return;
    }
    const uint2 localId = localId3.xy;
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(localId);
    int faceId = frameData.visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int2 canvasSize = frameData.canvasSizePixels;
    // Pack the per-voxel priority tier (low 2 bits of Voxel.reserved, #1960) into
    // the top 2 bits of the stored entity id via the shared carrier chokepoint.
    // Default reserved == 0 ⇒ id unchanged. Read by f_trixel_to_framebuffer as the
    // per-trixel tier; masked off by every id reader (decodeEntityId).
    uint2 packedEntityId =
        encodeEntityIdWithPriority(entityIds[voxelIndex], voxels[voxelIndex].reserved & 0x3u);

    // Re-voxelize marker — mirror of stage 1.
    const bool reVoxelize = frameData.visibleFaceIds.w != 0;

    // Stage 2 mirrors stage 1's exposed-face gate (#1278) so it doesn't waste a
    // depth compare on faces stage 1 skipped — and is BYPASSED for re-voxelize
    // (#1570) for the same reason (its `flags_` mask is in the unrotated
    // authoring frame, not the baked-in rotated cell frame; see the GLSL twin).
    // Re-voxelize now gates on the GPU-authored rotated-frame exposed mask
    // (mirror of stage 1) — same set of faces, so the colour tap matches the
    // distance tap. writeColorTap's depth re-test still keeps the occlusion
    // winner among the emitted faces.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Face selection — the visible-triplet × exposed-mask gate (#1278), the
    // silhouette-riser flip (#2207) + dual-emit predicate (#2157), and the fog
    // cut-face widening (#2125/#2126/#2127; per-axis #2128) — is shared with
    // stage 1 via ir_voxel_face_select.metal: both stages key their taps off
    // ONE definition, so the colour tap cannot desync from the distance tap.
    // The own-column drop (#2102/#2127) is NOT repeated here — stage 1 already
    // dropped those voxels' distances on every route, so the depth re-test in
    // writeColorTap rejects their colour taps.
    const VoxelFaceSelect sel = selectVoxelFace(
        canvasFogOfWar, fogObservers, faceId, reVoxelize,
        voxels[voxelIndex].reserved, flagsByte, voxelPosition,
        frameData.perAxisRoute, frameData.isDetachedCanvas,
        frameData.detachedWorldReceive
    );
    if (!sel.keepFace) return;
    faceId = sel.faceId;
    const int riserFlip = sel.riserFlip;
    const bool bothPolaritiesExposed = sel.bothPolaritiesExposed;
    // A face kept ONLY by the fog cut rule is the interior cross-section wall —
    // fold the flag into the id (bit 29) AFTER the priority encode (which strips
    // it via kEntityIdHighWordMask) so LIGHTING_TO_TRIXEL force-lights it
    // (#2124). Non-cut ⇒ id unchanged, so non-fog scenes stay byte-identical.
    packedEntityId = encodeEntityIdCutFace(packedEntityId, sel.isCutFace);

    // Per-slot deformation matrix — see stage 1 GLSL for the contract.
    const float2x2 D = float2x2(
        frameData.faceDeform[slot].xy,
        frameData.faceDeform[slot].zw
    );

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — mirrors
    // stage 1's geometry exactly so the color/entity-id tap lands on the same
    // single center cell. T3 stores one cell per face center; the framebuffer
    // scatter reconstructs the deformed face quad. See
    // c_voxel_to_trixel_stage_1.glsl for the contract.
    if (frameData.perAxisRoute != 0) {
        const int axis = frameData.perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store — mirrors stage 1's re-key.
        // Color tap lands on the same cardinal iso cell + depth the distance tap
        // did, so the per-axis depth re-test paints the occlusion winner.
        // Whole-iso base anchor (#1944) — MUST match stage 1's per-axis anchor.
        const int2 perAxisBase = trixelOriginOffsetZ1(frameData.canvasSizePixels) +
                                 int2(floor(frameData.frameCanvasOffset));
        // #1458: store at BASE (world-unit) resolution regardless of effSub —
        // on the subdivided path only the z=0 invocation writes. The cell + key
        // derive through the SAME shared helper stage 1's taps used.
        if (frameData.voxelRenderOptions.x != 0 && zIdx != 0) return;
        int voxelDistance;
        const int3 facePos =
            perAxisStoreFacePos(voxelPosition, faceId, slot, axis, riserFlip, voxelDistance);
        writeColorTapPerAxis(
            perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance, voxelColor,
            packedEntityId, voxelIndex, canvasSize, distanceScratch, perAxisWinnerIds,
            triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
        );
        return;
    }

    // Depth-only shadow-feeder path (#1740) — mirror of c_voxel_to_trixel_stage_2.glsl.
    // On the cardinal single-canvas world route, a voxel whose cardinal iso
    // position lies outside the un-widened visible viewport but inside the
    // shadow-feeder-widened cull is an off-screen SHADOW FEEDER: it only casts
    // sun shadows via stage 1's distance bake and is never displayed/lit/picked.
    // Stage 1 wrote its full-res depth (the bake + AO read only trixelDistances),
    // so skipping its colour/entity-id taps is byte-identical and removes the
    // feeder's stage-2 cost. visibleIsoBounds carries a +4-iso-px margin covering
    // the face footprint; with sun shadows off it equals cullIsoMin/Max so
    // nothing is skipped.
    if (frameData.residualYaw == 0.0f && frameData.isDetachedCanvas < 0.5f) {
        int3 feederPos = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            feederPos = rotateCardinalZ(feederPos, cardinalIndex);
            feederPos += cardinalLowerCornerShift(cardinalIndex);
        }
        const int2 feederIso = pos3DtoPos2DIso(feederPos);
        if (feederIso.x < frameData.visibleIsoBounds.x ||
            feederIso.x > frameData.visibleIsoBounds.z ||
            feederIso.y < frameData.visibleIsoBounds.y ||
            feederIso.y > frameData.visibleIsoBounds.w) {
            return;
        }
    }

    if (frameData.voxelRenderOptions.x == 0) {
        // roundHalfUp mirrors stage 1's cell resolution exactly — hardware
        // round() ties are implementation-defined, and a mismatch here makes
        // writeColorTap's depth re-test reject the tap at tie positions.
        int3 voxelPositionInt = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities project occlusion depth onto the entity-rotated
        // iso axis (#1462); world/GRID keeps the fixed (1,1,1) via
        // pos3DtoDistance. MUST mirror stage 1's distance-tap depth or the
        // re-test in writeColorTap rejects the tap (#1499).
        const int rawDepth = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(voxelPositionInt, frameData.voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot, riserFlip);
        const int2 base =
            trixelFrameOffset(
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions
            ) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(
            base,
            D,
            voxelDistance,
            voxelColor,
            packedEntityId,
            localId,
            frameData.isDetachedCanvas > 0.5f,
            faceId,
            reVoxelize,
            canvasSize,
            distanceScratch,
            triangleCanvasColors,
            triangleCanvasDistances,
            triangleCanvasEntityIds
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
            , perAxisWinnerIds
#endif
        );
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const int u = zIdx / subdivisions;
    const int v = zIdx % subdivisions;

    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = roundHalfUp(voxelPositionAligned * float(subdivisions));
    const int2 frameOffsetFixed = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    // View-space micro position at non-zero cardinals (#2424) — byte-identical
    // mirror of stage 1's form (rotate the CELL origin + FACE ID, then run
    // cardinal-0 face math); the colour tap desyncs from the distance if the
    // two stages disagree here. See c_voxel_to_trixel_stage_1_body.glsl for
    // the seam rationale.
    int3 viewCellFixed = voxelPositionFixed;
    int viewFaceId = faceId;
    if (cardinalIndex != 0) {
        viewCellFixed = rotateCardinalZ(voxelPositionFixed, cardinalIndex) +
            cardinalLowerCornerShift(cardinalIndex) * subdivisions;
        viewFaceId = rotateFaceIdCardinalZ(faceId, cardinalIndex);
    }
    const int3 microPositionFixed =
        faceMicroPositionFixed6(viewFaceId, viewCellFixed, u, v, subdivisions);
    // Detached: mirror stage 1's entity-rotated occlusion axis (#1462 / #1499);
    // depth is in subdivision units on both branches so the encode is unchanged.
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot, riserFlip);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base,
        D,
        voxelDistance,
        voxelColor,
        packedEntityId,
        localId,
        frameData.isDetachedCanvas > 0.5f,
        viewFaceId,
        reVoxelize,
        canvasSize,
        distanceScratch,
        triangleCanvasColors,
        triangleCanvasDistances,
        triangleCanvasEntityIds
#if IR_STORE_WINNER_ELECTION
        , voxelIndex
        , perAxisWinnerIds
#endif
    );

    // Both-exposed dual emit (#2157) — mirror of stage 1's opposite-face emit
    // so the colour tap lands on the riser plane's pixels too. GLSL twin has
    // the full rationale.
    // `viewFaceId ^ 1` after rotation == rotating the opposite face, since
    // rotateFaceIdCardinalZ maps opposite-face pairs to opposite-face pairs.
    if (bothPolaritiesExposed) {
        const int3 microOpposite =
            faceMicroPositionFixed6(viewFaceId ^ 1, viewCellFixed, u, v, subdivisions);
        const int depthOpposite = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(microOpposite, frameData.voxelDepthAxis.xyz)
            : (microOpposite.x + microOpposite.y + microOpposite.z);
        // Mirror of stage 1: the opposite plane is the non-triplet polarity.
        const int distanceOpposite = encodeDepthWithFace(depthOpposite, slot, riserFlip ^ 1);
        const int2 baseOpposite = frameOffsetFixed + pos3DtoPos2DIso(microOpposite);
        emitDeformedFace(
            baseOpposite,
            D,
            distanceOpposite,
            voxelColor,
            packedEntityId,
            localId,
            frameData.isDetachedCanvas > 0.5f,
            viewFaceId ^ 1,
            reVoxelize,
            canvasSize,
            distanceScratch,
            triangleCanvasColors,
            triangleCanvasDistances,
            triangleCanvasEntityIds
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
            , perAxisWinnerIds
#endif
        );
    }
}
