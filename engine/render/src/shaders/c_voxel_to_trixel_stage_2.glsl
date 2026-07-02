#version 450 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (see stage 1 + #1309).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    // Pre-T-293 screen-space residual composite (T-058 / T-322) retired by
    // T-323; consumed inline via faceDeform[] in the trixel emit below.
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas — see
    // c_voxel_to_trixel_stage_1.glsl for the super-sampling contract.
    uniform float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major into vec4 (see
    // c_voxel_to_trixel_stage_1.glsl for the layout). T-293 + #1278.
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5). See stage 1 + #1278 for the contract.
    uniform ivec4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). Stage 2 MUST read the same axis stage 1 wrote the
    // distance tap on, or the depth re-test below rejects every detached
    // color tap off a snap (the #1499 sliver). (1,1,1) for world / identity
    // so the GRID path stays byte-identical. Appended after visibleFaceIds
    // (offset 144) to match the CPU struct + stage 1's binding-7 layout.
    uniform vec4 voxelDepthAxis;
    // detachedWorldReceive_ (offset 160): `.xyz` = the world cell origin of a
    // world-placed detached re-voxelize solid (`roundVec3HalfUp(translation)`),
    // `.w` = 1.0 when world-placed (#1576 P4b-2 / #2127), else 0.0. Stage 2 reads
    // it to recover each detached voxel's WORLD column for the fog cut-face
    // predicate, mirroring stage 1 + the c_lighting_to_trixel recovery. All-zero
    // on the world / per-axis / screen-locked canvas.
    uniform vec4 detachedWorldReceive;
    // Un-widened iso cull viewport for the depth-only feeder path (#1740):
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

// Fog cut-face inputs (#2125). STAGE_1 binds the fog grid on image slot 0 (free
// there), but slot 0 is the colour output here, so the fog grid binds on slot 3
// instead (slots 1/2 are the distance + entity-id outputs). The world fog canvas
// binds its 256² grid; every non-fog / detached canvas binds the shared 1×1
// all-visible placeholder + a count-0 observer buffer, so fogColumnReveal
// short-circuits to "fully visible" and those scenes stay byte-identical.
const int kFogOfWarHalfExtent = 128;
const float kFogExploredThreshold = 0.25;
const int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
layout(rgba8, binding = 3) readonly uniform image2D canvasFogOfWar;
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
};

// Fog reveal of world grid COLUMN `col` in [0,1]. MUST stay byte-identical to
// c_voxel_to_trixel_stage_1.glsl::fogColumnReveal — stage 1 emits the cut face's
// DISTANCE for `reveal < 1.0` (#2126 P2), and stage 2 must paint colour on the same
// set of faces or the cut wall reads as cleared background. Kept inline (not in
// ir_iso_common) because the fog grid binds on a DIFFERENT slot in each stage
// (0 vs 3), and adding a symbol to ir_iso_common risks the cardinal fast path's
// byte-identity (see the #1944 NOTE there).
float fogColumnReveal(ivec2 col) {
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return 1.0; // 1×1 all-visible placeholder (non-fog / detached canvas)
    }
    const ivec2 cell = col + ivec2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0; // out-of-range column reads as visible
    }
    if (imageLoad(canvasFogOfWar, cell).r >= kFogExploredThreshold) {
        return 1.0; // explored / visible grid memory — keep (FOG_TO_TRIXEL fades it)
    }
    float reveal = 0.0;
    for (int i = 0; i < visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(vec2(col), visionCircles[i], 0.0));
    }
    return reveal;
}

