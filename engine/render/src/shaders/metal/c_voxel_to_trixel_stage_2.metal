#include "ir_iso_common.metal"
#include "ir_constants.metal"

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

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-sampling gated by isDetached — see c_voxel_to_trixel_stage_1.glsl
// for the full super-sampling contract.
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

// Fog cut-face inputs (#2125). STAGE_1 binds the fog grid at [[texture(0)]] (free
// there), but [[texture(0)]] is the colour output here, so the fog grid binds at
// [[texture(3)]] instead. Every non-fog / detached canvas binds a 1×1 all-visible
// placeholder + count-0 observers, so fogColumnReveal short-circuits to "fully
// visible" and those scenes stay byte-identical.
constant int kFogOfWarHalfExtent = 128;
constant float kFogExploredThreshold = 0.25f;
constant int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
    int _fogObsPad0;
    int _fogObsPad1;
    int _fogObsPad2;
};

// Fog reveal of world grid COLUMN `col` in [0,1]. MUST stay byte-identical to
// c_voxel_to_trixel_stage_1.metal::fogColumnReveal (and the GLSL twin) — stage 1
// emits the cut face's DISTANCE for `reveal < 1.0` (#2126 P2), and stage 2 must
// paint colour on the same set of faces or the cut wall reads as cleared
// background.
static float fogColumnReveal(
    texture2d<float, access::read> fog, constant FogObserverData& obs, int2 col
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return 1.0f; // 1×1 all-visible placeholder (non-fog / detached canvas)
    }
    const int2 cell = col + int2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f; // out-of-range column reads as visible
    }
    if (fog.read(uint2(cell)).r >= kFogExploredThreshold) {
        return 1.0f; // explored / visible grid memory — keep
    }
    float reveal = 0.0f;
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(float2(col), obs.visionCircles[i], 0.0f));
    }
    return reveal;
}

// Neighbor-cell FULL-reveal test for the cut-face gate (#2124 analytic
// cross-section band) — MUST stay byte-identical to
// c_voxel_to_trixel_stage_1.metal::fogColumnRevealFarthest (and the GLSL
// twins): stage 1 emits a cut face's distance wherever ANY part of the facing
// neighbor cell pokes outside every vision circle, and this stage must paint
// colour on the same face set or the band wall reads as cleared background.
constant float kFogColumnCellHalf = 0.5f;
static float fogColumnRevealFarthest(
    texture2d<float, access::read> fog, constant FogObserverData& obs, int2 col
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return 1.0f;
    }
    const int2 cell = col + int2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    if (fog.read(uint2(cell)).r >= kFogExploredThreshold) {
        return 1.0f;
    }
    float reveal = 0.0f;
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        const float2 delta = float2(col) - obs.visionCircles[i].xy;
        const float2 farthest = float2(col) + float2(
            delta.x >= 0.0f ? kFogColumnCellHalf : -kFogColumnCellHalf,
            delta.y >= 0.0f ? kFogColumnCellHalf : -kFogColumnCellHalf
        );
        reveal = max(reveal, fogVisionCircleReveal(farthest, obs.visionCircles[i], 0.0f));
    }
    return reveal;
}

// Minimum raster density for the analytic band projection: the band sits
// 2 micros inside the disc (so lattice rounding of the projected tap can
// never cross the mask's AA rim), which at coarse subdivisions becomes a
// visible cell-scale retraction of the cut. Below this density the cut
// keeps its #2180 lattice-wall behaviour (mask-trimmed) — the band engages
// exactly when zoom (effective subdivisions) makes it visible.
constant int kFogBandMinSubdivisions = 4;

// Guards the radial-re-homing divide-by-length below: a vision circle whose
// center coincides with the cut-face micro's world position (radial ~= 0)
// would divide by zero. Unreachable in practice (vision sources aren't
// placed inside solid geometry), but cheap insurance for determinism.
constant float kFogBandRadialEpsilon = 1e-4;

// Continuous-point analytic fog reveal for the per-micro clip — MUST stay
// byte-identical to c_voxel_to_trixel_stage_1.metal::fogPointReveal (and the
// GLSL twins): stage 1 skips a clipped micro's distance tap, and this stage
// must skip the same micro's colour tap so the two never desync on a depth
// tie.
static float fogPointReveal(
    texture2d<float, access::read> fog, constant FogObserverData& obs,
    float2 worldXY, int2 col, float aa
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return 1.0f;
    }
    const int2 cell = col + int2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    if (fog.read(uint2(cell)).r >= kFogExploredThreshold) {
        return 1.0f;
    }
    float reveal = 0.0f;
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(worldXY, obs.visionCircles[i], aa));
    }
    return reveal;
}