// Neighbor-cell FULL-reveal test for the cut-face gate (#2124 analytic
// cross-section band) — MUST stay byte-identical to
// c_voxel_to_trixel_stage_1.glsl::fogColumnRevealFarthest (see there for the
// band-wall rationale): stage 1 emits a cut face's distance wherever ANY part
// of the facing neighbor cell pokes outside every vision circle, and this
// stage must paint colour on the same face set or the band wall reads as
// cleared background.
const float kFogColumnCellHalf = 0.5;
float fogColumnRevealFarthest(ivec2 col) {
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return 1.0;
    }
    const ivec2 cell = col + ivec2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0;
    }
    if (imageLoad(canvasFogOfWar, cell).r >= kFogExploredThreshold) {
        return 1.0;
    }
    float reveal = 0.0;
    for (int i = 0; i < visionCircleCount; ++i) {
        const vec2 delta = vec2(col) - visionCircles[i].xy;
        const vec2 farthest = vec2(col) + vec2(
            delta.x >= 0.0 ? kFogColumnCellHalf : -kFogColumnCellHalf,
            delta.y >= 0.0 ? kFogColumnCellHalf : -kFogColumnCellHalf
        );
        reveal = max(reveal, fogVisionCircleReveal(farthest, visionCircles[i], 0.0));
    }
    return reveal;
}

// Minimum raster density for the analytic band projection: the band sits
// 2 micros inside the disc (so lattice rounding of the projected tap can
// never cross the mask's AA rim), which at coarse subdivisions becomes a
// visible cell-scale retraction of the cut. Below this density the cut
// keeps its #2180 lattice-wall behaviour (mask-trimmed) — the band engages
// exactly when zoom (effective subdivisions) makes it visible.
const int kFogBandMinSubdivisions = 4;