kernel void c_voxel_to_trixel_stage_2(
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
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint compactedIdx = groupId.x + groupId.y * indirectParams.numGroupsX;
    if (compactedIdx >= indirectParams.visibleCount) {
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
    const int faceId = frameData.visibleFaceIds[slot];

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

    // Exposed-face gate + fog CUT-FACE widening (#2125/#2127; per-axis #2128) —
    // MUST mirror stage 1's predicate EXACTLY so the colour tap lands on the same
    // face set stage 1 wrote distances for, on the single-canvas (0) and X/Y
    // per-axis routes (1/2) alike (a cut face is non-exposed; without this it'd read
    // as cleared background). The world-column recovery handles a world-placed
    // detached re-voxelize canvas (model + detachedWorldReceive.xy); see the GLSL
    // twin (incl. why a `perAxisRoute` comparison term is kept in `fogActive` for
    // non-fog byte-identity). The own-column drop (#2102/#2127) is NOT repeated here
    // — stage 1 already dropped those voxels' distances on every route, so
    // writeColorTap's depth re-test rejects their colour taps.
    const bool fogActive = fogObservers.visionCircleCount > 0 &&
        frameData.perAxisRoute <= 2 &&
        (frameData.isDetachedCanvas < 0.5f ||
         (frameData.perAxisRoute == 0 && frameData.detachedWorldReceive.w != 0.0f));
    int2 worldColumn = int2(0);
    if (fogActive) {
        worldColumn = int3(round(voxelPosition.xyz)).xy +
            (frameData.isDetachedCanvas > 0.5f
                 ? int2(round(frameData.detachedWorldReceive.xy))
                 : int2(0));
    }
    bool keepFace = faceIsExposed(flagsByte, faceId);
    bool isCutFace = false;
    if (!keepFace && faceId < kFaceZNeg && fogActive) {
        // P2 (#2126) widened by the analytic band (#2124): on the single-canvas
        // world route the neighbor test is the farthest-corner variant —
        // mirrors stage 1's gate EXACTLY (see fogColumnRevealFarthest there for
        // the band-wall rationale). The per-axis routes keep the center-based
        // gate (#2128).
        const int2 neighborColumn = worldColumn + faceOutwardNormal6I(faceId).xy;
        keepFace = (frameData.perAxisRoute == 0 && frameData.isDetachedCanvas < 0.5f
                        ? fogColumnRevealFarthest(canvasFogOfWar, fogObservers, neighborColumn)
                        : fogColumnReveal(canvasFogOfWar, fogObservers, neighborColumn)) < 1.0f;
        // A non-exposed VERTICAL face kept ONLY by the fog cut rule is the interior
        // cross-section wall — flag it so LIGHTING_TO_TRIXEL force-lights it as a
        // clean exposed face (#2124 lit-cross-section follow-up). GLSL twin.
        isCutFace = keepFace;
    }
    if (!keepFace) return;
    // Fold the cut-face flag into the id AFTER the priority encode (which strips it
    // via kEntityIdHighWordMask). Non-cut ⇒ id unchanged (byte-identical).
    packedEntityId = encodeEntityIdCutFace(packedEntityId, isCutFace);

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
        if (frameData.voxelRenderOptions.x == 0) {
            const int3 worldPos = int3(round(voxelPosition.xyz));
            const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeColorTap(
                perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance, voxelColor,
                packedEntityId, canvasSize, distanceScratch,
                triangleCanvasColors, triangleCanvasDistances, triangleCanvasEntityIds
            );
            return;
        }
        // #1458: mirror stage 1's base-resolution store (z=0 only).
        if (groupId.z != 0) return;
        const float3 worldAligned_s2 = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const int3 worldPos_s2 = int3(round(worldAligned_s2));
        const int3 facePos_s2 = faceMicroPositionFixed6(faceId, worldPos_s2, 0, 0, 1);
        const float3 fracInCell_s2 = worldAligned_s2 - float3(worldPos_s2);
        const int voxelDistance_s2 =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_s2), slot, axis, fracInCell_s2);
        writeColorTap(
            perAxisBase + pos3DtoPos2DIso(facePos_s2), voxelDistance_s2, voxelColor,
            packedEntityId, canvasSize, distanceScratch,
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
        int3 feederPos = int3(round(voxelPosition.xyz));
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
        int3 voxelPositionInt = int3(round(voxelPosition.xyz));
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
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
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
        );
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const int u = int(groupId.z) / subdivisions;
    const int v = int(groupId.z) % subdivisions;

    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = int3(round(voxelPositionAligned * float(subdivisions)));
    const int2 frameOffsetFixed = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    int3 microPositionFixed =
        faceMicroPositionFixed6(faceId, voxelPositionFixed, u, v, subdivisions);
    // Per-micro analytic fog clip — MUST stay byte-identical to stage 1's twin
    // block (see the GLSL twin for the smooth-silhouette rationale): stage 1
    // skipped the clipped micro's distance tap, so painting its colour here
    // could land on a coincidentally-equal depth from another face.
    if (fogActive && frameData.isDetachedCanvas < 0.5f) {
        const int faceAxis = faceId >> 1;
        float2 microCenterFixed = float2(int2(microPositionFixed.xy));
        if (faceAxis != 0) microCenterFixed.x += 0.5f;
        if (faceAxis != 1) microCenterFixed.y += 0.5f;
        const float2 microWorldXY = microCenterFixed / float(subdivisions);
        if (fogPointReveal(canvasFogOfWar, fogObservers, microWorldXY, worldColumn,
                           0.5f / float(subdivisions)) <= 0.0f) {
            return;
        }
        // Analytic band projection — MUST stay byte-identical to stage 1's twin
        // block (see the GLSL twin for the radial-map rationale) so the colour
        // tap lands on the same projected cell + depth the distance tap did.
        if (isCutFace && subdivisions >= kFogBandMinSubdivisions) {
            const float microHalf = 1.0f / float(subdivisions);
            float bestMargin = 1e9f;
            int bestCircle = -1;
            for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
                if (fogObservers.visionCircles[i].w != 0.0f) continue; // hard discs only (Mode A)
                const float margin = fogObservers.visionCircles[i].z -
                    length(microWorldXY - fogObservers.visionCircles[i].xy);
                if (margin > 0.0f && margin < bestMargin) {
                    bestMargin = margin;
                    bestCircle = i;
                }
            }
            if (bestCircle >= 0 && bestMargin > 2.0f * microHalf &&
                bestMargin <= 1.5f + 2.0f * microHalf) {
                // Band shading slot + dual emit — MUST mirror stage 1's twin
                // block exactly (same positions, depths, and slot bits) so the
                // colour taps land wherever the distance taps won. See the
                // GLSL twin for the radial-map + backstop + shading-slot
                // rationale.
                const float2 radial =
                    microWorldXY - fogObservers.visionCircles[bestCircle].xy;
                const int xSlot = (frameData.visibleFaceIds[0] >> 1) == 0
                    ? 0 : (((frameData.visibleFaceIds[1] >> 1) == 0) ? 1 : 2);
                const int ySlot = (frameData.visibleFaceIds[0] >> 1) == 1
                    ? 0 : (((frameData.visibleFaceIds[1] >> 1) == 1) ? 1 : 2);
                const int bandSlot = abs(radial.x) >= abs(radial.y) ? xSlot : ySlot;
                int3 latticeTap = microPositionFixed;
                if (cardinalIndex != 0) {
                    latticeTap = rotateCardinalZ(latticeTap, cardinalIndex);
                    latticeTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                }
                // World canvas only here (block gate), so depth is the plain
                // (x+y+z) iso sum — no detached depth-axis branch.
                const int latticeDepth = latticeTap.x + latticeTap.y + latticeTap.z;
                emitDeformedFace(
                    frameOffsetFixed + pos3DtoPos2DIso(latticeTap),
                    D,
                    encodeDepthWithFace(latticeDepth, bandSlot),
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
                );
                // If the vision circle center coincides with the micro's
                // position, skip the re-homing divide and rely on the
                // lattice tap above.
                const float radialLen = length(radial);
                if (radialLen >= kFogBandRadialEpsilon) {
                    const float target = fogObservers.visionCircles[bestCircle].z - 2.0f * microHalf;
                    const float2 surfFixedXY =
                        (fogObservers.visionCircles[bestCircle].xy +
                         radial * (target / radialLen)) *
                        float(subdivisions);
                    int3 bandTap = int3(roundHalfUp(surfFixedXY), microPositionFixed.z);
                    if (cardinalIndex != 0) {
                        bandTap = rotateCardinalZ(bandTap, cardinalIndex);
                        bandTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                    }
                    const int bandDepth = bandTap.x + bandTap.y + bandTap.z;
                    emitDeformedFace(
                        frameOffsetFixed + pos3DtoPos2DIso(bandTap),
                        D,
                        encodeDepthWithFace(bandDepth, bandSlot),
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
                    );
                }
                return;
            }
        }
    }
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    // Detached: mirror stage 1's entity-rotated occlusion axis (#1462 / #1499);
    // depth is in subdivision units on both branches so the encode is unchanged.
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
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
    );
}