// Continuous-point analytic fog reveal for the per-micro clip — MUST stay
// byte-identical to c_voxel_to_trixel_stage_1.glsl::fogPointReveal (see there
// for the smooth-silhouette rationale): stage 1 skips a clipped micro's
// distance tap, and this stage must skip the same micro's colour tap so the
// two never desync on a depth tie.
float fogPointReveal(vec2 worldXY, ivec2 col, float aa) {
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return 1.0;
    }
    const ivec2 cell = col + ivec2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0;
    }
    if (imageLoad(canvasFogOfWar, cell).r >= kFogExploredThreshold) {
        return 1.0;
    }
    float reveal = 0.0;
    for (int i = 0; i < visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(worldXY, visionCircles[i], aa));
    }
    return reveal;
}

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

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-sampling gated by isDetachedCanvas — see stage-1 for the contract.
void emitDeformedFace(
    const ivec2 base,
    const mat2 D,
    const int voxelDistance,
    const vec4 voxelColor,
    const uvec2 packedEntityId,
    const int faceId,
    const bool reVoxelize
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 1;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B): mirror stage 1's re-voxelize
    // footprint dilation so the colour/entity tap reaches the same gap pixels
    // the distance tap claimed. writeColorTap's depth re-test then paints only
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
            writeColorTap(p, voxelDistance, voxelColor, packedEntityId);
            if (reVoxelize) {
                writeColorTap(p + su, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p - su, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p + sv, voxelDistance, voxelColor, packedEntityId);
                writeColorTap(p - sv, voxelDistance, voxelColor, packedEntityId);
            }
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];
    vec4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    // Pack the per-voxel priority tier (low 2 bits of Voxel.reserved, #1960) into
    // the top 2 bits of the stored entity id via the shared carrier chokepoint.
    // Default reserved == 0 ⇒ id unchanged. f_trixel_to_framebuffer reads it as the
    // per-trixel tier; every id reader masks it off (decodeEntityId).
    uvec2 packedEntityId =
        encodeEntityIdWithPriority(entityIds[voxelIndex], voxels[voxelIndex].reserved & 0x3u);
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    const int faceId = visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Re-voxelize marker — mirror of stage 1.
    const bool reVoxelize = visibleFaceIds.w != 0;

    // Stage 2 mirrors stage 1's exposed-face gate so it doesn't waste an
    // `imageLoad` + depth compare on faces stage 1 already skipped. Re-voxelize
    // gates here too now that c_revoxelize_detached authors the rotated-frame
    // mask (see c_voxel_to_trixel_stage_1.glsl) — same face set as stage 1, so
    // the colour tap matches the distance tap.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Exposed-face gate + fog CUT-FACE widening (#2125/#2127; per-axis #2128) —
    // MUST mirror stage 1's predicate EXACTLY so the colour tap lands on the same
    // face set stage 1 wrote distances for, on the single-canvas (0) and X/Y
    // per-axis routes (1/2) alike. A cut face is non-exposed, so without this
    // widening stage 2 would skip it and the cut wall would read as the cleared
    // background colour (distance set by stage 1, colour unset). See
    // c_voxel_to_trixel_stage_1.glsl for the rationale + the world-column recovery
    // (a world-placed detached re-voxelize canvas rasters in the MODEL frame, so its
    // world column is model + detachedWorldReceive.xy) and why a `perAxisRoute`
    // comparison term is kept in `fogActive` for non-fog byte-identity. The
    // own-column drop (#2102/#2127) is NOT repeated here — stage 1 already dropped
    // those voxels' distances on every route, so the depth re-test in writeColorTap
    // rejects their colour taps.
    const bool fogActive = visionCircleCount > 0 && perAxisRoute <= 2 &&
        (isDetachedCanvas < 0.5 ||
         (perAxisRoute == 0 && detachedWorldReceive.w != 0.0));
    ivec2 worldColumn = ivec2(0);
    if (fogActive) {
        worldColumn = ivec3(round(voxelPosition.xyz)).xy +
            (isDetachedCanvas > 0.5 ? ivec2(round(detachedWorldReceive.xy)) : ivec2(0));
    }
    bool keepFace = faceIsExposed(flagsByte, faceId);
    bool isCutFace = false;
    if (!keepFace && faceId < kFaceZNeg && fogActive) {
        // P2 (#2126) widened by the analytic band (#2124): on the single-canvas
        // world route the neighbor test is the farthest-corner variant —
        // mirrors stage 1's gate EXACTLY (see fogColumnRevealFarthest there for
        // the band-wall rationale). The per-axis routes keep the center-based
        // gate (#2128).
        const ivec2 neighborColumn = worldColumn + faceOutwardNormal6I(faceId).xy;
        keepFace = (perAxisRoute == 0 && isDetachedCanvas < 0.5
                        ? fogColumnRevealFarthest(neighborColumn)
                        : fogColumnReveal(neighborColumn)) < 1.0;
        // A non-exposed VERTICAL face kept ONLY by the fog cut rule is the object's
        // interior cross-section wall — flag it (bit 29 of the stored id) so
        // LIGHTING_TO_TRIXEL force-lights it as a clean exposed face rather than
        // self-shadowing it with the fog-hidden neighbors (#2124 lit-cross-section
        // follow-up). Exposed faces (keepFace true above) are NOT flagged.
        isCutFace = keepFace;
    }
    if (!keepFace) return;
    // Fold the cut-face flag into the id AFTER the priority encode (which strips
    // this bit via kEntityIdHighWordMask). Non-cut ⇒ id unchanged, so non-fog
    // scenes stay byte-identical.
    packedEntityId = encodeEntityIdCutFace(packedEntityId, isCutFace);

    // Per-slot deformation matrix — see stage 1 for the contract.
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — mirrors
    // stage 1's geometry exactly so the color/entity-id tap lands on the same
    // single center cell the distance tap did. T3 stores one cell per face
    // center (not the emitDeformedFace cluster); the framebuffer scatter
    // reconstructs the face quad. See c_voxel_to_trixel_stage_1.glsl.
    if (perAxisRoute != 0) {
        const int axis = perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store — mirrors stage 1's re-key so the color /
        // entity-id tap lands on the same cardinal iso cell + depth the distance
        // tap did. See c_voxel_to_trixel_stage_1.glsl for the full rationale.
        // Whole-iso base anchor (#1944) — MUST match stage 1's per-axis anchor
        // exactly, or color/id taps land on a different cell than the distance.
        const ivec2 perAxisBase = trixelOriginOffsetZ1(canvasSizePixels) + ivec2(floor(frameCanvasOffset));
        if (voxelRenderOptions.x == 0) {
            const ivec3 worldPos = ivec3(round(voxelPosition.xyz));
            const ivec3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeColorTap(
                perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance,
                voxelColor, packedEntityId
            );
            return;
        }
        // #1458: mirror stage 1's base-resolution store (z=0 only).
        if (gl_WorkGroupID.z != 0) return;
        const vec3 worldAligned_s2 = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const ivec3 worldPos_s2 = ivec3(round(worldAligned_s2));
        const ivec3 facePos_s2 = faceMicroPositionFixed6(faceId, worldPos_s2, 0, 0, 1);
        const vec3 fracInCell_s2 = worldAligned_s2 - vec3(worldPos_s2);
        const int voxelDistance_s2 =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_s2), slot, axis, fracInCell_s2);
        writeColorTap(
            perAxisBase + pos3DtoPos2DIso(facePos_s2), voxelDistance_s2,
            voxelColor, packedEntityId
        );
        return;
    }

    // Depth-only shadow-feeder path (#1740). On the cardinal single-canvas world
    // route (perAxisRoute == 0 guaranteed here; the per-axis / smooth / detached
    // routes returned above), a voxel whose cardinal iso position lies outside
    // the un-widened visible viewport but inside the shadow-feeder-widened cull
    // is a SHADOW FEEDER: it exists only to cast sun shadows onto on-screen
    // pixels through stage 1's distance bake, and is never displayed, lit, or
    // picked. Stage 1 already wrote its full-resolution depth (the sun-shadow
    // bake + AO read ONLY trixelDistances), so skipping its colour + entity-id
    // taps here is byte-identical in rendered output and removes the feeder's
    // entire stage-2 cost. The +4-iso-px margin baked into visibleIsoBounds
    // covers the voxel face footprint, so no on-screen pixel is ever a feeder.
    // When sun shadows are off visibleIsoBounds == cullIsoMin/Max, so nothing is
    // skipped — byte-identical. Matches the compact cull's cardinal projection
    // (c_voxel_visibility_compact.glsl) so the classification agrees.
    if (residualYaw == 0.0 && isDetachedCanvas < 0.5) {
        ivec3 feederPos = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            feederPos = rotateCardinalZ(feederPos, cardinalIndex);
            feederPos += cardinalLowerCornerShift(cardinalIndex);
        }
        const ivec2 feederIso = pos3DtoPos2DIso(feederPos);
        if (feederIso.x < visibleIsoBounds.x || feederIso.x > visibleIsoBounds.z ||
            feederIso.y < visibleIsoBounds.y || feederIso.y > visibleIsoBounds.w) {
            return;
        }
    }

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities project occlusion depth onto the entity-rotated
        // iso axis (#1462); world/GRID keeps the fixed (1,1,1) via
        // pos3DtoDistance. MUST mirror stage 1's distance-tap depth or the
        // re-test in writeColorTap rejects the tap (#1499).
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, voxelColor, packedEntityId, faceId, reVoxelize);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    ivec3 microPositionFixed =
        faceMicroPositionFixed6(faceId, voxelPositionFixed, u, v, subdivisions);
    // Per-micro analytic fog clip — MUST stay byte-identical to stage 1's twin
    // block (see there for the smooth-silhouette rationale): stage 1 skipped
    // the clipped micro's distance tap, so painting its colour here could land
    // on a coincidentally-equal depth from another face.
    if (fogActive && isDetachedCanvas < 0.5) {
        const int faceAxis = faceId >> 1;
        vec2 microCenterFixed = vec2(microPositionFixed.xy);
        if (faceAxis != 0) microCenterFixed.x += 0.5;
        if (faceAxis != 1) microCenterFixed.y += 0.5;
        const vec2 microWorldXY = microCenterFixed / float(subdivisions);
        if (fogPointReveal(microWorldXY, worldColumn, 0.5 / float(subdivisions)) <= 0.0) {
            return;
        }
        // Analytic band projection — MUST stay byte-identical to stage 1's twin
        // block (see there for the radial-map rationale) so the colour tap lands
        // on the same projected cell + depth the distance tap did.
        if (isCutFace && subdivisions >= kFogBandMinSubdivisions) {
            const float microHalf = 1.0 / float(subdivisions);
            float bestMargin = 1e9;
            int bestCircle = -1;
            for (int i = 0; i < visionCircleCount; ++i) {
                if (visionCircles[i].w != 0.0) continue; // hard discs only (Mode A)
                const float margin =
                    visionCircles[i].z - length(microWorldXY - visionCircles[i].xy);
                if (margin > 0.0 && margin < bestMargin) {
                    bestMargin = margin;
                    bestCircle = i;
                }
            }
            if (bestCircle >= 0 && bestMargin > 2.0 * microHalf &&
                bestMargin <= 1.5 + 2.0 * microHalf) {
                // Band shading slot + dual emit — MUST mirror stage 1's twin
                // block exactly (same positions, depths, and slot bits) so the
                // colour taps land wherever the distance taps won. See stage 1
                // for the radial-map + backstop + shading-slot rationale.
                const vec2 radial = microWorldXY - visionCircles[bestCircle].xy;
                const int xSlot = (visibleFaceIds[0] >> 1) == 0
                    ? 0 : (((visibleFaceIds[1] >> 1) == 0) ? 1 : 2);
                const int ySlot = (visibleFaceIds[0] >> 1) == 1
                    ? 0 : (((visibleFaceIds[1] >> 1) == 1) ? 1 : 2);
                const int bandSlot = abs(radial.x) >= abs(radial.y) ? xSlot : ySlot;
                ivec3 latticeTap = microPositionFixed;
                if (cardinalIndex != 0) {
                    latticeTap = rotateCardinalZ(latticeTap, cardinalIndex);
                    latticeTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                }
                // World canvas only here (block gate), so depth is the plain
                // (x+y+z) iso sum — no detached depth-axis branch.
                const int latticeDepth = latticeTap.x + latticeTap.y + latticeTap.z;
                emitDeformedFace(
                    frameOffsetFixed + pos3DtoPos2DIso(latticeTap), D,
                    encodeDepthWithFace(latticeDepth, bandSlot), voxelColor,
                    packedEntityId, faceId, reVoxelize
                );
                const float target = visionCircles[bestCircle].z - 2.0 * microHalf;
                const vec2 surfFixedXY =
                    (visionCircles[bestCircle].xy +
                     radial * (target / length(radial))) *
                    float(subdivisions);
                ivec3 bandTap = ivec3(roundHalfUp(surfFixedXY), microPositionFixed.z);
                if (cardinalIndex != 0) {
                    bandTap = rotateCardinalZ(bandTap, cardinalIndex);
                    bandTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                }
                const int bandDepth = bandTap.x + bandTap.y + bandTap.z;
                emitDeformedFace(
                    frameOffsetFixed + pos3DtoPos2DIso(bandTap), D,
                    encodeDepthWithFace(bandDepth, bandSlot), voxelColor,
                    packedEntityId, faceId, reVoxelize
                );
                return;
            }
        }
    }
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
        // Shift is per-world-unit; scale to subdivision units to match
        // `voxelPositionFixed = round(worldPos * subdivisions)`.
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    // Detached: mirror stage 1's entity-rotated occlusion axis (#1462 / #1499);
    // depth is in subdivision units on both branches so the encode is unchanged.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    // packedEntityId, not voxelIndex — emitDeformedFace's 5th param is uvec2 (#1960 carrier).
    emitDeformedFace(base, D, voxelDistance, voxelColor, packedEntityId, faceId, reVoxelize);
}
